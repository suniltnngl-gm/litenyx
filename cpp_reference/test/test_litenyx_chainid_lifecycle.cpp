// Litenyx Phase 5 standalone proof — pure ChainId lifecycle authority engine
// (spec docs/litenyx_chainid_lifecycle_spec_v0.1.md).
//
// Proves the PERSISTENT, NON-RECYCLED ChainId identity layer above the frozen
// Phase-4 topology (nN) is deterministic and PATH-INDEPENDENT, with NO
// ConnectBlock hook and NO singleton/tracker/clock/mempool/RPC input.
//
// Properties asserted:
//   P1 genesis L_0 shape + FROZEN serialization KAT + hash KAT.
//   P2 G HOLD/SPLIT/MERGE deltas; lastLifecycleHeight; nextChainId monotonic.
//   P3 L0 persistence: merge-then-split NEVER reuses a ChainId.
//   P4 L1 bijection + L2 dense-allocation status oracle.
//   P5 G fail-closed (§4.1): |ΔnN|>1, out-of-range nN, non-boundary change.
//   P6 L3 exhaustion at UINT32_MAX fails closed (never wrap/recycle).
//   P7 ValidateExecutionContext matrix: active / retired / nonexistent (§5.1).
//   P8 boundary timing (§5.2): activated CID usable at boundary h; retired not.
//   P9 path-independence: sequential == IBD == disconnect/reconnect == reorg
//      re-derivation yield byte-identical LifecycleStateHash at every height.
//   PA FLAGSHIP (§10.1): lane2->CID2 -> merge retires CID2 -> lane2->CID3;
//      CID2 permanently invalid; identical across all derivation paths.

#include <litenyx/LITENYX_chainid_lifecycle.h>
#include <litenyx/LITENYX_topology.h>
#include <litenyx/LITENYX_types.h>

#define BOOST_TEST_MODULE LITENYX_chainid_lifecycle_test
#include <boost/test/included/unit_test.hpp>

#include <vector>
#include <cstdint>
#include <cstring>
#include <string>
#include <algorithm>

static std::string HexOf(const unsigned char h[32]) {
    static const char* d = "0123456789abcdef";
    std::string s; s.reserve(64);
    for (int i = 0; i < 32; ++i) { s += d[h[i] >> 4]; s += d[h[i] & 0xF]; }
    return s;
}

static std::string HexOfVec(const std::vector<unsigned char>& v) {
    static const char* d = "0123456789abcdef";
    std::string s; s.reserve(v.size() * 2);
    for (size_t i = 0; i < v.size(); ++i) { s += d[v[i] >> 4]; s += d[v[i] & 0xF]; }
    return s;
}

// Fold G over a sequence of nN values, one per observation-window boundary
// (heights W, 2W, 3W, ...). nSeq[0] is the state AFTER the first boundary.
// Returns the sequence of states L_0, L_W, L_2W, ... (size = nSeq.size()+1).
// On any G failure, `ok` is set false and derivation stops.
static std::vector<LitenyxChainIdLifecycleState>
DeriveLifecycleSeq(const std::vector<uint8_t>& nSeq, bool& ok) {
    ok = true;
    const uint32_t W = LITENYX_TOPOLOGY_OBS_WINDOW;
    std::vector<LitenyxChainIdLifecycleState> states;
    LitenyxChainIdLifecycleState cur = LitenyxChainIdLifecycleGenesis();
    states.push_back(cur);
    uint8_t Nprev = LITENYX_MIN_CHAINS;
    for (size_t i = 0; i < nSeq.size(); ++i) {
        uint8_t Ncur = nSeq[i];
        uint32_t h = (uint32_t)(W * (i + 1));
        LitenyxChainIdLifecycleState next;
        if (!LitenyxAdvanceChainIdLifecycle(cur, Nprev, Ncur, h, next)) { ok = false; break; }
        cur = next;
        states.push_back(cur);
        Nprev = Ncur;
    }
    return states;
}

static std::vector<std::string>
HashSeq(const std::vector<LitenyxChainIdLifecycleState>& states) {
    std::vector<std::string> hs;
    for (size_t i = 0; i < states.size(); ++i) {
        unsigned char h[32];
        LitenyxLifecycleStateHash(states[i], h);
        hs.push_back(HexOf(h));
    }
    return hs;
}

