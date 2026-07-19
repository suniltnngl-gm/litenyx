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
- The UTXO delta becomes visible to the coins state at `view.Flush()` INSIDE
  `ConnectTip`, AFTER `ConnectBlock` succeeds.
- **But `view.Flush()` is NOT the final failure-capable step of `ConnectTip`.** In
  the Bitcoin/Dogecoin 1.14 lineage, after `view.Flush()` the tip is connected only
  once the REMAINDER of `ConnectTip` completes successfully — notably
  `FlushStateToDisk(state, FLUSH_STATE_IF_NEEDED)`, which can return false and cause
  `ConnectTip` to return false (tip NOT connected). Therefore the canonical
  connect-transition completion boundary is **successful return of `ConnectTip`**,
  not specifically `view.Flush()`.
- Durability (crash boundary) is later/again at `FlushStateToDisk` with a stronger
  mode; crash-atomicity is a DIFFERENT concern from logical commit (see §4.6).

> **Caveat (verification-deferred).** The vendored Dogecoin tree is not present in
> this repo (patches + docs only). The lifecycle above is asserted from the known
> 1.14 lineage; INT-Q3/INT-Q4 require confirming the exact post-`view.Flush()`
> failure-capable steps against the vendored source at implementation time. The
> spec deliberately binds M3 to the ABSTRACT "successful `ConnectTip` completion"
> boundary so it stays correct regardless of which concrete step is last.

### 2.2 Consequence for the fix

```
The canonical connect-transition COMPLETION boundary lies OUTSIDE (above)
ConnectBlock, and is NOT necessarily view.Flush() — it is successful ConnectTip
completion.
```

Therefore **merely reordering `RecordSpend` to run after P6 inside `ConnectBlock`
is INSUFFICIENT** to satisfy SS-INV-4: after P6 passes and `ConnectBlock` returns
true, later steps in `ConnectTip` (e.g. `FlushStateToDisk` returning false, or a
shutdown) could still fail to make the block canonical, yet the reordered
`RecordSpend` would already have mutated the singleton. Reorder closes today's
specific leak but not the invariant.

The correct solution stages a delta across `ConnectBlock` and publishes it ONLY on
the successful `ConnectTip` canonical-transition completion path — NOT bound
specifically to `view.Flush()`, since failure-capable steps may follow it.

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

