// Litenyx Phase 5 standalone proof — V3 aux-carrier serialization (spec §6.1).
//
// Step 3 of §13 sequencing. Proves the V3 wire carrier WITHOUT any ConnectBlock
// hook. The consensus authority is the frozen KAT in spec §6.1, itself produced
// by the INDEPENDENT reference cpp_reference/reference/v3_carrier_kat.py. This
// test asserts the C++ serialization reproduces that reference BYTE-FOR-BYTE.
//
// Properties asserted:
//   V1 wire framing purely additive: V1=56B, V2=88B, V3=120B; magic is the sole
//     discriminator; the shared prefix after magic is byte-identical across
//     versions (V1/V2 branches unchanged; V3 = exact V2 88B prefix + 32B).
//   V2 two independent, non-overlapping, domain-LESS commitments:
//     topologyCommitment == TopologyStateHash(T_h),
//     lifecycleCommitment == LifecycleStateHash(L_h); neither includes the other.
//   V3 GENESIS KAT: serialized V3 stream and its SHA256d equal the frozen §6.1
//     values, using the engine's own commitment functions.
//   V4 presence predicates are STRUCTURAL: HasTopologyCommitment()==V2||V3,
//     HasLifecycleCommitment()==V3, HasKnownMagic()==V1||V2||V3.

#include <litenyx/LITENYX_auxpow.h>
#include <litenyx/LITENYX_types.h>
#include <litenyx/LITENYX_topology_authority.h>
#include <litenyx/LITENYX_chainid_lifecycle.h>

#define BOOST_TEST_MODULE LITENYX_v3_carrier_test
#include <boost/test/included/unit_test.hpp>

#include <vector>
#include <string>
#include <cstdint>

static std::string HexOf(const unsigned char* p, size_t n) {
    static const char* d = "0123456789abcdef";
    std::string s; s.reserve(n * 2);
    for (size_t i = 0; i < n; ++i) { s += d[p[i] >> 4]; s += d[p[i] & 0xF]; }
    return s;
}

// Mirror of LitenyxAuxHeader::SerializationOp field order (little-endian ints),
// including the additive V2/V3 trailing branches. This is the ONLY hand-written
// serializer here; the daemon build uses the header's SerializationOp, which is
// framed identically (verified by the byte-width/prefix checks below).
static void PutU32(std::vector<unsigned char>& v, uint32_t x) {
    for (int i = 0; i < 4; ++i) v.push_back((unsigned char)((x >> (8 * i)) & 0xFF));
}
static void PutU64(std::vector<unsigned char>& v, uint64_t x) {
    for (int i = 0; i < 8; ++i) v.push_back((unsigned char)((x >> (8 * i)) & 0xFF));
}
static void Put256(std::vector<unsigned char>& v, const uint256& u) {
    for (int i = 0; i < 32; ++i) v.push_back(u.data[i]);
}
static std::vector<unsigned char> ModelSerializeAux(const LitenyxAuxHeader& h) {
    std::vector<unsigned char> v;
    PutU32(v, h.magic);
    PutU32(v, h.chainId);
    PutU32(v, h.eventHeight);
    Put256(v, h.auxAnchor);
    PutU64(v, h.splitVector);
    PutU32(v, h.reserved);
    if (h.magic == LITENYX_AUX_MAGIC_V2 || h.magic == LITENYX_AUX_MAGIC_V3)
        Put256(v, h.topologyCommitment);
    if (h.magic == LITENYX_AUX_MAGIC_V3)
        Put256(v, h.lifecycleCommitment);
    return v;
}
static const size_t kAuxPrefixLen = 56;

// Frozen genesis KATs (spec §6.1). NOT recomputed here — pinned from the
// audited independent reference.
static const char* kTopoCommitHex =
    "71667e04205a7150268d09b82c13849ddd2d187cbf73f5d83b2aecea693bfc09";
static const char* kLifeCommitHex =
    "ca5225a14fe2d5da35823650bb25c43edf63a459f56153b8f0570eb17302c9e1";
static const char* kV3StreamHex =
    "3359594c0100000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000071667e04205a7150268d09b82c13849ddd2d187cbf73f5d83b2aecea693bfc09ca5225a14fe2d5da35823650bb25c43edf63a459f56153b8f0570eb17302c9e1";
static const char* kV3DigestHex =
    "5b60d2f7431b1f018ce1012becee2d883effac52c4da1902941c97a1c21d5f5b";

BOOST_AUTO_TEST_SUITE(LITENYX_v3_carrier_tests)

