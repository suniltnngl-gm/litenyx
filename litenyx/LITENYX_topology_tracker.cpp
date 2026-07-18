// Litenyx Phase 3 — Topology Tracker implementation.
// See LITENYX_topology_tracker.h. All decision math is delegated to
// LITENYX_topology.h (single source of truth).

#include "LITENYX_topology_tracker.h"

#include <algorithm>

void LitenyxTopologyTracker::Connect(uint8_t chainId, uint32_t height, int Mc)
{
    std::lock_guard<std::mutex> lock(m_mut);
    Accumulate(chainId, Mc);
    if (height % LITENYX_TOPOLOGY_OBS_WINDOW == 0) {
        Finalize(height);
    }
}

void LitenyxTopologyTracker::Disconnect(uint8_t chainId, uint32_t height, int Mc)
{
    std::lock_guard<std::mutex> lock(m_mut);
    (void)chainId; (void)Mc;
    // Reverse any transition recorded at this height.
    auto it = m_transitions.find(height);
    if (it != m_transitions.end()) {
        // Roll N back to the previous transition's value (or the floor).
        uint8_t prev = LITENYX_MIN_CHAINS;
        for (auto& kv : m_transitions) {
            if (kv.first < height) prev = kv.second;
        }
        m_nChains = prev;
        m_lastTransition = 0;
        for (auto& kv : m_transitions) {
            if (kv.first < height && kv.first > m_lastTransition)
                m_lastTransition = kv.first;
        }
        m_transitions.erase(it);
    }
    // Window accumulators are rebuilt on the next Connect pass for real blocks;
    // for the regtest rollback path we leave them (Tick/Reset manage windows).
}

void LitenyxTopologyTracker::Tick(uint32_t height)
{
    std::lock_guard<std::mutex> lock(m_mut);
    Finalize(height);
    ClearWindow();
}

void LitenyxTopologyTracker::Finalize(uint32_t h_obs)
{
    // Build the per-chain observation vector from the current window using the
    // ACTIVE chain count N. Chains beyond N have no observations this window.
    LitenyxObservations obs;
    obs.reserve(m_nChains);
    for (uint8_t c = 0; c < m_nChains; ++c) {
        int Mc = (m_cntM[c] > 0) ? (int)(m_sumM[c] / (int64_t)m_cntM[c]) : 0;
        LitenyxChainObservation o; o.M_c = (int32_t)std::max(0, std::min(100, Mc));
        obs.push_back(o);
    }

    LitenyxTopoDecision d = LitenyxTopoDecide(obs, m_nChains, h_obs, m_lastTransition);
    if (d == LitenyxTopoDecision::HOLD) return;

    uint8_t newN = LitenyxTopoApply(m_nChains, d);
    uint32_t th = LitenyxTopoTransitionHeight(h_obs);
    m_transitions[th] = newN;
    m_nChains = newN;
    m_lastTransition = th;
}
