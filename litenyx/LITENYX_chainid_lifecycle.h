// Litenyx Phase 5 — ChainId lifecycle authority (PURE engine).
//
// Spec: docs/litenyx_chainid_lifecycle_spec_v0.1.md (baseline 967e2c9 + 95b9f26
// + 7cafce1). This header is a PURE, standalone-testable engine. It has NO
// ConnectBlock hook, NO singleton, NO tracker/clock/mempool/RPC input. It is
// the identity layer ABOVE the frozen Phase-4 topology authority: it consumes
// only the frozen Phase-4 active-lane count `nN` (topology spec §0.1) and never
// invents an `activeChainIds` field.
//
// Frozen invariants (spec): L0 persistent non-recycled ChainId; L1 bijection
// lane<->ChainId; L2 dense sequential allocation (nextChainId is a sufficient
// status oracle); L3 fail-closed at UINT32_MAX (never wrap, never recycle).
//
// C++11-compatible (compiled-and-exercised lesson from Phase 4): explicit
// constructors, no default member initializers on brace-initialized structs.

#ifndef LITENYX_CHAINID_LIFECYCLE_H
#define LITENYX_CHAINID_LIFECYCLE_H

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

#include "LITENYX_types.h"
#include "LITENYX_topology.h"           // LITENYX_MIN_CHAINS / TOPO_MAX_CHAINS / OBS_WINDOW
#include "LITENYX_topology_authority.h" // litenyx_detail::double_sha256 (FROZEN, reused)

// Lifecycle-state schema version (spec §3). Independent of topology nVersion.
static const uint16_t LITENYX_LIFECYCLE_STATE_VERSION = 1;

// ---- Staged, INDEPENDENT Phase-5 activation (spec §8) ----------------------
// Reuses the frozen Phase-4 three-regime machinery (LitenyxTopoRegime) but with
// its OWN per-network heights (H_cid_derive / H_cid_enforce). DISABLED shares
// the Phase-4 "never" sentinel value/name so tests read identically.
static const uint32_t LITENYX_CHAINID_ACTIVATION_DISABLED =
    LITENYX_TOPO_ACTIVATION_DISABLED;

struct LitenyxChainIdActivation {
    uint32_t hDerive;   // H_cid_derive
    uint32_t hEnforce;  // H_cid_enforce

    LitenyxChainIdActivation()
        : hDerive(LITENYX_CHAINID_ACTIVATION_DISABLED),
          hEnforce(LITENYX_CHAINID_ACTIVATION_DISABLED) {}
    LitenyxChainIdActivation(uint32_t d, uint32_t e) : hDerive(d), hEnforce(e) {}

    bool IsDisabled() const { return hDerive == LITENYX_CHAINID_ACTIVATION_DISABLED; }

    // §8 ordering rules, incl. both-disabled coupling and 0 < H_derive.
    bool WellFormed() const {
        if (hDerive == LITENYX_CHAINID_ACTIVATION_DISABLED)
            return hEnforce == LITENYX_CHAINID_ACTIVATION_DISABLED;
        if (hDerive == 0) return false;
        return hDerive <= hEnforce;
    }

    LitenyxTopoRegime RegimeAt(uint32_t h) const {
        if (IsDisabled() || h < hDerive) return LitenyxTopoRegime::PreDerivation;
        if (h < hEnforce)                return LitenyxTopoRegime::SoftAdvisory;
        return LitenyxTopoRegime::HardAuthority;
    }
};

// Frozen per-network Phase-5 activations (spec §8 table). regtest crosses both
// boundaries cheaply in CI; mainnet DISABLED (deliberate future decision). Each
// satisfies the §8 dependency on Phase 4 (H_cid_derive >= Phase-4 H_derive;
// H_cid_enforce at/after Phase-4 H_topology).
inline LitenyxChainIdActivation LitenyxChainIdActivationRegtest() {
    return LitenyxChainIdActivation(200, 400);
}
inline LitenyxChainIdActivation LitenyxChainIdActivationTestnet() {
    return LitenyxChainIdActivation(1000, 3000);
}
inline LitenyxChainIdActivation LitenyxChainIdActivationMainnet() {
    return LitenyxChainIdActivation(LITENYX_CHAINID_ACTIVATION_DISABLED,
                                    LITENYX_CHAINID_ACTIVATION_DISABLED);
}
inline LitenyxChainIdActivation LitenyxChainIdActivationForNetwork(
    const std::string& netId) {
    if (netId == "regtest") return LitenyxChainIdActivationRegtest();
    if (netId == "test")    return LitenyxChainIdActivationTestnet();
    if (netId == "main")    return LitenyxChainIdActivationMainnet();
    return LitenyxChainIdActivation(LITENYX_CHAINID_ACTIVATION_DISABLED,
                                    LITENYX_CHAINID_ACTIVATION_DISABLED);
}

