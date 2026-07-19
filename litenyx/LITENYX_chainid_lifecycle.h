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
#include <vector>

#include "LITENYX_types.h"
#include "LITENYX_topology.h"           // LITENYX_MIN_CHAINS / TOPO_MAX_CHAINS / OBS_WINDOW
#include "LITENYX_topology_authority.h" // litenyx_detail::double_sha256 (FROZEN, reused)

// Lifecycle-state schema version (spec §3). Independent of topology nVersion.
static const uint16_t LITENYX_LIFECYCLE_STATE_VERSION = 1;

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

#endif // LITENYX_CHAINID_LIFECYCLE_H
