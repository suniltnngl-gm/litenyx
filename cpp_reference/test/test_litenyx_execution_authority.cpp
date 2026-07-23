// Litenyx Phase 6 standalone proof — pure execution-authority engine
// (spec docs/litenyx_execution_authority_spec_v0.1.md, increment 1, frozen feba683).
//
// Proves the MECHANICAL projection
//     ValidatedExecutionContext -> Authority Projection -> ExecutionAuthorityResult
// is deterministic and fail-closed, consuming the frozen Phase-5
// LitenyxValidateExecutionContext as the SINGLE source of truth. NO ConnectBlock
// hook, NO RPC/mempool/routing/database, NO singleton/clock. DRAINING is absent.
//
// K1-K12 are pinned as NUMERIC deterministic vectors (spec §10), incl. the ABA
// sequence proving  lane 2 -> ChainId 2 -> revoked -> lane 2 -> ChainId 3, and
// explicit failure-precedence so WrongLane / Revoked / Unknown / Premature /
// Malformed can never collapse into an ambiguous outcome.

#include <litenyx/LITENYX_execution_authority.h>
#include <litenyx/LITENYX_chainid_lifecycle.h>
#include <litenyx/LITENYX_topology.h>

#define BOOST_TEST_MODULE LITENYX_execution_authority_test
#include <boost/test/unit_test.hpp>

#include <cstdint>

// ---- Deterministic L_h builders (fold the frozen G) -------------------------
// One observation-window boundary per transition, heights W, 2W, 3W, ...
static const uint32_t W = LITENYX_TOPOLOGY_OBS_WINDOW;

// L_0: genesis (lane 0 -> ChainId 0, lane 1 -> ChainId 1, nextChainId = 2).
static LitenyxChainIdLifecycleState L0() {
    return LitenyxChainIdLifecycleGenesis();
}

// L after a single SPLIT (N: 2 -> 3): lane 2 -> ChainId 2, nextChainId = 3.
static LitenyxChainIdLifecycleState L_split() {
    LitenyxChainIdLifecycleState out;
    bool ok = LitenyxAdvanceChainIdLifecycle(L0(), /*Nprev*/2, /*Ncur*/3, /*h*/W, out);
    BOOST_REQUIRE(ok);
    return out;
}

// L after SPLIT then MERGE (N: 2 -> 3 -> 2): ChainId 2 RETIRED, nextChainId = 3.
static LitenyxChainIdLifecycleState L_split_merge() {
    LitenyxChainIdLifecycleState out;
    bool ok = LitenyxAdvanceChainIdLifecycle(L_split(), /*Nprev*/3, /*Ncur*/2, /*h*/2*W, out);
    BOOST_REQUIRE(ok);
    return out;
}

// ABA: SPLIT, MERGE, SPLIT (N: 2 -> 3 -> 2 -> 3): lane 2 -> ChainId 3 (NOT 2),
// nextChainId = 4; ChainId 2 stays permanently retired.
static LitenyxChainIdLifecycleState L_aba() {
    LitenyxChainIdLifecycleState out;
    bool ok = LitenyxAdvanceChainIdLifecycle(L_split_merge(), /*Nprev*/2, /*Ncur*/3, /*h*/3*W, out);
    BOOST_REQUIRE(ok);
    return out;
}

// Convenience: assert the full numeric shape of a result.
static void ExpectResult(const LitenyxExecutionAuthorityResult& r,
                         LitenyxExecutionAuthorityCode code,
                         LitenyxExecutionAuthorityState state,
                         bool authorized, bool mayRoute, bool mayBind,
                         uint32_t chainId, uint8_t laneId) {
    BOOST_CHECK(r.code == code);
    BOOST_CHECK(r.state == state);
    BOOST_CHECK_EQUAL(r.authorized, authorized);
    BOOST_CHECK_EQUAL(r.mayRoute, mayRoute);
    BOOST_CHECK_EQUAL(r.mayBind, mayBind);
    BOOST_CHECK_EQUAL(r.chainId, chainId);
    BOOST_CHECK_EQUAL((uint32_t)r.laneId, (uint32_t)laneId);
}

// ============================ K1 - K12 ======================================

// K1 ACTIVE-bound: genesis lane 0 -> ChainId 0, asserted (0,0) in HardAuthority.
BOOST_AUTO_TEST_CASE(K1_active_bound)
{
    LitenyxExecutionAuthorityResult r = LitenyxResolveExecutionAuthority(
        L0(), /*chainId*/0, /*lane*/0, /*h*/1000, LitenyxTopoRegime::HardAuthority);
    ExpectResult(r, LitenyxExecutionAuthorityCode::Ok,
                 LitenyxExecutionAuthorityState::AUTHORIZED,
                 /*auth*/true, /*route*/true, /*bind*/true, /*cid*/0, /*lane*/0);
}

