# Litenyx Topology / Split-Merge Consensus Specification — v0.1

*Status: EXPERIMENTAL (Phase 3 substrate). Defines deterministic dynamic
chain-count control for the fixed multi-chain shared-state core. This document
extends `litenyx_consensus_spec_v0.1.md` (which covers the fixed N=2 core) and
is ONLY valid once the Phase-2 acceptance gate is GREEN and frozen at tag
`phase2-green`.*

This spec covers **Phase 3: Dynamic Chain Split/Merge** with N bounded by the
proven `N=2` baseline on the low end and a LOCKED `LITENYX_MAX_CHAINS` target
on the high end. It introduces NO other FUTURE controllers (block size, reward,
negative supply, wallet count, unified feedback) — those remain `FUTURE`.

---

## 1. Locked principles (carried from Phase 2)

1. **1 Blockchain family + 1 Currency + 1 Global Monetary State + N Parallel
   Chains.** Split/merge changes N only; it never creates a new currency or a
   second UTXO universe.
2. **Shared UTXO.** The spent-set is global and single, across all N chains,
   before, during, and after a topology change.
3. **ConsensusCore != RuntimePolicy != WalletPolicy.** Topology control is a
   ConsensusCore behavior: it MUST be deterministic and reproducible from chain
   state alone. No node-local heuristic may alter the effective N.
4. The Phase-2 invariant survives: `Spend(U, C_i) => NOT Spend(U, C_j)` for
   `i != j`, for ALL valid N.

---

## 2. Topology State

The **active chain count** at height `h` is `N_h`. It is a consensus-derived
value, not a node preference.

| Name | Value | Status |
|------|-------|--------|
| `LITENYX_MIN_CHAINS` | 2 | LOCKED (Phase 2 baseline) |
| `LITENYX_MAX_CHAINS` | TARGET-REGULATED, fixed upper bound (e.g. 8) | LOCKED bound |
| `N_h` | current active chain count at height h | DERIVED |
| `LITENYX_TOPOLOGY_OBS_WINDOW` | observation window length (blocks) | LOCKED param |
| `LITENYX_TOPOLOGY_COOLDOWN` | min blocks between topology changes | LOCKED param |
| `LITENYX_TOPOLOGY_HYST_LOW` | deviation band low edge | LOCKED param |
| `LITENYX_TOPOLOGY_HYST_HIGH` | deviation band high edge | LOCKED param |

`N_h` changes ONLY at explicit topology-transition heights (Section 5). Between
transition heights `N_h` is constant.

Each chain `c in [0, N_h)` retains its fixed acceptance-parameter table entry
from Phase 2 (`nTargetSpacing`, `nMinFeeRate`, `nMaxBlockSize`) keyed by
`chainId`. Split creates new `chainId`s reusing the same parameter table shape;
merge retires `chainId`s without discarding their historical blocks (those
blocks remain part of the canonical record, just no longer extended).

---

## 3. Topology Observatory

Over a sliding window of `LITENYX_TOPOLOGY_OBS_WINDOW` blocks ending at the
observation height, each active chain `c` contributes a **deterministic
measurement** `M_c`. `M_c` MUST be computable by every node from block data
alone (no off-chain input).

Phase 3 defines `M_c` as:

```
M_c = normalized demand pressure on chain c
    = (observed block fullness / target fullness)
      adjusted by (observed fee pressure vs nMinFeeRate)
```

The **aggregate topology signal** is:

```
S = max_c(M_c) - min_c(M_c)          // cross-chain imbalance
```

`S` measures how unevenly load is distributed across the active chains. High `S`
suggests one chain is saturated while another is idle -> a SPLIT or MERGE may be
indicated depending on absolute levels (see Section 4).

Observatory inputs are strictly: block sizes, transaction counts, fee rates, and
timestamps already committed in the window. No mempool, no wall-clock, no RNG.

---

## 4. Topology Target Controller

At each observation height `h_obs` (a multiple of `LITENYX_TOPOLOGY_OBS_WINDOW`),
the controller evaluates the deterministic decision:

```
DECISION = HOLD, unless a band crossing is detected.

Let A = aggregate absolute load = mean_c(M_c).

if A > LITENYX_TOPOLOGY_HYST_HIGH and N_h < LITENYX_MAX_CHAINS:
    DECISION = SPLIT          # add one chain to absorb pressure
elif A < LITENYX_TOPOLOGY_HYST_LOW and N_h > LITENYX_MIN_CHAINS:
    DECISION = MERGE          # remove one chain, reclaim idle capacity
else:
    DECISION = HOLD
```

**Hysteresis:** `A` must cross the band edge; inside the band the decision is
always HOLD. This prevents oscillation.

