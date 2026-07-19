// Litenyx Phase 4A standalone proof — pure consensus-authoritative topology
// engine (spec docs/litenyx_topology_authority_spec_v0.1.md).
//
// Proves the AUTHORITATIVE derivation is deterministic and PATH-INDEPENDENT,
// with NO ConnectBlock hook and NO singleton/tracker/clock/mempool/RPC input.
//
// Properties asserted:
//   A1 D_v1 integer-only, bounded 0..DEMAND_SCALE, exact floor semantics.
//   A2 M_c_v1 full-precision aggregation; per-sample downscale would differ.
//   A3 GOLDEN VECTORS: fixed-point -> floor(/100) -> strict <20/>80 boundary.
//   A4 determinism: identical history => identical state AND identical hash.
//   A5 path-independence: sequential vs IBD vs shuffled-arrival => identical hash.
//   A6 disconnect/reconnect: rederive after truncation == original prefix.
//   A7 reorg: canonical prefix identical; both branches deterministic.
//   A8 TopologyStateHash: canonical serialization, stable, sensitive to fields.

#include <litenyx/LITENYX_topology_authority.h>
#include <litenyx/LITENYX_topology.h>
#include <litenyx/LITENYX_types.h>

#define BOOST_TEST_MODULE LITENYX_topo_authority_test
#include <boost/test/included/unit_test.hpp>

#include <vector>
#include <cstdint>
#include <cstring>
#include <random>
#include <algorithm>
#include <string>
#include <utility>

static std::string HexOf(const unsigned char h[32]) {
    static const char* d = "0123456789abcdef";
    std::string s; s.reserve(64);
    for (int i = 0; i < 32; ++i) { s += d[h[i] >> 4]; s += d[h[i] & 0xF]; }
    return s;
}

// Build a deterministic committed history: per boundary, per-chain M_c_v1.
// hot boundaries -> saturated (9500), cold -> idle (500). Alternating pattern.
static LitenyxCommittedHistory MakeHistory(uint32_t tip, uint8_t chains) {
    LitenyxCommittedHistory H;
    const uint32_t W = LITENYX_TOPOLOGY_OBS_WINDOW;
    for (uint32_t h = W; h <= tip; h += W) {
        bool hot = ((h / W) % 2) == 0;
        std::vector<int32_t> mc(chains, hot ? 9500 : 500);
        H.add(h, mc);
    }
    return H;
}

BOOST_AUTO_TEST_SUITE(LITENYX_topology_authority_tests)

// A1: D_v1 = floor(weight * DEMAND_SCALE / MAX_BLOCK_WEIGHT), bounded.
BOOST_AUTO_TEST_CASE(demand_v1_bounds_and_floor)
{
    BOOST_CHECK_EQUAL(LitenyxDemandV1(0), 0);
    BOOST_CHECK_EQUAL(LitenyxDemandV1(LITENYX_MAX_BLOCK_WEIGHT), (int32_t)LITENYX_DEMAND_SCALE); // 100%
    BOOST_CHECK_EQUAL(LitenyxDemandV1(LITENYX_MAX_BLOCK_WEIGHT / 2), 5000);                       // 50%
    // Floor semantics: just under 50% must truncate down, not round.
    int64_t justUnderHalf = (LITENYX_MAX_BLOCK_WEIGHT / 2) - 1;
    BOOST_CHECK_LT(LitenyxDemandV1(justUnderHalf), 5000);
    BOOST_CHECK_EQUAL(LitenyxDemandV1(justUnderHalf), 4999);
    // Defensive clamps.
    BOOST_CHECK_EQUAL(LitenyxDemandV1(-1), 0);
    BOOST_CHECK_EQUAL(LitenyxDemandV1(LITENYX_MAX_BLOCK_WEIGHT * 3), (int32_t)LITENYX_DEMAND_SCALE);
}

// A2: full-precision aggregation differs from (wrong) per-sample downscaling.
BOOST_AUTO_TEST_CASE(mc_v1_full_precision_aggregation)
{
    // Samples whose mean is 8099 (-> controller 80), but per-sample floor(/100)
    // then mean would lose the sub-100 information at the boundary.
    std::vector<int32_t> samples = {8000, 8000, 8000, 8000, 8495}; // mean 8099
    int32_t mc = LitenyxMcV1(samples);
    BOOST_CHECK_EQUAL(mc, 8099);
    BOOST_CHECK_EQUAL(LitenyxMcToControllerInput(mc), 80); // 80, NOT >80

    // Empty window -> 0 (idle).
    BOOST_CHECK_EQUAL(LitenyxMcV1({}), 0);

    // Prove per-sample downscaling would be LOSSIER: floor(each/100) then mean.
    long perSampleMeanTimes = 0;
    for (int32_t s : samples) perSampleMeanTimes += (s / 100);
    int32_t perSampleMean = (int32_t)(perSampleMeanTimes / (long)samples.size()); // == 80 here too
    // The point: full precision retains 8099; the naive path discards it BEFORE
    // aggregation. Assert the canonical path keeps the extra resolution.
    BOOST_CHECK_EQUAL(mc / 100, perSampleMean); // agree at THIS mean...
    BOOST_CHECK_NE(mc, perSampleMean * 100);    // ...but full value is richer.
}

