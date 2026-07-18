#ifndef LITENYX_AUXPOW_H
#define LITENYX_AUXPOW_H

#include <cstdint>
#include <uint256.h>
#include <serialize.h>
#include "LITENYX_types.h"

// Litenyx aux extension (Phase 2). Appended after the 80-byte CBlockHeader
// (AuxPoW-style). Carries the chainId and a same-chain anchor. This is the
// shared-state multi-chain substrate: chains are validation contexts that all
// write to ONE global spent-set (see docs/litenyx_consensus_spec_v0.1.md §3).

struct LitenyxAuxHeader {
    uint32_t magic;          // LITENYX_AUX_MAGIC
    uint32_t chainId;        // 0..N-1 (selects acceptance params, not a ledger)
    uint32_t eventHeight;    // height the event is evaluated at (Phase 3; 0 now)
    uint256  auxAnchor;      // commits to the parent tip on the SAME chain
    uint64_t splitVector;    // reserved for Phase 3 (split/merge vector)
    uint32_t reserved;       // must be zero

    void SetMagic() { magic = LITENYX_AUX_MAGIC; }
    bool HasMagic() const { return magic == LITENYX_AUX_MAGIC; }

    SERIALIZE_METHODS(LitenyxAuxHeader, obj)
    {
        READWRITE(obj.magic);
        READWRITE(obj.chainId);
        READWRITE(obj.eventHeight);
        READWRITE(obj.auxAnchor);
        READWRITE(obj.splitVector);
        READWRITE(obj.reserved);
    }

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
