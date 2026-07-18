#ifndef LITENYX_TYPES_H
#define LITENYX_TYPES_H

#include <cstdint>

// Litenyx: a fork of Dogecoin applying the locked multi-chain principles.
// Phase 2 (this header set): FIXED two parallel chains sharing ONE UTXO
// universe and ONE currency. Chains differ only in acceptance parameters
// (fee rate, block size, block time) fixed at the fork via a per-chainId table.
//
// The central invariant (see docs/litenyx_consensus_spec_v0.1.md §3):
//   Spend(U, Chain_A) => NOT Spend(U, Chain_B)   for the same outpoint U
// is enforced by a SINGLE global shared spent-set, not per-chain ledgers.

// --- Fixed topology (Phase 2) ------------------------------------------------
static constexpr uint8_t LITENYX_MIN_CHAINS = 2;
static constexpr uint8_t LITENYX_MAX_CHAINS = 2; // fixed during Phase 2

// --- Per-chain acceptance parameters (fixed at fork) ------------------------
// Indexed by chainId (0..MAX_CHAINS-1). These select validation parameters
// ONLY; they never bind a transaction to a ledger or change the currency.
struct LitenyxChainParams {
    uint32_t nMinFeeRate;   // minimum fee rate, sat/vB
    uint64_t nMaxBlockSize; // maximum block size, bytes
    uint32_t nTargetSpacing; // target block time, seconds
};

static constexpr LitenyxChainParams LITENYX_CHAIN_PARAMS[LITENYX_MAX_CHAINS] = {
    // chain 0: fast, cheap (e.g. consumer payments)
    {     10, 1'000'000, 60 },
    // chain 1: larger blocks, slower (e.g. settlement)
    {    100, 4'000'000, 300 },
};

// --- Aux extension identity ---------------------------------------------------
static constexpr uint32_t LITENYX_AUX_MAGIC = 0x4C595858; // "LYXX"

// Event bits (Phase 3). Reserved/unused in Phase 2.
static constexpr int32_t LITENYX_SPLIT_EVENT_BIT = (1 << 29);
static constexpr int32_t LITENYX_MERGE_EVENT_BIT = (1 << 30);

// --- Helpers -----------------------------------------------------------------

// Select the acceptance parameters for a given chainId. Out-of-range ids clamp
// to MIN_CHAINS (defensive; Phase 2 always expects 0..1).
inline const LitenyxChainParams& LitenyxChainParamsFor(uint8_t nChainId)
{
    if (nChainId >= LITENYX_MAX_CHAINS) nChainId = LITENYX_MIN_CHAINS;
    return LITENYX_CHAIN_PARAMS[nChainId];
}

#endif // LITENYX_TYPES_H