// --------------------------------------------------------------------------
// P1: genesis shape + FROZEN KATs.
// --------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(p1_genesis_shape_and_frozen_kats) {
    LitenyxChainIdLifecycleState L = LitenyxChainIdLifecycleGenesis();
    BOOST_CHECK_EQUAL((int)L.nVersion, (int)LITENYX_LIFECYCLE_STATE_VERSION);
    BOOST_CHECK_EQUAL(L.nextChainId, (uint32_t)LITENYX_MIN_CHAINS);
    BOOST_CHECK_EQUAL(L.lastLifecycleHeight, 0u);
    BOOST_REQUIRE_EQUAL(L.activeBindings.size(), (size_t)LITENYX_MIN_CHAINS);
    for (uint8_t i = 0; i < LITENYX_MIN_CHAINS; ++i) {
        BOOST_CHECK_EQUAL((int)L.activeBindings[i].laneId, (int)i);
        BOOST_CHECK_EQUAL(L.activeBindings[i].chainId, (uint32_t)i);
    }
    BOOST_CHECK(LitenyxLifecycleStateCoherent(L, LITENYX_MIN_CHAINS));

    // FROZEN serialization KAT (21 bytes; §3.1 layout, MIN_CHAINS=2).
    std::vector<unsigned char> ser = LitenyxSerializeLifecycleState(L);
    BOOST_CHECK_EQUAL(ser.size(), (size_t)21);
    BOOST_CHECK_EQUAL(HexOfVec(ser),
        std::string("010002000000020000000000010100000000000000"));

    // FROZEN genesis hash KAT (double-SHA256 of the above).
    unsigned char h[32];
    LitenyxLifecycleStateHash(L, h);
    BOOST_CHECK_EQUAL(HexOf(h),
        std::string("ca5225a14fe2d5da35823650bb25c43edf63a459f56153b8f0570eb17302c9e1"));
}

// --------------------------------------------------------------------------
// P2: G HOLD / SPLIT / MERGE.
// --------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(p2_transition_deltas) {
    const uint32_t W = LITENYX_TOPOLOGY_OBS_WINDOW;
    LitenyxChainIdLifecycleState L0 = LitenyxChainIdLifecycleGenesis();

    // HOLD (2 -> 2): unchanged.
    LitenyxChainIdLifecycleState hold;
    BOOST_REQUIRE(LitenyxAdvanceChainIdLifecycle(L0, 2, 2, W, hold));
    BOOST_CHECK(hold == L0);

    // SPLIT (2 -> 3): lane 2 -> ChainId 2, nextChainId 2->3.
    LitenyxChainIdLifecycleState s3;
    BOOST_REQUIRE(LitenyxAdvanceChainIdLifecycle(L0, 2, 3, W, s3));
    BOOST_CHECK_EQUAL(s3.nextChainId, 3u);
    BOOST_CHECK_EQUAL(s3.lastLifecycleHeight, W);
    BOOST_REQUIRE_EQUAL(s3.activeBindings.size(), (size_t)3);
    BOOST_CHECK_EQUAL((int)s3.activeBindings[2].laneId, 2);
    BOOST_CHECK_EQUAL(s3.activeBindings[2].chainId, 2u);
    BOOST_CHECK(LitenyxLifecycleStateCoherent(s3, 3));

    // MERGE (3 -> 2): lane 2 retired; nextChainId UNCHANGED (stays 3).
    LitenyxChainIdLifecycleState m2;
    BOOST_REQUIRE(LitenyxAdvanceChainIdLifecycle(s3, 3, 2, 2 * W, m2));
    BOOST_CHECK_EQUAL(m2.nextChainId, 3u);        // L0: NOT decremented
    BOOST_CHECK_EQUAL(m2.lastLifecycleHeight, 2 * W);
    BOOST_REQUIRE_EQUAL(m2.activeBindings.size(), (size_t)2);
    BOOST_CHECK(LitenyxLifecycleStateCoherent(m2, 2));
    // ChainId 2 no longer bound.
    BOOST_CHECK(LitenyxClassifyChainId(m2, 2) == LitenyxChainIdStatus::Retired);
}

