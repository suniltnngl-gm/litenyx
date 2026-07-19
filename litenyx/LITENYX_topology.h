// Litenyx Phase 3 — Topology / Split-Merge controller (deterministic, spec v0.1).
//
// This header is the SINGLE SOURCE OF TRUTH for the consensus-core topology
// math. It is pure, header-only, and depends only on integer arithmetic so it
// can be unit-tested standalone AND compiled into the daemon later.
//
// Contract (docs/litenyx_topology_spec_v0.1.md):
//   - N_h is DERIVED from canonical history alone (ConsensusCore behavior).
//   - Decision is a PURE FUNCTION of (M_0..M_{N-1}, N_h, h_obs, lastTransH).
//   - Observation window is exact & boundary-aligned: [h_obs-W+1, h_obs].
//   - Cooldown: decision at h_obs is cached and applied at the first boundary
//     >= h_obs + COOLDOWN. No second decision between h_obs and that boundary.
//   - Bounds: LITENYX_MIN_CHAINS (2) <= N <= LITENYX_MAX_CHAINS. MERGE can never
//     collapse to N=1 (permanent multi-chain).
//   - S (imbalance) is TELEMETRY ONLY and plays no role in the decision.

#ifndef LITENYX_TOPOLOGY_H
#define LITENYX_TOPOLOGY_H

#include <cstdint>
#include <vector>
#include <algorithm>
#include <ostream>

#include "LITENYX_types.h"

// Topology controller parameters (LOCKED — must match spec §2).
// Values are illustrative; the invariants hold for any valid choice.
//
// NOTE: the topology target bound is INDEPENDENT of the Phase-2 shared-state
// cap LITENYX_MAX_CHAINS (=2, LOCKED for Phase 2). The shared-state cap bounds
// valid chainId values at runtime; the topology controller may target up to
// LITENYX_TOPO_MAX_CHAINS parallel chains once Phase 3 activates. The Phase-2
// lock (N fixed at 2) is preserved because no topology transition is permitted
// until Phase 3 logic is wired in; this constant only sizes the controller math.
static const uint32_t LITENYX_TOPO_MAX_CHAINS     = 8;     // LOCKED target upper bound
static const uint32_t LITENYX_TOPOLOGY_OBS_WINDOW = 100;   // blocks per window
static const uint32_t LITENYX_TOPOLOGY_COOLDOWN    = 200;  // min blocks between transitions
// Hysteresis band edges on aggregate absolute load A (mean of M_c).
// M_c is normalized demand pressure; A > HIGH => saturated, A < LOW => idle.
static const int32_t  LITENYX_TOPOLOGY_HYST_HIGH  = 80;    // 0..100 scale (percent of target)
static const int32_t  LITENYX_TOPOLOGY_HYST_LOW   = 20;

enum class LitenyxTopoDecision {
    HOLD = 0,
    SPLIT = 1,   // N -> N+1
    MERGE = 2,   // N -> N-1
};

// Allowed in BOOST_CHECK_EQUAL / diagnostics.
inline std::ostream& operator<<(std::ostream& os, LitenyxTopoDecision d) {
    switch (d) {
        case LitenyxTopoDecision::HOLD:  return os << "HOLD";
        case LitenyxTopoDecision::SPLIT: return os << "SPLIT";
        case LitenyxTopoDecision::MERGE: return os << "MERGE";
    }
    return os << "?<" << (int)d << ">";
}

// A block observation record for one chain over the window. We store only what
// the observatory needs: fullness and fee pressure, already normalized to an
// integer 0..100 "demand pressure" M_c by the caller (the daemon computes this
// from real block data; the proof feeds it directly).
struct LitenyxChainObservation {
    int32_t M_c; // normalized demand pressure for chain c, 0..100
    // Explicit ctors so brace-init works under the daemon's C++ standard (a
    // default member initializer would make this a non-aggregate pre-C++14).
    LitenyxChainObservation() : M_c(0) {}
    LitenyxChainObservation(int32_t mc) : M_c(mc) {}
};

// A per-chain observation vector (index == chainId). Defined here so both the
// daemon tracker and the standalone proof share the exact same type.
using LitenyxObservations = std::vector<LitenyxChainObservation>;

