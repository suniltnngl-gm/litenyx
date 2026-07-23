// Litenyx Phase 7 — Draining authority (PURE engine, increment 1).
//
// Spec: docs/litenyx_draining_authority_spec_v0.1.md (v0.2 R-REP delta),
// frozen against phase6-green @ a95507f.
//
// This header is the MECHANICAL implementation of the frozen Phase-7 contract
// for the parts the frozen protocol can already support:
//
//     frozen DrainCommitment (INPUT FACT)
//       + frozen Phase-5 L_h
//       + frozen Phase-6 ExecutionAuthorityResult
//         -> monotonic capability restriction
//         -> DrainCapabilityProjection { drainStatus, effMayBind, effMayRoute }
//
// It is PURE and standalone-testable. It has NO ConnectBlock hook, NO ATMP/
// mempool/RPC/routing/database input, NO singleton, NO clock, and it introduces
// NO new committed/hashed state here (the DrainCommitment carrier byte layout is
// a separate mechanical follow-on). It NEVER reconstructs topology / lifecycle /
// lanes / allocation / authority: it CONSUMES the frozen Phase-5 classifier and
// the frozen Phase-6 result as the single sources of truth (D7).
//
// DELIBERATELY OUT OF SCOPE (spec §4.4.5, OPEN): the autonomous emitter /
// DrainDecisionEngine and the provenance predicate ReproduceDrainCommitment.
// This header therefore implements ONLY ValidateDrainCommitmentSemantics — the
// currently representable, canonical-state validation — and does NOT claim the
// full D12 provenance statement (which is deferred with the emitter gate).
//
// D0 (LOCKED): DRAINING is an operational-capability MODE, not a 4th authority
// state. The Phase-6 enum/result is untouched; nothing here is added to it.
//
// C++11-compatible (compiled-and-exercised discipline): explicit constructors,
// no default member initializers on brace-initialized structs.

#ifndef LITENYX_DRAINING_AUTHORITY_H
#define LITENYX_DRAINING_AUTHORITY_H

#include <cstdint>
#include <string>

#include "LITENYX_topology.h"              // LITENYX_TOPOLOGY_OBS_WINDOW, MIN/MAX
#include "LITENYX_chainid_lifecycle.h"     // L_h, ClassifyChainId, status, activation
#include "LITENYX_execution_authority.h"   // frozen Phase-6 result (consumed, not modified)

// ---- §6 Staged, INDEPENDENT Phase-7 activation (strictly later than Phase 6) --
// Reuses the frozen three-regime machinery with its OWN per-network heights
// (H_drain_derive / H_drain_enforce). Dependency (spec §6 / D6, FROZEN):
//   H_drain_derive  >= Phase-6 H_exec_derive   and
//   H_drain_enforce >  Phase-6 H_exec_enforce
// so drain never enforces before execution authority itself is enforced. DISABLED
// shares the frozen sentinel; mainnet DISABLED this increment.
static const uint32_t LITENYX_DRAIN_ACTIVATION_DISABLED =
    LITENYX_CHAINID_ACTIVATION_DISABLED;

// regtest strictly AFTER Phase-6 regtest {600,800}; test after Phase-6 {4000,6000};
// main DISABLED (deliberate future decision, as Phase 4/5/6).
inline LitenyxChainIdActivation LitenyxDrainActivationRegtest() {
    return LitenyxChainIdActivation(1000, 1200);
}
inline LitenyxChainIdActivation LitenyxDrainActivationTestnet() {
    return LitenyxChainIdActivation(8000, 10000);
}
inline LitenyxChainIdActivation LitenyxDrainActivationMainnet() {
    return LitenyxChainIdActivation(LITENYX_DRAIN_ACTIVATION_DISABLED,
                                    LITENYX_DRAIN_ACTIVATION_DISABLED);
}
inline LitenyxChainIdActivation LitenyxDrainActivationForNetwork(
    const std::string& netId) {
    if (netId == "regtest") return LitenyxDrainActivationRegtest();
    if (netId == "test")    return LitenyxDrainActivationTestnet();
    if (netId == "main")    return LitenyxDrainActivationMainnet();
    return LitenyxChainIdActivation(LITENYX_DRAIN_ACTIVATION_DISABLED,
                                    LITENYX_DRAIN_ACTIVATION_DISABLED);
}