// A3: GOLDEN VECTORS pinning fixed-point precision x floor(/100) x strict band.
BOOST_AUTO_TEST_CASE(golden_boundary_vectors)
{
    struct V { int32_t mcV1; int32_t controller; };
    const V vec[] = {
        {1999, 19}, {2000, 20}, {2099, 20}, {2100, 21},
        {7999, 79}, {8000, 80}, {8099, 80}, {8100, 81},
    };
    for (const auto& v : vec)
        BOOST_CHECK_EQUAL(LitenyxMcToControllerInput(v.mcV1), v.controller);

    // Strict controller semantics on the aggregate A (mean of controller M_c):
    // all chains at controller 80 => A=80 => NOT SPLIT (80 > 80 == false).
    std::vector<LitenyxChainObservation> at80(LITENYX_TOPO_MAX_CHAINS, {80});
    BOOST_CHECK_EQUAL(LitenyxTopoDecide(at80, LITENYX_MIN_CHAINS, 100, 0),
                      LitenyxTopoDecision::HOLD);
    // All at 81 => A=81 => SPLIT.
    std::vector<LitenyxChainObservation> at81(LITENYX_TOPO_MAX_CHAINS, {81});
    BOOST_CHECK_EQUAL(LitenyxTopoDecide(at81, LITENYX_MIN_CHAINS, 100, 0),
                      LitenyxTopoDecision::SPLIT);
    // All at 20 => A=20 => NOT MERGE (20 < 20 == false).
    std::vector<LitenyxChainObservation> at20(LITENYX_TOPO_MAX_CHAINS, {20});
    BOOST_CHECK_EQUAL(LitenyxTopoDecide(at20, LITENYX_TOPO_MAX_CHAINS, 100, 0),
                      LitenyxTopoDecision::HOLD);
    // All at 19 => A=19 => MERGE.
    std::vector<LitenyxChainObservation> at19(LITENYX_TOPO_MAX_CHAINS, {19});
    BOOST_CHECK_EQUAL(LitenyxTopoDecide(at19, LITENYX_TOPO_MAX_CHAINS, 100, 0),
                      LitenyxTopoDecision::MERGE);

    // End-to-end via M_c_v1: 8100 across all chains -> controller 81 -> SPLIT.
    std::vector<int32_t> mcHigh(LITENYX_TOPO_MAX_CHAINS, 8100);
    LitenyxTopologyState after = LitenyxDeriveTopologyAtBoundary(
        LitenyxTopologyState::Genesis(), 100, mcHigh);
    BOOST_CHECK_EQUAL(after.nN, (uint8_t)(LITENYX_MIN_CHAINS + 1));
    // 8099 -> controller 80 -> HOLD -> no split.
    std::vector<int32_t> mcEdge(LITENYX_TOPO_MAX_CHAINS, 8099);
    LitenyxTopologyState hold = LitenyxDeriveTopologyAtBoundary(
        LitenyxTopologyState::Genesis(), 100, mcEdge);
    BOOST_CHECK_EQUAL(hold.nN, (uint8_t)LITENYX_MIN_CHAINS);
}

// A4: identical history => identical final state AND identical hash.
BOOST_AUTO_TEST_CASE(determinism_state_and_hash)
{
    LitenyxCommittedHistory H = MakeHistory(3000, LITENYX_TOPO_MAX_CHAINS);
    LitenyxTopologyState s1 = LitenyxCalculateExpectedTopology(
        LitenyxTopologyState::Genesis(), H, 3000);
    LitenyxTopologyState s2 = LitenyxCalculateExpectedTopology(
        LitenyxTopologyState::Genesis(), H, 3000);
    BOOST_CHECK(s1 == s2);
    unsigned char h1[32], h2[32];
    LitenyxTopologyStateHash(s1, h1);
    LitenyxTopologyStateHash(s2, h2);
    BOOST_CHECK_EQUAL(HexOf(h1), HexOf(h2));
}

