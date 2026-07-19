# Litenyx SharedSpendSet — Canonical-State Doctrine Decision Brief — v0.1

> **Status: SPEC-FIRST ANALYSIS ONLY — doctrine + SS-INV-1..7 RATIFIED/FROZEN
> (v0.1).** No code. No Phase-2 reopening. This brief selects the
> canonical-ownership doctrine for `SharedSpendSet` and freezes the invariants
> (Section 9, ratified via the Section 9.1 ambiguity review) that must precede any
> separate fix for INT-OPEN-1, RPC-OPEN-1, or PR-OPEN-1. It is the prerequisite
> identified in the Component 1-11 synthesis
> (`litenyx_ecosystem_critique_v0.1.md`).

## 0. Why this brief exists

The three highest-priority OPEN boundaries are one problem in three guises
(critique synthesis):

```
INT-OPEN-1  (transactional atomicity)
RPC-OPEN-1  (mutation access control)      ->  SharedSpendSet canonical-state problem
SSC/PR-OPEN-1 (recovery convergence)
```

The correct fix for each depends on what `SharedSpendSet` *is*. This brief decides
that, and nothing more.

## 1. The decisive invariant (candidate to freeze)

```
SharedSpendSet_h  ==  Fold( Canonical Multi-Chain Spend History through height h )
```

Read strictly: the MEANING of the spend-set at height `h` is a deterministic fold
over canonical, committed multi-chain history up to `h` — nothing else. Any
storage, cache, checkpoint, or database is at most a REPRESENTATION or ACCELERATOR
of that fold, never an independent source of truth — UNLESS this brief explicitly
decides otherwise (it does not).

Two corollaries this invariant forces:
- **C-1 (Determinism).** Two honest nodes with the same canonical history through
  `h` MUST hold byte-identical `SharedSpendSet_h`, regardless of arrival order,
  timing, restarts, or storage backend.
- **C-2 (No exogenous input).** No value that is not derivable from canonical
  history (RPC writes, wall-clock, mempool, peer hints) may alter
  `SharedSpendSet_h`. (This is the same purity discipline P4/P5/P6 already hold;
  Phase 2 is currently the exception — that is the whole finding.)

## 2. Evaluation criteria (applied uniformly)

| # | Criterion |
| --- | --- |
| K1 | Canonical source of truth (single vs. dual) |
| K2 | `ConnectBlock` atomicity (INT-OPEN-1) |
| K3 | `DisconnectBlock` / reorg reversibility |
| K4 | Restart / reindex / crash convergence (PR-OPEN-1) |
| K5 | Pruning compatibility |
| K6 | Corruption detection |
| K7 | RPC mutation isolation (RPC-OPEN-1) |
| K8 | Implementation complexity |
| K9 | Risk of a second source of truth |
| K10 | Fit with the frozen P4/P5/P6 pure-fold doctrine |

## 3. Model A — Transactional Derived Fold

*The set has no persistent existence; it is an in-memory fold, mutated only by
successful block connection, staged transactionally so nothing commits unless the
whole block connects.*

| K | Assessment |
| --- | --- |
| K1 | SINGLE. History is truth; the set is a pure projection. |
| K2 | STRONG. Staging scoped to successful `ConnectBlock` closes INT-OPEN-1 by construction (commit-last or scoped-rollback). |
| K3 | STRONG. Reorg reversibility is the fold running on the new prefix; explicit `RevertSpend` on the connected-block path already exists. |
| K4 | WEAK without help. Cold start requires FULL replay of canonical history to rebuild the fold — O(chain) every startup/reindex. |
| K5 | Replay needs spend history; on a pruned node the bodies may be gone -> replay may be impossible without an alternative source (PR-NOGO-3 tension). |
| K6 | Trivial (no stored artifact to corrupt) but also nothing to cross-check a live divergence against. |
| K7 | STRONG. RPC mutation is illegitimate BY DEFINITION (not derivable from history); RPC-OPEN-1 closes hard. |
| K8 | LOW-MEDIUM (staging discipline only). |
| K9 | NONE. |
| K10 | PERFECT — identical to P4/P5/P6. |

**Fails on:** K4/K5 at scale — full replay on every restart, and a pruning wall.
The doctrine is right; the recovery ergonomics are impractical alone.

## 4. Model B — Persistent Canonical State

*The set is stored (e.g. LevelDB), flushed and rolled back in lockstep with the
UTXO CoinsView, and treated as canonical state in its own right.*

