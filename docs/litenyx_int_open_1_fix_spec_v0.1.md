# Litenyx INT-OPEN-1 — Cross-Phase Rejection Atomicity Fix — Spec v0.1

> **Status: SPEC-FIRST ONLY.** No Phase-2 code changes. This spec designs the
> atomicity fix for INT-OPEN-1 AGAINST the frozen doctrine
> (`litenyx_sharedspendset_doctrine_v0.1.md`, SS-INV-1..7) and the Component-11
> finding (`litenyx_ecosystem_critique_v0.1.md`). It establishes the exact
> canonical commit boundary, rejection semantics, disconnect symmetry, and crash
> interaction BEFORE any implementation.

## 0. The frozen acceptance condition

```
ConnectBlock(B) = Reject          =>  SSS_after = SSS_before   (bit-for-bit)   [SS-INV-4]
Δ SSS (production)                 <=> successful canonical connect/disconnect  [SS-INV-3]
```

The fix is correct iff a SharedSpendSet delta becomes observable IF AND ONLY IF the
canonical block transition it belongs to actually commits — and never for a block
that is subsequently rejected by ANY layer.

## 1. The defect restated (from Component 11 Surface 4)

Current order inside `ConnectBlock` (`litenyx-validation.patch:43-111`):

```
1. LitenyxCheckAuxHeader
2. LitenyxConnectSharedState   <-- RecordSpend commits into the process-global singleton
3. LitenyxCheckTopologyCommitment    (can return false)
4. LitenyxCheckLifecycleCommitment   (can return false)
5. LitenyxCheckExecutionAuthority    (can return false)
```

Steps 3/4/5 `return false` with no `RevertSpend`; the singleton is not part of the
discarded `CCoinsViewCache`, so leaked spends persist. Violates SS-INV-4.

## 2. The decisive question — where is the canonical commit boundary?

```
Earliest point at which a SSS delta may become visible such that NO subsequent
failure can leave it committed for a non-canonical block.
```

### 2.1 Dogecoin/Bitcoin-lineage lifecycle (verified structurally)

The Litenyx hook sits near the END of `ConnectBlock` (after `view.SetBestBlock`,
at the "Index writing" bench marker — `litenyx-validation.patch:112-115`). But
`ConnectBlock` returning true is NOT the canonical commit. The call chain is:

```
ActivateBestChainStep
  └─ ConnectTip(pindexNew, block)
       ├─ ConnectBlock(block, pindexNew, view)      // Litenyx hooks live here
       │     ... returns false  => block rejected, view discarded
       ├─ view.Flush()                              // <-- writes UTXO delta into pcoinsTip (in-memory batch)
       ├─ (later) FlushStateToDisk / pcoinsTip->Flush()  // <-- durable
       └─ mempool / wallet updates
```

Key facts for this lineage:
- If `ConnectBlock` returns false, `ConnectTip` returns false and the
  `CCoinsViewCache view` (a stack-local layered over `pcoinsTip`) is DISCARDED
  without `Flush()`. UTXO changes vanish; the standalone SharedSpendSet singleton
  does NOT (the defect).
- The UTXO delta becomes visible to the node state at `view.Flush()` INSIDE
  `ConnectTip`, AFTER `ConnectBlock` succeeds — i.e. the true logical
  connect-commit boundary for coins state is `ConnectTip`'s `view.Flush()`, not the
  end of `ConnectBlock`.
- Durability (crash boundary) is later still, at `FlushStateToDisk`.

### 2.2 Consequence for the fix

```
The canonical connect-commit boundary lies OUTSIDE (above) ConnectBlock.
```

Therefore **merely reordering `RecordSpend` to run after P6 inside `ConnectBlock`
is INSUFFICIENT** to satisfy SS-INV-4: after P6 passes and `ConnectBlock` returns
true, later steps in `ConnectTip` (or a `view.Flush()` failure, or a shutdown
between `ConnectBlock` and flush) could still fail to make the block canonical,
yet the reordered `RecordSpend` would already have mutated the singleton. Reorder
closes today's specific leak but not the invariant.