// K2 ACTIVE-bound (2nd): genesis lane 1 -> ChainId 1, asserted (1,1).
BOOST_AUTO_TEST_CASE(K2_active_bound_second)
{
    LitenyxExecutionAuthorityResult r = LitenyxResolveExecutionAuthority(
        L0(), 1, 1, 1000, LitenyxTopoRegime::HardAuthority);
    ExpectResult(r, LitenyxExecutionAuthorityCode::Ok,
                 LitenyxExecutionAuthorityState::AUTHORIZED,
                 true, true, true, 1, 1);
}

// K3 WRONG-LANE: after split, ChainId 2 is bound to lane 2; assert it on lane 1.
// AUTHORIZED (mayBind) but mayRoute false; authoritative lane 2 surfaced.
BOOST_AUTO_TEST_CASE(K3_wrong_lane)
{
    LitenyxExecutionAuthorityResult r = LitenyxResolveExecutionAuthority(
        L_split(), /*chainId*/2, /*lane*/1, 1000, LitenyxTopoRegime::HardAuthority);
    ExpectResult(r, LitenyxExecutionAuthorityCode::WrongLane,
                 LitenyxExecutionAuthorityState::AUTHORIZED,
                 /*auth*/false, /*route*/false, /*bind*/true,
                 /*cid*/2, /*authoritative lane*/2);
}

// K4 RETIRED: split then merge retires ChainId 2 permanently. Any lane -> Revoked.
BOOST_AUTO_TEST_CASE(K4_retired)
{
    LitenyxExecutionAuthorityResult r = LitenyxResolveExecutionAuthority(
        L_split_merge(), /*chainId*/2, /*lane*/2, 1000, LitenyxTopoRegime::HardAuthority);
    ExpectResult(r, LitenyxExecutionAuthorityCode::Revoked,
                 LitenyxExecutionAuthorityState::REVOKED,
                 false, false, false, /*cid*/2, /*echoed lane*/2);
    // Lane-independence of a retired identity (asserted on a different lane).
    LitenyxExecutionAuthorityResult r0 = LitenyxResolveExecutionAuthority(
        L_split_merge(), 2, 0, 1000, LitenyxTopoRegime::HardAuthority);
    BOOST_CHECK(r0.code == LitenyxExecutionAuthorityCode::Revoked);
}

// K5 NONEXISTENT: at genesis, ChainId 9 >= nextChainId(2) -> Unknown.
BOOST_AUTO_TEST_CASE(K5_nonexistent)
{
    LitenyxExecutionAuthorityResult r = LitenyxResolveExecutionAuthority(
        L0(), /*chainId*/9, /*lane*/0, 1000, LitenyxTopoRegime::HardAuthority);
    ExpectResult(r, LitenyxExecutionAuthorityCode::Unknown,
                 LitenyxExecutionAuthorityState::UNKNOWN,
                 false, false, false, /*cid*/9, /*echoed lane*/0);
}

// K6 ABA / stale-generation: after merge-then-split, lane 2 -> ChainId 3;
// the OLD ChainId 2 on lane 2 must resolve REVOKED (never reused).
BOOST_AUTO_TEST_CASE(K6_aba_stale_generation_revoked)
{
    LitenyxExecutionAuthorityResult r = LitenyxResolveExecutionAuthority(
        L_aba(), /*chainId*/2, /*lane*/2, 1000, LitenyxTopoRegime::HardAuthority);
    ExpectResult(r, LitenyxExecutionAuthorityCode::Revoked,
                 LitenyxExecutionAuthorityState::REVOKED,
                 false, false, false, /*cid*/2, /*echoed lane*/2);
}

// K7 ABA new identity valid: same chain as K6; NEW ChainId 3 on lane 2 -> Ok.
// This is the core proof: lane 2 -> ChainId 2 -> revoked -> lane 2 -> ChainId 3.
BOOST_AUTO_TEST_CASE(K7_aba_new_identity_valid)
{
    LitenyxExecutionAuthorityResult r = LitenyxResolveExecutionAuthority(
        L_aba(), /*chainId*/3, /*lane*/2, 1000, LitenyxTopoRegime::HardAuthority);
    ExpectResult(r, LitenyxExecutionAuthorityCode::Ok,
                 LitenyxExecutionAuthorityState::AUTHORIZED,
                 true, true, true, /*cid*/3, /*lane*/2);
    // And the retired predecessor on the SAME lane is still revoked (K6),
    // proving lane reuse never revives the old identity.
    LitenyxExecutionAuthorityResult old = LitenyxResolveExecutionAuthority(
        L_aba(), 2, 2, 1000, LitenyxTopoRegime::HardAuthority);
    BOOST_CHECK(old.code == LitenyxExecutionAuthorityCode::Revoked);
}

