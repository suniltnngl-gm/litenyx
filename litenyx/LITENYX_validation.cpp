#include "LITENYX_validation.h"

#include "LITENYX_sharedstate.h"
#include "LITENYX_topology_authority.h"
#include "LITENYX_chainid_lifecycle.h"
#include "primitives/transaction.h"
#include "primitives/block.h"
#include "consensus/validation.h"
#include "chain.h"
#include "validation.h" // ConnectBlock context, ReadBlockFromDisk, GetBlockWeight
#include "util.h"

#include <vector>

bool LitenyxCheckAuxHeader(const CBlock& block, const CBlockIndex* pindexPrev,
                           CValidationState& state)
{
    const LitenyxAuxHeader& aux = block.nyx_aux;

    // Before shared-state activation the chain behaves as plain Dogecoin:
    // aux extension is optional and unvalidated.
    int nHeight = pindexPrev ? pindexPrev->nHeight + 1 : 0;
    if (nHeight < LITENYX_SHARED_STATE_HEIGHT) {
        return true;
    }

    // The aux extension is OPT-IN in Phase 2: a normally-mined block carries no
    // Litenyx magic and is treated as lane 0 (default acceptance parameters).
    // Only when magic IS present do we validate chainId + same-chain anchor.
    if (!aux.HasMagic()) {
        return true;
    }
    if (aux.reserved != 0) {
        return state.Invalid(false, 0,
                             "Litenyx aux reserved must be zero");
    }
    if (aux.chainId >= LITENYX_MAX_CHAINS) {
        return state.Invalid(false, 0,
                             "Litenyx chainId out of range");
    }
    if (pindexPrev) {
        uint256 parentTip = pindexPrev->GetBlockHash();
        if (aux.auxAnchor != parentTip) {
            return state.Invalid(false, 0,
                                 "Litenyx aux anchor does not commit to parent tip");
        }
    }
    return true;
}

bool LitenyxConnectSharedState(const CBlock& block, CValidationState& state)
{
    uint8_t nChainId = block.nyx_aux.chainId;

    // Phase 1: check ALL inputs are globally unspent BEFORE mutating the set, so
    // a rejected block never leaks partial records into the shared state.
    for (const CTransactionRef& tx : block.vtx) {
        if (tx->IsCoinBase()) continue;
        for (const CTxIn& txin : tx->vin) {
            if (LitenyxIsSharedSpent(txin.prevout.hash, txin.prevout.n)) {
                return state.Invalid(false, 0,
                                     "Litenyx global double spend (cross-chain) rejected");
            }
        }
    }

    // Phase 2: now commit all spends (guaranteed unspent at this point).
    for (const CTransactionRef& tx : block.vtx) {
        if (tx->IsCoinBase()) continue;
        for (const CTxIn& txin : tx->vin) {
            LitenyxRecordSharedSpend(txin.prevout.hash, txin.prevout.n, nChainId);
        }
    }
    return true;
}

void LitenyxDisconnectSharedState(const CBlock& block)
{
    uint8_t nChainId = block.nyx_aux.chainId;
    (void)nChainId;
    for (const CTransactionRef& tx : block.vtx) {
        if (tx->IsCoinBase()) continue;
        for (const CTxIn& txin : tx->vin) {
            LitenyxRevertSharedSpend(txin.prevout.hash, txin.prevout.n);
        }
    }
}

// ---- Phase 4B(4): topology-commitment enforcement (spec §5.7/§9) -----------

namespace {

// Build the canonical (chainId, GetBlockWeight) sequence for heights 1..h, where
// h == pindexPrev->nHeight + 1. Index i corresponds to height i+1. The tip block
// is `block` (already in memory); ancestors are read from disk via CANONICAL
// chain data ONLY. Returns false if any ancestor body cannot be read.
bool LitenyxBuildCanonicalBlocks(
    const CBlock& block,
    const CBlockIndex* pindexPrev,
    const Consensus::Params& consensus,
    std::vector<LitenyxCommittedBlock>& out)
{
    const int nHeight = pindexPrev ? pindexPrev->nHeight + 1 : 0;
    if (nHeight <= 0) return true; // nothing to reconstruct at/under genesis

    out.assign((size_t)nHeight, LitenyxCommittedBlock{});

    // Tip (this block) at index nHeight-1.
    out[nHeight - 1].chainId = block.nyx_aux.chainId;
    out[nHeight - 1].blockWeight = (int64_t)::GetBlockWeight(block);

    // Ancestors: walk pindexPrev down to genesis, reading each body from disk.
    for (const CBlockIndex* pi = pindexPrev; pi && pi->nHeight >= 1; pi = pi->pprev) {
        CBlock anc;
        if (!ReadBlockFromDisk(anc, pi, consensus)) {
            return false; // pruned/missing body: cannot reconstruct authoritatively
        }
        const int idx = pi->nHeight - 1; // height h -> index h-1
        out[idx].chainId = anc.nyx_aux.chainId;
        out[idx].blockWeight = (int64_t)::GetBlockWeight(anc);
    }
    return true;
}

} // namespace

