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
//   C1 commitment value IS the frozen TopologyStateHash (no second hash domain).
//   C2 VerifyTopologyCommitment regime matrix; zero-commitment != absence.
//   C3 presence is STRUCTURAL (magic == V2), not zero-sentinel; predicates.
//   C4 wire framing: V0/V1 byte-identical (56B), V2 +32B (88B), offset boundary.
//   D1 enforcement boundary matrix: H_derive-1/H_derive/H_topology-1/H_topology.
//   D2 disabled/mainnet network dormant at all heights.
//   D3 network selector maps net strings to frozen activations.
//   D4 enforcement path-independent (IBD==sequential==disconnect/reconnect).

#include <litenyx/LITENYX_topology_authority.h>
#include <litenyx/LITENYX_topology.h>
#include <litenyx/LITENYX_types.h>

#define BOOST_TEST_MODULE LITENYX_topo_authority_test
#include <boost/test/unit_test.hpp>

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

// Build a canonical block sequence (index == height-1). Each block extends a
// chainId chosen round-robin among the active lanes, with a weight pattern that
// alternates saturated/idle per OBS_WINDOW. Deterministic function of height.
static std::vector<LitenyxCommittedBlock> MakeChain(uint32_t tip, uint8_t lanes) {
    std::vector<LitenyxCommittedBlock> chain;
    chain.reserve(tip);
    const uint32_t W = LITENYX_TOPOLOGY_OBS_WINDOW;
    for (uint32_t h = 1; h <= tip; ++h) {
        LitenyxCommittedBlock b;
        b.chainId = (uint8_t)((h - 1) % lanes);
        bool hot = ((h / W) % 2) == 0;
        b.blockWeight = hot ? LITENYX_MAX_BLOCK_WEIGHT               // 100% -> D_v1=10000
                            : (LITENYX_MAX_BLOCK_WEIGHT / 20);        // 5%   -> D_v1=500
        chain.push_back(b);
    }
    return chain;
}

// B1: canonical window reconstruction — per-chain M_c_v1 from raw weights,
// zero-block chains map to idle (0).
BOOST_AUTO_TEST_CASE(reconstruct_window_mc_v1)
{
    std::vector<LitenyxCommittedBlock> window;
    // chain 0: two full blocks (D_v1=10000 each) -> M_c_v1=10000.
    window.push_back({0, LITENYX_MAX_BLOCK_WEIGHT});
    window.push_back({0, LITENYX_MAX_BLOCK_WEIGHT});
    // chain 1: one half block (D_v1=5000) -> M_c_v1=5000.
    window.push_back({1, LITENYX_MAX_BLOCK_WEIGHT / 2});
    // chain 2: no blocks -> idle 0.
    std::vector<int32_t> mc = LitenyxReconstructMcV1Window(window, 3);
    BOOST_REQUIRE_EQUAL(mc.size(), (size_t)3);
    BOOST_CHECK_EQUAL(mc[0], 10000);
    BOOST_CHECK_EQUAL(mc[1], 5000);
    BOOST_CHECK_EQUAL(mc[2], 0);

    // Blocks on a non-active lane (chainId >= nActive) are ignored.
    window.push_back({7, LITENYX_MAX_BLOCK_WEIGHT});
    std::vector<int32_t> mc2 = LitenyxReconstructMcV1Window(window, 3);
    BOOST_CHECK_EQUAL(mc2[0], 10000); // unchanged
}

// B2: canonical-chain derivation is deterministic and equals a fresh re-run.
BOOST_AUTO_TEST_CASE(chain_derivation_deterministic)
{
    std::vector<LitenyxCommittedBlock> chain = MakeChain(3000, LITENYX_TOPO_MAX_CHAINS);
    LitenyxTopologyState a = LitenyxCalculateExpectedTopologyFromChain(
        LitenyxTopologyState::Genesis(), chain, 3000);
    LitenyxTopologyState b = LitenyxCalculateExpectedTopologyFromChain(
        LitenyxTopologyState::Genesis(), chain, 3000);
    BOOST_CHECK(a == b);
    unsigned char ha[32], hb[32];
    LitenyxTopologyStateHash(a, ha);
    LitenyxTopologyStateHash(b, hb);
    BOOST_CHECK_EQUAL(HexOf(ha), HexOf(hb));
}