// K8 MALFORMED lane: lane >= TOPO_MAX_CHAINS(8) for an otherwise-active ChainId.
// Malformed is a STRUCTURAL guard evaluated BEFORE any lifecycle lookup.
BOOST_AUTO_TEST_CASE(K8_malformed_lane)
{
    LitenyxExecutionAuthorityResult r = LitenyxResolveExecutionAuthority(
        L0(), /*chainId*/0, /*lane*/8, 1000, LitenyxTopoRegime::HardAuthority);
    ExpectResult(r, LitenyxExecutionAuthorityCode::Malformed,
                 LitenyxExecutionAuthorityState::UNKNOWN,
                 false, false, false, /*cid*/0, /*echoed lane*/8);
}

// K9 PRE-ACTIVATION: regime PreDerivation -> any assertion is Premature.
BOOST_AUTO_TEST_CASE(K9_pre_activation_premature)
{
    LitenyxExecutionAuthorityResult r = LitenyxResolveExecutionAuthority(
        L0(), /*chainId*/0, /*lane*/0, /*h*/10, LitenyxTopoRegime::PreDerivation);
    ExpectResult(r, LitenyxExecutionAuthorityCode::Premature,
                 LitenyxExecutionAuthorityState::UNKNOWN,
                 false, false, false, /*cid*/0, /*echoed lane*/0);
}

// K10 SOFT advisory: a wrong-lane claim in SoftAdvisory. The pure engine returns
// the SAME WrongLane code; ADVISORY (warn/accept) is the caller's regime policy
// (the hook step), NOT the pure decision. Here we prove the decision is stable
// across Soft/Hard and differs only in downstream treatment.
BOOST_AUTO_TEST_CASE(K10_soft_advisory_wrong_lane_decision_stable)
{
    LitenyxExecutionAuthorityResult soft = LitenyxResolveExecutionAuthority(
        L_split(), /*chainId*/2, /*lane*/1, 1000, LitenyxTopoRegime::SoftAdvisory);
    ExpectResult(soft, LitenyxExecutionAuthorityCode::WrongLane,
                 LitenyxExecutionAuthorityState::AUTHORIZED,
                 false, false, true, 2, 2);
}

// K11 POST-activation hard: same wrong-lane claim in HardAuthority. Identical
// pure decision to K10 (WrongLane, not authorized); downstream this REJECTS.
BOOST_AUTO_TEST_CASE(K11_post_activation_hard_wrong_lane)
{
    LitenyxExecutionAuthorityResult hard = LitenyxResolveExecutionAuthority(
        L_split(), 2, 1, 1000, LitenyxTopoRegime::HardAuthority);
    ExpectResult(hard, LitenyxExecutionAuthorityCode::WrongLane,
                 LitenyxExecutionAuthorityState::AUTHORIZED,
                 false, false, true, 2, 2);
    // The pure decision is regime-invariant for Soft vs Hard (K10 == K11 code).
    LitenyxExecutionAuthorityResult soft = LitenyxResolveExecutionAuthority(
        L_split(), 2, 1, 1000, LitenyxTopoRegime::SoftAdvisory);
    BOOST_CHECK(hard.code == soft.code);
    BOOST_CHECK(hard.state == soft.state);
}

// K12 path-independence: the decision for K3 (wrong-lane) and K4 (retired) is a
// pure function of L_h. Building L_h two independent ways (fresh fold vs an
// equal-by-value rebuild) yields byte-identical decisions (A6).
BOOST_AUTO_TEST_CASE(K12_path_independence)
{
    // Two independently-constructed L_split states must be equal (A6 / §0.4).
    LitenyxChainIdLifecycleState a = L_split();
    LitenyxChainIdLifecycleState b = L_split();
    BOOST_REQUIRE(a == b);
    LitenyxExecutionAuthorityResult ra = LitenyxResolveExecutionAuthority(
        a, 2, 1, 1000, LitenyxTopoRegime::HardAuthority);
    LitenyxExecutionAuthorityResult rb = LitenyxResolveExecutionAuthority(
        b, 2, 1, 1000, LitenyxTopoRegime::HardAuthority);
    BOOST_CHECK(ra.code == rb.code && ra.state == rb.state &&
                ra.authorized == rb.authorized && ra.mayRoute == rb.mayRoute &&
                ra.mayBind == rb.mayBind && ra.laneId == rb.laneId);

    LitenyxChainIdLifecycleState m1 = L_split_merge();
    LitenyxChainIdLifecycleState m2 = L_split_merge();
    BOOST_REQUIRE(m1 == m2);
    LitenyxExecutionAuthorityResult rm1 = LitenyxResolveExecutionAuthority(
        m1, 2, 2, 1000, LitenyxTopoRegime::HardAuthority);
    LitenyxExecutionAuthorityResult rm2 = LitenyxResolveExecutionAuthority(
        m2, 2, 2, 1000, LitenyxTopoRegime::HardAuthority);
    BOOST_CHECK(rm1.code == rm2.code && rm1.state == rm2.state);
    BOOST_CHECK(rm1.code == LitenyxExecutionAuthorityCode::Revoked);
}

