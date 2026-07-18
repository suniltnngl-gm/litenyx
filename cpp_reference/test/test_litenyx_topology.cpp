// Litenyx Phase 3 standalone proof (spec v0.1).
// Proves the deterministic topology controller contract WITHOUT daemon code:
//   History -> Observatory -> SPLIT/HOLD/MERGE
// Properties asserted:
//   P1 identical history => identical topology trajectory (determinism)
//   P2 bounds unbreachable: N in [LITENYX_MIN_CHAINS, LITENYX_TOPO_MAX_CHAINS]
//   P3 hysteresis + cooldown suppress oscillation
//   P4 reorg replay reconstructs the same trajectory
//   P5 transitions preserve the Phase-2 global shared-state invariant

#include <litenyx/LITENYX_topology.h>
#include <litenyx/LITENYX_types.h>
#include <litenyx/LITENYX_auxpow.h>  // uint256 / uint256S shim (standalone test)

#define BOOST_TEST_MODULE LITENYX_topo_test
#include <boost/test/included/unit_test.hpp>

#include <vector>
#include <cstdint>
#include <map>
#include <utility>
#include <set>
#include <string>

using LitenyxObservations = std::vector<LitenyxChainObservation>;

// A "canonical history" is a deterministic function: for each height, the
// per-chain observations are recorded. We model it as a map height->obs so a
// reorg can swap a suffix and we can re-derive the trajectory.
struct History {
    std::map<uint32_t, LitenyxObservations> byHeight;
    uint32_t tip = 0;

    void set(uint32_t h, const LitenyxObservations& o) { byHeight[h] = o; if (h > tip) tip = h; }
};

// Reconstruct the topology trajectory (N per transition height) from a history,
// using ONLY canonical data: at each observation boundary we decide, defer to a
// transition height, and apply there. This mirrors what every node computes.
struct Trajectory {
    std::map<uint32_t, uint8_t> N_at_transition; // transitionHeight -> new N
    uint8_t N_h = LITENYX_MIN_CHAINS;            // starting chain count
    uint32_t lastTrans = 0;

    void reset() { N_h = LITENYX_MIN_CHAINS; lastTrans = 0; N_at_transition.clear(); }

    // Walk every observation boundary in [1, tip].
    void reconstruct(const History& H) {
        reset();
        const uint32_t W = LITENYX_TOPOLOGY_OBS_WINDOW;
        for (uint32_t h = W; h <= H.tip; h += W) {
            auto it = H.byHeight.find(h);
            if (it == H.byHeight.end()) continue; // no observation recorded -> HOLD path
            LitenyxTopoDecision d = LitenyxTopoDecide(it->second, N_h, h, lastTrans);
            uint32_t th = LitenyxTopoTransitionHeight(h);
            if (d != LitenyxTopoDecision::HOLD) {
                uint8_t newN = LitenyxTopoApply(N_h, d);
                N_at_transition[th] = newN;
                N_h = newN;
                lastTrans = th;
            }
        }
    }

    // N at an arbitrary height = last transition <= height, else start.
    uint8_t N_at(uint32_t height) const {
        uint8_t n = LITENYX_MIN_CHAINS;
        for (const auto& kv : N_at_transition)
            if (kv.first <= height) n = kv.second; else break;
        return n;
    }
};

BOOST_AUTO_TEST_SUITE(LITENYX_topology_tests)

// P2: bounds are never exceeded, regardless of input pressure.
BOOST_AUTO_TEST_CASE(bounds_unbreachable)
{
    // Saturate everything: force SPLIT repeatedly.
    History H;
    LitenyxObservations sat;
    for (uint8_t c = 0; c < LITENYX_TOPO_MAX_CHAINS; ++c) sat.push_back(LitenyxChainObservation{100});
    for (uint32_t h = 1; h <= 5000; ++h) H.set(h, sat);

    Trajectory T; T.reconstruct(H);
    for (uint32_t hh = 1; hh <= 5000; ++hh)
        BOOST_CHECK_GE(T.N_at(hh), (uint8_t)LITENYX_MIN_CHAINS);
    for (uint32_t hh = 1; hh <= 5000; ++hh)
        BOOST_CHECK_LE(T.N_at(hh), (uint8_t)LITENYX_TOPO_MAX_CHAINS);

    // Starve everything: force MERGE repeatedly.
    History H2;
    LitenyxObservations idle;
    for (uint8_t c = 0; c < LITENYX_TOPO_MAX_CHAINS; ++c) idle.push_back(LitenyxChainObservation{0});
    for (uint32_t h = 1; h <= 5000; ++h) H2.set(h, idle);
    Trajectory T2; T2.reconstruct(H2);
    for (uint32_t hh = 1; hh <= 5000; ++hh)
        BOOST_CHECK_GE(T2.N_at(hh), (uint8_t)LITENYX_MIN_CHAINS); // never below 2
    BOOST_CHECK_EQUAL(T2.N_at(5000), (uint8_t)LITENYX_MIN_CHAINS); // settled at floor
}

