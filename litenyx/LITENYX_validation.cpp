#include "LITENYX_validation.h"

#include "LITENYX_sharedstate.h"
#include "primitives/transaction.h"
#include "validation.h"

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
        return state.Invalid(false, "bad-Litenyx-auxreserved",
                             "Litenyx aux reserved must be zero");
    }
    if (aux.chainId >= LITENYX_MAX_CHAINS) {
        return state.Invalid(false, "bad-Litenyx-chainid",
                             "Litenyx chainId out of range");
    }
    if (pindexPrev) {
        uint256 parentTip = pindexPrev->GetBlockHash();
        if (aux.auxAnchor != parentTip) {
            return state.Invalid(false, "bad-Litenyx-auxanchor",
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
                return state.Invalid(false, "bad-Litenyx-sharedspend",
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