// ---- §4.4.1 DrainCommitment (INPUT FACT; keyed on identity for ABA safety) ----
// Minimal FROZEN logical shape. Keyed on PersistentChainId (NEVER a lane) so a
// reused lane later bound to a different identity can never inherit a historical
// drain commitment (D8 / ABA / A3). `present` distinguishes "no commitment" from
// a zero-valued one without needing an optional. The concrete committed byte
// layout is a separate follow-on; this struct is the engine's input view.
struct LitenyxDrainCommitment {
    bool     present;          // false => no commitment for this identity
    uint32_t chainId;         // PersistentChainId subject (identity key)
    uint32_t drainStartHeight; // OBS_WINDOW-aligned boundary at/after which draining is in force

    LitenyxDrainCommitment()
        : present(false), chainId(0), drainStartHeight(0) {}
    LitenyxDrainCommitment(uint32_t c, uint32_t startH)
        : present(true), chainId(c), drainStartHeight(startH) {}
};

// ---- §3 OperationalCapabilityMode (derived; NEVER serialized) ---------------
// A mode of an already-AUTHORIZED identity — NOT an authority state (D0).
enum class LitenyxOperationalCapabilityMode {
    NORMAL   = 0,
    DRAINING = 1,
};

// ---- §3 DrainCapabilityProjection (pure decision object) --------------------
// Consumes a frozen Phase-6 result; NEVER duplicates its authority semantics.
// effMayBind/effMayRoute are the MONOTONICALLY restricted capabilities (D1);
// mode is DRAINING iff the drain is operationally in force for an AUTHORIZED,
// Active identity (D2). state echoes the frozen Phase-6 state for convenience
// (unchanged; D0).
struct LitenyxDrainCapabilityProjection {
    LitenyxOperationalCapabilityMode mode;
    LitenyxExecutionAuthorityState   state;    // == Phase-6 state (echoed, unchanged)
    bool                             effMayBind;
    bool                             effMayRoute;
    uint32_t                         chainId;
    uint8_t                          laneId;
    uint32_t                         height;

    LitenyxDrainCapabilityProjection()
        : mode(LitenyxOperationalCapabilityMode::NORMAL),
          state(LitenyxExecutionAuthorityState::UNKNOWN),
          effMayBind(false), effMayRoute(false),
          chainId(0), laneId(0), height(0) {}
};

// ---- §4.4.2 IsDraining (completion = actual Phase-5 retirement) --------------
// P7-DRAIN-COMPLETE (D9): draining is in force IFF a valid commitment exists,
// h has reached its start, AND Phase 5 STILL classifies the identity as Active.
// The moment the frozen merge fold retires the identity (Active -> Retired),
// this returns false automatically — with NO Phase-7 completion event and NO
// commitment-deletion mechanism. PURE, fail-closed (D5).
inline bool LitenyxIsDraining(
    const LitenyxChainIdLifecycleState& L,
    const LitenyxDrainCommitment& C,
    uint32_t height)
{
    if (!C.present) return false;
    if (height < C.drainStartHeight) return false;
    // Completion is subordinate to Phase-5: only an Active identity can drain.
    return LitenyxClassifyChainId(L, C.chainId) == LitenyxChainIdStatus::Active;
}

// ---- §4.4.4 AuthoritativeLane helper (from L_h ALONE) -----------------------
// The unique lane bound to an Active chainId in L_h (L1 uniqueness). Returns
// false if the chainId is not Active/bound. Used for edge-only eligibility and
// never to establish authority (that is Phase 6's job).
inline bool LitenyxAuthoritativeLaneForChainId(
    const LitenyxChainIdLifecycleState& L,
    uint32_t chainId,
    uint8_t& laneOut)
{
    for (size_t i = 0; i < L.activeBindings.size(); ++i) {
        if (L.activeBindings[i].chainId == chainId) {
            laneOut = L.activeBindings[i].laneId;
            return true;
        }
    }
    return false;
}

// The current highest active lane == N_h - 1, where N_h == number of active
// bindings (dense positional lanes {0..N-1}, spec §3). Returns false if empty.
inline bool LitenyxCurrentHighestActiveLane(
    const LitenyxChainIdLifecycleState& L,
    uint8_t& highestLaneOut)
{
    const size_t n = L.activeBindings.size();
    if (n == 0) return false;
    highestLaneOut = (uint8_t)(n - 1);
    return true;
}