| K | Assessment |
| --- | --- |
| K1 | DUAL RISK. If "canonical in its own right," the DB competes with history as truth. |
| K2 | STRONG IF batched with the CoinsView flush (fails-with-the-block atomically). |
| K3 | STRONG IF rollback is tied to the same reorg batch. |
| K4 | STRONG. Persisted -> fast restart; no full replay. |
| K5 | GOOD. Survives pruning (the stored set does not need historical bodies). |
| K6 | Requires an explicit integrity mechanism (checksums); otherwise silent DB corruption becomes silent consensus divergence. |
| K7 | MEDIUM. RPC mutation becomes a "maintenance write" to real canonical state — still MUST be gated, but harder to declare categorically illegitimate. |
| K8 | HIGH (serialization, flush/rollback coupling, DB lifecycle). |
| K9 | HIGH unless explicitly subordinated to history (K1). |
| K10 | DIVERGES from P4/P5/P6 (which deliberately persist nothing) — introduces the one committed-cache pattern PR-NOGO-2 warns against. |

**Fails on:** K1/K9/K10 — elevates a snapshot toward independent truth and breaks
the uniform pure-fold doctrine. Strong ergonomics, wrong ontology.

## 5. Model C — Authenticated Checkpoint + Deterministic Verified Replay

*History remains truth. Periodically an AUTHENTICATED checkpoint of the fold state
is written (hash-committed). On startup, load the latest checkpoint whose hash
verifies, then replay canonical history forward from the checkpoint height only.*

| K | Assessment |
| --- | --- |
| K1 | SINGLE, if the checkpoint is explicitly an accelerator (verified against re-derivation), not truth. |
| K2 | Neutral — checkpointing is orthogonal to the connect path; still needs Model A's staging for INT-OPEN-1. |
| K3 | Neutral — reorg handled by the fold; checkpoints below the fork stay valid, those above are discarded. |
| K4 | STRONG. Replay only from last good checkpoint -> bounded startup cost. |
| K5 | GOOD if the checkpoint stores enough fold state to resume WITHOUT pre-checkpoint bodies (replay only post-checkpoint bodies, which an appropriately-configured pruned node retains). Must be stated explicitly (PR-NOGO-3). |
| K6 | STRONG. The checkpoint is hash-authenticated; a corrupt checkpoint fails verification and is rejected, falling back to an earlier checkpoint or full replay. |
| K7 | STRONG. A checkpoint is a derived artifact; RPC still cannot legitimately write the logical set. |
| K8 | MEDIUM (checkpoint format + verification + fallback), less than full DB coupling. |
| K9 | LOW — checkpoint is subordinate to history by verification. |
| K10 | COMPATIBLE — same pure-fold truth, plus an accelerator that P4/P5/P6 could later adopt too. |

**Fails on:** nothing categorically; residual cost is the checkpoint format/verify
machinery and a precise pruning statement. It is Model A's ontology with Model B's
ergonomics — but only for RECOVERY, not for runtime semantics.

## 6. Candidate hybrid doctrine (the one to test hardest)

```
Canonical meaning   =  Derived Fold                         (Model A ontology)
Runtime impl        =  Transactional State                  (staged commit; solves INT-OPEN-1)
Recovery accel      =  Optional Verified Checkpoint         (Model C; solves PR-OPEN-1)
Mutation access     =  Access-controlled / history-only      (solves RPC-OPEN-1)
```

This layers three answers onto ONE truth: history defines meaning; transactional
staging gives atomicity; a verified checkpoint gives fast, corruption-detecting
recovery; and because the logical set is history-derived, RPC mutation is
non-canonical by definition.

### 6.1 Stress tests (where the hybrid could break)

- **Multi-chain ordering.** The fold must be defined over a CANONICAL total order
  of spends across all chains at height `h`, not per-chain arrival. RESULT: safe
  IFF the fold's input ordering is a deterministic function of committed history
  (block height, then a fixed intra-block tx/input order), identical to how
  `ConnectSharedState` already iterates `block.vtx`. **Freeze the ordering rule**
  or determinism (C-1) is not guaranteed across implementations.
- **Reorg vs. checkpoint.** A checkpoint written ABOVE a later fork point must be
  invalidated on reorg. RESULT: safe IFF checkpoints are keyed by block hash (not
  just height) and any checkpoint not on the active chain is discarded. **Freeze
  checkpoint-validity = on-active-chain + hash-verified.**
- **Crash between stage and commit.** A crash after staging but before the atomic
  commit must leave the set equal to `SharedSpendSet_{h-1}`. RESULT: safe IFF
  staging is never observable until the block-connect commit point — i.e. the
  commit boundary coincides with the block's own commit boundary (CoinsView flush
  or equivalent). **Freeze commit-coincidence.**