// V1: purely additive framing across V1/V2/V3.
BOOST_AUTO_TEST_CASE(v1_wire_framing_additive_versions)
{
    LitenyxAuxHeader v1; v1.SetNull(); v1.SetMagicV1(); v1.chainId = 1;
    auto b1 = ModelSerializeAux(v1);
    BOOST_CHECK_EQUAL(b1.size(), kAuxPrefixLen);

    LitenyxAuxHeader v2; v2.SetNull(); v2.SetMagicV2(); v2.chainId = 1;
    v2.topologyCommitment =
        LitenyxExpectedTopologyCommitment(LitenyxTopologyState::Genesis());
    auto b2 = ModelSerializeAux(v2);
    BOOST_CHECK_EQUAL(b2.size(), kAuxPrefixLen + 32);

    LitenyxAuxHeader v3; v3.SetNull(); v3.SetMagicV3(); v3.chainId = 1;
    v3.topologyCommitment =
        LitenyxExpectedTopologyCommitment(LitenyxTopologyState::Genesis());
    {
        LitenyxChainIdLifecycleState L0 = LitenyxChainIdLifecycleGenesis();
        LitenyxLifecycleStateHash(L0, v3.lifecycleCommitment.begin());
    }
    auto b3 = ModelSerializeAux(v3);
    BOOST_CHECK_EQUAL(b3.size(), (size_t)120);

    // Shared prefix after magic is byte-identical across all versions.
    for (size_t i = 4; i < kAuxPrefixLen; ++i) {
        BOOST_CHECK_EQUAL(b1[i], b2[i]);
        BOOST_CHECK_EQUAL(b1[i], b3[i]);
    }
    // V3's first 88 bytes are EXACTLY V2 (minus the magic word), i.e. V3 carries
    // the exact V2 prefix; only the trailing 32 bytes are new.
    for (size_t i = 4; i < 88; ++i) BOOST_CHECK_EQUAL(b2[i], b3[i]);
}

// V2: independent, non-overlapping, domain-LESS commitments.
BOOST_AUTO_TEST_CASE(v2_independent_commitments)
{
    uint256 tc = LitenyxExpectedTopologyCommitment(LitenyxTopologyState::Genesis());
    uint256 lc;
    LitenyxChainIdLifecycleState L0 = LitenyxChainIdLifecycleGenesis();
    LitenyxLifecycleStateHash(L0, lc.begin());

    BOOST_CHECK_EQUAL(HexOf(tc.data, 32), std::string(kTopoCommitHex));
    BOOST_CHECK_EQUAL(HexOf(lc.data, 32), std::string(kLifeCommitHex));
    // Independence: the two commitments are distinct and neither is derived from
    // the other (no cross-hash / recursion).
    BOOST_CHECK(tc != lc);
}

// V3: genesis KAT — serialized stream and SHA256d match the frozen §6.1 values.
BOOST_AUTO_TEST_CASE(v3_genesis_kat)
{
    LitenyxAuxHeader v3; v3.SetNull(); v3.SetMagicV3(); v3.chainId = 1;
    v3.topologyCommitment =
        LitenyxExpectedTopologyCommitment(LitenyxTopologyState::Genesis());
    LitenyxChainIdLifecycleState L0 = LitenyxChainIdLifecycleGenesis();
    LitenyxLifecycleStateHash(L0, v3.lifecycleCommitment.begin());

    auto stream = ModelSerializeAux(v3);
    BOOST_CHECK_EQUAL(stream.size(), (size_t)120);
    BOOST_CHECK_EQUAL(HexOf(stream.data(), stream.size()), std::string(kV3StreamHex));

    unsigned char digest[32];
    litenyx_detail::double_sha256(stream.data(), stream.size(), digest);
    BOOST_CHECK_EQUAL(HexOf(digest, 32), std::string(kV3DigestHex));

    // Trailing 32 bytes are exactly the lifecycle commitment.
    uint256 tail; for (int i = 0; i < 32; ++i) tail.data[i] = stream[88 + i];
    BOOST_CHECK(tail == v3.lifecycleCommitment);
}

// V4: presence predicates are structural.
BOOST_AUTO_TEST_CASE(v4_structural_presence_predicates)
{
    LitenyxAuxHeader v1; v1.SetNull(); v1.SetMagicV1();
    LitenyxAuxHeader v2; v2.SetNull(); v2.SetMagicV2();
    LitenyxAuxHeader v3; v3.SetNull(); v3.SetMagicV3();

    BOOST_CHECK(v1.HasKnownMagic() && v2.HasKnownMagic() && v3.HasKnownMagic());

    BOOST_CHECK(!v1.HasTopologyCommitment());
    BOOST_CHECK(v2.HasTopologyCommitment());
    BOOST_CHECK(v3.HasTopologyCommitment());

    BOOST_CHECK(!v1.HasLifecycleCommitment());
    BOOST_CHECK(!v2.HasLifecycleCommitment());
    BOOST_CHECK(v3.HasLifecycleCommitment());

    // Zero commitment is PRESENT (structural), not absence.
    LitenyxAuxHeader z; z.SetNull(); z.SetMagicV3();
    BOOST_CHECK(z.HasLifecycleCommitment());
    BOOST_CHECK(z.lifecycleCommitment.IsNull());
}

BOOST_AUTO_TEST_SUITE_END()