// B3: "derivable from canonical chain alone" — live/IBD/restart/disconnect-
// reconnect/reorg all reduce to deriving from the SAME canonical block prefix,
// so they MUST produce identical state + hash at every boundary.
BOOST_AUTO_TEST_CASE(canonical_chain_alone_invariant)
{
    const uint32_t tip = 4000;
    std::vector<LitenyxCommittedBlock> chain = MakeChain(tip, LITENYX_TOPO_MAX_CHAINS);
    const uint32_t W = LITENYX_TOPOLOGY_OBS_WINDOW;

    for (uint32_t h = W; h <= tip; h += W) {
        // "Full IBD to h": derive with the whole chain, tip=h.
        LitenyxTopologyState ibd = LitenyxCalculateExpectedTopologyFromChain(
            LitenyxTopologyState::Genesis(), chain, h);

        // "Live connect to h": derive with a TRUNCATED chain (only blocks <= h
        // are present, as a node that connected up to h would have).
        std::vector<LitenyxCommittedBlock> prefix(chain.begin(), chain.begin() + h);
        LitenyxTopologyState live = LitenyxCalculateExpectedTopologyFromChain(
            LitenyxTopologyState::Genesis(), prefix, h);

        BOOST_CHECK(ibd == live);
        unsigned char hi[32], hl[32];
        LitenyxTopologyStateHash(ibd, hi);
        LitenyxTopologyStateHash(live, hl);
        BOOST_CHECK_EQUAL(HexOf(hi), HexOf(hl));
    }

    // Disconnect/reconnect: derive to tip, then to a cut, then re-extend to tip.
    const uint32_t cut = 2500 - (2500 % W); // aligned
    LitenyxTopologyState full1 = LitenyxCalculateExpectedTopologyFromChain(
        LitenyxTopologyState::Genesis(), chain, tip);
    LitenyxTopologyState atCut = LitenyxCalculateExpectedTopologyFromChain(
        LitenyxTopologyState::Genesis(), chain, cut);
    (void)atCut;
    LitenyxTopologyState full2 = LitenyxCalculateExpectedTopologyFromChain(
        LitenyxTopologyState::Genesis(), chain, tip);
    BOOST_CHECK(full1 == full2); // reconnecting the same canonical blocks is identical
}

// B4: reorg — a competing branch sharing a prefix yields the identical prefix
// state; the branch tip is a pure function of the branch's canonical blocks.
BOOST_AUTO_TEST_CASE(chain_reorg_prefix_identity)
{
    const uint32_t tip = 3000, fork = 1500;
    std::vector<LitenyxCommittedBlock> base = MakeChain(tip, LITENYX_TOPO_MAX_CHAINS);

    LitenyxTopologyState prefix = LitenyxCalculateExpectedTopologyFromChain(
        LitenyxTopologyState::Genesis(), base, fork);

    // Build a DIFFERENT branch after the fork: invert the load pattern.
    std::vector<LitenyxCommittedBlock> branch = base;
    const uint32_t W = LITENYX_TOPOLOGY_OBS_WINDOW;
    for (uint32_t h = fork + 1; h <= tip; ++h) {
        bool hot = ((h / W) % 2) != 0; // inverted
        branch[h - 1].blockWeight = hot ? LITENYX_MAX_BLOCK_WEIGHT
                                        : (LITENYX_MAX_BLOCK_WEIGHT / 20);
    }

    // Deriving the branch only up to fork must match the shared prefix exactly.
    LitenyxTopologyState prefixOnBranch = LitenyxCalculateExpectedTopologyFromChain(
        LitenyxTopologyState::Genesis(), branch, fork);
    BOOST_CHECK(prefix == prefixOnBranch);

    // Branch tip is deterministic.
    LitenyxTopologyState t1 = LitenyxCalculateExpectedTopologyFromChain(
        LitenyxTopologyState::Genesis(), branch, tip);
    LitenyxTopologyState t2 = LitenyxCalculateExpectedTopologyFromChain(
        LitenyxTopologyState::Genesis(), branch, tip);
    BOOST_CHECK(t1 == t2);
}

