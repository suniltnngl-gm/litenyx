# Litenyx PR-OPEN-1 — SharedSpendSet Recovery / Cold-Start Convergence — Spec v0.1

> **Status: SPEC-FIRST ONLY.** No Phase-2 code changes. Designed AGAINST the frozen
> doctrine (`litenyx_sharedspendset_doctrine_v0.1.md`, esp. SS-INV-1/2/6/7), the
> Component-9 finding (PR-OPEN-1), and the INT-OPEN-1 crash-window deferral
> (`litenyx_int_open_1_fix_spec_v0.1.md` §4.6). Mechanism selection is GATED behind
> the SS-INV-2 sufficiency question (§1) per the doctrine's own escape clause.

## 0. The frozen acceptance condition

```
RecoveredSSS_h = Fold(Canonical Multi-Chain Spend History through h)   [SS-INV-1]
```

with recovery having exactly two admissible outcomes (SS-INV-6):

```
established equivalence to the canonical fold  -> proceed
cannot establish equivalence                    -> FAIL CLOSED (never proceed)
```

## 1. GATE — Is SS-INV-2 sufficient to define one deterministic global multi-chain fold?

This gate MUST resolve before any mechanism is chosen; recovery cannot be designed
around an underspecified fold.

### 1.1 SS-INV-2 frozen order
Ascending block height; within a block, ascending `block.vtx` transaction index
(coinbase excluded); within a transaction, ascending `vin` input index. Frozen as
consensus-normative, with an escape clause: if a future multi-chain structure (e.g.
interleaved per-lane blocks at equal height) is not disambiguated by
`(height, txindex, vinindex)`, a successor spec MUST extend the ordering BEFORE that
structure is activated.

### 1.2 What the FROZEN implementation actually is (verified)
- `LitenyxBuildCanonicalBlocks` (`LITENYX_validation.cpp:98-124`) reconstructs
  history by walking `pindexPrev->pprev` DOWN TO GENESIS: **one block per height, a
  single linear ancestor sequence.** There is exactly one canonical block at each
  height (the active-chain ancestor).
- `chainId` is a **per-block lane LABEL carried inside the block**
  (`block.nyx_aux.chainId`, `LITENYX_validation.cpp:54,110,120`), NOT a separate
  parallel block series. "Multi-chain" in Litenyx Phase-2 means multiple logical
  lanes MULTIPLEXED onto ONE linear block DAG.
- The SharedSpendSet stores `outpoint -> chainId` (`LITENYX_sharedstate.h:73`);
  `RecordSpend` is first-writer-wins (rejects an already-spent outpoint,
  `LITENYX_sharedstate.h:46-49`). So the confirming-chain value of an outpoint is
  determined by WHICH block records it first — i.e. by fold order. Order is
  therefore semantically load-bearing (it fixes `ConfirmingChain`), not cosmetic.

### 1.3 Gate verdict

```
For the CURRENT frozen Phase-2 model, (height, txindex, vinindex) IS a TOTAL order
over all recorded spends, because the canonical history is a single linear block
sequence (one block per height). SS-INV-2 is SUFFICIENT to define one deterministic
global fold. No same-height multi-block ambiguity exists in the frozen system.
```

- **No doctrine-version issue is triggered.** SS-INV-2 is not amended.
- **Watch-item PR-W1 (recorded, not a blocker):** IF a future phase introduces
  genuinely concurrent per-lane blocks at equal height (multiple canonical blocks
  sharing a height), `(height, txindex, vinindex)` would cease to be total and the
  SS-INV-2 escape clause fires: a successor spec MUST add a chain/lane ordering
  dimension BEFORE activation. This spec does NOT design for that structure and
  does NOT silently amend SS-INV-2. Recovery below is defined over the linear model
  that exists.

Only now is mechanism selection admissible.

## 2. The defect restated (from Component 9)

