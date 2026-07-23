// Litenyx INT-OPEN-1 / M3 unit tests — attempt-scoped candidate spend delta.
//
// Proves the M3 contract (docs/litenyx_int_open_1_fix_spec_v0.1.md R1-R3,
// map v0.1) directly against the REAL classes in litenyx/LITENYX_sharedstate.cpp
// (LitenyxCandidateSpendDelta, LitenyxSpendPublishScope, LitenyxSharedSpendSet),
// NOT a model. The attach into ConnectTip is verified separately (patch dry-run
// against dogecoin v1.14.9); here we prove the mechanism the hooks drive:
//
//   R1 invisibility     — staged-but-unpublished spends are NOT visible to the
//                         live reader (LitenyxIsSharedSpent).
//   R2 publish-once     — an explicit publish applies the delta exactly once.
//   R3 discard-default  — a scope that exits WITHOUT publish leaves the live set
//                         bit-for-bit unchanged (the FlushStateToDisk-fail window).
//   symmetry            — publish then RevertSpend cancels (connect/disconnect).
//   within-attempt DS   — same outpoint staged twice is rejected, nothing staged.
//   batch fold          — N published deltas == fold of all N, order preserved.
//
// This TU compiles the real LITENYX_sharedstate.cpp (it depends only on
// uint256.h + tinyformat.h from the vendored src include path).

#include <litenyx/LITENYX_sharedstate.h>

#define BOOST_TEST_MODULE LITENYX_shared_delta_test
#include <boost/test/unit_test.hpp>

#include <cstdint>
#include <cstdio>
#include <string>

// Bring the real implementation into this standalone test binary.
#include "../../litenyx/LITENYX_sharedstate.cpp"

namespace {

// Build a distinct uint256 from an integer seed via its hex string. Deterministic
// and collision-free across the small seeds used here.
LitenyxOutPoint OP(uint32_t seed, uint32_t n)
{
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%08x", seed);
    return LitenyxOutPoint{uint256S(std::string(buf)), n};
}

// Clean the global singleton before each case so cases are independent.
struct CleanSet {
    CleanSet() { LitenyxSharedSpendSet::Instance().Reset(); }
    ~CleanSet() { LitenyxSharedSpendSet::Instance().Reset(); }
};

} // namespace

BOOST_AUTO_TEST_SUITE(LITENYX_shared_delta_tests)

// R1: staging is invisible to the live reader until publish.
BOOST_AUTO_TEST_CASE(staged_delta_is_invisible_to_live_reader)
{
    CleanSet clean;
    LitenyxOutPoint u = OP(0xA1, 0);

    {
        LitenyxSpendPublishScope scope;
        LitenyxCandidateSpendDelta* d = LitenyxActiveCandidateDelta();
        BOOST_REQUIRE(d != nullptr);

        BOOST_CHECK(d->StageSpend(u, 0));
        // Live reader must NOT see the staged spend (R1 / single reader path).
        BOOST_CHECK(!LitenyxSharedSpendSet::Instance().IsSpent(u));
        BOOST_CHECK(!LitenyxIsSharedSpent(u.hash, u.n));
        BOOST_CHECK(d->StagedHere(u)); // but it IS staged internally
        // no PublishActive() -> discard on scope exit
    }

    // R3: scope exited without publish -> live set unchanged.
    BOOST_CHECK(!LitenyxSharedSpendSet::Instance().IsSpent(u));
}

// R3 (core, INT-Q4 window): a connect attempt that stages then aborts before
// publish leaves the live set bit-for-bit unchanged.
BOOST_AUTO_TEST_CASE(discard_by_default_leaves_live_set_unchanged)
{
    CleanSet clean;
    LitenyxOutPoint pre = OP(0xB0, 0); // pre-existing live spend
    BOOST_REQUIRE(LitenyxSharedSpendSet::Instance().RecordSpend(pre, 1));

    LitenyxOutPoint a = OP(0xB1, 0);
    LitenyxOutPoint b = OP(0xB2, 0);
    {
        LitenyxSpendPublishScope scope; // simulates ConnectTip attempt
        LitenyxCandidateSpendDelta* d = LitenyxActiveCandidateDelta();
        BOOST_REQUIRE(d != nullptr);
        BOOST_CHECK(d->StageSpend(a, 0));
        BOOST_CHECK(d->StageSpend(b, 0));
        // ... imagine ConnectBlock succeeded but FlushStateToDisk then fails:
        // we simply return without PublishActive().
    }

    // Live set == exactly the pre-existing state; the aborted attempt leaked nothing.
    BOOST_CHECK(LitenyxSharedSpendSet::Instance().IsSpent(pre));
    BOOST_CHECK(!LitenyxSharedSpendSet::Instance().IsSpent(a));
    BOOST_CHECK(!LitenyxSharedSpendSet::Instance().IsSpent(b));
}