**Cooldown:** a topology change may only occur if at least
`LITENYX_TOPOLOGY_COOLDOWN` blocks have elapsed since the last transition height.
If cooldown is active, DECISION is forced to HOLD.

**Bounds:** SPLIT is impossible at `N_h == LITENYX_MAX_CHAINS`; MERGE is
impossible at `N_h == LITENYX_MIN_CHAINS`. The controller MUST clamp.

The controller output is a single deterministic enum. Two honest nodes at the
same `h_obs` with the same chain state compute the identical DECISION.

---

## 5. Topology Transition Engine

A topology decision becomes **consensus-effective** when it is committed in a
block at the transition height. The transition height is the first block height
`>= h_obs + LITENYX_TOPOLOGY_COOLDOWN` that is a multiple of
`LITENYX_TOPOLOGY_OBS_WINDOW` (i.e. the next observation boundary after cooldown).

### 5.1 SPLIT (N -> N+1)

- A new `chainId = N` is activated with acceptance parameters copied from the
  parameter table at index `N mod table_len` (Phase 2 table is reused; this does
  NOT introduce dynamic parameters).
- The new chain's genesis-of-activation is the current parent tip on the chain
  chosen as its anchor (by deterministic rule: the least-loaded existing chain).
- `LitenyxAuxHeader.splitVector` carries the activation bitmask; `eventHeight`
  carries the transition height.
- The shared spent-set is UNCHANGED. No coin is created, moved, or duplicated.

### 5.2 MERGE (N -> N-1)

- The highest `chainId` (`N-1`) is retired: no new blocks may extend it after
  the transition height.
- Its unspent outputs and spent entries REMAIN in the global shared set. A merged
  chain's confirmed history is preserved; only its future extension stops.
- Transactions referencing outpoints from the retired chain remain globally
  spendable/verifiable through the shared set; they are simply no longer
  *routed* to a live chain for new confirmation until re-confirmed on a surviving
  chain (which is allowed: shared universe).
- `LitenyxAuxHeader.splitVector` carries the retirement bitmask.

### 5.3 Deterministic activation

Every node, upon validating the block at the transition height, applies the same
`N_h` update locally. There is no separate signaling vote: the controller math in
Section 4 IS the consensus rule. Split/merge version bits
(`LITENYX_SPLIT_EVENT_BIT` / `LITENYX_MERGE_EVENT_BIT`) are set in the block
version as a *record* of the deterministic decision, not as a trigger.

---

## 6. Transition Safety Gate

A topology change MUST NOT violate any locked invariant. The gate requires, for
every transition:

1. **No currency duplication.** `total_supply` before == `total_supply` after.
   Split/merge touches only routing/N, never coin issuance.
2. **No state loss.** The global shared spent-set is byte-identical in membership
   before and after the transition (only `N_h` and the active-chain routing table
   change).
3. **No spend-state loss.** Every outpoint spent before the transition remains
   spent after it; every unspent outpoint remains unspent (and still excluded
   from re-spend on ALL chains, including newly created or retired ones).
4. **Cross-chain exclusion preserved.** For the new N:
   `Spend(U, C_i) => NOT Spend(U, C_j)` holds for all `i != j` in `[0, N_h)`.
5. **Reorg rollback preserved.** A reorg that displaces a transition block MUST
   roll `N_h` back to its pre-transition value and MUST leave the shared
   spent-set consistent (reuse of `LitenyxDisconnectSharedState` semantics).

---

## 7. Acceptance gate (Phase 3)

Extends the Phase-2 gate. For a range of dynamically varying N (driven by the
deterministic controller under injected load):

- The Phase-2 invariants hold at EVERY N observed, including during and
  immediately after SPLIT and MERGE transitions.
- `N -> N+1` and `N -> N-1` transitions occur, and in ALL cases:
  - zero duplicated coins,
  - zero lost coins,
  - zero outpoint spendable on two chains simultaneously,
  - shared spent-state survives topology change and rollback.
- A deep reorg across a transition height restores the prior `N_h` and the prior
  shared spent-set with no corruption.

This gate MUST pass (mandatory CI regtests, including adversarial split/merge
under reorg) before Phase 4 begins.

---

## 8. Out of scope for v0.1 (still FUTURE)

- Dynamic block size — Phase 4.
- Dynamic block reward — Phase 5.
- Negative supply — Phase 6.
- Dynamic wallet count / negative position — Phase 7.
- Unified controller feedback across multiple FUTURE controllers — Phase 8.

Phase 3 implements ONLY the dynamic N controller under target-regulated
principles, on top of the frozen Phase-2 shared-state core.
