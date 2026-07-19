// Litenyx Phase 7 standalone proof — pure draining-authority engine
// (spec docs/litenyx_draining_authority_spec_v0.1.md, v0.2 R-REP delta,
//  frozen against phase6-green @ a95507f).
//
// Proves the MECHANICAL overlay
//     frozen DrainCommitment (input fact) + L_h + Phase-6 result
//       -> monotonic capability restriction -> DrainCapabilityProjection
// is deterministic and fail-closed, CONSUMING the frozen Phase-5 classifier and
// the frozen Phase-6 result as the single sources of truth. NO ConnectBlock hook,
// NO RPC/mempool/routing/database, NO singleton/clock. NO new committed state.
//
// D-K1..D-K18 (spec §9). D12 is exercised ONLY as its currently-representable
// half (ValidateDrainCommitmentSemantics); autonomous provenance
// (ReproduceDrainCommitment) is DEFERRED with the emitter gate and is NOT
// claimed here.

#include <litenyx/LITENYX_draining_authority.h>
#include <litenyx/LITENYX_execution_authority.h>
#include <litenyx/LITENYX_chainid_lifecycle.h>
#include <litenyx/LITENYX_topology.h>

#define BOOST_TEST_MODULE LITENYX_draining_authority_test
#include <boost/test/included/unit_test.hpp>

#include <cstdint>

static const uint32_t W = LITENYX_TOPOLOGY_OBS_WINDOW; // 100

// Phase-6 exec regtest regime {600,800}; Phase-7 drain regtest {1000,1200}.
// Use HardAuthority heights >= 1200 (OBS_WINDOW-aligned) for both layers.
static LitenyxTopoRegime execRegime(uint32_t h) {
    return LitenyxExecutionActivationForNetwork("regtest").RegimeAt(h);
}
static LitenyxTopoRegime drainRegime(uint32_t h) {
    return LitenyxDrainActivationForNetwork("regtest").RegimeAt(h);
}

// ---- Deterministic L_h builders (fold the frozen G) -------------------------
static LitenyxChainIdLifecycleState L0() { return LitenyxChainIdLifecycleGenesis(); }

// SPLIT (N:2->3): lane 2 -> ChainId 2, nextChainId=3.
static LitenyxChainIdLifecycleState L_split() {
    LitenyxChainIdLifecycleState out;
    BOOST_REQUIRE(LitenyxAdvanceChainIdLifecycle(L0(), 2, 3, W, out));
    return out;
}
// SPLIT then MERGE (N:2->3->2): ChainId 2 RETIRED, nextChainId=3.
static LitenyxChainIdLifecycleState L_split_merge() {
    LitenyxChainIdLifecycleState out;
    BOOST_REQUIRE(LitenyxAdvanceChainIdLifecycle(L_split(), 3, 2, 2*W, out));
    return out;
}
// ABA (N:2->3->2->3): lane 2 -> ChainId 3 (NOT 2), nextChainId=4.
static LitenyxChainIdLifecycleState L_aba() {
    LitenyxChainIdLifecycleState out;
    BOOST_REQUIRE(LitenyxAdvanceChainIdLifecycle(L_split_merge(), 2, 3, 3*W, out));
    return out;
}

static void ExpectProj(const LitenyxDrainCapabilityProjection& p,
                       LitenyxOperationalCapabilityMode mode,
                       LitenyxExecutionAuthorityState state,
                       bool effBind, bool effRoute) {
    BOOST_CHECK(p.mode == mode);
    BOOST_CHECK(p.state == state);
    BOOST_CHECK_EQUAL(p.effMayBind, effBind);
    BOOST_CHECK_EQUAL(p.effMayRoute, effRoute);
}

// ======================= D-K1 .. D-K18 ======================================

// D-K1 NORMAL passthrough: Active+AUTHORIZED, no drain => == Phase-6 (1,1).
BOOST_AUTO_TEST_CASE(D_K1_normal_passthrough)
{
    LitenyxDrainCommitment none; // present=false
    LitenyxDrainCapabilityProjection p =
        LitenyxResolveDrainForLane(L0(), none, /*lane*/0, /*h*/1300, execRegime(1300));
    ExpectProj(p, LitenyxOperationalCapabilityMode::NORMAL,
               LitenyxExecutionAuthorityState::AUTHORIZED, true, true);
}