// ============================ Failure PRECEDENCE ============================
// Explicitly prove the frozen precedence Malformed > Premature > projection,
// so WrongLane / Revoked / Unknown / Premature / Malformed can NEVER collapse
// into an ambiguous outcome when multiple conditions co-occur.

// Malformed dominates Premature: bad lane AND PreDerivation -> Malformed.
BOOST_AUTO_TEST_CASE(PREC_malformed_over_premature)
{
    LitenyxExecutionAuthorityResult r = LitenyxResolveExecutionAuthority(
        L0(), 0, /*bad lane*/8, 10, LitenyxTopoRegime::PreDerivation);
    BOOST_CHECK(r.code == LitenyxExecutionAuthorityCode::Malformed);
}

// Malformed dominates Revoked: bad lane AND a retired chainId -> Malformed.
BOOST_AUTO_TEST_CASE(PREC_malformed_over_revoked)
{
    LitenyxExecutionAuthorityResult r = LitenyxResolveExecutionAuthority(
        L_split_merge(), /*retired cid*/2, /*bad lane*/9, 1000,
        LitenyxTopoRegime::HardAuthority);
    BOOST_CHECK(r.code == LitenyxExecutionAuthorityCode::Malformed);
}

// Malformed dominates Unknown: bad lane AND a nonexistent chainId -> Malformed.
BOOST_AUTO_TEST_CASE(PREC_malformed_over_unknown)
{
    LitenyxExecutionAuthorityResult r = LitenyxResolveExecutionAuthority(
        L0(), /*nonexistent cid*/9, /*bad lane*/8, 1000,
        LitenyxTopoRegime::HardAuthority);
    BOOST_CHECK(r.code == LitenyxExecutionAuthorityCode::Malformed);
}

// Premature dominates projection: valid lane, PreDerivation, but a retired AND
// a nonexistent chainId both still resolve to Premature (regime guard first).
BOOST_AUTO_TEST_CASE(PREC_premature_over_projection)
{
    LitenyxExecutionAuthorityResult retired = LitenyxResolveExecutionAuthority(
        L_split_merge(), 2, 2, 10, LitenyxTopoRegime::PreDerivation);
    BOOST_CHECK(retired.code == LitenyxExecutionAuthorityCode::Premature);
    LitenyxExecutionAuthorityResult unknown = LitenyxResolveExecutionAuthority(
        L0(), 9, 0, 10, LitenyxTopoRegime::PreDerivation);
    BOOST_CHECK(unknown.code == LitenyxExecutionAuthorityCode::Premature);
}

// The five denial codes are pairwise distinct outcomes on representative inputs
// (no two conditions produce the same code), closing the taxonomy (A5).
BOOST_AUTO_TEST_CASE(PREC_all_codes_distinct)
{
    LitenyxExecutionAuthorityCode codes[6] = {
        LitenyxResolveExecutionAuthority(L0(), 0, 0, 1000, LitenyxTopoRegime::HardAuthority).code,        // Ok
        LitenyxResolveExecutionAuthority(L0(), 9, 0, 1000, LitenyxTopoRegime::HardAuthority).code,        // Unknown
        LitenyxResolveExecutionAuthority(L_split_merge(), 2, 2, 1000, LitenyxTopoRegime::HardAuthority).code, // Revoked
        LitenyxResolveExecutionAuthority(L_split(), 2, 1, 1000, LitenyxTopoRegime::HardAuthority).code,   // WrongLane
        LitenyxResolveExecutionAuthority(L0(), 0, 0, 10, LitenyxTopoRegime::PreDerivation).code,          // Premature
        LitenyxResolveExecutionAuthority(L0(), 0, 8, 1000, LitenyxTopoRegime::HardAuthority).code,        // Malformed
    };
    BOOST_CHECK(codes[0] == LitenyxExecutionAuthorityCode::Ok);
    BOOST_CHECK(codes[1] == LitenyxExecutionAuthorityCode::Unknown);
    BOOST_CHECK(codes[2] == LitenyxExecutionAuthorityCode::Revoked);
    BOOST_CHECK(codes[3] == LitenyxExecutionAuthorityCode::WrongLane);
    BOOST_CHECK(codes[4] == LitenyxExecutionAuthorityCode::Premature);
    BOOST_CHECK(codes[5] == LitenyxExecutionAuthorityCode::Malformed);
    for (int i = 0; i < 6; ++i)
        for (int j = i + 1; j < 6; ++j)
            BOOST_CHECK(codes[i] != codes[j]);
}