- **Pruning + checkpoint gap.** If the last verified checkpoint is at `h_c` and
  bodies below `h_c` are pruned, replay from `h_c` forward must need only bodies
  `> h_c`. RESULT: safe IFF the checkpoint captures the COMPLETE fold state at
  `h_c` (not a delta). **Freeze checkpoint-completeness.** If a node has neither a
  valid checkpoint nor the bodies to replay, it MUST fail closed (no weaker
  fallback) — consistent with Component 11 Surface 5.
- **Checkpoint corruption / absence.** RESULT: safe IFF verification failure
  degrades to earlier checkpoint -> full replay -> fail-closed, never to an
  unverified snapshot.

### 6.2 What the hybrid does NOT decide (and must not silently assume)

- Whether a checkpoint is REQUIRED or OPTIONAL for mainnet. (Recommend OPTIONAL
  accelerator; correctness must hold with checkpoints entirely absent = Model A.)
- The concrete serialization/format. (Deferred to the eventual PR-OPEN-1 design;
  this brief only fixes that a checkpoint is hash-authenticated and subordinate.)

## 7. Comparison summary

| Criterion | A Derived Fold | B Persistent | C Checkpoint+Replay | Hybrid (A+C+access) |
| --- | --- | --- | --- | --- |
| K1 single truth | ++ | -- | + | ++ |
| K2 connect atomicity | ++ | + | 0 (needs A) | ++ |
| K3 reorg reversibility | ++ | + | + | ++ |
| K4 restart/reindex/crash | -- | ++ | ++ | ++ |
| K5 pruning | - | ++ | + | + |
| K6 corruption detection | 0 | - | ++ | ++ |
| K7 RPC isolation | ++ | 0 | ++ | ++ |
| K8 complexity | ++ | -- | + | + |
| K9 no 2nd truth | ++ | -- | + | ++ |
| K10 fit P4/5/6 | ++ | -- | + | ++ |

## 8. Recommendation

**Adopt the candidate hybrid doctrine (Section 6):**

```
Derived-fold TRUTH  +  transactional-state RUNTIME  +  optional verified-checkpoint RECOVERY  +  history-only MUTATION
```

Rationale: it is the only option that keeps a SINGLE source of truth (K1/K9/K10),
closes all three OPEN items at their correct layer, and preserves parity with the
frozen P4/P5/P6 pure-fold doctrine. Model B is rejected for elevating a snapshot
toward independent truth; Model A alone is rejected for impractical recovery;
Model C alone is incomplete without A's runtime staging.

## 9. Invariants to FREEZE before designing the three fixes

These are the doctrine outputs. Freeze them first; then INT-OPEN-1, RPC-OPEN-1,
and PR-OPEN-1 are each designed AGAINST them (not improvised).

> **RATIFICATION STATUS: FROZEN (v0.1).** SS-INV-1..7 below are ratified after the
> four-point ambiguity review (Section 9.1). They are now the binding constraints
> for INT-OPEN-1, RPC-OPEN-1, and PR-OPEN-1. Any later change is a doctrine
> revision (v0.2+), not a fix-time decision.

- **SS-INV-1 (Truth).** `SharedSpendSet_h == Fold(canonical multi-chain spend
  history through h)`. Storage/checkpoints are representations, never truth.
- **SS-INV-2 (Determinism / total canonical ordering).** The fold consumes spends
  in a TOTAL deterministic order that is a fixed function of committed history
  ALONE, sufficient for any two validators folding identical canonical histories
  to produce byte-identical `SharedSpendSet_h`. The order is: ascending block
  height; within a block, ascending transaction index (`block.vtx` order, coinbase
  excluded); within a transaction, ascending input index (`vin` order). This total
  order MUST be treated as consensus-normative — NOT arrival/timing/branch order.
  If any future multi-chain structure (e.g. interleaved per-lane blocks at equal
  height) is not fully disambiguated by (height, txindex, vinindex), a successor
  spec MUST extend this ordering BEFORE that structure is activated; the ordering
  source may be deferred to that spec, but its EXISTENCE and consensus-normativity
  are frozen here.
- **SS-INV-3 (History-only mutation / Purity).** In production, the logical set
  changes IF AND ONLY IF a canonical connect/disconnect transition succeeds:

  ```
  Δ SharedSpendSet (production)  <=>  successful canonical connect/disconnect transition
  ```

  No value not derivable from canonical history (RPC writes, wall-clock, mempool,
  peer hints) may alter the logical set. Recovery reconstruction is permitted to
  MATERIALIZE the derived state (it computes the fold) but is NOT an exogenous
  mutation and NEVER a new source of truth. Consequence (closes RPC-OPEN-1 at the
  doctrine level): RPC mutation of the logical set is non-canonical and
  impermissible on any network; any debug affordance is compile-time/runtime
  regtest-gated and cannot exist on main/test.
