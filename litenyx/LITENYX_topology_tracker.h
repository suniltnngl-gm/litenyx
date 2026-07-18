// Litenyx Phase 3 — Topology Tracker (daemon-side stateful wrapper).
//
// This wraps the PURE controller math in LITENYX_topology.h with canonical
// block-processing state. It is the SINGLE place the daemon advances / rolls
// back the active chain count N_h. Two entry points:
//
//   * Connect/Disconnect  — driven by REAL blocks in validation.cpp. The
//     observatory reads each block's fullness vs its chain's max block size.
//   * Observe/Tick/Reset  — regtest-only injection so the transition engine,
//     bounds, cooldown and rollback can be exercised deterministically. These
//     call the EXACT same LitenyxTopoDecide / LitenyxTopoApply math as the real
//     path, so they test the genuine module (mirrors the Phase-2 shared-state
//     RPC pattern). They are regtest-only and NEVER affect consensus for real
//     blocks.

#ifndef LITENYX_TOPOLOGY_TRACKER_H
#define LITENYX_TOPOLOGY_TRACKER_H

#include "LITENYX_topology.h"

#include <cstdint>
#include <vector>
#include <map>
#include <mutex>

class LitenyxTopologyTracker {
public:
    // ---- lifecycle ----
    void Reset() {
        std::lock_guard<std::mutex> lock(m_mut);
        m_nChains = LITENYX_MIN_CHAINS;
        m_lastTransition = 0;
        m_transitions.clear();
        ClearWindow();
    }

    // ---- real block path (validation.cpp) ----
    // Connect one block mined on chainId at the given height. Mc is the block's
    // normalized fullness (0..100). On an observation boundary the window is
    // finalized and a decision may be applied.
    void Connect(uint8_t chainId, uint32_t height, int Mc);

    // Disconnect one block. Reverses the window contribution; if this block sat
    // at a recorded transition height, N_h rolls back to the prior value.
    void Disconnect(uint8_t chainId, uint32_t height, int Mc);

    // ---- regtest-only injection ----
    // Feed a synthetic measurement for chainId into the current window.
    void Observe(uint8_t chainId, int Mc) {
        std::lock_guard<std::mutex> lock(m_mut);
        Accumulate(chainId, Mc);
    }

    // Treat `height` as an observation boundary: finalize the current window,
    // decide + apply (if any), then start a fresh window. This is what the
    // regtest calls to advance the topology deterministically.
    void Tick(uint32_t height);

    // ---- read ----
    uint8_t Chains() const {
        std::lock_guard<std::mutex> lock(m_mut);
        return m_nChains;
    }
    uint32_t LastTransition() const {
        std::lock_guard<std::mutex> lock(m_mut);
        return m_lastTransition;
    }

    static LitenyxTopologyTracker& Instance() {
        static LitenyxTopologyTracker s;
        return s;
    }

private:
    mutable std::mutex m_mut;
    uint8_t  m_nChains = LITENYX_MIN_CHAINS;
    uint32_t m_lastTransition = 0;
    // height -> new N recorded at that transition (for rollback).
    std::map<uint32_t, uint8_t> m_transitions;

    // Per-chain window accumulators for the CURRENT (not yet finalized) window.
    std::vector<int64_t> m_sumM;
    std::vector<int>     m_cntM;

    void ClearWindow() {
        m_sumM.assign(LITENYX_TOPO_MAX_CHAINS, 0);
        m_cntM.assign(LITENYX_TOPO_MAX_CHAINS, 0);
    }
    void Accumulate(uint8_t chainId, int Mc) {
        if (chainId >= LITENYX_TOPO_MAX_CHAINS) return;
        m_sumM[chainId] += Mc;
        m_cntM[chainId] += 1;
    }
    // Finalize the window using current accumulators and the active N. Applies a
    // decision if one is produced. Records the transition for rollback.
    void Finalize(uint32_t h_obs);
};

#endif // LITENYX_TOPOLOGY_TRACKER_H