// A single active binding: a TopologyLaneId (positional, reusable) mapped to a
// persistent, never-recycled ChainId (spec §0.1, §3).
struct LitenyxChainIdBinding {
    uint8_t  laneId;
    uint32_t chainId;

    LitenyxChainIdBinding() : laneId(0), chainId(0) {}
    LitenyxChainIdBinding(uint8_t l, uint32_t c) : laneId(l), chainId(c) {}

    bool operator==(const LitenyxChainIdBinding& o) const {
        return laneId == o.laneId && chainId == o.chainId;
    }
};

// Canonical ChainId lifecycle state L_h (spec §3). activeBindings is kept sorted
// ascending by laneId (single canonical encoding).
struct LitenyxChainIdLifecycleState {
    uint16_t nVersion;
    uint32_t nextChainId;                             // monotonic; next free ChainId
    std::vector<LitenyxChainIdBinding> activeBindings; // ascending by laneId
    uint32_t lastLifecycleHeight;

    LitenyxChainIdLifecycleState()
        : nVersion(LITENYX_LIFECYCLE_STATE_VERSION),
          nextChainId(0),
          activeBindings(),
          lastLifecycleHeight(0) {}

    bool operator==(const LitenyxChainIdLifecycleState& o) const {
        return nVersion == o.nVersion &&
               nextChainId == o.nextChainId &&
               lastLifecycleHeight == o.lastLifecycleHeight &&
               activeBindings == o.activeBindings;
    }
    bool operator!=(const LitenyxChainIdLifecycleState& o) const { return !(*this == o); }
};

// ---- Genesis L_0 (spec §4.0, FROZEN) ---------------------------------------
// Identity relabeling of the frozen Phase-4 genesis: lane i -> ChainId i for
// i in [0, MIN_CHAINS); nextChainId = MIN_CHAINS.
inline LitenyxChainIdLifecycleState LitenyxChainIdLifecycleGenesis()
{
    LitenyxChainIdLifecycleState L;
    L.nVersion = LITENYX_LIFECYCLE_STATE_VERSION;
    L.nextChainId = LITENYX_MIN_CHAINS;
    L.activeBindings.clear();
    for (uint8_t i = 0; i < LITENYX_MIN_CHAINS; ++i)
        L.activeBindings.push_back(LitenyxChainIdBinding(i, (uint32_t)i));
    L.lastLifecycleHeight = 0;
    return L;
}

// ---- Canonical serialization (spec §3.1) -----------------------------------
// Fixed little-endian layout, no padding:
//   nVersion(2) || nextChainId(4) || len(activeBindings)(1) ||
//     [ laneId(1) || chainId(4) ] * len   (ascending by laneId) ||
//   lastLifecycleHeight(4)
inline std::vector<unsigned char> LitenyxSerializeLifecycleState(
    const LitenyxChainIdLifecycleState& L)
{
    std::vector<unsigned char> out;
    out.reserve(2 + 4 + 1 + L.activeBindings.size() * 5 + 4);

    out.push_back((unsigned char)(L.nVersion & 0xFF));
    out.push_back((unsigned char)((L.nVersion >> 8) & 0xFF));

    out.push_back((unsigned char)(L.nextChainId & 0xFF));
    out.push_back((unsigned char)((L.nextChainId >> 8) & 0xFF));
    out.push_back((unsigned char)((L.nextChainId >> 16) & 0xFF));
    out.push_back((unsigned char)((L.nextChainId >> 24) & 0xFF));

    out.push_back((unsigned char)(L.activeBindings.size() & 0xFF));

    for (size_t i = 0; i < L.activeBindings.size(); ++i) {
        const LitenyxChainIdBinding& b = L.activeBindings[i];
        out.push_back(b.laneId);
        out.push_back((unsigned char)(b.chainId & 0xFF));
        out.push_back((unsigned char)((b.chainId >> 8) & 0xFF));
        out.push_back((unsigned char)((b.chainId >> 16) & 0xFF));
        out.push_back((unsigned char)((b.chainId >> 24) & 0xFF));
    }

    out.push_back((unsigned char)(L.lastLifecycleHeight & 0xFF));
    out.push_back((unsigned char)((L.lastLifecycleHeight >> 8) & 0xFF));
    out.push_back((unsigned char)((L.lastLifecycleHeight >> 16) & 0xFF));
    out.push_back((unsigned char)((L.lastLifecycleHeight >> 24) & 0xFF));

    return out;
}

