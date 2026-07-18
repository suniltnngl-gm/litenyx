# Litenyx Consensus Specification — v0.1

*Status: EXPERIMENTAL (Phase 2 substrate). Defines exactly what a validating
node must calculate for the fixed two-chain shared-state core.*

This document covers only **Phase 2: Fixed Multi-Chain Shared-State Consensus**
with N = 2 fixed parallel chains. Later phases extend it; they are marked
`FUTURE` and MUST NOT be implemented against this spec version.

---

## 1. Global constants

| Name | Value | Status |
|------|-------|--------|
| `LITENYX_MIN_CHAINS` | 2 | LOCKED |
| `LITENYX_MAX_CHAINS` | 2 (fixed in Phase 2) | LOCKED for Phase 2 |
| `LITENYX_CHAIN_PARAMS[0..1].nTargetSpacing` | per-chain block time (seconds) | LOCKED |
| `LITENYX_CHAIN_PARAMS[0..1].nMinFeeRate` | per-chain min fee rate (sat/vB) | LOCKED |
| `LITENYX_CHAIN_PARAMS[0..1].nMaxBlockSize` | per-chain max block size (bytes) | LOCKED |
| `LITENYX_H_FORK` | height at which multi-chain activation begins | FUTURE (Phase 1) |
| `LITENYX_SPLIT_EVENT_BIT` | version bit for split (Phase 3) | FUTURE |
| `LITENYX_MERGE_EVENT_BIT` | version bit for merge (Phase 3) | FUTURE |

Per-chain acceptance parameters are fixed at fork time via the chainId table.
They never change the shared currency or UTXO universe.

---

## 2. Chain identity

Each parallel chain `c in [0, N)` is identified by `chainId` carried in the
block's aux extension (`LitenyxAuxHeader.chainId`). `chainId` selects
acceptance parameters only; it does NOT bind a transaction to a ledger.

The aux extension is appended after the 80-byte `CBlockHeader` (AuxPoW-style),
carrying:

```
struct LitenyxAuxHeader {
    uint32_t magic;          // LITENYX_AUX_MAGIC
    uint32_t chainId;        // 0..N-1
    uint32_t eventHeight;    // height the event is evaluated at (Phase 3)
    uint256  auxAnchor;      // commits to parent tip on the SAME chain
    uint64_t splitVector;    // reserved for Phase 3
    uint32_t reserved;       // must be zero
};
```

---

## 3. Shared spent-state (the core invariant)

**LOCKED.** The UTXO spent-set is global and single. An outpoint, once spent by
any transaction confirmed in **any** chain, is globally spent. No chain may
re-accept a transaction that spends an already-spent outpoint.

A validating node MUST maintain one shared `CCoinsView` / spent-index across all
chains. Chain confirmation is a *routing* property; spent-state is *global*.

```
Spend(U, Chain_A) => NOT Spend(U, Chain_B)        (same outpoint U)
```

### 3.1 Transaction visibility

A transaction is **visible and valid on every chain** provided its inputs
reference unspent outpoints in the shared set and its outputs/ fee satisfy the
**accepting chain's** acceptance parameters (fee rate, block size, block time).
There is no per-chain replay protection at the ledger level: a tx valid on one
chain is valid on all (shared universe).

### 3.2 Competing cross-chain spends

If the same outpoint is spent in two different chains within competing blocks,
the normal chain-selection / reorganization rules (most work / AuxPoW) decide
which spend wins; the losing spend's outpoint remains globally spent by the
winning chain's confirmation. Double confirmation of the same spend is
impossible because spent-state is shared.

### 3.3 Reorganization

Reorganization on one chain only affects that chain's tip and the shared
spent-set entries it added/removed. Because the spent-set is shared, a reorg on
Chain_A cannot resurrect an outpoint that Chain_B spent (and vice versa) unless
the reorg also displaces Chain_B's confirming block — which it cannot, since
chains have independent tips but one shared spent-set. The shared spent-set is
the single source of truth for "is this outpoint spendable".

---

## 4. Block validation (Phase 2, fixed N=2)

For each block on chain `c`:

1. Verify PoW / AuxPoW as in Dogecoin baseline.
2. Verify `block.nyx_aux.magic == LITENYX_AUX_MAGIC`.
3. Verify `block.nyx_aux.chainId == c` (block belongs to the chain it extends).
4. Verify `block.nyx_aux.auxAnchor == parentTip_on_chain_c` (anchor commits to
   the same chain's parent).
5. Verify every transaction input references an outpoint unspent in the **global**
   shared spent-set.
6. Verify the block satisfies chain `c`'s acceptance parameters
   (`nMinFeeRate`, `nMaxBlockSize`, `nTargetSpacing` via timestamp rules).
7. Apply transaction effects to the **global** shared spent-set.

Steps 5 and 7 are the invariant-enforcing steps. They MUST use the shared set,
never a per-chain copy.

---

## 5. Out of scope for v0.1 (FUTURE)

- Dynamic N (split/merge) — Phase 3.
- Dynamic block size — Phase 4.
- Dynamic block reward — Phase 5.
- Negative supply — Phase 6.
- Dynamic wallet count / negative position — Phase 7.
- Unified controller feedback — Phase 8.

---

## 6. Acceptance gate (Phase 2)

Two fixed chains MUST run for millions of simulated blocks under adversarial
double-spend and reorg testing, with **zero** occurrences of:

- duplicated coins,
- lost coins,
- an outpoint spendable on two chains simultaneously,
- invalid shared wallet state,
- broken AuxPoW.

This gate MUST pass before Phase 3 begins.