bool LitenyxCheckTopologyCommitment(const CBlock& block,
                                    const CBlockIndex* pindexPrev,
                                    const std::string& netId,
                                    const Consensus::Params& consensus,
                                    CValidationState& state)
{
    const uint32_t nHeight =
        (uint32_t)(pindexPrev ? pindexPrev->nHeight + 1 : 0);

    // 1. Regime from the frozen per-network activation.
    const LitenyxTopoActivation act = LitenyxTopoActivationForNetwork(netId);
    const LitenyxTopoRegime regime = act.RegimeAt(nHeight);

    const bool hasCommit = block.nyx_aux.HasTopologyCommitment(); // structural (V2)

    // Fast path: PreDerivation (incl. disabled networks). No derivation needed;
    // a present (V2) commitment is premature and rejected, else legacy behavior.
    if (regime == LitenyxTopoRegime::PreDerivation) {
        const LitenyxCommitVerdict v = LitenyxVerifyTopologyCommitment(
            regime, hasCommit, block.nyx_aux.topologyCommitment,
            LitenyxTopologyState::Genesis());
        if (v == LitenyxCommitVerdict::Invalid) {
            return state.Invalid(false, 0,
                                 "litenyx-topo-commit-premature");
        }
        return true;
    }

    // 2. Derive expected authoritative topology from CANONICAL CHAIN ALONE.
    std::vector<LitenyxCommittedBlock> chainBlocks;
    if (!LitenyxBuildCanonicalBlocks(block, pindexPrev, consensus, chainBlocks)) {
        // Reconstruction inputs unavailable (e.g. pruned body). In an active
        // regime we cannot prove the commitment; fail closed rather than guess.
        return state.Invalid(false, 0,
                             "litenyx-topo-reconstruct-unavailable");
    }
    const LitenyxTopologyState expected = LitenyxCalculateExpectedTopologyFromChain(
        LitenyxTopologyState::Genesis(), chainBlocks, nHeight);

    // 3. Pure verification.
    const LitenyxCommitVerdict verdict = LitenyxVerifyTopologyCommitment(
        regime, hasCommit, block.nyx_aux.topologyCommitment, expected);

    // 4. Map verdict to consensus result.
    switch (verdict) {
        case LitenyxCommitVerdict::Valid:
            return true;
        case LitenyxCommitVerdict::AdvisoryMismatch:
            // Soft regime: reportable, NOT consensus-invalid. Failure-contained.
            try {
                LogPrintf("Litenyx: topology commitment advisory mismatch at "
                          "height %u (soft regime)\n", nHeight);
            } catch (...) {}
            return true;
        case LitenyxCommitVerdict::Invalid:
        default:
            return state.Invalid(false, 0,
                                 "litenyx-topo-commit-invalid");
    }
}

// ---- Phase 5: ChainId-lifecycle-commitment enforcement (spec §6.2/§9) ------

bool LitenyxCheckLifecycleCommitment(const CBlock& block,
                                     const CBlockIndex* pindexPrev,
                                     const std::string& netId,
                                     const Consensus::Params& consensus,
                                     CValidationState& state)
{
    const uint32_t nHeight =
        (uint32_t)(pindexPrev ? pindexPrev->nHeight + 1 : 0);

    // 1. Phase-5 regime from the frozen INDEPENDENT per-network activation (§8).
    const LitenyxChainIdActivation act = LitenyxChainIdActivationForNetwork(netId);
    const LitenyxTopoRegime regime = act.RegimeAt(nHeight);

    // Structural (V3) presence — never a zero sentinel (spec §6.1/§9.1).
    const bool hasCommit = block.nyx_aux.HasLifecycleCommitment();

    // Fast path: PreDerivation (incl. disabled). No derivation needed; a present
    // (V3) commitment is premature and rejected, else legacy behavior (§9.1).
    if (regime == LitenyxTopoRegime::PreDerivation) {
        const LitenyxCommitVerdict v = LitenyxVerifyLifecycleCommitment(
            regime, hasCommit, block.nyx_aux.lifecycleCommitment,
            LitenyxChainIdLifecycleGenesis());
        if (v == LitenyxCommitVerdict::Invalid) {
            return state.Invalid(false, 0,
                                 "litenyx-lifecycle-commit-premature");
        }
        return true;
    }

    // 2. Reconstruct expected L_h from CANONICAL CHAIN HISTORY ALONE (the SAME
    //    reconstruction Phase 4 uses), folding G over the topology boundaries.
    std::vector<LitenyxCommittedBlock> chainBlocks;
    if (!LitenyxBuildCanonicalBlocks(block, pindexPrev, consensus, chainBlocks)) {
        // Reconstruction inputs unavailable (e.g. pruned body). Active regime:
        // cannot prove the commitment; fail closed rather than guess.
        return state.Invalid(false, 0,
                             "litenyx-lifecycle-reconstruct-unavailable");
    }
    LitenyxChainIdLifecycleState expected;
    if (!LitenyxCalculateExpectedLifecycleFromChain(chainBlocks, nHeight, expected)) {
        // An impossible topology delta was folded into G (spec §9.9). The block's
        // canonical history is itself lifecycle-invalid: fail closed.
        return state.Invalid(false, 0,
                             "litenyx-lifecycle-derivation-invalid");
    }

    // 3. Pure verification (spec §9.1 presence x regime matrix).
    const LitenyxCommitVerdict verdict = LitenyxVerifyLifecycleCommitment(
        regime, hasCommit, block.nyx_aux.lifecycleCommitment, expected);

    // 4. Map verdict to consensus result.
    switch (verdict) {
        case LitenyxCommitVerdict::Valid:
            return true;
        case LitenyxCommitVerdict::AdvisoryMismatch:
            try {
                LogPrintf("Litenyx: lifecycle commitment advisory mismatch at "
                          "height %u (soft regime)\n", nHeight);
            } catch (...) {}
            return true;
        case LitenyxCommitVerdict::Invalid:
        default:
            return state.Invalid(false, 0,
                                 "litenyx-lifecycle-commit-invalid");
    }
}