// LifecycleStateHash: double-SHA256 of the canonical serialization (spec §3.1).
// Reuses the FROZEN Phase-4 self-contained hasher for byte-identical results
// across every build/platform (path-independence, spec §0.4).
inline void LitenyxLifecycleStateHash(
    const LitenyxChainIdLifecycleState& L, unsigned char out[32])
{
    std::vector<unsigned char> ser = LitenyxSerializeLifecycleState(L);
    litenyx_detail::double_sha256(ser.empty() ? (const unsigned char*)"" : &ser[0],
                                  ser.size(), out);
}

// ---- Structural coherence check (spec §3, §3.2, §3.3) ----------------------
// Verifies L is internally consistent for the given expected lane count Nexp:
//   - len(activeBindings) == Nexp and laneIds are exactly {0..Nexp-1} ascending;
//   - Nexp in [MIN_CHAINS, TOPO_MAX_CHAINS];
//   - every bound chainId < nextChainId (L2 domain);
//   - bindings form a bijection (unique laneIds AND unique chainIds) (L1).
inline bool LitenyxLifecycleStateCoherent(
    const LitenyxChainIdLifecycleState& L, uint8_t Nexp)
{
    if (Nexp < LITENYX_MIN_CHAINS || Nexp > LITENYX_TOPO_MAX_CHAINS) return false;
    if (L.activeBindings.size() != (size_t)Nexp) return false;

    // laneIds exactly {0..Nexp-1} ascending; chainIds unique and < nextChainId.
    for (size_t i = 0; i < L.activeBindings.size(); ++i) {
        const LitenyxChainIdBinding& b = L.activeBindings[i];
        if (b.laneId != (uint8_t)i) return false;             // contiguous prefix + ascending
        if (b.chainId >= L.nextChainId) return false;         // L2 domain
        for (size_t j = 0; j < i; ++j)                        // L1 uniqueness of chainId
            if (L.activeBindings[j].chainId == b.chainId) return false;
    }
    return true;
}

// ---- G: authoritative lifecycle transition (spec §4, PURE) -----------------
// Consumes the frozen Phase-4 active-lane counts N_{h-1} -> N_h (spec §0.1).
// The delta d = N_h - N_{h-1} is one of {-1, 0, +1} on a valid chain.
//   d == 0  : HOLD, L_h = L_{h-1}.
//   d == +1 : SPLIT, bind lane N_{h-1} -> nextChainId; nextChainId++.
//   d == -1 : MERGE, retire lane N_h (its chainId permanently); nextChainId same.
// Fail-closed (spec §4.1/§9): returns false on |d|>1, out-of-range nN,
// non-boundary change, internal inconsistency, or L3 exhaustion. On false, out
// is left unspecified and MUST be treated as a consensus failure by callers.
inline bool LitenyxAdvanceChainIdLifecycle(
    const LitenyxChainIdLifecycleState& prev,
    uint8_t Nprev,
    uint8_t Ncur,
    uint32_t height,
    LitenyxChainIdLifecycleState& out)
{
    // Bounds on both endpoints (spec §4.1).
    if (Nprev < LITENYX_MIN_CHAINS || Nprev > LITENYX_TOPO_MAX_CHAINS) return false;
    if (Ncur  < LITENYX_MIN_CHAINS || Ncur  > LITENYX_TOPO_MAX_CHAINS) return false;

    // Incoming state must already be coherent for Nprev (defense in depth).
    if (!LitenyxLifecycleStateCoherent(prev, Nprev)) return false;

    const int delta = (int)Ncur - (int)Nprev;
    if (delta < -1 || delta > 1) return false; // spec §4.1: |ΔnN| <= 1

    // A non-HOLD change may only occur at an observation-window boundary.
    if (delta != 0 && (height % LITENYX_TOPOLOGY_OBS_WINDOW) != 0) return false;

    out = prev;
    out.nVersion = LITENYX_LIFECYCLE_STATE_VERSION;

    if (delta == 0) {
        // HOLD: unchanged (lastLifecycleHeight preserved).
        return LitenyxLifecycleStateCoherent(out, Ncur);
    }

    if (delta == +1) {
        // SPLIT: lane Nprev activates. L3: never wrap/recycle at UINT32_MAX.
        if (out.nextChainId == 0xFFFFFFFFu) return false; // exhaustion, fail closed
        const uint8_t newLane = Nprev;
        const uint32_t newChainId = out.nextChainId;
        out.activeBindings.push_back(LitenyxChainIdBinding(newLane, newChainId));
        out.nextChainId += 1;
        out.lastLifecycleHeight = height;
        return LitenyxLifecycleStateCoherent(out, Ncur);
    }

    // delta == -1: MERGE. Retire the highest active lane (== Ncur == Nprev-1).
    const uint8_t retiredLane = Ncur; // = Nprev - 1
    bool removed = false;
    for (size_t i = 0; i < out.activeBindings.size(); ++i) {
        if (out.activeBindings[i].laneId == retiredLane) {
            out.activeBindings.erase(out.activeBindings.begin() + (long)i);
            removed = true;
            break;
        }
    }
    if (!removed) return false;         // internal inconsistency (spec §4.1)
    // nextChainId UNCHANGED (L0: retired chainId permanently gone, never reused).
    out.lastLifecycleHeight = height;
    return LitenyxLifecycleStateCoherent(out, Ncur);
}