- **SS-INV-4 (Atomic commit-coincidence).** A candidate block's SharedSpendSet
  mutations become observable IF AND ONLY IF the corresponding canonical block
  transition successfully commits. Equivalently:

  ```
  ConnectBlock(B) = Reject   =>   SSS_after = SSS_before   (bit-for-bit)
  ```

  The set's mutation for `B` is visible ONLY at `B`'s own connect-commit boundary;
  a rejected block leaves `SharedSpendSet_{h-1}` exactly unchanged (closes
  INT-OPEN-1 at the doctrine level). This binds the SharedSpendSet commit point to
  the block's own commit point — no earlier partial visibility.
- **SS-INV-5 (Reorg reversibility).** Disconnecting a block restores exactly the
  pre-block fold state; checkpoints are keyed by block hash and invalidated when
  not on the active chain.
- **SS-INV-6 (Recovery convergence + fail-closed, with the two-state distinction).**
  After restart/reindex/crash, the set MUST converge to `Fold(canonical history)`
  via verified-checkpoint + forward-replay OR full replay. Recovery has exactly two
  admissible outcomes:

  ```
  (established equivalence to the canonical fold)  -> proceed
  (cannot establish equivalence)                   -> fail closed (halt/refuse), NEVER proceed
  ```

  A node that cannot establish equivalence to the canonical fold MUST NOT silently
  start with an empty/partial set, MUST NOT treat a checkpoint as unquestioned
  truth, and MUST NOT fall back to any weaker state. Inability-to-reconstruct is
  distinct from permission-to-continue; only the former's resolution (proven
  equivalence) grants the latter (closes PR-OPEN-1 at the doctrine level).
- **SS-INV-7 (Checkpoint subordination, completeness, and canonical binding).** A
  checkpoint MUST be: (a) hash-authenticated; (b) BOUND to an unambiguous canonical
  history position (block hash + height), so a stale or fork-relative checkpoint is
  detectable; (c) COMPLETE — it captures the full fold state at that position (not
  a delta requiring pre-position bodies); (d) accompanied by a VERIFIABLE
  CONTINUATION PATH — the post-checkpoint canonical bodies needed to replay forward
  to the tip must be present, else the checkpoint is unusable and recovery degrades
  per SS-INV-6; (e) OPTIONAL (correctness holds with none present); and (f) always
  verifiable against re-derivation. Authentication ALONE is insufficient: without
  (b) and (d) an authentic checkpoint could become a second source of truth, which
  SS-INV-1 forbids.

### 9.1 Four-point ambiguity review (record of ratification)

1. **SS-INV-2 ordering source** — RESOLVED: froze the concrete
   (height, txindex, vinindex) total order as consensus-normative AND required a
   successor spec to extend it before any multi-chain structure it cannot
   disambiguate is activated. Existence + normativity frozen; extended source may
   be deferred.
2. **SS-INV-4 commit coincidence** — RESOLVED: stated as an iff bound to successful
   canonical commit, with the explicit `Reject => SSS_after = SSS_before`
   (bit-for-bit) guarantee.
3. **SS-INV-6 fail-closed** — RESOLVED: split into the two admissible outcomes;
   inability-to-reconstruct is separated from permission-to-continue; empty/partial
   start and unquestioned-checkpoint start are both forbidden.
4. **SS-INV-7 completeness** — RESOLVED: authentication augmented with canonical
   position binding (b) and a verifiable continuation path (d), preventing an
   authentic-but-stale/fork-relative checkpoint from becoming a second truth.

Plus **SS-INV-3 strengthening** — RESOLVED: restated as the production iff
`Δ SharedSpendSet <=> successful canonical connect/disconnect`, with recovery
explicitly allowed to MATERIALIZE (not mutate) the derived state.

## 10. Disposition

Doctrine selected and **FROZEN (v0.1)**: **derived-fold truth + transactional
runtime + optional verified-checkpoint recovery + history-only mutation.** Seven
invariants (SS-INV-1..7) are ratified after the four-point ambiguity review
(Section 9.1). This brief proposes NO code and reopens NO Phase-2 behavior. The
three fixes now proceed independently AGAINST the frozen invariants, in the
synthesis-ordered sequence: `doctrine -> INT-OPEN-1 -> RPC-OPEN-1 -> PR-OPEN-1`.
Any change to SS-INV-1..7 is a doctrine revision (v0.2+), never a fix-time
decision.