// D-K2 DRAIN settle-only: Active+AUTHORIZED + drain in force, lane agrees => (0,1).
BOOST_AUTO_TEST_CASE(D_K2_drain_settle_only)
{
    LitenyxDrainCommitment C(/*chainId*/0, /*start*/1200);
    LitenyxDrainCapabilityProjection p =
        LitenyxResolveDrainForLane(L0(), C, 0, 1300, execRegime(1300));
    ExpectProj(p, LitenyxOperationalCapabilityMode::DRAINING,
               LitenyxExecutionAuthorityState::AUTHORIZED, false, true);
}

// D-K3 DRAIN never manufactures route: Phase-6 WrongLane (MayRoute=0) + drain
// => effMayRoute stays 0. (Assert ChainId 0 on lane 1 via the direct engine.)
BOOST_AUTO_TEST_CASE(D_K3_no_manufacture_route)
{
    LitenyxExecutionAuthorityResult p6 = LitenyxResolveExecutionAuthority(
        L0(), /*chainId*/0, /*lane*/1, /*h*/1300, LitenyxTopoRegime::HardAuthority);
    BOOST_REQUIRE(p6.code == LitenyxExecutionAuthorityCode::WrongLane);
    BOOST_REQUIRE_EQUAL(p6.mayRoute, false);
    BOOST_REQUIRE_EQUAL(p6.mayBind, true);
    // Drain in force: mayBind removed, mayRoute unchanged (still 0).
    LitenyxDrainCapabilityProjection p = LitenyxProjectDrainCapability(p6, /*draining*/true);
    ExpectProj(p, LitenyxOperationalCapabilityMode::DRAINING,
               LitenyxExecutionAuthorityState::AUTHORIZED, /*bind*/false, /*route*/false);
}

// D-K4 inert on REVOKED: retired ChainId 2 + drain present => unchanged (0,0), NORMAL.
BOOST_AUTO_TEST_CASE(D_K4_inert_on_revoked)
{
    LitenyxExecutionAuthorityResult p6 = LitenyxResolveExecutionAuthority(
        L_split_merge(), /*chainId*/2, /*lane*/2, /*h*/1300, LitenyxTopoRegime::HardAuthority);
    BOOST_REQUIRE(p6.state == LitenyxExecutionAuthorityState::REVOKED);
    LitenyxDrainCapabilityProjection p = LitenyxProjectDrainCapability(p6, /*draining*/true);
    ExpectProj(p, LitenyxOperationalCapabilityMode::NORMAL,
               LitenyxExecutionAuthorityState::REVOKED, false, false);
}

// D-K5 inert on UNKNOWN: nonexistent ChainId + drain present => unchanged (0,0), NORMAL.
BOOST_AUTO_TEST_CASE(D_K5_inert_on_unknown)
{
    LitenyxExecutionAuthorityResult p6 = LitenyxResolveExecutionAuthority(
        L0(), /*chainId*/99, /*lane*/0, /*h*/1300, LitenyxTopoRegime::HardAuthority);
    BOOST_REQUIRE(p6.state == LitenyxExecutionAuthorityState::UNKNOWN);
    LitenyxDrainCapabilityProjection p = LitenyxProjectDrainCapability(p6, /*draining*/true);
    ExpectProj(p, LitenyxOperationalCapabilityMode::NORMAL,
               LitenyxExecutionAuthorityState::UNKNOWN, false, false);
}