// ---- Pure observatory ------------------------------------------------------
// Aggregate absolute load A = mean_c(M_c). Defined for any N >= 1.
inline int32_t LitenyxTopoAggregateLoad(
    const std::vector<LitenyxChainObservation>& obs)
{
    if (obs.empty()) return 0;
    int64_t sum = 0;
    for (const auto& o : obs) sum += o.M_c;
    // integer mean, truncated; deterministic.
    return (int32_t)(sum / (int64_t)obs.size());
}

// Telemetry-only imbalance S = max_c(M_c) - min_c(M_c). NOT used in decisions.
inline int32_t LitenyxTopoImbalance(
    const std::vector<LitenyxChainObservation>& obs)
{
    if (obs.empty()) return 0;
    int32_t lo = obs[0].M_c, hi = obs[0].M_c;
    for (const auto& o : obs) { lo = std::min(lo, o.M_c); hi = std::max(hi, o.M_c); }
    return hi - lo;
}

// ---- Pure controller -------------------------------------------------------
// Decision is computed at observation height h_obs for current chain count N_h,
// given the last transition height (canonical-history-derived; 0 if none yet).
// Returns the cached DECISION. Does NOT mutate state.
inline LitenyxTopoDecision LitenyxTopoDecide(
    const std::vector<LitenyxChainObservation>& obs,
    uint8_t N_h,
    uint32_t h_obs,
    uint32_t lastTransitionHeight)
{
    (void)h_obs; // window alignment is guaranteed by caller (height determinism)
    if (N_h < LITENYX_MIN_CHAINS) N_h = LITENYX_MIN_CHAINS; // clamp defensively
    if (N_h > LITENYX_TOPO_MAX_CHAINS) N_h = LITENYX_TOPO_MAX_CHAINS;

    // Cooldown: if a transition happened too recently, force HOLD. The deferred
    // transition height is the first boundary >= lastTransitionHeight + COOLDOWN;
    // equivalently, any h_obs within COOLDOWN of lastTransitionHeight cannot yield
    // a change. Use SIGNED arithmetic: h_obs may be < lastTransitionHeight (e.g. a
    // decision computed at an earlier boundary than the recorded transition), and
    // an unsigned subtraction would wrap to a huge value and defeat the guard.
    if (lastTransitionHeight != 0) {
        int64_t since = (int64_t)h_obs - (int64_t)lastTransitionHeight;
        if (since < (int64_t)LITENYX_TOPOLOGY_COOLDOWN)
            return LitenyxTopoDecision::HOLD;
    }

    int32_t A = LitenyxTopoAggregateLoad(obs); // decision uses ONLY A, never S

    if (A > LITENYX_TOPOLOGY_HYST_HIGH && N_h < LITENYX_TOPO_MAX_CHAINS)
        return LitenyxTopoDecision::SPLIT;   // need more capacity
    if (A < LITENYX_TOPOLOGY_HYST_LOW && N_h > LITENYX_MIN_CHAINS)
        return LitenyxTopoDecision::MERGE;   // reclaim idle capacity
    return LitenyxTopoDecision::HOLD;
}

// ---- Transition height helper ----------------------------------------------
// The deferred transition height for a decision made at h_obs: the first
// multiple of OBS_WINDOW that is >= h_obs + COOLDOWN. Deterministic.
inline uint32_t LitenyxTopoTransitionHeight(uint32_t h_obs)
{
    uint32_t candidate = h_obs + LITENYX_TOPOLOGY_COOLDOWN;
    uint32_t rem = candidate % LITENYX_TOPOLOGY_OBS_WINDOW;
    if (rem != 0) candidate += (LITENYX_TOPOLOGY_OBS_WINDOW - rem);
    return candidate;
}

// ---- Topology state machine (reconstructible from history) ------------------
// Applies a cached decision to produce the new N. Pure function of (N_h, dec).
inline uint8_t LitenyxTopoApply(uint8_t N_h, LitenyxTopoDecision dec)
{
    if (dec == LitenyxTopoDecision::SPLIT && N_h < LITENYX_TOPO_MAX_CHAINS)
        return (uint8_t)(N_h + 1);
    if (dec == LitenyxTopoDecision::MERGE && N_h > LITENYX_MIN_CHAINS)
        return (uint8_t)(N_h - 1);
    return N_h; // HOLD, or clamped-out-of-bounds decision
}

#endif // LITENYX_TOPOLOGY_H