- The SharedSpendSet is a process-global in-memory singleton
  (`LITENYX_sharedstate.h:66-69`) with `Reset()` but NO persistence and NO
  cold-start reconstruction. P4/5/6 authority state is RE-DERIVED from canonical
  chain on demand (verified), but the Phase-2 SharedSpendSet has no defined
  cold-start path (PR-OPEN-1). On restart it begins EMPTY unless something rebuilds
  it.
- Combined with INT-OPEN-1 §4.6: after M3, a crash may leave the chain durably
  advanced while the in-memory SSS is stale/empty. Recovery MUST rebuild from
  canonical history, never trust the pre-crash singleton.

## 3. Mechanism comparison

Requirement for all: end state == `Fold(canonical history through active tip)`;
fail closed otherwise; never elevate cached state to truth (SS-INV-1).

### R1 — Full deterministic replay
- On startup, walk the active canonical chain genesis..tip, reading each block body,
  applying `RecordSpend` in SS-INV-2 order; result is `Fold(history)` by
  construction. This is exactly `LitenyxBuildCanonicalBlocks` extended to fold
  spends instead of topology.
- Convergence: GUARANTEED (it IS the fold definition). SS-INV-1/2/6: satisfied.
- Cost: O(chain) block reads every startup. Requires all bodies present
  (unpruned) OR fails closed (pruned body => cannot reconstruct => refuse; the
  SAME fail-closed pattern P4/5/6 already use, `LITENYX_validation.cpp:116-118`).
- Verdict: **correct baseline, always available when bodies are present**; the
  reference against which any checkpoint is verified.

### R2 — Authenticated checkpoint + verified continuation replay
- Persist a checkpoint = complete fold state at a bound canonical position; on
  startup, IF the checkpoint authenticates AND binds to a still-canonical position
  AND the post-position bodies are available, load it and replay only
  position+1..tip.
- Convergence: equals R1 IFF the checkpoint is proven equivalent to
  `Fold(history through its position)` AND continuation obeys SS-INV-2. Otherwise it
  MUST fall back to R1 or fail closed (SS-INV-6).
- Cost: O(tip - checkpoint) replay; big win on long chains.
- Risk: a checkpoint is a SECOND representation; SS-INV-1/7 require it never become
  truth. Requires the §4 binding/completeness guarantees to be safe.
- Verdict: **recommended optimization LAYERED STRICTLY ON TOP OF R1**, never
  replacing R1's authority.

### R3 — Daemon-integrated recovery materialization (no cached elevation)
- Hook recovery into the daemon's existing startup/reindex path so the SSS is
  materialized as part of chainstate load, reusing the P4/5/6 canonical
  reconstruction machinery, and NEVER reads a "last known singleton" as authority.
- This is not a separate truth source — it is the DELIVERY mechanism for R1 (and
  optionally R2) at the right lifecycle point (before the node serves/validates as
  if SSS were populated).
- Verdict: **adopt as the integration wrapper** around R1 (+optional R2).

### Selected: R1 as authority, R2 as optional verified accelerator, delivered via R3.

## 4. Answers to the seven high-value questions

### 4.1 Canonical position (Q1) — minimum canonical identifier
Height alone is INSUFFICIENT (forks share heights). The minimum binding is the
**block hash of the checkpoint's tip position** (`uint256`), which uniquely
identifies a point in the block DAG, PLUS its height (for locating/ordering during
replay). Formally a checkpoint is bound to `(blockHash, height)` where `blockHash`
is authoritative and `height` is a convenience/consistency check. On load, the
`blockHash` MUST be an ancestor of (or equal to) the current active tip via the
block index; if not, the checkpoint is off the active chain and MUST be rejected
(see 4.6).

### 4.2 Checkpoint completeness (Q2) — proving it is the ENTIRE fold
A checkpoint MUST carry a **content digest of the complete fold state** at its bound
position — a canonical hash over the full `{outpoint -> chainId}` set serialized in a
fixed deterministic order (e.g. sorted by `(outpoint.hash, outpoint.n)`). Two
completeness guarantees:
1. **Self-consistency:** the stored digest matches a re-hash of the loaded set
   (detects truncation/corruption of the snapshot itself).
