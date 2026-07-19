#ifndef LITENYX_AUXPOW_H
#define LITENYX_AUXPOW_H

#include <cstdint>
#include "LITENYX_types.h"

// Litenyx aux extension (Phase 2). Appended after the 80-byte CBlockHeader
// (AuxPoW-style). Carries the chainId and a same-chain anchor. This is the
// shared-state multi-chain substrate: chains are validation contexts that all
// write to ONE global spent-set (see docs/litenyx_consensus_spec_v0.1.md §3).

#ifdef KERRNYX_STANDALONE_TEST
// Minimal uint256 shim so the standalone unit test is self-contained and does
// NOT pull Dogecoin's uint256.h / compat/endian.h (which redefine glibc's
// byte-swap helpers outside the daemon's configured defines). The daemon build
// (no KERRNYX_STANDALONE_TEST) uses the real Dogecoin uint256.
#include <cstring>
struct uint256 {
    unsigned char data[32] = {};
    bool operator==(const uint256& o) const { return std::memcmp(data, o.data, 32) == 0; }
    bool operator!=(const uint256& o) const { return !(*this == o); }
    bool operator<(const uint256& o) const { return std::memcmp(data, o.data, 32) < 0; }
    // Subset of the real Dogecoin uint256 surface used by Litenyx consensus code,
    // so identical code compiles standalone AND in the daemon.
    bool IsNull() const {
        for (int i = 0; i < 32; ++i) if (data[i]) return false;
        return true;
    }
    void SetNull() { std::memset(data, 0, 32); }
    unsigned char* begin() { return data; }
    const unsigned char* begin() const { return data; }
};
inline uint256 uint256S(const char* hex) {
    uint256 r{};
    size_t n = 0;
    for (const char* p = hex; *p && n < 32; ++p, ++n) r.data[n] = (unsigned char)(*p);
    return r;
}
#else
#include <uint256.h>
#include <serialize.h>
#endif

struct LitenyxAuxHeader {
    uint32_t magic;          // LITENYX_AUX_MAGIC
    uint32_t chainId;        // 0..N-1 (selects acceptance params, not a ledger)
    uint32_t eventHeight;    // height the event is evaluated at (Phase 3; 0 now)
    uint256  auxAnchor;      // commits to the parent tip on the SAME chain
    uint64_t splitVector;    // reserved for Phase 3 (split/merge vector)
    uint32_t reserved;       // must be zero
    // Phase 4 (V2 layout ONLY): commitment carried by the block. It is the
    // frozen TopologyStateHash of the network's INDEPENDENTLY derived expected
    // T_h (§3). The AuxHeader CARRIES the commitment; it NEVER defines
    // authoritative topology. PRESENCE is structural (magic == V2), NOT inferred
    // from a zero value. Only serialized/meaningful when IsV2(). See §5.7.
    uint256  topologyCommitment;

    // Phase 5 (V3 layout ONLY): the block's lifecycle commitment. It is the
    // frozen LifecycleStateHash of the network's INDEPENDENTLY derived expected
    // L_h (spec §4.0). Like topologyCommitment it CARRIES, never defines,
    // authority. PRESENCE is structural (magic == V3), NOT a zero sentinel.
    // Only serialized/meaningful when IsV3(). See spec §6.1.
    uint256  lifecycleCommitment;

    // --- Wire-format version predicates (spec §5.7 / §6.1) -------------------
    bool IsV1() const { return magic == LITENYX_AUX_MAGIC_V1; }
    bool IsV2() const { return magic == LITENYX_AUX_MAGIC_V2; }
    bool IsV3() const { return magic == LITENYX_AUX_MAGIC_V3; }
    // Recognized Litenyx-aware format (V1, V2, or V3). Replaces the old
    // single-magic notion; consensus code that gated on "is this a Litenyx
    // header" uses this.
    bool HasKnownMagic() const { return IsV1() || IsV2() || IsV3(); }

    void SetMagicV1() { magic = LITENYX_AUX_MAGIC_V1; }
    void SetMagicV2() { magic = LITENYX_AUX_MAGIC_V2; }
    void SetMagicV3() { magic = LITENYX_AUX_MAGIC_V3; }
    // Deprecated spelling retained for existing Phase 2/3 call sites: sets V1.
    void SetMagic() { magic = LITENYX_AUX_MAGIC_V1; }
    // Deprecated spelling retained for existing Phase 2/3 call sites: recognizes
    // ANY known Litenyx format (V1 or V2) so V2 headers are not misclassified.
    bool HasMagic() const { return HasKnownMagic(); }

    void SetNull() {
        magic = 0;
        chainId = 0;
        eventHeight = 0;
        auxAnchor = uint256();
        splitVector = 0;
        reserved = 0;
        topologyCommitment = uint256();
        lifecycleCommitment = uint256();
    }

    // Phase 4/5 accessor: a topology commitment is STRUCTURALLY present iff the
    // header is V2 OR V3 (V3's 88-byte prefix carries the exact V2 field, spec
    // §6.1). A header with an all-zero commitment is PRESENT (it will simply
    // fail comparison unless zero is the expected hash).
    bool HasTopologyCommitment() const { return IsV2() || IsV3(); }

    // Phase 5 accessor: a lifecycle commitment is STRUCTURALLY present iff the
    // header is V3 (spec §6.1), mirroring the topology presence ruling.
    bool HasLifecycleCommitment() const { return IsV3(); }

#ifndef KERRNYX_STANDALONE_TEST
    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(magic);
        READWRITE(chainId);
        READWRITE(eventHeight);
        READWRITE(auxAnchor);
        READWRITE(splitVector);
        READWRITE(reserved);
        // V2/V3 trailing field. magic is read first, so the parser knows the
        // exact byte-width of nyx_aux before decoding subsequent block fields.
        // V0/V1 streams carry NO topology bytes (byte-identical to Phase 2/3).
        // V3 carries the EXACT V2 88-byte prefix (topologyCommitment included),
        // then appends lifecycleCommitment -> 120 bytes total (spec §6.1).
        if (magic == LITENYX_AUX_MAGIC_V2 || magic == LITENYX_AUX_MAGIC_V3) {
            READWRITE(topologyCommitment);
        }
        if (magic == LITENYX_AUX_MAGIC_V3) {
            READWRITE(lifecycleCommitment);
        }
    }
#endif

    // Validate the aux header for a block evaluated at nEventHeight against a
    // declared target chain count. Phase 2 ignores eventHeight/splitVector.
    bool ValidateAuxEvent(uint32_t nEventHeight, uint8_t nTargetCount) const {
        if (!HasMagic()) return false;
        if (reserved != 0) return false;
        if (chainId >= LITENYX_MAX_CHAINS) return false;
        if (nTargetCount < LITENYX_MIN_CHAINS) return false;
        if (nTargetCount > LITENYX_MAX_CHAINS) return false;
        (void)nEventHeight; // reserved for Phase 3
        return true;
    }
};

#endif // LITENYX_AUXPOW_H