// --------------------------------------------------------------------------
// P3 + PA: FLAGSHIP identity distinction (§10.1) — lane reuse != ChainId reuse.
// --------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(p3_pa_flagship_lane_reuse_new_chainid) {
    // Scripted nN per boundary: split, merge, split.
    std::vector<uint8_t> nSeq;
    nSeq.push_back(3); // SPLIT 2->3  : lane 2 -> ChainId 2
    nSeq.push_back(2); // MERGE 3->2  : lane 2 retired, ChainId 2 gone forever
    nSeq.push_back(3); // SPLIT 2->3  : lane 2 -> ChainId 3 (NOT 2)

    bool ok = false;
    std::vector<LitenyxChainIdLifecycleState> st = DeriveLifecycleSeq(nSeq, ok);
    BOOST_REQUIRE(ok);
    BOOST_REQUIRE_EQUAL(st.size(), (size_t)4); // L0, L_W, L_2W, L_3W

    const LitenyxChainIdLifecycleState& fin = st.back();
    // Final: lanes {0,1,2}; lane 2 bound to ChainId 3; nextChainId == 4.
    BOOST_CHECK_EQUAL(fin.nextChainId, 4u);
    BOOST_REQUIRE_EQUAL(fin.activeBindings.size(), (size_t)3);
    BOOST_CHECK_EQUAL((int)fin.activeBindings[2].laneId, 2);
    BOOST_CHECK_EQUAL(fin.activeBindings[2].chainId, 3u);

    // ChainId 2 is permanently RETIRED; ChainId 3 is Active on lane 2.
    BOOST_CHECK(LitenyxClassifyChainId(fin, 2) == LitenyxChainIdStatus::Retired);
    BOOST_CHECK(LitenyxClassifyChainId(fin, 3) == LitenyxChainIdStatus::Active);

    LitenyxValidatedExecutionContext c2 =
        LitenyxValidateExecutionContext(fin, 2, 3 * LITENYX_TOPOLOGY_OBS_WINDOW, 1);
    BOOST_CHECK(!c2.valid); // retired => invalid, forever
    LitenyxValidatedExecutionContext c3 =
        LitenyxValidateExecutionContext(fin, 3, 3 * LITENYX_TOPOLOGY_OBS_WINDOW, 1);
    BOOST_CHECK(c3.valid);
    BOOST_CHECK_EQUAL((int)c3.laneId, 2);

    // ChainId 2 never appears in any active binding across the whole history.
    for (size_t i = 0; i < st.size(); ++i)
        for (size_t j = 0; j < st[i].activeBindings.size(); ++j)
            if (st[i].activeBindings[j].chainId == 2u && i >= 2)
                BOOST_FAIL("ChainId 2 re-appeared active after retirement");
}

// --------------------------------------------------------------------------
// P4: L1 bijection + L2 dense-allocation status oracle over a long history.
// --------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(p4_bijection_and_dense_allocation) {
    std::vector<uint8_t> nSeq;
    // climb 2->3->4->5, then fall 5->4->3, then climb 3->4.
    uint8_t path[] = {3,4,5,4,3,4};
    for (size_t i = 0; i < sizeof(path); ++i) nSeq.push_back(path[i]);

    bool ok = false;
    std::vector<LitenyxChainIdLifecycleState> st = DeriveLifecycleSeq(nSeq, ok);
    BOOST_REQUIRE(ok);

    for (size_t s = 0; s < st.size(); ++s) {
        const LitenyxChainIdLifecycleState& L = st[s];
        // L1: unique laneIds and unique chainIds.
        for (size_t i = 0; i < L.activeBindings.size(); ++i)
            for (size_t j = i + 1; j < L.activeBindings.size(); ++j) {
                BOOST_CHECK(L.activeBindings[i].laneId  != L.activeBindings[j].laneId);
                BOOST_CHECK(L.activeBindings[i].chainId != L.activeBindings[j].chainId);
            }
        // L2: every chainId in [0, nextChainId) is Active or Retired (never a hole
        // that is neither); and >= nextChainId is Nonexistent.
        for (uint32_t c = 0; c < L.nextChainId + 2; ++c) {
            LitenyxChainIdStatus st2 = LitenyxClassifyChainId(L, c);
            if (c >= L.nextChainId)
                BOOST_CHECK(st2 == LitenyxChainIdStatus::Nonexistent);
            else
                BOOST_CHECK(st2 == LitenyxChainIdStatus::Active ||
                            st2 == LitenyxChainIdStatus::Retired);
        }
    }
}