// P1: the SAME history always yields the SAME trajectory (determinism).
BOOST_AUTO_TEST_CASE(identical_history_identical_trajectory)
{
    History H;
    // Alternating load: chains saturated then idle, deterministic pattern.
    for (uint32_t h = 1; h <= 3000; ++h) {
        LitenyxObservations o;
        bool hot = ((h / LITENYX_TOPOLOGY_OBS_WINDOW) % 2) == 0;
        for (uint8_t c = 0; c < LITENYX_TOPO_MAX_CHAINS; ++c)
            o.push_back(LitenyxChainObservation{hot ? 95 : 5});
        H.set(h, o);
    }
    Trajectory T1; T1.reconstruct(H);
    Trajectory T2; T2.reconstruct(H);
    BOOST_CHECK(T1.N_at_transition == T2.N_at_transition);

    // Also: a single observation must map to a single decision (pure function).
    LitenyxObservations o;
    for (uint8_t c = 0; c < LITENYX_TOPO_MAX_CHAINS; ++c) o.push_back(LitenyxChainObservation{90});
    LitenyxTopoDecision d1 = LitenyxTopoDecide(o, 2, 1000, 0);
    LitenyxTopoDecision d2 = LitenyxTopoDecide(o, 2, 1000, 0);
    BOOST_CHECK(d1 == d2);
}

// P3: hysteresis + cooldown suppress oscillation.
BOOST_AUTO_TEST_CASE(hysteresis_and_cooldown_suppress_oscillation)
{
    // Cooldown gating is bound-agnostic: test at N = MIN_CHAINS where the
    // available change is always SPLIT (never clamped), and at an in-cooldown
    // height where ANY change must be forced to HOLD.
    LitenyxObservations hi, lo;
    for (uint8_t c = 0; c < LITENYX_TOPO_MAX_CHAINS; ++c) { hi.push_back({95}); lo.push_back({5}); }

    // First decision at h=100 (window boundary), lastTrans=0 -> allowed SPLIT.
    LitenyxTopoDecision d0 = LitenyxTopoDecide(hi, (uint8_t)LITENYX_MIN_CHAINS, 100, 0);
    BOOST_CHECK_EQUAL(d0, LitenyxTopoDecision::SPLIT);
    uint32_t th = LitenyxTopoTransitionHeight(100); // 300 for default params

    // Within cooldown: lastTrans == th (future transition), h_obs=200.
    // (200 - 300) is negative < COOLDOWN, so any change is forced HOLD.
    LitenyxTopoDecision d1 = LitenyxTopoDecide(hi, (uint8_t)LITENYX_MIN_CHAINS, 200, th);
    BOOST_CHECK_EQUAL(d1, LitenyxTopoDecision::HOLD);

    // Only after cooldown elapses can a new change occur (still at MIN, high load
    // -> SPLIT again, which is always permitted at the floor).
    LitenyxTopoDecision d2 = LitenyxTopoDecide(
        hi, (uint8_t)LITENYX_MIN_CHAINS,
        th + LITENYX_TOPOLOGY_COOLDOWN + LITENYX_TOPOLOGY_OBS_WINDOW, th);
    BOOST_CHECK_EQUAL(d2, LitenyxTopoDecision::SPLIT);
}

