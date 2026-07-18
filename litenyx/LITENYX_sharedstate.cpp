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
