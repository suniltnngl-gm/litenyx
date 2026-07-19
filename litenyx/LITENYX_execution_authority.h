// Litenyx Phase 6 — Execution authority (PURE engine, increment 1).
//
// Spec: docs/litenyx_execution_authority_spec_v0.1.md (frozen at feba683,
// against main@cbb5cca; tested Phase-5 checkpoint phase5-green@0a2bddb).
//
// This header is the MECHANICAL implementation of the frozen §3-§7 contract:
//
//     ValidatedExecutionContext  ->  Authority Projection  ->  ExecutionAuthorityResult
//
// It is PURE and standalone-testable. It has NO ConnectBlock hook, NO ATMP/
// mempool/RPC/routing/database input, NO singleton, NO clock. It NEVER
// reconstructs lifecycle or lane state itself: it consumes the frozen Phase-5
// LitenyxValidateExecutionContext as the SINGLE source of truth (spec §4), and
// projects an authority decision on top of it. It introduces NO new committed
// or hashed state (A1): ExecutionAuthorityState/Result are pure projections.
//
// DRAINING is DEFERRED (spec §3.1 / A7): it is deliberately ABSENT from the
// enum below so it can never accidentally become API surface. A future drain
// window, if ever defined, is separately-committed new state with its own KATs.
//
// C++11-compatible (compiled-and-exercised discipline from Phase 4/5): explicit
// constructors, no default member initializers on brace-initialized structs.

#ifndef LITENYX_EXECUTION_AUTHORITY_H
#define LITENYX_EXECUTION_AUTHORITY_H

#include <cstdint>

#include "LITENYX_topology.h"            // LITENYX_TOPO_MAX_CHAINS
#include "LITENYX_chainid_lifecycle.h"   // LitenyxValidateExecutionContext, status, activation

// ---- §3 ExecutionAuthorityState (derived projection; NEVER serialized) ------
// Total, injective projection of the frozen Phase-5 LitenyxChainIdStatus.
// NOTE: no DRAINING member (deferred, spec §3.1 / A7).
enum class LitenyxExecutionAuthorityState {
    AUTHORIZED = 0,  // <- Phase-5 Active   (bound to a lane in L_h)
    REVOKED    = 1,  // <- Phase-5 Retired  (< nextChainId, unbound; permanent)
    UNKNOWN    = 2,  // <- Phase-5 Nonexistent (>= nextChainId; not yet created)
};

// ---- §7 Closed failure taxonomy (F1-F5) ------------------------------------
// Ok is the sole success value; every denial maps to EXACTLY one of F1-F5.
enum class LitenyxExecutionAuthorityCode {
    Ok         = 0,  // Authorized: MayRoute + MayBind
    Unknown    = 1,  // F1: chainId >= nextChainId (nonexistent)
    Revoked    = 2,  // F2: chainId < nextChainId but unbound (retired)
    WrongLane  = 3,  // F3: AUTHORIZED but asserted lane != L_h bound lane
    Premature  = 4,  // F4: authority asserted while regime == PreDerivation
    Malformed  = 5,  // F5: asserted lane/chainId fails structural epoch bounds
};

// ---- §4/§5 ExecutionAuthorityResult (pure decision object) -----------------
// authorized == true IFF code == Ok. mayBind is true whenever the identity is
// AUTHORIZED (even on WrongLane); mayRoute additionally requires exact lane
// agreement (spec §5 matrix). state is the projected authority state; for
// Malformed/Premature (structural/regime guards evaluated BEFORE projection)
// state is reported as UNKNOWN (no identity was resolved).
struct LitenyxExecutionAuthorityResult {
    bool                            authorized; // code == Ok
    bool                            mayRoute;   // §5: AUTHORIZED && lane matches
    bool                            mayBind;    // §5: AUTHORIZED (any lane)
    LitenyxExecutionAuthorityState  state;
    LitenyxExecutionAuthorityCode   code;
    uint32_t                        chainId;    // echoed assertion
    uint8_t                         laneId;     // AUTHORITATIVE bound lane on Ok; else echoed assertion
    uint32_t                        height;     // h

    LitenyxExecutionAuthorityResult()
        : authorized(false), mayRoute(false), mayBind(false),
          state(LitenyxExecutionAuthorityState::UNKNOWN),
          code(LitenyxExecutionAuthorityCode::Unknown),
          chainId(0), laneId(0), height(0) {}
};