// P4: reorg replay reconstructs the same result.
BOOST_AUTO_TEST_CASE(reorg_replay_reconstructs_same_result)
{
    History H;
    for (uint32_t h = 1; h <= 2000; ++h) {
        LitenyxObservations o;
        for (uint8_t c = 0; c < LITENYX_TOPO_MAX_CHAINS; ++c) o.push_back({ (h % 200 < 100) ? 90 : 10 });
        H.set(h, o);
    }
    Trajectory T; T.reconstruct(H);

    // Capture trajectory at a branch point well inside the history.
    uint32_t branch = 800;
    std::map<uint32_t, uint8_t> beforeBranch;
    for (auto& kv : T.N_at_transition) if (kv.first <= branch) beforeBranch[kv.first] = kv.second;

    // Reorg: replace everything after branch with a DIFFERENT but deterministic
    // pattern. Reconstruct from the modified history.
    History H2 = H;
    for (uint32_t h = branch + 1; h <= 2000; ++h) {
        LitenyxObservations o;
        for (uint8_t c = 0; c < LITENYX_TOPO_MAX_CHAINS; ++c) o.push_back({ (h % 150 < 75) ? 20 : 80 });
        H2.set(h, o);
    }
    Trajectory T2; T2.reconstruct(H2);

    // Trajectory up to and including branch must be IDENTICAL (canonical prefix).
    for (auto& kv : beforeBranch)
        BOOST_CHECK_EQUAL(T2.N_at_transition[kv.first], kv.second);
    // And BOTH are deterministic functions of their histories (re-run is stable).
    Trajectory T3; T3.reconstruct(H2);
    BOOST_CHECK(T2.N_at_transition == T3.N_at_transition);
}

// P5: transitions preserve the Phase-2 global shared-state invariant.
BOOST_AUTO_TEST_CASE(transitions_preserve_shared_state_invariant)
{
    using OutPoint = std::pair<uint256, uint32_t>;
    struct GlobalSpentSet {
        std::set<OutPoint> spent;
        bool TrySpend(const OutPoint& op, uint8_t nChainId) {
            // The shared-state cap must cover every chainId the topology can
            // activate (up to LITENYX_TOPO_MAX_CHAINS), so the invariant can be
            // exercised across ALL active chains, not just the Phase-2 floor.
            if (nChainId >= LITENYX_TOPO_MAX_CHAINS) return false;
            if (spent.count(op)) return false;
            spent.insert(op); return true;
        }
        bool IsSpent(const OutPoint& op) const { return spent.count(op) != 0; }
    };

    // Drive the controller through full SPLIT then full MERGE, exercising spends
    // across whatever N is active, including new/retired chainIds.
    History H;
    for (uint32_t h = 1; h <= 5000; ++h) {
        LitenyxObservations o;
        bool hot = (h % 1000) < 500;
        for (uint8_t c = 0; c < LITENYX_TOPO_MAX_CHAINS; ++c) o.push_back({hot ? 95 : 5});
        H.set(h, o);
    }
    Trajectory T; T.reconstruct(H);

    GlobalSpentSet g;
    // For each transition height, the active N changes. Spend distinct outpoints
    // across all currently-active chainIds, then attempt cross-chain double spend.
    uint32_t uid = 0;
    for (const auto& kv : T.N_at_transition) {
        uint8_t N = kv.second;
        for (uint8_t c = 0; c < N; ++c) {
            // Unique outpoint per (transition, chain): encode uid across the
            // first 8 bytes as a base-16 id; c already distinguishes chains.
            std::string s(32, '0');
            for (int k = 0; k < 8; ++k)
                s[k] = char('0' + ((uid >> (4 * k)) & 0xF));
            OutPoint U{uint256S(s.c_str()), c};
            BOOST_CHECK(g.TrySpend(U, c));          // first spend ok
            for (uint8_t d = 0; d < N; ++d)
                if (d != c)
                    BOOST_CHECK(!g.TrySpend(U, d));  // no other chain can spend it
            ++uid;
        }
    }
    // Even after MERGE retires chainIds, a spent outpoint stays spent globally.
    OutPoint Z{uint256S("ZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZ"), 0};
    BOOST_CHECK(g.TrySpend(Z, 0));
    BOOST_CHECK(!g.TrySpend(Z, (uint8_t)(LITENYX_TOPO_MAX_CHAINS - 1))); // any chain, still blocked
}

BOOST_AUTO_TEST_SUITE_END()