// R2: explicit publish applies the whole delta exactly once; a second publish is
// a no-op (never double-applies on reorg re-entry).
BOOST_AUTO_TEST_CASE(publish_applies_delta_exactly_once)
{
    CleanSet clean;
    LitenyxOutPoint a = OP(0xC1, 0);
    LitenyxOutPoint b = OP(0xC2, 7);

    {
        LitenyxSpendPublishScope scope;
        LitenyxCandidateSpendDelta* d = LitenyxActiveCandidateDelta();
        BOOST_REQUIRE(d != nullptr);
        BOOST_CHECK(d->StageSpend(a, 0));
        BOOST_CHECK(d->StageSpend(b, 1));
        BOOST_CHECK(!LitenyxSharedSpendSet::Instance().IsSpent(a)); // still invisible
        scope.PublishActive();
        // Now visible with the correct confirming chain.
        BOOST_CHECK(LitenyxSharedSpendSet::Instance().IsSpent(a));
        BOOST_CHECK(LitenyxSharedSpendSet::Instance().IsSpent(b));
        BOOST_CHECK_EQUAL(LitenyxSharedSpendSet::Instance().ConfirmingChain(a), 0);
        BOOST_CHECK_EQUAL(LitenyxSharedSpendSet::Instance().ConfirmingChain(b), 1);
        BOOST_CHECK(d->IsPublished());
        scope.PublishActive(); // idempotent, no throw / no change
    }
    BOOST_CHECK(LitenyxSharedSpendSet::Instance().IsSpent(a));
    BOOST_CHECK(LitenyxSharedSpendSet::Instance().IsSpent(b));
}

// Connect/disconnect symmetry: publish (connect) then RevertSpend (disconnect)
// restores the live set — the candidate mechanism does not alter the frozen
// canonical inverse.
BOOST_AUTO_TEST_CASE(publish_then_revert_restores_live_set)
{
    CleanSet clean;
    LitenyxOutPoint a = OP(0xD1, 0);
    LitenyxOutPoint b = OP(0xD2, 0);

    {
        LitenyxSpendPublishScope scope;
        LitenyxActiveCandidateDelta()->StageSpend(a, 0);
        LitenyxActiveCandidateDelta()->StageSpend(b, 0);
        scope.PublishActive();
    }
    BOOST_CHECK(LitenyxSharedSpendSet::Instance().IsSpent(a));
    BOOST_CHECK(LitenyxSharedSpendSet::Instance().IsSpent(b));

    // Disconnect inverse (LitenyxDisconnectSharedState calls RevertSpend per input).
    LitenyxSharedSpendSet::Instance().RevertSpend(a);
    LitenyxSharedSpendSet::Instance().RevertSpend(b);
    BOOST_CHECK(!LitenyxSharedSpendSet::Instance().IsSpent(a));
    BOOST_CHECK(!LitenyxSharedSpendSet::Instance().IsSpent(b));
}

// Within-attempt double spend: same outpoint staged twice is rejected and nothing
// beyond the first stage is added; a live-set conflict is likewise rejected.
BOOST_AUTO_TEST_CASE(within_attempt_and_live_conflict_rejected)
{
    CleanSet clean;
    LitenyxOutPoint u = OP(0xE1, 0);
    LitenyxOutPoint live = OP(0xE2, 0);
    BOOST_REQUIRE(LitenyxSharedSpendSet::Instance().RecordSpend(live, 0));

    LitenyxSpendPublishScope scope;
    LitenyxCandidateSpendDelta* d = LitenyxActiveCandidateDelta();

    BOOST_CHECK(d->StageSpend(u, 0));     // first stage ok
    BOOST_CHECK(!d->StageSpend(u, 1));    // within-attempt double spend rejected
    BOOST_CHECK_EQUAL(d->Size(), 1u);     // nothing extra staged
    BOOST_CHECK(!d->StageSpend(live, 0)); // live-set conflict rejected
    BOOST_CHECK_EQUAL(d->Size(), 1u);
}

// Batch/IBD: N sequential attempts each publish independently; the final live set
// equals the fold of all N deltas (one publish per connected block, INT-Q3).
BOOST_AUTO_TEST_CASE(batch_publishes_fold_in_order)
{
    CleanSet clean;
    const int N = 5;
    for (int i = 0; i < N; ++i) {
        LitenyxSpendPublishScope scope;
        LitenyxOutPoint op = OP(0xF000 + i, 0);
        BOOST_REQUIRE(LitenyxActiveCandidateDelta()->StageSpend(op, (uint8_t)(i % 2)));
        scope.PublishActive();
    }
    for (int i = 0; i < N; ++i) {
        LitenyxOutPoint op = OP(0xF000 + i, 0);
        BOOST_CHECK(LitenyxSharedSpendSet::Instance().IsSpent(op));
        BOOST_CHECK_EQUAL(LitenyxSharedSpendSet::Instance().ConfirmingChain(op), (uint8_t)(i % 2));
    }
    // An outpoint never staged remains unspent.
    BOOST_CHECK(!LitenyxSharedSpendSet::Instance().IsSpent(OP(0xF000 + N, 0)));
}

BOOST_AUTO_TEST_SUITE_END()