### M3 — Candidate-state transaction: stage Δ, publish on successful canonical-transition completion
- Stage `Δ_B` (the block's spends) in a scoped object during ConnectBlock; the
  logical set is unchanged until the successful `ConnectTip` completion path
  publishes `Δ_B`. On any failure before that publication, `Δ_B` is discarded (RAII /
  scope exit), never applied.
- SS-INV-4: **YES by construction** — visibility coincides with canonical-transition
  completion; no per-failure enumeration (discard is the default).
- SS-INV-3: **YES** — Δ published iff the canonical transition completes.
- SS-INV-5: symmetric — disconnect stages a reverse delta published on the
  disconnect transition completion (mirrors existing `LitenyxDisconnectSharedState`).
- SS-INV-6: compatible — recovery still re-derives the fold; staging is a runtime
  concern only.
- Crash-safety: LOGICAL commit-coincidence (M3's guarantee) is distinct from
  CRASH-ATOMIC durability (deferred). A crash in the narrow window between
  persistent chain advancement and in-memory SSS materialization is NOT closed by
  M3; it is handled by recovery (SS-INV-6/7) re-deriving `Fold(canonical history)`.
  The in-memory publish is never treated as durable truth. See §4.6.
- Enumerate-every-path: **NO**. Complexity: MEDIUM (a staging object + one
  publish/discard on the transition completion path).
- Verdict: **recommended**.

### Preference note
The user's initial candidate (stage -> validate -> commit) is exactly M3, PROVIDED
"commit" is bound to the successful `ConnectTip` canonical-transition COMPLETION
path — NOT to the end of `ConnectBlock`, and NOT specifically to `view.Flush()`
(which is not the final failure-capable step, §2.1). This spec adopts M3 with that
boundary correction.

## 4. Recommended design (spec-level, no code)

### 4.0 Frozen M3 requirement (the load-bearing contract)

```
(R1) The SSS CandidateDelta remains INVISIBLE to every reader throughout ConnectBlock.
(R2) It may be published ONLY as part of the successful ConnectTip
     canonical-transition COMPLETION path.
(R3) Any failure before that publication discards the CandidateDelta by DEFAULT.
```

These three are the frozen contract. The implementation hook point is subordinate to
them: publication must occur on the transition-completion path, not at a specifically
named intermediate step. Do NOT bind publication to `view.Flush()` — it is not the
final failure-capable operation (§2.1).

### 4.1 Commit boundary
Publish the SharedSpendSet delta on the successful `ConnectTip` completion path
(the logical canonical connect-transition boundary). This makes the SSS delta
become visible only when the block actually becomes canonical.

```
SSS_before -> Stage(Δ_B) during ConnectBlock -> [P4/P5/P6 + all ConnectBlock checks]
   -> ConnectBlock true -> ... remaining ConnectTip steps (incl. FlushStateToDisk) ...
   -> ConnectTip completes successfully  => publish Δ_B
   -> any failure before that completion: Δ_B discarded, SSS_after = SSS_before  [SS-INV-4]
```

**Logical vs crash-atomic (explicit).** R1–R3 guarantee LOGICAL commit-coincidence
during normal execution: no rejected/aborted transition leaves an SSS delta behind.
They do NOT provide a single crash-atomic transaction spanning the persistent coins
write and the in-memory SSS publish. That crash window is out of scope here and is
governed by PR-OPEN-1 / SS-INV-6/7 (§4.6).

### 4.2 Staging object (logical shape only)
- A per-connect scoped `Δ_B` = ordered list of `(outpoint, chainId)` to add,
  computed by the SAME check-all-then-commit-all logic (preserving Phase-2
  self-atomicity) but writing into `Δ_B` instead of the live singleton.
- Double-spend check during staging tests `live_set ∪ Δ_B` (so intra-block and
  against-committed conflicts are still caught before publish).
- Ordering of `Δ_B` application follows SS-INV-2 (height, txindex, vinindex).

### 4.3 Publish / discard
- **Publish:** on the successful `ConnectTip` completion path, apply `Δ_B` to the
  live set in one step. Idempotent and total (all entries pre-validated unspent).
- **Discard:** default on scope exit without publish (ConnectBlock false, or any
  failure before transition completion). No enumeration of failure causes required.

### 4.4 Disconnect symmetry (SS-INV-5)
- Disconnect stages a reverse delta (`RevertSpend` set for the block) published on
  the successful disconnect transition completion, mirroring 4.3. Existing
  `LitenyxDisconnectSharedState` semantics preserved; only the visibility point is
  aligned to the transition-completion boundary.

### 4.5 Scope boundary of this fix (what M3 does and does NOT close)
- **CLOSES:** INT-OPEN-1's rejected-connect contamination. During normal
  execution, a rejected or aborted transition (P4/P5/P6 failure, ConnectBlock
  false, or any `ConnectTip` step failing before completion) leaves
  `SSS_after = SSS_before` bit-for-bit. This is the SS-INV-4 obligation, fully met.
- **DOES NOT CLOSE:** crash-atomicity between persistent canonical-chain
  advancement and in-memory SSS materialization. If the process crashes after the
  chain has durably advanced but before/while the SSS publish is applied,
  reconciliation is NOT M3's job.

### 4.6 Crash interaction — deferred to PR-OPEN-1 (SS-INV-6/7)
- The in-memory publish is NEVER treated as durable truth. The crash window in §4.5
  is resolved on restart by recovery re-deriving `Fold(canonical history)` (verified
  checkpoint + forward replay, or full replay), which by SS-INV-1 is the sole truth.
- Thus M3 needs NO crash-atomic coupling to the coins write: correctness after a
  crash is a recovery property, not a runtime-commit property. This cleanly
  separates INT-OPEN-1 (logical commit-coincidence) from PR-OPEN-1 (crash/cold-start
  convergence), matching the frozen synthesis ordering.

## 5. Interaction with the other frozen invariants (cross-check)

| Invariant | Interaction | Result |
| --- | --- | --- |
| SS-INV-1 (truth) | staging/publish are runtime representation only | preserved |
| SS-INV-2 (ordering) | Δ_B applied in (height,txindex,vinindex) order | preserved |
| SS-INV-3 (history-only) | Δ published iff canonical commit; RPC still cannot stage/publish | preserved / reinforced |
| SS-INV-4 (atomicity) | visibility == successful ConnectTip completion | SATISFIED (logical; the fix target) |
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
- **G-INT-1** — SharedSpendSet mutation is now published only on the successful
  canonical-transition completion path. Satisfied.

## 7. Open sub-questions deferred to implementation (not decided here)

- **INT-Q1** — the precise code hook point for "publish on `ConnectTip` completion":
  whether it is a call at the tail of the success path in `ConnectTip`, or a
  registered batch the success path drives. (Implementation detail; both satisfy
  §4.0 R1–R3.)
- **INT-Q2** — whether staging should reuse the existing free-function API or a new
  scoped type. (Ergonomics; must not create a second reader path — RPC-NOGO/G-INT-3.)
- **INT-Q3** — reindex/IBD path: confirm the same stage/publish boundary applies
  when many blocks connect in sequence (expected: yes, one publish per connected
  block at its own completion). To be verified against the vendored tree.
- **INT-Q4 (load-bearing verification)** — confirm against the VENDORED Dogecoin
  tree the exact ordered list of failure-capable steps in `ConnectTip` AFTER
  `view.Flush()` (expected at least `FlushStateToDisk`), so the publish hook is
  bound to the true LAST failure-capable step's success. If any failure-capable
  step exists after the assumed one, the publish point moves accordingly — R1–R3
  remain the invariant regardless.

## 8. Disposition

INT-OPEN-1's fix is specified as **M3 — candidate-state transaction: stage the
spend delta during `ConnectBlock`, keep it invisible throughout, and publish it
ONLY on the successful `ConnectTip` canonical-transition completion path,
discard-by-default on any earlier failure (frozen as §4.0 R1–R3).** This satisfies
SS-INV-4's LOGICAL commit-coincidence by construction — WITHOUT enumerating failure
paths and WITHOUT try/catch on consensus-critical steps. Two corrections over the
prior draft are load-bearing: (1) the canonical-transition completion boundary lies
ABOVE `ConnectBlock` and is NOT specifically `view.Flush()` (failure-capable steps
follow it), so publication binds to successful `ConnectTip` completion; (2) LOGICAL
commit-coincidence (this fix) is explicitly separated from CRASH-ATOMIC durability
(PR-OPEN-1 / SS-INV-6/7). M3 closes rejected-connect contamination; the crash
window between durable chain advancement and in-memory SSS materialization is closed
by recovery re-deriving the canonical fold, not by this fix. The last
failure-capable step in `ConnectTip` must be verified against the vendored tree
(INT-Q4). No Phase-2 code written; no invariant reopened. Next in sequence (after
this spec is accepted): RPC-OPEN-1 design against SS-INV-3, then PR-OPEN-1 against
SS-INV-6/7.