// Project the frozen Phase-5 lifecycle status to the Phase-6 authority state
// (spec §3 table). Pure total function.
inline LitenyxExecutionAuthorityState LitenyxProjectAuthorityState(
    LitenyxChainIdStatus s)
{
    switch (s) {
        case LitenyxChainIdStatus::Active:      return LitenyxExecutionAuthorityState::AUTHORIZED;
        case LitenyxChainIdStatus::Retired:     return LitenyxExecutionAuthorityState::REVOKED;
        case LitenyxChainIdStatus::Nonexistent: return LitenyxExecutionAuthorityState::UNKNOWN;
    }
    return LitenyxExecutionAuthorityState::UNKNOWN; // unreachable; fail closed
}

// ---- §4 Deterministic resolution + §7 failure precedence -------------------
// ResolveExecutionAuthority: given the frozen lifecycle state L_h, an asserted
// (PersistentChainId, TopologyLaneId) route, height h, and the Phase-6
// activation regime, return the deterministic ExecutionAuthorityResult.
//
// Failure precedence (FROZEN, fail-closed, mutually unambiguous):
//   1. F5 Malformed  - structural epoch bounds, BEFORE any lifecycle lookup.
//   2. F4 Premature  - regime == PreDerivation (authority not yet active).
//   3. projection    - F1 Unknown / F2 Revoked / F3 WrongLane / Ok, decided
//                      SOLELY by the frozen LitenyxValidateExecutionContext
//                      (single Phase-5 source of truth) + lane agreement.
//
// The engine NEVER reconstructs lifecycle/lane state; step 3 wraps the frozen
// LitenyxValidateExecutionContext. This is the pure decision only; making it
// non-bypassable in the production path is a SEPARATE, later-gated increment.
inline LitenyxExecutionAuthorityResult LitenyxResolveExecutionAuthority(
    const LitenyxChainIdLifecycleState& L,
    uint32_t chainId,
    uint32_t laneId,
    uint32_t height,
    LitenyxTopoRegime regime)
{
    LitenyxExecutionAuthorityResult r;
    r.chainId = chainId;
    r.laneId  = (uint8_t)laneId; // echoed until an authoritative lane replaces it
    r.height  = height;

    // ---- 1. F5 Malformed: structural epoch bounds (spec §7 F5) ----
    // A lane is a positional index in [0, TOPO_MAX_CHAINS). chainId is a uint32
    // by construction (parameter type); the only structural failure available at
    // this layer is an out-of-epoch lane index.
    if (laneId >= LITENYX_TOPO_MAX_CHAINS) {
        r.state = LitenyxExecutionAuthorityState::UNKNOWN;
        r.code  = LitenyxExecutionAuthorityCode::Malformed;
        return r; // authorized/mayRoute/mayBind stay false (fail closed)
    }

    // ---- 2. F4 Premature: regime guard (spec §6/§7 F4) ----
    // In PreDerivation the authority is dormant; asserting it is premature.
    if (regime == LitenyxTopoRegime::PreDerivation) {
        r.state = LitenyxExecutionAuthorityState::UNKNOWN;
        r.code  = LitenyxExecutionAuthorityCode::Premature;
        return r;
    }

    // ---- 3. Projection via the frozen Phase-5 source of truth (spec §4) ----
    // consensusVersion is unused by the frozen validator (reserved); pass 0.
    LitenyxValidatedExecutionContext ctx =
        LitenyxValidateExecutionContext(L, chainId, height, /*consensusVersion*/ 0);

    r.state = LitenyxProjectAuthorityState(ctx.status);

    if (!ctx.valid) {
        // Non-active identity: F1 Unknown (nonexistent) or F2 Revoked (retired).
        // Distinguishable via the frozen status; both fail closed (A4/A5).
        r.code = (ctx.status == LitenyxChainIdStatus::Nonexistent)
                     ? LitenyxExecutionAuthorityCode::Unknown
                     : LitenyxExecutionAuthorityCode::Revoked;
        return r;
    }

    // AUTHORIZED: the authoritative lane is EXCLUSIVELY the L_h binding (A2).
    r.mayBind = true;                   // §5: AUTHORIZED grants MayBind on any lane
    if ((uint32_t)ctx.laneId == laneId) {
        r.laneId     = ctx.laneId;      // report the authoritative bound lane
        r.mayRoute   = true;            // §5: lane agreement grants MayRoute
        r.authorized = true;
        r.code       = LitenyxExecutionAuthorityCode::Ok;
    } else {
        // F3 WrongLane: identity is AUTHORIZED (mayBind) but the claimed route
        // contradicts the authoritative binding (mayRoute stays false).
        r.laneId = ctx.laneId;          // surface the AUTHORITATIVE lane, not the claim
        r.code   = LitenyxExecutionAuthorityCode::WrongLane;
    }
    return r;
}

#endif // LITENYX_EXECUTION_AUTHORITY_H