// A5: PATH-INDEPENDENCE (spec §0.1). Sequential, IBD-batch, and shuffled arrival
// of the SAME committed boundaries must yield byte-identical hashes at EVERY
// boundary height.
BOOST_AUTO_TEST_CASE(path_independence_across_arrival_orders)
{
    const uint32_t tip = 4000;
    LitenyxCommittedHistory canonical = MakeHistory(tip, LITENYX_TOPO_MAX_CHAINS);

    // Shuffled arrival: same boundaries, permuted insertion order.
    LitenyxCommittedHistory shuffled = canonical;
    std::mt19937 rng(12345);
    std::shuffle(shuffled.boundaries.begin(), shuffled.boundaries.end(), rng);

    // Reverse arrival: worst-case ordering.
    LitenyxCommittedHistory reversed = canonical;
    std::reverse(reversed.boundaries.begin(), reversed.boundaries.end());

    const uint32_t W = LITENYX_TOPOLOGY_OBS_WINDOW;
    for (uint32_t h = W; h <= tip; h += W) {
        LitenyxTopologyState a = LitenyxCalculateExpectedTopology(
            LitenyxTopologyState::Genesis(), canonical, h);
        LitenyxTopologyState b = LitenyxCalculateExpectedTopology(
            LitenyxTopologyState::Genesis(), shuffled, h);
        LitenyxTopologyState c = LitenyxCalculateExpectedTopology(
            LitenyxTopologyState::Genesis(), reversed, h);
        BOOST_CHECK(a == b);
        BOOST_CHECK(a == c);
        unsigned char ha[32], hb[32], hc[32];
        LitenyxTopologyStateHash(a, ha);
        LitenyxTopologyStateHash(b, hb);
        LitenyxTopologyStateHash(c, hc);
        BOOST_CHECK_EQUAL(HexOf(ha), HexOf(hb));
        BOOST_CHECK_EQUAL(HexOf(ha), HexOf(hc));
    }
}

// A6: disconnect/reconnect. Deriving to tip, then truncating to a lower height
// and re-deriving, must match deriving directly to that lower height.
BOOST_AUTO_TEST_CASE(disconnect_reconnect_matches_prefix)
{
    const uint32_t tip = 3000, cut = 1500;
    LitenyxCommittedHistory H = MakeHistory(tip, LITENYX_TOPO_MAX_CHAINS);

    LitenyxTopologyState atCutDirect = LitenyxCalculateExpectedTopology(
        LitenyxTopologyState::Genesis(), H, cut);

    // "Disconnect": drop boundaries above cut, rederive.
    LitenyxCommittedHistory truncated = H;
    truncated.boundaries.erase(
        std::remove_if(truncated.boundaries.begin(), truncated.boundaries.end(),
            [cut](const std::pair<uint32_t, std::vector<int32_t>>& kv) {
                return kv.first > cut;
            }),
        truncated.boundaries.end());
    LitenyxTopologyState atCutTruncated = LitenyxCalculateExpectedTopology(
        LitenyxTopologyState::Genesis(), truncated, cut);

    BOOST_CHECK(atCutDirect == atCutTruncated);
    unsigned char hd[32], ht[32];
    LitenyxTopologyStateHash(atCutDirect, hd);
    LitenyxTopologyStateHash(atCutTruncated, ht);
    BOOST_CHECK_EQUAL(HexOf(hd), HexOf(ht));
}

// A7: reorg. A competing branch after a fork point must (a) share the exact
// canonical prefix state and (b) be a deterministic function of its own history.
BOOST_AUTO_TEST_CASE(reorg_prefix_identical_branches_deterministic)
{
    const uint32_t tip = 3000, fork = 1500;
    LitenyxCommittedHistory base = MakeHistory(tip, LITENYX_TOPO_MAX_CHAINS);

    // State at the fork point is the shared prefix.
    LitenyxTopologyState prefixA = LitenyxCalculateExpectedTopology(
        LitenyxTopologyState::Genesis(), base, fork);

    // Build a DIFFERENT but deterministic branch after the fork.
    LitenyxCommittedHistory branch = base;
    branch.boundaries.erase(
        std::remove_if(branch.boundaries.begin(), branch.boundaries.end(),
            [fork](const std::pair<uint32_t, std::vector<int32_t>>& kv) {
                return kv.first > fork;
            }),
        branch.boundaries.end());
    const uint32_t W = LITENYX_TOPOLOGY_OBS_WINDOW;
    for (uint32_t h = fork + W; h <= tip; h += W) {
        bool hot = ((h / W) % 3) != 0; // different rhythm than base
        std::vector<int32_t> mc(LITENYX_TOPO_MAX_CHAINS, hot ? 9000 : 1000);
        branch.add(h, mc);
    }

    // Prefix state at fork must be identical when derived on the branch history.
    LitenyxTopologyState prefixB = LitenyxCalculateExpectedTopology(
        LitenyxTopologyState::Genesis(), branch, fork);
    BOOST_CHECK(prefixA == prefixB);

    // The branch tip is a deterministic function of the branch (re-run stable).
    LitenyxTopologyState tip1 = LitenyxCalculateExpectedTopology(
        LitenyxTopologyState::Genesis(), branch, tip);
    LitenyxTopologyState tip2 = LitenyxCalculateExpectedTopology(
        LitenyxTopologyState::Genesis(), branch, tip);
    BOOST_CHECK(tip1 == tip2);
    unsigned char h1[32], h2[32];
    LitenyxTopologyStateHash(tip1, h1);
    LitenyxTopologyStateHash(tip2, h2);
    BOOST_CHECK_EQUAL(HexOf(h1), HexOf(h2));
}