The correct solution stages a delta across `ConnectBlock` and publishes it AT the
same boundary the UTXO delta is published (`view.Flush()` in `ConnectTip`), so the
SharedSpendSet delta and the coins delta share ONE commit boundary.

## 3. Mechanism comparison

Criteria: SS-INV-4 satisfaction (no non-canonical commit), SS-INV-3 (production iff),
SS-INV-5 (reorg symmetry), SS-INV-6 (recovery), crash-safety, complexity, and
"requires enumerating every future failure path?".

### M1 — Deferred commit inside ConnectBlock (reorder RecordSpend after P6)
- SS-INV-4: **NO** — closes the P4/P5/P6 leak but not failures ABOVE ConnectBlock
  (§2.2). A block can pass all Litenyx+ConnectBlock checks and still not become
  canonical.
- Complexity: LOW. Enumerate-every-path: NO (only the in-ConnectBlock tail).
- Verdict: **insufficient** as the sole fix; correct direction, wrong boundary.

### M2 — Commit-then-guaranteed-rollback (try/guard)
- SS-INV-4: fragile — requires catching EVERY downstream failure (including above
  ConnectBlock) and reliably reverting. Enumerate-every-path: **YES** (the exact
  brittleness the doctrine wants to avoid).
- Also risks needing try/catch around consensus-critical steps — forbidden by
  INT-NOGO-3.
- Verdict: **rejected** (enumeration burden + NOGO-3 tension).

### M3 — Candidate-state transaction: stage Δ, publish atomically at the canonical commit boundary
- Stage `Δ_B` (the block's spends) in a scoped object during ConnectBlock; the
  logical set is unchanged until the SAME boundary that flushes the coins view
  publishes `Δ_B`. On any failure before that boundary, `Δ_B` is discarded (RAII /
  scope exit), never applied.
- SS-INV-4: **YES by construction** — visibility coincides with canonical commit;
  no per-failure enumeration (discard is the default).
- SS-INV-3: **YES** — Δ published iff the canonical transition commits.
- SS-INV-5: symmetric — disconnect stages a reverse delta published at the
  disconnect commit boundary (mirrors existing `LitenyxDisconnectSharedState`).
- SS-INV-6: compatible — recovery still re-derives the fold; staging is a runtime
  concern only.
- Crash-safety: a crash before publish leaves `SSS_{h-1}`; a crash after publish
  but before durable flush is handled by recovery (SS-INV-6) re-deriving from
  canonical history — the in-memory publish is never treated as durable truth.
- Enumerate-every-path: **NO**. Complexity: MEDIUM (a staging object + one
  publish/discard call at the coins-flush boundary).
- Verdict: **recommended**.

### Preference note
The user's initial candidate (stage -> validate -> commit) is exactly M3, PROVIDED
"commit" is bound to the canonical coins-flush boundary in `ConnectTip`, not to the
end of `ConnectBlock`. This spec adopts M3 with that boundary correction.

## 4. Recommended design (spec-level, no code)

### 4.1 Commit boundary
Publish the SharedSpendSet delta at the SAME boundary the UTXO delta is published:
`ConnectTip`'s `view.Flush()` (logical connect-commit). This makes coins-state and
spend-set share one atomic visibility point.

```
SSS_before -> Stage(Δ_B) during ConnectBlock -> [P4/P5/P6 + all ConnectBlock checks]
   -> ConnectBlock true -> ConnectTip publishes coins view AND Δ_B together
   -> any failure before publish: Δ_B discarded, SSS_after = SSS_before   [SS-INV-4]
```

### 4.2 Staging object (logical shape only)
- A per-connect scoped `Δ_B` = ordered list of `(outpoint, chainId)` to add,
  computed by the SAME check-all-then-commit-all logic (preserving Phase-2
  self-atomicity) but writing into `Δ_B` instead of the live singleton.
- Double-spend check during staging tests `live_set ∪ Δ_B` (so intra-block and
  against-committed conflicts are still caught before publish).
- Ordering of `Δ_B` application follows SS-INV-2 (height, txindex, vinindex).

### 4.3 Publish / discard
- **Publish:** at the connect-commit boundary, apply `Δ_B` to the live set in one
  step. Idempotent and total (all entries pre-validated unspent).
- **Discard:** default on scope exit without publish (ConnectBlock false, or any
  failure before the boundary). No enumeration of failure causes required.

### 4.4 Disconnect symmetry (SS-INV-5)
- Disconnect stages a reverse delta (`RevertSpend` set for the block) published at
  the disconnect commit boundary, mirroring 4.3. Existing
  `LitenyxDisconnectSharedState` semantics preserved; only the visibility point is
  aligned to the coins boundary.

### 4.5 Crash interaction (SS-INV-6/7)
- The in-memory publish is NEVER treated as durable truth. Durability +
  cold-start convergence remain PR-OPEN-1's responsibility (verified checkpoint /
  replay). This spec guarantees only: no non-canonical in-memory commit. A crash
  at any point yields, after recovery, `Fold(canonical history)` — never a leaked
  partial delta.

