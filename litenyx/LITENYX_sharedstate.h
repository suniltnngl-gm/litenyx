#ifndef LITENYX_SHAREDSTATE_H
#define LITENYX_SHAREDSTATE_H

#include <cstdint>
#include <uint256.h>
#include <mutex>
#include <unordered_map>
#include "LITENYX_types.h"

// Litenyx Phase-2 global shared spent-set.
//
// The core invariant (docs/litenyx_consensus_spec_v0.1.md §3):
//     Spend(U, Chain_A) => NOT Spend(U, Chain_B)        for the same outpoint U
//
// There is ONE global spent-set across all parallel chains. An outpoint, once
// spent by any block (on any chainId), is globally spent and cannot be spent
// again on any chain.
//
// REORG-SAFETY: the set is mutated ONLY through RecordSpend (on ConnectBlock)
// and RevertSpend (on DisconnectBlock). Every connect adds exactly the
// outpoints a block spends; every disconnect removes exactly those. Therefore a
// reorganization that disconnects a block undoes precisely the global state
// transitions that block introduced, before the newly connected history is
// applied. The set is canonical-history aware, NOT an irreversible global flag.

struct LitenyxOutPoint {
    uint256 hash;
    uint32_t n;

    bool operator==(const LitenyxOutPoint& o) const {
        return hash == o.hash && n == o.n;
    }
};

namespace std {
template <> struct hash<LitenyxOutPoint> {
    size_t operator()(const LitenyxOutPoint& o) const {
        return o.hash.GetHash().GetCheapHash() ^ o.n;
    }
};
} // namespace std

class LitenyxSharedSpendSet {
public:
    // Record that `op` was spent by a block mined on `nChainId`.
    // Returns false (and does NOT record) if `op` is already globally spent.
    // Re-entrancy for the same (op, chainId) within one connect is idempotent
    // only at the outpoint level: a true double spend is rejected.
    bool RecordSpend(const LitenyxOutPoint& op, uint8_t nChainId);

    // Revert a spend recorded by RecordSpend (called on DisconnectBlock).
    // Removes `op` from the global set. No-op if `op` is not recorded.
    void RevertSpend(const LitenyxOutPoint& op);

    // Is `op` globally spent right now?
    bool IsSpent(const LitenyxOutPoint& op) const;

    // Confirming chainId for a spent outpoint (LITENYX_MAX_CHAINS if unknown).
    uint8_t ConfirmingChain(const LitenyxOutPoint& op) const;

    void Reset() {
        std::lock_guard<std::mutex> lock(m_mut);
        m_spent.clear();
    }

    static LitenyxSharedSpendSet& Instance() {
        static LitenyxSharedSpendSet s;
        return s;
    }

private:
    mutable std::mutex m_mut;
    std::unordered_map<LitenyxOutPoint, uint8_t> m_spent; // outpoint -> chainId
};

// Helpers used by validation.cpp and the regtest RPC.
bool LitenyxRecordSharedSpend(const uint256& txid, uint32_t n, uint8_t nChainId);
void LitenyxRevertSharedSpend(const uint256& txid, uint32_t n);
bool LitenyxIsSharedSpent(const uint256& txid, uint32_t n);
uint8_t LitenyxConfirmingChain(const uint256& txid, uint32_t n);

#endif // LITENYX_SHAREDSTATE_H