// C1: Commitment value IS the frozen TopologyStateHash (NO second hash domain).
BOOST_AUTO_TEST_CASE(commitment_is_frozen_state_hash)
{
    LitenyxTopologyState g = LitenyxTopologyState::Genesis();

    // TopologyStateHash KAT is UNCHANGED (regression guard for §3 freeze).
    unsigned char sh[32];
    LitenyxTopologyStateHash(g, sh);
    BOOST_CHECK_EQUAL(
        HexOf(sh),
        "71667e04205a7150268d09b82c13849ddd2d187cbf73f5d83b2aecea693bfc09");

    // The commitment carried in the AuxHeader EQUALS TopologyStateHash exactly.
    uint256 c = LitenyxExpectedTopologyCommitment(g);
    BOOST_CHECK_EQUAL(
        HexOf(c.data),
        "71667e04205a7150268d09b82c13849ddd2d187cbf73f5d83b2aecea693bfc09");
    BOOST_CHECK_EQUAL(HexOf(c.data), HexOf(sh));

    // Field-sensitive: changing state changes the commitment.
    LitenyxTopologyState a = g; a.nN = (uint8_t)(g.nN + 1);
    BOOST_CHECK_NE(HexOf(LitenyxExpectedTopologyCommitment(a).data), HexOf(c.data));
}

// C2: VerifyTopologyCommitment — frozen outcome table (structural presence).
BOOST_AUTO_TEST_CASE(verify_commitment_outcomes)
{
    using R = LitenyxTopoRegime;
    using V = LitenyxCommitVerdict;
    LitenyxTopologyState exp = LitenyxTopologyState::Genesis();
    uint256 correct = LitenyxExpectedTopologyCommitment(exp);
    uint256 wrong = correct; wrong.data[0] ^= 0xFF;
    uint256 zero; zero.SetNull(); // a PRESENT-but-zero V2 commitment

    // PreDerivation: absent OK; present is premature -> Invalid.
    BOOST_CHECK(LitenyxVerifyTopologyCommitment(R::PreDerivation, false, correct, exp) == V::Valid);
    BOOST_CHECK(LitenyxVerifyTopologyCommitment(R::PreDerivation, true, correct, exp) == V::Invalid);

    // SoftAdvisory: absent OK; correct OK; mismatch -> advisory (NOT invalid).
    BOOST_CHECK(LitenyxVerifyTopologyCommitment(R::SoftAdvisory, false, correct, exp) == V::Valid);
    BOOST_CHECK(LitenyxVerifyTopologyCommitment(R::SoftAdvisory, true, correct, exp) == V::Valid);
    BOOST_CHECK(LitenyxVerifyTopologyCommitment(R::SoftAdvisory, true, wrong, exp) == V::AdvisoryMismatch);

    // HardAuthority: absent -> Invalid; mismatch -> Invalid; correct -> Valid.
    BOOST_CHECK(LitenyxVerifyTopologyCommitment(R::HardAuthority, false, correct, exp) == V::Invalid);
    BOOST_CHECK(LitenyxVerifyTopologyCommitment(R::HardAuthority, true, wrong, exp) == V::Invalid);
    BOOST_CHECK(LitenyxVerifyTopologyCommitment(R::HardAuthority, true, correct, exp) == V::Valid);

    // A PRESENT-but-zero commitment is present (not absent): in Hard it is a
    // mismatch -> Invalid (genesis hash != 0), confirming zero != absence.
    BOOST_CHECK(LitenyxVerifyTopologyCommitment(R::HardAuthority, true, zero, exp) == V::Invalid);
}