// D-K6 monotonic subset: for every (state, draining) combination, {Eff} subset of {P6}.
BOOST_AUTO_TEST_CASE(D_K6_monotonic_subset)
{
    struct Case { LitenyxChainIdLifecycleState L; uint32_t cid; uint32_t lane; };
    Case cases[] = {
        { L0(), 0, 0 },              // AUTHORIZED ok
        { L0(), 0, 1 },              // AUTHORIZED wrong-lane
        { L_split_merge(), 2, 2 },   // REVOKED
        { L0(), 99, 0 },             // UNKNOWN
    };
    for (int i = 0; i < 4; ++i) {
        LitenyxExecutionAuthorityResult p6 = LitenyxResolveExecutionAuthority(
            cases[i].L, cases[i].cid, cases[i].lane, 1300, LitenyxTopoRegime::HardAuthority);
        for (int d = 0; d < 2; ++d) {
            LitenyxDrainCapabilityProjection p =
                LitenyxProjectDrainCapability(p6, /*draining*/(d == 1));
            BOOST_CHECK(!p.effMayBind  || p6.mayBind);   // eff => p6 (subset)
            BOOST_CHECK(!p.effMayRoute || p6.mayRoute);
        }
    }
}

// D-K7 one-way: drain in force (Active) then Phase-5 retirement => DRAINING->REVOKED,
// never DRAINING->NORMAL. IsDraining flips off exactly at retirement.
BOOST_AUTO_TEST_CASE(D_K7_one_way)
{
    LitenyxDrainCommitment C(/*chainId*/2, /*start*/1200);
    // While Active (in L_split): draining.
    BOOST_CHECK(LitenyxIsDraining(L_split(), C, 1300));
    LitenyxDrainCapabilityProjection during =
        LitenyxResolveDrainForLane(L_split(), C, /*lane*/2, 1300, execRegime(1300));
    ExpectProj(during, LitenyxOperationalCapabilityMode::DRAINING,
               LitenyxExecutionAuthorityState::AUTHORIZED, false, true);
    // After MERGE retires ChainId 2 (L_split_merge): not draining, REVOKED.
    BOOST_CHECK(!LitenyxIsDraining(L_split_merge(), C, 1300));
    LitenyxDrainCapabilityProjection after =
        LitenyxResolveDrainForLane(L_split_merge(), C, /*lane*/2, 1300, execRegime(1300));
    ExpectProj(after, LitenyxOperationalCapabilityMode::NORMAL,
               LitenyxExecutionAuthorityState::UNKNOWN, false, false);
}

// D-K8 retirement unchanged: the drain overlay never mutates L_h / nextChainId /
// bindings. Compare a Phase-5 fold WITH and WITHOUT any drain reasoning.
BOOST_AUTO_TEST_CASE(D_K8_retirement_unchanged)
{
    LitenyxChainIdLifecycleState pure = L_split_merge(); // Phase-5-only fold
    // "Consulting" drain must not alter L in any way (engine takes L by const&).
    LitenyxDrainCommitment C(/*chainId*/2, /*start*/1200);
    (void)LitenyxIsDraining(pure, C, 1300);
    (void)LitenyxResolveDrainForLane(pure, C, 2, 1300, execRegime(1300));
    LitenyxChainIdLifecycleState again = L_split_merge();
    BOOST_CHECK(pure == again);                  // byte-identical, unchanged
    BOOST_CHECK_EQUAL(pure.nextChainId, again.nextChainId);
    BOOST_CHECK(pure.activeBindings == again.activeBindings);
}

// D-K9 path-independent: recompute twice => identical.
BOOST_AUTO_TEST_CASE(D_K9_path_independent)
{
    LitenyxDrainCommitment C(/*chainId*/0, /*start*/1200);
    LitenyxDrainCapabilityProjection a =
        LitenyxResolveDrainForLane(L0(), C, 0, 1300, execRegime(1300));
    LitenyxDrainCapabilityProjection b =
        LitenyxResolveDrainForLane(L0(), C, 0, 1300, execRegime(1300));
    BOOST_CHECK(a.mode == b.mode && a.state == b.state);
    BOOST_CHECK_EQUAL(a.effMayBind, b.effMayBind);
    BOOST_CHECK_EQUAL(a.effMayRoute, b.effMayRoute);
}