// --------------------------------------------------------------------------
// P5: G fail-closed rejections (§4.1).
// --------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(p5_fail_closed_rejections) {
    const uint32_t W = LITENYX_TOPOLOGY_OBS_WINDOW;
    LitenyxChainIdLifecycleState L0 = LitenyxChainIdLifecycleGenesis();
    LitenyxChainIdLifecycleState out;

    // |ΔnN| > 1.
    BOOST_CHECK(!LitenyxAdvanceChainIdLifecycle(L0, 2, 4, W, out));
    // nN below MIN.
    BOOST_CHECK(!LitenyxAdvanceChainIdLifecycle(L0, 2, 1, W, out));
    // nN above MAX.
    BOOST_CHECK(!LitenyxAdvanceChainIdLifecycle(L0, 8, 9, W, out));
    // Non-boundary height with a change.
    BOOST_CHECK(!LitenyxAdvanceChainIdLifecycle(L0, 2, 3, W + 1, out));
    // Non-boundary height with HOLD is fine (no change).
    BOOST_CHECK(LitenyxAdvanceChainIdLifecycle(L0, 2, 2, W + 1, out));
    // Incoherent prev (Nprev mismatched with bindings count) is rejected.
    BOOST_CHECK(!LitenyxAdvanceChainIdLifecycle(L0, 3, 4, W, out));
}

// --------------------------------------------------------------------------
// P6: L3 exhaustion — never wrap, never recycle.
// --------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(p6_l3_exhaustion_fail_closed) {
    const uint32_t W = LITENYX_TOPOLOGY_OBS_WINDOW;
    // Construct a coherent state at nN=2 with nextChainId == UINT32_MAX.
    LitenyxChainIdLifecycleState L;
    L.nVersion = LITENYX_LIFECYCLE_STATE_VERSION;
    L.nextChainId = 0xFFFFFFFFu;
    L.activeBindings.clear();
    L.activeBindings.push_back(LitenyxChainIdBinding(0, 100));
    L.activeBindings.push_back(LitenyxChainIdBinding(1, 200));
    L.lastLifecycleHeight = 0;
    BOOST_REQUIRE(LitenyxLifecycleStateCoherent(L, 2));

    // A split would require allocating at UINT32_MAX -> fail closed.
    LitenyxChainIdLifecycleState out;
    BOOST_CHECK(!LitenyxAdvanceChainIdLifecycle(L, 2, 3, W, out));
    // A merge (no allocation) is still permitted at the boundary.
    BOOST_CHECK(LitenyxAdvanceChainIdLifecycle(L, 2, 2, W, out)); // HOLD ok
}

// --------------------------------------------------------------------------
// P7: ValidateExecutionContext matrix (§5.1).
// --------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(p7_validate_execution_context_matrix) {
    std::vector<uint8_t> nSeq; nSeq.push_back(3); nSeq.push_back(2); // CID2 then retire
    bool ok = false;
    std::vector<LitenyxChainIdLifecycleState> st = DeriveLifecycleSeq(nSeq, ok);
    BOOST_REQUIRE(ok);
    const LitenyxChainIdLifecycleState& L = st.back(); // nN=2, nextChainId=3, CID2 retired

    // Active.
    BOOST_CHECK(LitenyxValidateExecutionContext(L, 0, 999, 1).valid);
    BOOST_CHECK(LitenyxValidateExecutionContext(L, 1, 999, 1).valid);
    // Retired.
    LitenyxValidatedExecutionContext r = LitenyxValidateExecutionContext(L, 2, 999, 1);
    BOOST_CHECK(!r.valid);
    BOOST_CHECK(r.status == LitenyxChainIdStatus::Retired);
    // Nonexistent.
    LitenyxValidatedExecutionContext n = LitenyxValidateExecutionContext(L, 3, 999, 1);
    BOOST_CHECK(!n.valid);
    BOOST_CHECK(n.status == LitenyxChainIdStatus::Nonexistent);
    LitenyxValidatedExecutionContext n2 = LitenyxValidateExecutionContext(L, 1000, 999, 1);
    BOOST_CHECK(!n2.valid);
    BOOST_CHECK(n2.status == LitenyxChainIdStatus::Nonexistent);
}