2. **Equivalence-to-fold (the real proof):** the digest MUST equal the digest of
   `Fold(history through blockHash)`. Since recomputing the full fold defeats the
   purpose, this is established by CONSTRUCTION — a checkpoint is only ever WRITTEN
   at a canonical commit boundary from the live in-memory set that already equals
   the fold (SS-INV-1). A checkpoint of unknown provenance is NEVER trusted as
   complete; if provenance cannot be established, treat as absent (fall back to R1).

An authentic-but-PARTIAL snapshot is thereby rejected: authentication proves
integrity of stored bytes, not completeness; only (2)'s construction-time invariant
proves entirety.

### 4.3 Continuation ordering (Q3) — SS-INV-2 compliance
Replay of `position+1..tip` MUST apply spends in SS-INV-2 order
(height asc, txindex asc, vinindex asc). Per the §1 gate this is a total order in
the current linear model, so continuation is deterministic and equals R1's tail.
PR-W1 governs any future concurrent-height structure.

### 4.4 Pruning (Q4) — minimum body availability + fail-closed
- **Full replay (R1):** requires ALL block bodies genesis..tip. A missing/pruned
  body => cannot reconstruct => FAIL CLOSED (identical to
  `LITENYX_validation.cpp:116-118`).
- **Checkpoint continuation (R2):** requires only bodies for
  `checkpoint.height+1..tip`. This is the pruning-tolerance BENEFIT of R2: a node
  pruned below the checkpoint can still recover IFF a valid checkpoint + the tail
  bodies exist.
- If NEITHER a full body set NOR (valid checkpoint + tail bodies) can establish
  equivalence, recovery FAILS CLOSED — refuse to operate with an unverified SSS
  (SS-INV-6). No empty/partial start.

### 4.5 Crash window (Q5) — the M3 deferral
The INT-OPEN-1 §4.6 case (chain durably advanced, in-memory SSS publish lost) is
resolved here: startup recovery ALWAYS reconstructs from canonical history (R1, or
R2 verified against it) and NEVER infers correctness from the pre-crash singleton.
The pre-crash in-memory set has no authority; if a persisted checkpoint exists it is
subject to the full §4.1/§4.2/§4.6 gating. Thus the crash window is closed by
re-derivation, matching the doctrine's separation of logical commit (INT) from
crash/cold-start convergence (PR).

### 4.6 Reorg / checkpoint interaction (Q6)
A checkpoint whose bound `blockHash` is NOT on the current active chain MUST NEVER be
used as authoritative starting state, EVEN IF its authentication and completeness
digest are valid. Authentication proves "these bytes are an intact fold of SOME
position", not "of a CANONICAL position". On load, verify `blockHash` is an ancestor
of the active tip in the block index; if the active chain has reorged away from it,
the checkpoint is invalid-for-use and recovery falls back to R1 (or an older valid
checkpoint that IS on the active chain), else fails closed. Checkpoints SHOULD be
invalidated/superseded on reorg past their position (mirrors SS-INV-5 reorg
reversibility).

### 4.7 Corruption / absence (Q7)
Any of: authentication failure, missing checkpoint, incomplete continuation bodies,
off-chain checkpoint (4.6), or unavailable required history => the node MUST either
(a) take another PROVABLY EQUIVALENT path (R1 full replay, or a different valid
on-chain checkpoint), or (b) REFUSE to operate with an unverified SharedSpendSet.
Never (c) proceed with empty/partial/unverified state. This is SS-INV-6's two-outcome
rule applied to every failure mode.

## 5. Recommended design (spec-level, no code)

1. **Authority = R1 full deterministic replay** over the active canonical chain in
   SS-INV-2 order, reusing the P4/5/6 `LitenyxBuildCanonicalBlocks` fail-closed
   pattern; result is `Fold(history)` by construction.