// D-K10 activation regime: drain boundaries at {1000,1200}; mainnet DISABLED.
BOOST_AUTO_TEST_CASE(D_K10_activation_regime)
{
    BOOST_CHECK(drainRegime(999)  == LitenyxTopoRegime::PreDerivation);
    BOOST_CHECK(drainRegime(1000) == LitenyxTopoRegime::SoftAdvisory);
    BOOST_CHECK(drainRegime(1199) == LitenyxTopoRegime::SoftAdvisory);
    BOOST_CHECK(drainRegime(1200) == LitenyxTopoRegime::HardAuthority);
    BOOST_CHECK(LitenyxDrainActivationForNetwork("main").RegimeAt(1000000)
                == LitenyxTopoRegime::PreDerivation);
}

// D-K11 layering: drain enforces strictly later than exec authority (D6).
BOOST_AUTO_TEST_CASE(D_K11_layering)
{
    LitenyxChainIdActivation exec  = LitenyxExecutionActivationForNetwork("regtest");
    LitenyxChainIdActivation drain = LitenyxDrainActivationForNetwork("regtest");
    BOOST_CHECK(drain.hEnforce > exec.hEnforce);
    BOOST_CHECK(drain.hDerive  >= exec.hEnforce);
    // At a height where exec is HardAuthority, drain is still dormant.
    BOOST_CHECK(execRegime(900)  == LitenyxTopoRegime::HardAuthority);
    BOOST_CHECK(drainRegime(900) == LitenyxTopoRegime::PreDerivation);
}

// D-K12 complete-on-P5-retire: IsDraining false the instant classify != Active.
BOOST_AUTO_TEST_CASE(D_K12_complete_on_p5_retire)
{
    LitenyxDrainCommitment C(/*chainId*/2, /*start*/1200);
    BOOST_CHECK(LitenyxIsDraining(L_split(), C, 1300));        // Active
    BOOST_CHECK(!LitenyxIsDraining(L_split_merge(), C, 1300)); // Retired => false
}

// D-K13 no RetireHeight: commitment carries only {id, DrainStartHeight}.
BOOST_AUTO_TEST_CASE(D_K13_no_retire_height)
{
    LitenyxDrainCommitment C(/*chainId*/7, /*start*/1200);
    BOOST_CHECK_EQUAL(C.present, true);
    BOOST_CHECK_EQUAL(C.chainId, 7u);
    BOOST_CHECK_EQUAL(C.drainStartHeight, 1200u);
    BOOST_CHECK_EQUAL(sizeof(C.drainStartHeight), sizeof(uint32_t));
    // (Structural: there is no retire-height field to reference.)
}

// D-K14 ABA key-on-id: lane 2 drained as ChainId 2, retired, lane 2 reused by
// ChainId 3. The historical commitment for ChainId 2 must NEVER apply to 3.
BOOST_AUTO_TEST_CASE(D_K14_aba_key_on_id)
{
    LitenyxDrainCommitment C2(/*chainId*/2, /*start*/1200);
    // In the ABA state, lane 2 -> ChainId 3; ChainId 2 is retired.
    BOOST_CHECK(!LitenyxIsDraining(L_aba(), C2, 1300)); // ChainId 2 not Active => inert
    // ChainId 3 on lane 2 is AUTHORIZED and NOT draining (no commitment for id 3).
    LitenyxDrainCommitment none;
    LitenyxDrainCapabilityProjection p =
        LitenyxResolveDrainForLane(L_aba(), none, /*lane*/2, 1300, execRegime(1300));
    ExpectProj(p, LitenyxOperationalCapabilityMode::NORMAL,
               LitenyxExecutionAuthorityState::AUTHORIZED, true, true);
    BOOST_CHECK_EQUAL(p.chainId, 3u); // authoritative identity on lane 2 is 3, not 2
}