// --------------------------------------------------------------------------
// P8: boundary timing (§5.2) — activated ChainId usable AT boundary h;
// retired ChainId invalid AT boundary h.
// --------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(p8_boundary_timing) {
    const uint32_t W = LITENYX_TOPOLOGY_OBS_WINDOW;
    LitenyxChainIdLifecycleState L0 = LitenyxChainIdLifecycleGenesis();

    // SPLIT at boundary W: ChainId 2 is valid for use IN block W (same block).
    LitenyxChainIdLifecycleState LW;
    BOOST_REQUIRE(LitenyxAdvanceChainIdLifecycle(L0, 2, 3, W, LW));
    BOOST_CHECK(LitenyxValidateExecutionContext(LW, 2, W, 1).valid);

    // MERGE at boundary 2W: ChainId 2 is invalid for use IN block 2W.
    LitenyxChainIdLifecycleState L2W;
    BOOST_REQUIRE(LitenyxAdvanceChainIdLifecycle(LW, 3, 2, 2 * W, L2W));
    BOOST_CHECK(!LitenyxValidateExecutionContext(L2W, 2, 2 * W, 1).valid);
}

// --------------------------------------------------------------------------
// P9: path-independence — sequential vs "IBD" (single fold over winning chain)
// vs disconnect/reconnect (truncate + re-derive) yield identical hash chains.
// --------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(p9_path_independence) {
    uint8_t path[] = {3,4,3,2,3,4,5};
    std::vector<uint8_t> nSeq(path, path + sizeof(path));

    bool okA = false, okB = false;
    std::vector<LitenyxChainIdLifecycleState> seqA = DeriveLifecycleSeq(nSeq, okA);
    std::vector<LitenyxChainIdLifecycleState> seqB = DeriveLifecycleSeq(nSeq, okB); // "IBD"
    BOOST_REQUIRE(okA); BOOST_REQUIRE(okB);
    BOOST_CHECK(HashSeq(seqA) == HashSeq(seqB));

    // Disconnect/reconnect: truncate to prefix of length k, then re-derive the
    // remainder; final hash chain must match the uninterrupted derivation.
    for (size_t k = 1; k + 1 < nSeq.size(); ++k) {
        std::vector<uint8_t> prefix(nSeq.begin(), nSeq.begin() + (long)k);
        bool okP = false;
        std::vector<LitenyxChainIdLifecycleState> pfx = DeriveLifecycleSeq(prefix, okP);
        BOOST_REQUIRE(okP);
        // Continue from the truncated tip using the remaining nN values.
        LitenyxChainIdLifecycleState cur = pfx.back();
        uint8_t Nprev = prefix.empty() ? LITENYX_MIN_CHAINS : prefix.back();
        std::vector<LitenyxChainIdLifecycleState> cont = pfx;
        bool okC = true;
        for (size_t i = k; i < nSeq.size(); ++i) {
            LitenyxChainIdLifecycleState nx;
            uint32_t h = (uint32_t)(LITENYX_TOPOLOGY_OBS_WINDOW * (i + 1));
            if (!LitenyxAdvanceChainIdLifecycle(cur, Nprev, nSeq[i], h, nx)) { okC = false; break; }
            cur = nx; cont.push_back(cur); Nprev = nSeq[i];
        }
        BOOST_REQUIRE(okC);
        BOOST_CHECK(HashSeq(cont) == HashSeq(seqA));
    }
}