// ---- Execution-context validation (spec §5, PURE) --------------------------
// The SOLE Phase 5 -> Phase 6 authority boundary.
enum class LitenyxChainIdStatus {
    Active = 0,       // bound to a lane in L_h
    Retired = 1,      // < nextChainId but not bound (permanent; L2 oracle)
    Nonexistent = 2,  // >= nextChainId (not yet created)
};

// Classify a chainId against L_h (spec §5.1 / §3.3).
inline LitenyxChainIdStatus LitenyxClassifyChainId(
    const LitenyxChainIdLifecycleState& L, uint32_t chainId)
{
    if (chainId >= L.nextChainId) return LitenyxChainIdStatus::Nonexistent;
    for (size_t i = 0; i < L.activeBindings.size(); ++i)
        if (L.activeBindings[i].chainId == chainId)
            return LitenyxChainIdStatus::Active;
    return LitenyxChainIdStatus::Retired; // < nextChainId, unbound => retired (L2)
}

// A validated execution context (spec §5). Constructible ONLY via
// LitenyxValidateExecutionContext (the `valid` flag gates construction).
struct LitenyxValidatedExecutionContext {
    bool     valid;
    uint32_t chainId;
    uint8_t  laneId;
    uint32_t height;
    LitenyxChainIdStatus status; // diagnostic (Active on success)

    LitenyxValidatedExecutionContext()
        : valid(false), chainId(0), laneId(0), height(0),
          status(LitenyxChainIdStatus::Nonexistent) {}
};

// ValidateExecutionContext (spec §5): PURE, fail-closed. Only a chainId ACTIVE
// in L_h yields a valid context (bound to its unique lane). Retired and
// Nonexistent both fail closed (distinguishable via .status for diagnostics).
inline LitenyxValidatedExecutionContext LitenyxValidateExecutionContext(
    const LitenyxChainIdLifecycleState& L,
    uint32_t chainId,
    uint32_t height,
    uint32_t /*consensusVersion*/)
{
    LitenyxValidatedExecutionContext ctx;
    ctx.chainId = chainId;
    ctx.height = height;
    ctx.status = LitenyxClassifyChainId(L, chainId);
    if (ctx.status == LitenyxChainIdStatus::Active) {
        for (size_t i = 0; i < L.activeBindings.size(); ++i) {
            if (L.activeBindings[i].chainId == chainId) {
                ctx.laneId = L.activeBindings[i].laneId;
                ctx.valid = true;
                break;
            }
        }
    }
    return ctx;
}