2. **Optional accelerator = R2** authenticated checkpoint bound to `(blockHash,
   height)` (4.1), carrying a complete-fold content digest (4.2), usable ONLY when
   on the active chain (4.6) with tail bodies present (4.4); always verified against
   / falling back to R1; never a truth source (SS-INV-1/7).
3. **Delivery = R3** integrate into the daemon startup/reindex/chainstate-load path
   so the SSS is materialized before the node acts on it; never elevate the
   pre-crash singleton (4.5).
4. **Fail-closed everywhere** per 4.4/4.6/4.7 (SS-INV-6).

## 6. Interaction with frozen invariants (cross-check)

| Invariant | Interaction | Result |
| --- | --- | --- |
| SS-INV-1 (truth) | recovery MATERIALIZES the fold; checkpoint never truth | preserved |
| SS-INV-2 (ordering) | replay + continuation in (h,txidx,vinidx); §1 gate PASSED | preserved (PR-W1 watch) |
| SS-INV-3 (history-only) | recovery is materialization, not exogenous mutation | preserved |
| SS-INV-4 (atomicity) | orthogonal (INT owns commit) | orthogonal |
| SS-INV-5 (reorg) | checkpoints invalidated off active chain (4.6) | preserved |
| SS-INV-6 (recovery + fail-closed) | two-outcome rule applied to all modes | SATISFIED (the target) |
| SS-INV-7 (checkpoint binding/completeness) | (blockHash,height) + complete digest + continuation | SATISFIED (the target) |

## 7. Open sub-questions deferred to implementation

- **PR-Q1** — checkpoint cadence/trigger (every N blocks? on clean shutdown?) and
  storage location (leveldb alongside chainstate?). Correctness holds with NONE
  (R1); this is purely a performance/pruning-tolerance knob.
- **PR-Q2** — exact serialization + digest function for the complete-fold hash
  (must be canonical/deterministic; candidate: sorted outpoints -> double-SHA256).
- **PR-Q3** — startup cost of R1 on long chains: whether R2 should be mandatory in
  practice for mainnet-scale histories (still optional for correctness).
- **PR-Q4** — interaction with existing Dogecoin chainstate flush/`FlushStateToDisk`
  so checkpoint writes align with a consistent chainstate snapshot (avoid a
  checkpoint ahead of the durable coins tip).
- **PR-Q5 (from PR-W1)** — if a future phase adds concurrent same-height per-lane
  blocks, raise a doctrine v0.2 to extend SS-INV-2's order BEFORE that activation;
  recovery continuation must then adopt the extended order.

## 8. Disposition

PR-OPEN-1's fix is specified as **R1 full deterministic replay as the sole authority
(result == `Fold(canonical history)` by construction, reusing the P4/5/6 fail-closed
reconstruction pattern), with R2 authenticated-checkpoint + verified-continuation as
an OPTIONAL accelerator bound to `(blockHash, height)` and a complete-fold digest,
usable only when on the active chain with tail bodies present and always subordinate
to R1, delivered via R3 daemon startup/reindex integration.** The load-bearing SS-INV-2
sufficiency GATE is RESOLVED: the current canonical history is a single linear block
sequence, so `(height, txindex, vinindex)` is total and the fold is deterministic;
no doctrine amendment is triggered, and the future concurrent-height case is recorded
as watch-item PR-W1 under SS-INV-2's own escape clause. Recovery converges to the
canonical fold or fails closed — never an empty/partial/unverified SharedSpendSet,
and never trusting the pre-crash singleton (closing the INT-OPEN-1 §4.6 crash
window). No Phase-2 code written; no invariant reopened.

With doctrine + INT-OPEN-1 + RPC-OPEN-1 + PR-OPEN-1 specified, the SharedSpendSet
canonical-state convergence (the Phase-2 boundary identified in the Component 1-11
synthesis) is fully covered at spec level.