// --------------------------------------------------------------------------
// P10: canonical-chain lifecycle derivation (spec §6.2) is layered EXACTLY on
// the frozen Phase-4 topology derivation, and is path-independent.
// --------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(p10_from_chain_derivation_layers_on_topology) {
    const uint32_t W = LITENYX_TOPOLOGY_OBS_WINDOW;
    const uint32_t tip = 5 * W; // five observation-window boundaries

    // Saturated demand on lane 0 across every window drives SPLIT decisions; the
    // exact nN path is whatever the FROZEN topology authority decides. We assert
    // the lifecycle tracks that authority — we do NOT hardcode thresholds.
    std::vector<LitenyxCommittedBlock> blocks((size_t)tip);
    for (uint32_t h = 1; h <= tip; ++h) {
        LitenyxCommittedBlock b;
        b.chainId = 0;
        b.blockWeight = LITENYX_MAX_BLOCK_WEIGHT; // full demand
        blocks[h - 1] = b;
    }

    LitenyxTopologyState topoTip =
        LitenyxCalculateExpectedTopologyFromChain(
            LitenyxTopologyState::Genesis(), blocks, tip);

    LitenyxChainIdLifecycleState lifeTip;
    bool ok = LitenyxCalculateExpectedLifecycleFromChain(blocks, tip, lifeTip);
    BOOST_REQUIRE(ok);

    // Layering invariant (spec §0.1): active-lane count == topology nN, and the
    // lane set is the contiguous prefix {0..nN-1}.
    BOOST_CHECK_EQUAL(lifeTip.activeBindings.size(), (size_t)topoTip.nN);
    BOOST_CHECK(LitenyxLifecycleStateCoherent(lifeTip, topoTip.nN));

    // Path independence (spec §0.4): re-deriving from the same canonical blocks
    // (any number of times / any earlier truncation) yields the identical hash.
    LitenyxChainIdLifecycleState again;
    BOOST_REQUIRE(LitenyxCalculateExpectedLifecycleFromChain(blocks, tip, again));
    unsigned char h1[32], h2[32];
    LitenyxLifecycleStateHash(lifeTip, h1);
    LitenyxLifecycleStateHash(again, h2);
    BOOST_CHECK_EQUAL(HexOf(h1), HexOf(h2));

    // Disconnect/reconnect: derive to an earlier boundary, then to the tip; the
    // tip result is identical to the uninterrupted derivation.
    LitenyxChainIdLifecycleState midThenTip;
    LitenyxChainIdLifecycleState mid;
    BOOST_REQUIRE(LitenyxCalculateExpectedLifecycleFromChain(blocks, 3 * W, mid));
    BOOST_REQUIRE(LitenyxCalculateExpectedLifecycleFromChain(blocks, tip, midThenTip));
    unsigned char h3[32];
    LitenyxLifecycleStateHash(midThenTip, h3);
    BOOST_CHECK_EQUAL(HexOf(h3), HexOf(h1));

    // The expected commitment equals the raw LifecycleStateHash (domain-less).
    uint256 commit = LitenyxExpectedLifecycleCommitment(lifeTip);
    BOOST_CHECK_EQUAL(HexOf(commit.data), HexOf(h1));
}

// --------------------------------------------------------------------------
// P11: LitenyxVerifyLifecycleCommitment — the §9.1 presence x regime matrix.
// Closes the carrier-to-consensus bypass (absence is never a silent skip).
// --------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(p11_verify_commitment_regime_matrix) {
    LitenyxChainIdLifecycleState L0 = LitenyxChainIdLifecycleGenesis();
    uint256 correct = LitenyxExpectedLifecycleCommitment(L0);
    uint256 wrong; wrong.data[0] = (unsigned char)(correct.data[0] ^ 0xFF);

    // PreDerivation: absent -> Valid; present (any value) -> Invalid (premature).
    BOOST_CHECK(LitenyxVerifyLifecycleCommitment(
        LitenyxTopoRegime::PreDerivation, false, uint256(), L0)
        == LitenyxCommitVerdict::Valid);
    BOOST_CHECK(LitenyxVerifyLifecycleCommitment(
        LitenyxTopoRegime::PreDerivation, true, correct, L0)
        == LitenyxCommitVerdict::Invalid);

    // SoftAdvisory: absent -> Valid; correct -> Valid; mismatch -> AdvisoryMismatch.
    BOOST_CHECK(LitenyxVerifyLifecycleCommitment(
        LitenyxTopoRegime::SoftAdvisory, false, uint256(), L0)
        == LitenyxCommitVerdict::Valid);
    BOOST_CHECK(LitenyxVerifyLifecycleCommitment(
        LitenyxTopoRegime::SoftAdvisory, true, correct, L0)
        == LitenyxCommitVerdict::Valid);
    BOOST_CHECK(LitenyxVerifyLifecycleCommitment(
        LitenyxTopoRegime::SoftAdvisory, true, wrong, L0)
        == LitenyxCommitVerdict::AdvisoryMismatch);

    // HardAuthority: absent -> Invalid; correct -> Valid; mismatch -> Invalid.
    BOOST_CHECK(LitenyxVerifyLifecycleCommitment(
        LitenyxTopoRegime::HardAuthority, false, uint256(), L0)
        == LitenyxCommitVerdict::Invalid);
    BOOST_CHECK(LitenyxVerifyLifecycleCommitment(
        LitenyxTopoRegime::HardAuthority, true, correct, L0)
        == LitenyxCommitVerdict::Valid);
    BOOST_CHECK(LitenyxVerifyLifecycleCommitment(
        LitenyxTopoRegime::HardAuthority, true, wrong, L0)
        == LitenyxCommitVerdict::Invalid);
}