## 5. Interaction with the other frozen invariants (cross-check)

| Invariant | Interaction | Result |
| --- | --- | --- |
| SS-INV-1 (truth) | staging/publish are runtime representation only | preserved |
| SS-INV-2 (ordering) | Δ_B applied in (height,txindex,vinindex) order | preserved |
| SS-INV-3 (history-only) | Δ published iff canonical commit; RPC still cannot stage/publish | preserved / reinforced |
| SS-INV-4 (atomicity) | visibility == canonical commit boundary | SATISFIED (the fix target) |
| SS-INV-5 (reorg) | symmetric reverse-delta at disconnect boundary | preserved |
| SS-INV-6 (recovery) | staging is runtime; recovery re-derives fold | orthogonal, compatible |
| SS-INV-7 (checkpoint) | unaffected (recovery-layer) | orthogonal |

## 6. No-go / guardrail compliance

- **INT-NOGO-1** (preserve Phase-2 self-atomicity + reorg reversibility) — M3 keeps
  check-all-then-commit-all and adds symmetric disconnect staging. OK.
- **INT-NOGO-2** (don't weaken P4->P5->P6 ordering / fail-closed / no-bypass) — the
  checks run unchanged; only the mutation VISIBILITY moves. OK.
- **INT-NOGO-3** (no try/catch on consensus-critical steps) — M3 uses scope-based
  discard, NOT try/catch around steps 2-5. OK.
- **G-INT-1** — SharedSpendSet mutation is now published only at the canonical
  commit boundary. Satisfied.

## 7. Open sub-questions deferred to implementation (not decided here)

- **INT-Q1** — the precise code hook point for "publish at `view.Flush()`": whether
  it is a call inside `ConnectTip` adjacent to the coins flush, or a registered
  batch that the flush drives. (Implementation detail; both satisfy §4.1.)
- **INT-Q2** — whether staging should reuse the existing free-function API or a new
  scoped type. (Ergonomics; must not create a second reader path — RPC-NOGO/G-INT-3.)
- **INT-Q3** — reindex/IBD path: confirm the same stage/publish boundary applies
  when many blocks connect in sequence (expected: yes, one publish per connected
  block at its own boundary). To be verified against the vendored tree at
  implementation time.

## 8. Disposition

INT-OPEN-1's fix is specified as **M3 — candidate-state transaction: stage the
spend delta during `ConnectBlock`, publish it atomically at `ConnectTip`'s
canonical coins-commit boundary, discard-by-default on any earlier failure.** This
satisfies SS-INV-4 by construction WITHOUT enumerating failure paths and WITHOUT
try/catch on consensus-critical steps, aligns SharedSpendSet visibility with the
UTXO commit, and leaves durability/recovery to PR-OPEN-1. The key correction over
the naive reorder (M1) is that the canonical commit boundary lies ABOVE
`ConnectBlock`, so the fix stages ACROSS `ConnectBlock` and commits at
`ConnectTip`. No Phase-2 code written; no invariant reopened. Next in sequence
(after this spec is accepted): RPC-OPEN-1 design against SS-INV-3, then PR-OPEN-1
against SS-INV-6/7.