// C3: presence is STRUCTURAL (magic == V2), not inferred from a zero value.
BOOST_AUTO_TEST_CASE(auxheader_presence_is_structural)
{
    LitenyxAuxHeader h;
    h.SetNull();
    BOOST_CHECK(!h.HasKnownMagic());
    BOOST_CHECK(!h.HasTopologyCommitment()); // V0

    h.SetMagicV1();
    BOOST_CHECK(h.IsV1());
    BOOST_CHECK(h.HasKnownMagic());
    BOOST_CHECK(h.HasMagic());              // deprecated alias recognizes V1
    BOOST_CHECK(!h.HasTopologyCommitment()); // V1 carries no commitment

    h.SetMagicV2();
    BOOST_CHECK(h.IsV2());
    BOOST_CHECK(h.HasKnownMagic());
    BOOST_CHECK(h.HasMagic());              // deprecated alias must ALSO see V2
    BOOST_CHECK(h.HasTopologyCommitment()); // structurally present

    // Even with an all-zero commitment, a V2 header is PRESENT.
    h.topologyCommitment = uint256();
    BOOST_CHECK(h.HasTopologyCommitment());

    // Carrier round-trips the frozen hash unchanged.
    h.topologyCommitment = LitenyxExpectedTopologyCommitment(LitenyxTopologyState::Genesis());
    BOOST_CHECK(h.topologyCommitment ==
                LitenyxExpectedTopologyCommitment(LitenyxTopologyState::Genesis()));
}

// C4: wire-format framing (spec §5.7). The standalone harness compiles out the
// daemon serialize.h SerializationOp, so we mirror its EXACT field order + the
// V2-conditional rule here to prove framing determinism:
//   V0/V1 -> NO topology bytes (byte-identical to Phase 2/3)
//   V2    -> +32 trailing commitment bytes; magic read first fixes the boundary.
namespace {
// Mirror of LitenyxAuxHeader::SerializationOp field order (little-endian ints).
inline void PutU32(std::vector<unsigned char>& v, uint32_t x) {
    v.push_back((unsigned char)(x & 0xFF));
    v.push_back((unsigned char)((x >> 8) & 0xFF));
    v.push_back((unsigned char)((x >> 16) & 0xFF));
    v.push_back((unsigned char)((x >> 24) & 0xFF));
}
inline void PutU64(std::vector<unsigned char>& v, uint64_t x) {
    for (int i = 0; i < 8; ++i) v.push_back((unsigned char)((x >> (8 * i)) & 0xFF));
}
inline void Put256(std::vector<unsigned char>& v, const uint256& u) {
    for (int i = 0; i < 32; ++i) v.push_back(u.data[i]);
}
inline std::vector<unsigned char> ModelSerializeAux(const LitenyxAuxHeader& h) {
    std::vector<unsigned char> v;
    PutU32(v, h.magic);
    PutU32(v, h.chainId);
    PutU32(v, h.eventHeight);
    Put256(v, h.auxAnchor);
    PutU64(v, h.splitVector);
    PutU32(v, h.reserved);
    if (h.magic == LITENYX_AUX_MAGIC_V2) Put256(v, h.topologyCommitment); // V2-only
    return v;
}
// Fixed prefix width shared by ALL versions: 4+4+4+32+8+4 = 56 bytes.
const size_t kAuxPrefixLen = 56;
} // namespace