// ---- §4.4.4 / §4.4.6 ValidateDrainCommitmentSemantics -----------------------
// The CURRENTLY REPRESENTABLE half of validation (D11 + the semantics half of
// D12). Decides admissibility of a commitment against CANONICAL state ALONE:
//
//   activation regime derived (not PreDerivation)   [§6]
//   AND drainStartHeight on an OBS_WINDOW boundary   [D8]
//   AND identity Active in L_h                        [D2/P5]
//   AND identity AUTHORIZED in Phase 6                [D2/P6]  (passed in)
//   AND authoritative lane == current highest (N-1)   [D11 edge-only, Model B]
//
// It DOES NOT and CANNOT prove provenance (that the emitter would have produced
// exactly this commitment). ReproduceDrainCommitment is DEFERRED with the
// emitter gate (§4.4.5). Callers MUST NOT treat a true return here as autonomous
// provenance — only as "this commitment is semantically well-formed and
// eligible at h". PURE, fail-closed.
inline bool LitenyxValidateDrainCommitmentSemantics(
    const LitenyxChainIdLifecycleState& L,
    const LitenyxDrainCommitment& C,
    uint32_t height,
    LitenyxTopoRegime regime,
    LitenyxExecutionAuthorityState phase6State)
{
    if (!C.present) return false;
    // Dormant regime: no admissible drain (P7 activation, §6).
    if (regime == LitenyxTopoRegime::PreDerivation) return false;
    // Start boundary must be OBS_WINDOW-aligned (D8).
    if (C.drainStartHeight % LITENYX_TOPOLOGY_OBS_WINDOW != 0) return false;
    // Evaluated only at/after the start boundary (mirrors IsDraining time gate).
    if (height < C.drainStartHeight) return false;
    // D2: meaningful only for an Active + AUTHORIZED identity.
    if (LitenyxClassifyChainId(L, C.chainId) != LitenyxChainIdStatus::Active)
        return false;
    if (phase6State != LitenyxExecutionAuthorityState::AUTHORIZED) return false;
    // D11 edge-only (Model B): the drained identity must occupy the current
    // highest active lane (N-1), computed from L_h alone — no future prediction.
    uint8_t boundLane = 0, highestLane = 0;
    if (!LitenyxAuthoritativeLaneForChainId(L, C.chainId, boundLane)) return false;
    if (!LitenyxCurrentHighestActiveLane(L, highestLane)) return false;
    if (boundLane != highestLane) return false;
    return true;
}

// ---- §3.1 DrainCapabilityProjection: the monotonic overlay ------------------
// CONSUMES a frozen Phase-6 result and the drain-in-force fact; applies the
// FROZEN monotonic restriction (D1):
//
//   effMayBind  = mayBind_P6  AND NOT isDraining
//   effMayRoute = mayRoute_P6                        (drain NEVER changes route)
//
// D2: drain is meaningful only when Phase-6 == AUTHORIZED; for REVOKED/UNKNOWN
// the Phase-6 result already denies both, isDraining is forced inert, and the
// mode stays NORMAL. The Phase-6 state is echoed UNCHANGED (D0). PURE.
inline LitenyxDrainCapabilityProjection LitenyxProjectDrainCapability(
    const LitenyxExecutionAuthorityResult& p6,
    bool isDraining)
{
    LitenyxDrainCapabilityProjection out;
    out.state   = p6.state;         // unchanged (D0)
    out.chainId = p6.chainId;
    out.laneId  = p6.laneId;
    out.height  = p6.height;

    // D2: drain only has meaning for an AUTHORIZED identity.
    const bool authorized = (p6.state == LitenyxExecutionAuthorityState::AUTHORIZED);
    const bool draining   = authorized && isDraining;

    out.mode = draining ? LitenyxOperationalCapabilityMode::DRAINING
                        : LitenyxOperationalCapabilityMode::NORMAL;

    // D1 monotonic restriction (subset of Phase-6 capabilities, always).
    out.effMayBind  = p6.mayBind && !draining;
    out.effMayRoute = p6.mayRoute;   // drain never grants nor revokes route

    return out;
}

// ---- Convenience: full pipeline from a lane assertion + drain fact ----------
// Mirrors how the daemon hook (later gated) would compose: resolve the frozen
// Phase-6 decision for the asserted lane, compute IsDraining from L_h + the
// commitment, then apply the monotonic overlay. Introduces NO authority logic.
inline LitenyxDrainCapabilityProjection LitenyxResolveDrainForLane(
    const LitenyxChainIdLifecycleState& L,
    const LitenyxDrainCommitment& C,
    uint32_t laneId,
    uint32_t height,
    LitenyxTopoRegime execRegime)
{
    const LitenyxExecutionAuthorityResult p6 =
        LitenyxResolveExecutionAuthorityForLane(L, laneId, height, execRegime);
    const bool draining = LitenyxIsDraining(L, C, height);
    return LitenyxProjectDrainCapability(p6, draining);
}

#endif // LITENYX_DRAINING_AUTHORITY_H
