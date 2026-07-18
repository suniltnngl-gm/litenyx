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
    bool operator<(const uint256& o) const { return std::memcmp(data, o.data, 32) < 0; }
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

    void SetMagic() { magic = LITENYX_AUX_MAGIC; }
    bool HasMagic() const { return magic == LITENYX_AUX_MAGIC; }

    void SetNull() {
        magic = 0;
        chainId = 0;
        eventHeight = 0;
        auxAnchor = uint256();
        splitVector = 0;
        reserved = 0;
    }

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