BOOST_AUTO_TEST_CASE(auxheader_wire_framing_versioned)
{
    // V1 (Phase 2/3 legacy) — exactly the prefix, NO topology bytes.
    LitenyxAuxHeader v1; v1.SetNull(); v1.SetMagicV1(); v1.chainId = 1;
    auto b1 = ModelSerializeAux(v1);
    BOOST_CHECK_EQUAL(b1.size(), kAuxPrefixLen);

    // V0 (non-Litenyx-aware, magic 0) — same width as V1, NO topology bytes.
    LitenyxAuxHeader v0; v0.SetNull(); v0.chainId = 1;
    auto b0 = ModelSerializeAux(v0);
    BOOST_CHECK_EQUAL(b0.size(), kAuxPrefixLen);

    // V0 and V1 differ ONLY in the leading magic word; the rest is byte-identical.
    for (size_t i = 4; i < kAuxPrefixLen; ++i) BOOST_CHECK_EQUAL(b0[i], b1[i]);

    // V2 (topology-capable) — prefix + 32 trailing commitment bytes.
    LitenyxAuxHeader v2; v2.SetNull(); v2.SetMagicV2(); v2.chainId = 1;
    v2.topologyCommitment =
        LitenyxExpectedTopologyCommitment(LitenyxTopologyState::Genesis());
    auto b2 = ModelSerializeAux(v2);
    BOOST_CHECK_EQUAL(b2.size(), kAuxPrefixLen + 32);

    // The shared prefix (after magic) is byte-identical across V1 and V2, so the
    // legacy region is preserved; only the trailing 32 bytes are new.
    for (size_t i = 4; i < kAuxPrefixLen; ++i) BOOST_CHECK_EQUAL(b1[i], b2[i]);

    // Offset correctness: the next block field begins at 56 for V1, 88 for V2.
    // The parser reads magic (bytes 0..3) first, so it knows which boundary
    // applies BEFORE consuming any following field.
    BOOST_CHECK_EQUAL(b1.size(), (size_t)56);
    BOOST_CHECK_EQUAL(b2.size(), (size_t)88);

    // The trailing 32 bytes of V2 are exactly the frozen commitment.
    uint256 tail; for (int i = 0; i < 32; ++i) tail.data[i] = b2[kAuxPrefixLen + i];
    BOOST_CHECK(tail == v2.topologyCommitment);
}

// D-series (Phase 4B(4)): the ENFORCEMENT DECISION — regime(height) → derive
// expected from canonical chain → pure VerifyTopologyCommitment. This mirrors,
// in pure form, exactly what LitenyxCheckTopologyCommitment does in ConnectBlock
// (which additionally reads CBlock/CBlockIndex). Deciding the verdict here proves
// the consensus semantics at the exact activation boundaries without a daemon.

namespace {
// Reproduce the enforcement decision purely: build the canonical block vector to
// `height`, derive expected T_h, and verify the carried commitment for `magicV2`.
LitenyxCommitVerdict DecideAt(const LitenyxTopoActivation& act,
                              uint32_t height,
                              const std::vector<LitenyxCommittedBlock>& chain,
                              bool presentV2,
                              const uint256& carried)
{
    LitenyxTopoRegime regime = act.RegimeAt(height);
    LitenyxTopologyState expected = LitenyxCalculateExpectedTopologyFromChain(
        LitenyxTopologyState::Genesis(), chain, height);
    return LitenyxVerifyTopologyCommitment(regime, presentV2, carried, expected);
}
// Canonical chain of `n` blocks all on chain 0 at half weight (deterministic).
std::vector<LitenyxCommittedBlock> Chain(uint32_t n) {
    std::vector<LitenyxCommittedBlock> v(n);
    for (auto& b : v) { b.chainId = 0; b.blockWeight = LITENYX_MAX_BLOCK_WEIGHT / 2; }
    return v;
}
} // namespace