// ---- Canonical-chain lifecycle derivation (spec §6.2, PURE) ----------------
// The AUTHORITATIVE reconstruction of L_h from the canonical block sequence
// ALONE, layered EXACTLY on the frozen Phase-4 topology derivation. It folds G
// over the SAME OBS_WINDOW boundaries the topology authority uses: at each
// boundary the topology nN may move by +/-1, and G consumes that N_{h-1}->N_h
// scalar (spec §0.1). Path-independent by construction (reads canonical heights
// in ascending order; no tracker/cache/arrival-order dependence, spec §0.4).
//
// Returns false (fail closed) if any boundary transition is rejected by G
// (§4.1/§9) — e.g. an impossible topology delta smuggled into the chain.
inline bool LitenyxCalculateExpectedLifecycleFromChain(
    const std::vector<LitenyxCommittedBlock>& chainBlocks, // index == height-1
    uint32_t tipHeight,
    LitenyxChainIdLifecycleState& out)
{
    const uint32_t W = LITENYX_TOPOLOGY_OBS_WINDOW;
    if (tipHeight > chainBlocks.size())
        tipHeight = (uint32_t)chainBlocks.size();

    LitenyxTopologyState topo = LitenyxTopologyState::Genesis();
    LitenyxChainIdLifecycleState life = LitenyxChainIdLifecycleGenesis();

    for (uint32_t h = W; h <= tipHeight; h += W) {
        const uint8_t Nprev = topo.nN;

        // Advance the FROZEN topology authority by exactly one boundary.
        std::vector<LitenyxCommittedBlock> window;
        window.reserve(W);
        for (uint32_t g = h - W + 1; g <= h; ++g)
            window.push_back(chainBlocks[g - 1]);
        std::vector<int32_t> mcV1 = LitenyxReconstructMcV1Window(window, topo.nN);
        topo = LitenyxDeriveTopologyAtBoundary(topo, h, mcV1);

        const uint8_t Ncur = topo.nN;

        // Fold G at this boundary, consuming the topology N_{h-1}->N_h scalar.
        LitenyxChainIdLifecycleState next;
        if (!LitenyxAdvanceChainIdLifecycle(life, Nprev, Ncur, h, next))
            return false; // impossible transition; fail closed (spec §9.9)
        life = next;
    }

    out = life;
    return true;
}

// ExpectedLifecycleCommitment: the value carried in
// LitenyxAuxHeader.lifecycleCommitment IS the frozen LifecycleStateHash of the
// expected L_h (spec §6.1). Domain-less, symmetric with the Phase-4 topology
// commitment. No second hash domain is introduced.
inline uint256 LitenyxExpectedLifecycleCommitment(
    const LitenyxChainIdLifecycleState& L)
{
    uint256 out;
    LitenyxLifecycleStateHash(L, out.begin());
    return out;
}

// VerifyLifecycleCommitment (spec §9.1): pure regime decision, IDENTICAL in
// shape to LitenyxVerifyTopologyCommitment. Presence is the STRUCTURAL V3 fact;
// value is the 32 committed bytes; validity is decided per regime. Reuses the
// Phase-4 LitenyxCommitVerdict / LitenyxTopoRegime enums (single source of the
// three-regime semantics).
inline LitenyxCommitVerdict LitenyxVerifyLifecycleCommitment(
    LitenyxTopoRegime regime,
    bool hasCommitment,
    const uint256& commitment,
    const LitenyxChainIdLifecycleState& expected)
{
    const bool matches =
        hasCommitment && (commitment == LitenyxExpectedLifecycleCommitment(expected));

    switch (regime) {
        case LitenyxTopoRegime::PreDerivation:
            // No lifecycle semantics yet; a present commitment is premature.
            return hasCommitment ? LitenyxCommitVerdict::Invalid
                                 : LitenyxCommitVerdict::Valid;

        case LitenyxTopoRegime::SoftAdvisory:
            if (!hasCommitment) return LitenyxCommitVerdict::Valid;
            return matches ? LitenyxCommitVerdict::Valid
                           : LitenyxCommitVerdict::AdvisoryMismatch;

        case LitenyxTopoRegime::HardAuthority:
            if (!hasCommitment) return LitenyxCommitVerdict::Invalid;
            return matches ? LitenyxCommitVerdict::Valid
                           : LitenyxCommitVerdict::Invalid;
    }
    return LitenyxCommitVerdict::Invalid; // unreachable; fail closed
}

#endif // LITENYX_CHAINID_LIFECYCLE_H