// --------------------------------------------------------------------------
// P12: staged independent Phase-5 activation (spec §8) — frozen heights,
// regime boundaries, well-formedness, and the Phase-4 dependency.
// --------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(p12_activation_regimes_and_frozen_heights) {
    // Frozen regtest heights (spec §8 table).
    LitenyxChainIdActivation rt = LitenyxChainIdActivationForNetwork("regtest");
    BOOST_CHECK_EQUAL(rt.hDerive, 200u);
    BOOST_CHECK_EQUAL(rt.hEnforce, 400u);
    BOOST_CHECK(rt.WellFormed());
    BOOST_CHECK(!rt.IsDisabled());

    // Regime boundaries: [.., 200) pre; [200,400) soft; [400, ..) hard.
    BOOST_CHECK(rt.RegimeAt(199) == LitenyxTopoRegime::PreDerivation);
    BOOST_CHECK(rt.RegimeAt(200) == LitenyxTopoRegime::SoftAdvisory);
    BOOST_CHECK(rt.RegimeAt(399) == LitenyxTopoRegime::SoftAdvisory);
    BOOST_CHECK(rt.RegimeAt(400) == LitenyxTopoRegime::HardAuthority);
    BOOST_CHECK(rt.RegimeAt(100000) == LitenyxTopoRegime::HardAuthority);

    // Mainnet DISABLED => PreDerivation at every height (dormant).
    LitenyxChainIdActivation mn = LitenyxChainIdActivationForNetwork("main");
    BOOST_CHECK(mn.IsDisabled());
    BOOST_CHECK(mn.WellFormed()); // both-disabled coupling holds
    BOOST_CHECK(mn.RegimeAt(0) == LitenyxTopoRegime::PreDerivation);
    BOOST_CHECK(mn.RegimeAt(0xFFFFFFF0u) == LitenyxTopoRegime::PreDerivation);

    // Unknown network defaults to DISABLED (fail-safe).
    BOOST_CHECK(LitenyxChainIdActivationForNetwork("bogus").IsDisabled());

    // Phase-4 dependency (spec §8): H_cid_derive >= Phase-4 H_derive and
    // H_cid_enforce at/after Phase-4 H_topology, on every enabled network.
    LitenyxTopoActivation p4rt = LitenyxTopoActivationForNetwork("regtest");
    BOOST_CHECK(rt.hDerive >= p4rt.hDerive);
    BOOST_CHECK(rt.hEnforce >= p4rt.hTopology);
    LitenyxChainIdActivation tn = LitenyxChainIdActivationForNetwork("test");
    LitenyxTopoActivation p4tn = LitenyxTopoActivationForNetwork("test");
    BOOST_CHECK(tn.WellFormed());
    BOOST_CHECK(tn.hDerive >= p4tn.hDerive);
    BOOST_CHECK(tn.hEnforce >= p4tn.hTopology);

    // Ill-formed guards: 0 < H_derive, and H_derive <= H_enforce.
    BOOST_CHECK(!LitenyxChainIdActivation(0, 100).WellFormed());
    BOOST_CHECK(!LitenyxChainIdActivation(400, 200).WellFormed());
}