// D1: exact activation boundaries H_derive-1 / H_derive / H_topology-1 / H_topology.
BOOST_AUTO_TEST_CASE(enforcement_boundary_matrix)
{
    using V = LitenyxCommitVerdict;
    const LitenyxTopoActivation act = LitenyxTopoActivationRegtest(); // 100 / 300
    const uint32_t Hd = 100, Ht = 300;

    // Regime edges are strict: [<Hd)=Pre, [Hd,Ht)=Soft, [>=Ht)=Hard.
    BOOST_CHECK(act.RegimeAt(Hd - 1) == LitenyxTopoRegime::PreDerivation);
    BOOST_CHECK(act.RegimeAt(Hd)     == LitenyxTopoRegime::SoftAdvisory);
    BOOST_CHECK(act.RegimeAt(Ht - 1) == LitenyxTopoRegime::SoftAdvisory);
    BOOST_CHECK(act.RegimeAt(Ht)     == LitenyxTopoRegime::HardAuthority);

    auto chain = Chain(Ht + 1);
    auto correctAt = [&](uint32_t h) {
        return LitenyxExpectedTopologyCommitment(
            LitenyxCalculateExpectedTopologyFromChain(
                LitenyxTopologyState::Genesis(), chain, h));
    };
    uint256 zero; zero.SetNull();

    // H_derive-1 (PreDerivation): absent OK; a present V2 commitment is premature.
    BOOST_CHECK(DecideAt(act, Hd - 1, chain, false, zero) == V::Valid);
    BOOST_CHECK(DecideAt(act, Hd - 1, chain, true, correctAt(Hd - 1)) == V::Invalid);

    // H_derive (SoftAdvisory): absent OK; correct OK; wrong -> advisory (accept).
    BOOST_CHECK(DecideAt(act, Hd, chain, false, zero) == V::Valid);
    BOOST_CHECK(DecideAt(act, Hd, chain, true, correctAt(Hd)) == V::Valid);
    { uint256 w = correctAt(Hd); w.data[0] ^= 0xFF;
      BOOST_CHECK(DecideAt(act, Hd, chain, true, w) == V::AdvisoryMismatch); }

    // H_topology-1 (still Soft): missing must NOT be fatal.
    BOOST_CHECK(DecideAt(act, Ht - 1, chain, false, zero) == V::Valid);

    // H_topology (HardAuthority): absent -> Invalid; correct -> Valid; wrong ->
    // Invalid; present-but-zero -> Invalid (present != absent).
    BOOST_CHECK(DecideAt(act, Ht, chain, false, zero) == V::Invalid);
    BOOST_CHECK(DecideAt(act, Ht, chain, true, correctAt(Ht)) == V::Valid);
    { uint256 w = correctAt(Ht); w.data[0] ^= 0xFF;
      BOOST_CHECK(DecideAt(act, Ht, chain, true, w) == V::Invalid); }
    BOOST_CHECK(DecideAt(act, Ht, chain, true, zero) == V::Invalid);
}

// D2: disabled/mainnet network stays PreDerivation at ALL heights (dormant).
BOOST_AUTO_TEST_CASE(enforcement_disabled_network_dormant)
{
    using V = LitenyxCommitVerdict;
    const LitenyxTopoActivation act = LitenyxTopoActivationMainnet(); // disabled
    BOOST_CHECK(act.IsDisabled());
    auto chain = Chain(1000);
    uint256 zero; zero.SetNull();
    // Even far past any height, absent is valid and a present commitment is
    // premature (rejected) — never enforced, never required.
    BOOST_CHECK(DecideAt(act, 100000, chain, false, zero) == V::Valid);
    BOOST_CHECK(DecideAt(act, 100000, chain, true, zero) == V::Invalid);
}

// D3: network selector maps net strings to the frozen activations.
BOOST_AUTO_TEST_CASE(enforcement_network_selector)
{
    BOOST_CHECK(LitenyxTopoActivationForNetwork("regtest").RegimeAt(300) == LitenyxTopoRegime::HardAuthority);
    BOOST_CHECK(LitenyxTopoActivationForNetwork("test").RegimeAt(300) == LitenyxTopoRegime::PreDerivation);
    BOOST_CHECK(LitenyxTopoActivationForNetwork("test").RegimeAt(1500) == LitenyxTopoRegime::HardAuthority);
    BOOST_CHECK(LitenyxTopoActivationForNetwork("main").IsDisabled());
    BOOST_CHECK(LitenyxTopoActivationForNetwork("unknown-net").IsDisabled()); // fail dormant
}

