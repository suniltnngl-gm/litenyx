#ifndef LITENYX_SHAREDSTATE_H
#define LITENYX_SHAREDSTATE_H

#include <cstdint>
#include <uint256.h>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>
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
        return o.hash.GetCheapHash() ^ o.n;
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

// ---------------------------------------------------------------------------
// INT-OPEN-1 / M3: attempt-scoped candidate spend delta (docs
// litenyx_int_open_1_fix_spec_v0.1.md R1-R3, map v0.1).
//
// PROBLEM: LitenyxConnectSharedState runs inside ConnectBlock (ConnectTip:2333),
// but ConnectTip has a LATER failure-capable step, FlushStateToDisk
// (ConnectTip:2348, verified against dogecoin v1.14.9). Committing spends
// directly into the live singleton at ConnectBlock time leaks spends from blocks
// that never become canonical (R1/R3/SS-INV-4 violation).
//
// FIX: during ConnectBlock we STAGE spends into an attempt-scoped delta that is
//   R1  invisible to readers (IsSpent/LitenyxIsSharedSpent never consult it),
//   R2  applied to the live singleton exactly once, only via explicit Publish()
//       at the ConnectTip non-failure-capable tail (post-2348), and
//   R3  discarded by default: any early return destroys it with ZERO effect on
//       the live set (scope exit, not try/catch).
//
// The delta is a WRITE-ONLY staging surface; the live singleton remains the sole
// reader path (G-INT-3 / RPC-NOGO / Component-11 single-reader).
class LitenyxCandidateSpendDelta {
public:
    // Stage one spend. Rejects (returns false, stages nothing) if `op` is already
    // globally spent in the LIVE set, OR already staged in THIS delta (within
    // block/batch double spend). Never mutates the live singleton.
    bool StageSpend(const LitenyxOutPoint& op, uint8_t nChainId);

    // Is `op` staged in this delta? (Used only for within-attempt coherence, never
    // as a public reader surface.)
    bool StagedHere(const LitenyxOutPoint& op) const;

    // Apply every staged spend to the live singleton, in staging order, via the
    // single canonical writer (RecordSpend). Idempotent guard: a second call is a
    // no-op. This is the ONLY connect-time mutation of the live set.
    void Publish();

    bool IsPublished() const { return m_published; }
    size_t Size() const { return m_staged.size(); }

private:
    std::vector<std::pair<LitenyxOutPoint, uint8_t>> m_staged;
    bool m_published = false;
};

// Attempt-scoped RAII guard OWNED BY THE ConnectTip PATH. Construction installs a
// thread-local active delta that LitenyxConnectSharedState stages into; the
// destructor uninstalls it. Discard-by-default: if PublishActive() was not called
// before destruction, the staged delta evaporates with ZERO effect on the live
// set (R3). Non-copyable/non-movable: strictly one active delta per attempt.
class LitenyxSpendPublishScope {
public:
    LitenyxSpendPublishScope();
    ~LitenyxSpendPublishScope();
    LitenyxSpendPublishScope(const LitenyxSpendPublishScope&) = delete;
    LitenyxSpendPublishScope& operator=(const LitenyxSpendPublishScope&) = delete;

    // Explicitly publish the active delta (call at the ConnectTip success tail,
    // post-FlushStateToDisk). Safe to call once; further calls are no-ops.
    void PublishActive();

private:
    friend LitenyxCandidateSpendDelta* LitenyxActiveCandidateDelta();
    LitenyxCandidateSpendDelta m_delta;
    LitenyxSpendPublishScope* m_prev; // restore-on-destruct (nested-safe)
};

// The currently-active attempt delta for THIS thread, or nullptr if none.
// LitenyxConnectSharedState stages into it. nullptr => no active attempt
// (legacy/non-ConnectTip callers, e.g. tests) which fall back to direct behavior.
LitenyxCandidateSpendDelta* LitenyxActiveCandidateDelta();

// Helpers used by validation.cpp and the regtest RPC.
bool LitenyxRecordSharedSpend(const uint256& txid, uint32_t n, uint8_t nChainId);
void LitenyxRevertSharedSpend(const uint256& txid, uint32_t n);
bool LitenyxIsSharedSpent(const uint256& txid, uint32_t n);
uint8_t LitenyxConfirmingChain(const uint256& txid, uint32_t n);

#endif // LITENYX_SHAREDSTATE_H