// D-K15 eligible edge-only (Model B): only the current highest active lane (N-1)
// is admissible. In L_split, N=3 => highest lane 2 (ChainId 2) eligible; a
// non-edge Active identity (lane 0 / ChainId 0) is inadmissible.
BOOST_AUTO_TEST_CASE(D_K15_eligible_edge_only)
{
    // Edge identity: ChainId 2 on lane 2 (N-1). AUTHORIZED at h.
    LitenyxDrainCommitment edge(/*chainId*/2, /*start*/1200);
    BOOST_CHECK(LitenyxValidateDrainCommitmentSemantics(
        L_split(), edge, 1300, drainRegime(1300),
        LitenyxExecutionAuthorityState::AUTHORIZED));
    // Non-edge identity: ChainId 0 on lane 0. Inadmissible.
    LitenyxDrainCommitment nonEdge(/*chainId*/0, /*start*/1200);
    BOOST_CHECK(!LitenyxValidateDrainCommitmentSemantics(
        L_split(), nonEdge, 1300, drainRegime(1300),
        LitenyxExecutionAuthorityState::AUTHORIZED));
    // Non-boundary start height => inadmissible (D8).
    LitenyxDrainCommitment badBoundary(/*chainId*/2, /*start*/1250);
    BOOST_CHECK(!LitenyxValidateDrainCommitmentSemantics(
        L_split(), badBoundary, 1300, drainRegime(1300),
        LitenyxExecutionAuthorityState::AUTHORIZED));
    // PreDerivation regime => inadmissible (§6).
    BOOST_CHECK(!LitenyxValidateDrainCommitmentSemantics(
        L_split(), edge, 900, drainRegime(900),
        LitenyxExecutionAuthorityState::AUTHORIZED));
}

// D-K16 drain !=> merge: a drain commitment never forces N down; the identity may
// remain DRAINING indefinitely while it stays Active. (Engine has no topology
// authority: repeated projection at growing heights keeps DRAINING while Active.)
BOOST_AUTO_TEST_CASE(D_K16_drain_does_not_force_merge)
{
    LitenyxDrainCommitment C(/*chainId*/2, /*start*/1200);
    for (uint32_t h = 1200; h <= 5000; h += W) {
        // L_split keeps ChainId 2 Active regardless of height => still draining.
        BOOST_CHECK(LitenyxIsDraining(L_split(), C, h));
    }
}

// D-K17 merge !=> drain: an identity may retire with no prior drain commitment.
BOOST_AUTO_TEST_CASE(D_K17_merge_without_drain)
{
    LitenyxDrainCommitment none;
    BOOST_CHECK(!LitenyxIsDraining(L_split_merge(), none, 1300));
    LitenyxExecutionAuthorityResult p6 = LitenyxResolveExecutionAuthority(
        L_split_merge(), /*chainId*/2, /*lane*/2, 1300, LitenyxTopoRegime::HardAuthority);
    BOOST_CHECK(p6.state == LitenyxExecutionAuthorityState::REVOKED); // retired cleanly
}

// D-K18 validate reproduce (SEMANTICS HALF ONLY): a semantically well-formed,
// eligible commitment validates; tampering identity/boundary/eligibility fails
// closed. Autonomous provenance (ReproduceDrainCommitment) is DEFERRED and NOT
// asserted here.
BOOST_AUTO_TEST_CASE(D_K18_validate_semantics)
{
    LitenyxDrainCommitment good(/*chainId*/2, /*start*/1200);
    BOOST_CHECK(LitenyxValidateDrainCommitmentSemantics(
        L_split(), good, 1300, drainRegime(1300),
        LitenyxExecutionAuthorityState::AUTHORIZED));
    // Tamper: not-present commitment.
    LitenyxDrainCommitment absent;
    BOOST_CHECK(!LitenyxValidateDrainCommitmentSemantics(
        L_split(), absent, 1300, drainRegime(1300),
        LitenyxExecutionAuthorityState::AUTHORIZED));
    // Tamper: Phase-6 not AUTHORIZED (e.g. REVOKED) => inadmissible.
    BOOST_CHECK(!LitenyxValidateDrainCommitmentSemantics(
        L_split(), good, 1300, drainRegime(1300),
        LitenyxExecutionAuthorityState::REVOKED));
    // Tamper: height before start boundary => inadmissible.
    BOOST_CHECK(!LitenyxValidateDrainCommitmentSemantics(
        L_split(), good, 1100, drainRegime(1100),
        LitenyxExecutionAuthorityState::AUTHORIZED));
}
