// Litenyx consensus unit tests (Phase 2: fixed two-chain shared-state core).
// Pure header-only logic in litenyx/*. Compiled standalone via deploy/Makefile.

#include <litenyx/LITENYX_auxpow.h>
#include <litenyx/LITENYX_types.h>

#define BOOST_TEST_MODULE LITENYX_test
#include <boost/test/unit_test.hpp>

#include <set>
#include <utility>

using OutPoint = std::pair<uint256, uint32_t>; // (txid, index) — simplified

// A minimal model of the GLOBAL shared spent-set. The entire Phase-2 invariant
// rests on this being ONE set across all chains (spec §3).
struct GlobalSpentSet {
    std::set<OutPoint> spent;

    // Attempt to spend `op` on `chainId`. Returns true only if the outpoint is
    // currently unspent GLOBALLY and the chainId is in range. On success the
    // outpoint becomes globally spent (visible to every chain).
    bool TrySpend(const OutPoint& op, uint8_t nChainId) {
        if (nChainId >= LITENYX_MAX_CHAINS) return false;
        if (spent.count(op)) return false; // already spent on ANY chain
        spent.insert(op);
        return true;
    }

    bool IsSpent(const OutPoint& op) const {
        return spent.count(op) != 0;
    }
};

BOOST_AUTO_TEST_SUITE(LITENYX_tests)

// --- Chain params table is fixed and in range -------------------------------
BOOST_AUTO_TEST_CASE(chain_params_fixed_and_in_range)
{
    for (uint8_t c = 0; c < LITENYX_MAX_CHAINS; ++c) {
        const auto& p = LitenyxChainParamsFor(c);
        BOOST_CHECK_GT(p.nMinFeeRate, 0U);
        BOOST_CHECK_GT(p.nMaxBlockSize, 0U);
        BOOST_CHECK_GT(p.nTargetSpacing, 0U);
    }
    BOOST_CHECK_EQUAL(LITENYX_CHAIN_PARAMS[0].nTargetSpacing, 60U);
    BOOST_CHECK_EQUAL(LITENYX_CHAIN_PARAMS[1].nTargetSpacing, 300U);
}

// --- Aux header magic + validation ------------------------------------------
BOOST_AUTO_TEST_CASE(aux_header_magic_and_validate)
{
    LitenyxAuxHeader h;
    h.SetMagic();
    h.chainId = 0;
    h.eventHeight = 0;
    h.splitVector = 0;
    h.reserved = 0;
    BOOST_CHECK(h.HasMagic());
    BOOST_CHECK(h.ValidateAuxEvent(0, LITENYX_MIN_CHAINS));

    // Out-of-range chainId must reject.
    LitenyxAuxHeader bad = h;
    bad.chainId = 9;
    BOOST_CHECK(!bad.ValidateAuxEvent(0, LITENYX_MIN_CHAINS));

    // Non-zero reserved must reject.
    LitenyxAuxHeader r = h;
    r.reserved = 1;
    BOOST_CHECK(!r.ValidateAuxEvent(0, LITENYX_MIN_CHAINS));
}

// --- THE CORE INVARIANT: a global spent-set forbids double spend across chains
BOOST_AUTO_TEST_CASE(shared_spent_set_blocks_cross_chain_double_spend)
{
    GlobalSpentSet g;
    OutPoint U{uint256S("0xaaaa"), 0};

    // Spend U on Chain_A (chainId 0).
    BOOST_CHECK(g.TrySpend(U, 0));

    // The SAME outpoint U must NOT be spendable on Chain_B (chainId 1).
    BOOST_CHECK(!g.TrySpend(U, 1));

    // And not even re-spendable on the original chain.
    BOOST_CHECK(!g.TrySpend(U, 0));

    BOOST_CHECK(g.IsSpent(U));
}

// --- Different outpoints spend independently on either chain -----------------
BOOST_AUTO_TEST_CASE(distinct_outpoints_spend_on_any_chain)
{
    GlobalSpentSet g;
    OutPoint A{uint256S("0xbbbb"), 0};
    OutPoint B{uint256S("0xcccc"), 0};

    BOOST_CHECK(g.TrySpend(A, 0)); // A on chain 0
    BOOST_CHECK(g.TrySpend(B, 1)); // B on chain 1
    BOOST_CHECK(g.IsSpent(A));
    BOOST_CHECK(g.IsSpent(B));
    BOOST_CHECK(!g.IsSpent(OutPoint{uint256S("0xdddd"), 0}));
}

BOOST_AUTO_TEST_SUITE_END()