// A8: TopologyStateHash canonical serialization — stable and field-sensitive.
BOOST_AUTO_TEST_CASE(topology_state_hash_canonical)
{
    LitenyxTopologyState g = LitenyxTopologyState::Genesis();
    unsigned char hg[32];
    LitenyxTopologyStateHash(g, hg);
    // Stable across calls.
    unsigned char hg2[32];
    LitenyxTopologyStateHash(g, hg2);
    BOOST_CHECK_EQUAL(HexOf(hg), HexOf(hg2));

    // Every field change must alter the hash (no field is dropped from serialize).
    LitenyxTopologyState a = g; a.nN = (uint8_t)(g.nN + 1);
    LitenyxTopologyState b = g; b.nHeight = g.nHeight + 1;
    LitenyxTopologyState c = g; c.nLastTransition = g.nLastTransition + 1;
    LitenyxTopologyState d = g; d.nVersion = g.nVersion + 1;
    unsigned char ha[32], hb[32], hc[32], hd[32];
    LitenyxTopologyStateHash(a, ha);
    LitenyxTopologyStateHash(b, hb);
    LitenyxTopologyStateHash(c, hc);
    LitenyxTopologyStateHash(d, hd);
    BOOST_CHECK_NE(HexOf(hg), HexOf(ha));
    BOOST_CHECK_NE(HexOf(hg), HexOf(hb));
    BOOST_CHECK_NE(HexOf(hg), HexOf(hc));
    BOOST_CHECK_NE(HexOf(hg), HexOf(hd));

    // Serialization is exactly 13 bytes in the pinned little-endian order.
    unsigned char ser[13];
    LitenyxSerializeTopologyState(g, ser);
    BOOST_CHECK_EQUAL((int)ser[0], (int)(LITENYX_TOPOLOGY_STATE_VERSION & 0xFF)); // version LE
    BOOST_CHECK_EQUAL((int)ser[8], (int)LITENYX_MIN_CHAINS);                      // nN

    // Full 13-byte canonical layout KAT: version=1, height=0, nN=2, lastTrans=0.
    static const unsigned char kGenesisSer[13] =
        {0x01,0,0,0, 0,0,0,0, 0x02, 0,0,0,0};
    BOOST_CHECK_EQUAL(std::memcmp(ser, kGenesisSer, 13), 0);

    // Known-answer vector pinning the EXACT double-SHA256 of genesis serialization
    // (locks byte layout + hash implementation across platforms/builds forever).
    BOOST_CHECK_EQUAL(
        HexOf(hg),
        "71667e04205a7150268d09b82c13849ddd2d187cbf73f5d83b2aecea693bfc09");
}

// A9: SHA-256d correctness KAT (Bitcoin CHash256-compatible). Guarantees the
// self-contained hash equals the canonical double-SHA256 the daemon uses, so the
// pure engine's commitment is byte-compatible with a future CHash256 wiring.
BOOST_AUTO_TEST_CASE(sha256d_known_answers)
{
    unsigned char out[32];
    litenyx_detail::double_sha256((const unsigned char*)"", 0, out);
    BOOST_CHECK_EQUAL(
        HexOf(out),
        "5df6e0e2761359d30a8275058e299fcc0381534545f55cf43e41983f5d4c9456");
    litenyx_detail::double_sha256((const unsigned char*)"abc", 3, out);
    BOOST_CHECK_EQUAL(
        HexOf(out),
        "4f8b42c22dd3729b519ba6f68d2da7cc5b2d606d05daed5ad5128cc03e6c6358");
}

BOOST_AUTO_TEST_SUITE_END()
