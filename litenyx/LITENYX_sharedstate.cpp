#include "LITENYX_sharedstate.h"

#include <tinyformat.h>

bool LitenyxSharedSpendSet::RecordSpend(const LitenyxOutPoint& op, uint8_t nChainId)
{
    std::lock_guard<std::mutex> lock(m_mut);
    if (m_spent.find(op) != m_spent.end()) {
        return false; // already globally spent -> double spend rejected
    }
    m_spent[op] = nChainId;
    return true;
}

void LitenyxSharedSpendSet::RevertSpend(const LitenyxOutPoint& op)
{
    std::lock_guard<std::mutex> lock(m_mut);
    m_spent.erase(op);
}

bool LitenyxSharedSpendSet::IsSpent(const LitenyxOutPoint& op) const
{
    std::lock_guard<std::mutex> lock(m_mut);
    return m_spent.find(op) != m_spent.end();
}

uint8_t LitenyxSharedSpendSet::ConfirmingChain(const LitenyxOutPoint& op) const
{
    std::lock_guard<std::mutex> lock(m_mut);
    auto it = m_spent.find(op);
    return (it == m_spent.end()) ? LITENYX_MAX_CHAINS : it->second;
}

bool LitenyxRecordSharedSpend(const uint256& txid, uint32_t n, uint8_t nChainId)
{
    return LitenyxSharedSpendSet::Instance().RecordSpend({txid, n}, nChainId);
}

void LitenyxRevertSharedSpend(const uint256& txid, uint32_t n)
{
    LitenyxSharedSpendSet::Instance().RevertSpend({txid, n});
}

bool LitenyxIsSharedSpent(const uint256& txid, uint32_t n)
{
    return LitenyxSharedSpendSet::Instance().IsSpent({txid, n});
}

uint8_t LitenyxConfirmingChain(const uint256& txid, uint32_t n)
{
    return LitenyxSharedSpendSet::Instance().ConfirmingChain({txid, n});
}

// ---------------------------------------------------------------------------
// INT-OPEN-1 / M3: attempt-scoped candidate spend delta.

bool LitenyxCandidateSpendDelta::StagedHere(const LitenyxOutPoint& op) const
{
    for (const auto& e : m_staged) {
        if (e.first == op) return true;
    }
    return false;
}

bool LitenyxCandidateSpendDelta::StageSpend(const LitenyxOutPoint& op, uint8_t nChainId)
{
    // Reject on live-set conflict (cross-chain / prior-block double spend) or a
    // within-attempt double spend. Never touches the live singleton.
    if (LitenyxSharedSpendSet::Instance().IsSpent(op)) {
        return false;
    }
    if (StagedHere(op)) {
        return false;
    }
    m_staged.emplace_back(op, nChainId);
    return true;
}

void LitenyxCandidateSpendDelta::Publish()
{
    if (m_published) {
        return; // idempotent: never double-apply (R2)
    }
    for (const auto& e : m_staged) {
        LitenyxSharedSpendSet::Instance().RecordSpend(e.first, e.second);
    }
    m_published = true;
}

// One active attempt delta per thread. ConnectTip is single-threaded for the tip
// transition; the thread-local also isolates any parallel validation helpers.
namespace {
thread_local LitenyxSpendPublishScope* g_activeScope = nullptr;
} // namespace

LitenyxSpendPublishScope::LitenyxSpendPublishScope()
    : m_prev(g_activeScope)
{
    g_activeScope = this;
}

LitenyxSpendPublishScope::~LitenyxSpendPublishScope()
{
    // Discard-by-default: if PublishActive() was never called, m_delta simply dies
    // here with ZERO effect on the live set (R3). Restore any outer scope.
    g_activeScope = m_prev;
}

void LitenyxSpendPublishScope::PublishActive()
{
    m_delta.Publish();
}

LitenyxCandidateSpendDelta* LitenyxActiveCandidateDelta()
{
    return g_activeScope ? &g_activeScope->m_delta : nullptr;
}