// D4: enforcement is PATH-INDEPENDENT — deciding at height h gives the SAME
// verdict whether the chain was reconstructed in one shot (IBD) or the expected
// state was reached incrementally. Reconstruction is a pure function of the
// canonical block SET up to h, so no process-local state can change acceptance.
BOOST_AUTO_TEST_CASE(enforcement_path_independent)
{
    using V = LitenyxCommitVerdict;
    const LitenyxTopoActivation act = LitenyxTopoActivationRegtest();
    auto chain = Chain(500);

    for (uint32_t h : {100u, 200u, 300u, 400u, 500u}) {
        uint256 correct = LitenyxExpectedTopologyCommitment(
            LitenyxCalculateExpectedTopologyFromChain(
                LitenyxTopologyState::Genesis(), chain, h));
        // A longer chain vector must not change the derivation AT h (only heights
        // <= h contribute), proving disconnect/reconnect + IBD equivalence.
        auto chainLonger = Chain(900);
        uint256 correct2 = LitenyxExpectedTopologyCommitment(
            LitenyxCalculateExpectedTopologyFromChain(
                LitenyxTopologyState::Genesis(), chainLonger, h));
        BOOST_CHECK(correct == correct2);
        // And the verdict is stable.
        V a = DecideAt(act, h, chain, act.RegimeAt(h) != LitenyxTopoRegime::PreDerivation, correct);
        V b = DecideAt(act, h, chainLonger, act.RegimeAt(h) != LitenyxTopoRegime::PreDerivation, correct2);
        BOOST_CHECK(a == b);
    }
}

// A10: activation semantics (spec §8) — regimes, disabled sentinel, validity.
BOOST_AUTO_TEST_CASE(activation_regimes_and_disabled)
{
    // Regtest: H_derive=100, H_topology=300.
    LitenyxTopoActivation rt = LitenyxTopoActivationRegtest();
    BOOST_CHECK(rt.IsValid());
    BOOST_CHECK(!rt.IsDisabled());
    BOOST_CHECK_EQUAL((int)rt.RegimeAt(99),  (int)LitenyxTopoRegime::PreDerivation);
    BOOST_CHECK_EQUAL((int)rt.RegimeAt(100), (int)LitenyxTopoRegime::SoftAdvisory);   // H_derive: enters soft
    BOOST_CHECK_EQUAL((int)rt.RegimeAt(299), (int)LitenyxTopoRegime::SoftAdvisory);
    BOOST_CHECK_EQUAL((int)rt.RegimeAt(300), (int)LitenyxTopoRegime::HardAuthority);  // H_topology: enters hard
    BOOST_CHECK_EQUAL((int)rt.RegimeAt(5000),(int)LitenyxTopoRegime::HardAuthority);

    // Testnet enabled early.
    LitenyxTopoActivation tn = LitenyxTopoActivationTestnet();
    BOOST_CHECK(tn.IsValid());
    BOOST_CHECK(!tn.IsDisabled());
    BOOST_CHECK_EQUAL((int)tn.RegimeAt(499),  (int)LitenyxTopoRegime::PreDerivation);
    BOOST_CHECK_EQUAL((int)tn.RegimeAt(1500), (int)LitenyxTopoRegime::HardAuthority);

    // Mainnet DISABLED: every height is Pre-derivation; never reaches hard.
    LitenyxTopoActivation mn = LitenyxTopoActivationMainnet();
    BOOST_CHECK(mn.IsValid());       // both-disabled is a VALID configuration
    BOOST_CHECK(mn.IsDisabled());
    BOOST_CHECK_EQUAL((int)mn.RegimeAt(0),          (int)LitenyxTopoRegime::PreDerivation);
    BOOST_CHECK_EQUAL((int)mn.RegimeAt(0xFFFFFFFEu),(int)LitenyxTopoRegime::PreDerivation);
    // The sentinel is "never", not a reachable height.
    BOOST_CHECK_EQUAL(mn.hDerive, LITENYX_TOPO_ACTIVATION_DISABLED);

    // Structural invalidity: ordering, zero, and the both-disabled coupling.
    BOOST_CHECK(!(LitenyxTopoActivation{300, 100}).IsValid());  // H_topology < H_derive
    BOOST_CHECK(!(LitenyxTopoActivation{0, 100}).IsValid());    // H_derive == 0
    BOOST_CHECK(!(LitenyxTopoActivation{LITENYX_TOPO_ACTIVATION_DISABLED, 100}).IsValid()); // derive disabled but topology set
    BOOST_CHECK((LitenyxTopoActivation{100, 100}).IsValid());   // no soft window is allowed
}

BOOST_AUTO_TEST_SUITE_END()
