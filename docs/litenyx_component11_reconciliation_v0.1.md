# Litenyx Component 11 — Daemon-Integration Reconciliation Pass — v0.1 (spec-first; no code)

> STATUS: This is a RECONCILIATION pass, NOT a re-critique. Component 11 was completed
> at `dc1e3c3`; its principal defect INT-OPEN-1 received the spec-first M3 design at
> `8d1bd40`. This document verifies that the FUTURE production composition can satisfy
> all post-critique frozen contracts SIMULTANEOUSLY, and classifies INT-OPEN-1's
> current state. No frozen invariant is reopened; no code is written; P4/P5/P6 pure
> engines are untouched.

Reconciliation chain:

```
Original C11 Findings → SS-INV-1..7 → INT M3 Contract → RPC Fix Contracts
                      → PR Recovery Contract → ATMP/XCT Semantics
```

---

## 0. What Component 11 originally established (unchanged, re-affirmed)

From `dc1e3c3` (`litenyx_ecosystem_critique_v0.1.md:1553-1740`):

```
Ordering: CORRECT | Reorg symmetry: CORRECT | Fail-closed: CORRECT | Bypass: NONE
Cross-phase rejection atomicity: GAP (INT-OPEN-1)
P7 production enforcement: NOT YET INTEGRATED (by design)
```

Five of six surfaces sound. The single defect: `LitenyxConnectSharedState` (step 2)
commits spends into the process-global singleton BEFORE steps 3/4/5 (P4/P5/P6) can
reject, with no rollback on the failed-connect path (Surface 4,
`critique:1591-1594,1633-1641`). Everything below tests whether the post-critique
contracts jointly close this without disturbing the five sound surfaces.

---

## 1. Seven-way simultaneous-satisfaction check

Each row states the requirement, the frozen contract that governs it, and the
verdict for the FUTURE production composition.

### (1) Candidate SSS changes remain INVISIBLE during ConnectBlock

- **Governing contract:** INT M3 **R1** (`int_open_1_fix_spec:156`): "The SSS
  CandidateDelta remains INVISIBLE to every reader throughout ConnectBlock."
- **Mechanism:** stage `Δ_B` in a per-connect scoped object; double-spend check tests
  `live_set ∪ Δ_B` (§4.2) so intra-block + against-committed conflicts are still
  caught, but the LIVE singleton is not mutated.
- **Verdict:** SATISFIABLE. Directly supersedes the original Surface-4 behaviour
  where step 2 mutated the live set. ✔

### (2) Publication ONLY upon successful canonical ConnectTip completion

- **Governing contract:** INT M3 **R2** (`:157-158`) + §4.1 boundary correction:
  publish on the successful `ConnectTip` completion path, NOT at end of
  `ConnectBlock`, NOT specifically at `view.Flush()` (which is not the last
  failure-capable step).
- **Residual verification:** **INT-Q4** (`:258-263`) — the EXACT final failure-capable
  step after `view.Flush()` must be confirmed against the vendored Dogecoin tree.
  The contract is bound to the ABSTRACT "successful ConnectTip completion" so it stays
  correct regardless of which concrete step is last.
- **Verdict:** SATISFIABLE, with INT-Q4 an implementation-time verification (NOT a
  design gap). ✔ (pending INT-Q4)

### (3) Failed connects leave the live SSS bit-for-bit unchanged

- **Governing contract:** SS-INV-4 (`doctrine`) + INT M3 **R3** (`:159`,
  discard-by-default) + §4.5.
- **Mechanism:** RAII/scope-exit discard; NO enumeration of failure causes, NO
  try/catch on consensus-critical steps (INT-NOGO-3 honored, `:242-243`).
- **Verdict:** SATISFIABLE by construction:
  `ConnectBlock(B)=Reject ⇒ SSS_after = SSS_before` bit-for-bit. ✔

### (4) Disconnect remains the canonical inverse transition

- **Governing contract:** SS-INV-5 + INT M3 §4.4; original Surface 2
  (`critique:1596-1609`).
- **Mechanism:** disconnect stages a REVERSE delta published on the disconnect
  transition completion, mirroring §4.3; existing `LitenyxDisconnectSharedState`
  semantics preserved, only visibility aligned to the transition boundary.
- **Cross-check:** the original disconnect symmetry (correct for the NORMAL reorg
  path) is preserved; M3 additionally makes the FAILED-connect path a no-op (via (3))
  so disconnect is never needed for a block that never connected. ✔

### (5) RPC has NO production mutation path

- **Governing contract:** RPC-OPEN-1 (mutation gating) + SS-INV-3 (history-only) +
  RPC-NOGO-1/3, G-RPC-1.
- **Mechanism:** RPC-OPEN-1 removes/guards the production writer path
  (`rpc_open_1_fix_spec`); SS-INV-3 forbids RPC from staging or publishing `Δ_B`
  (M3 cross-check row, `int_open_1_fix_spec:230`).
- **Reconciliation with RPC-OPEN-2:** RPC-OPEN-2's provenance labels present the
  spend-set query as CANONICAL confirmed-spend, never as a mutation surface —
  consistent with (5). ✔

### (6) Restart/crash recovery rematerializes Fold(CanonicalHistory), not pre-crash singleton

- **Governing contract:** PR-OPEN-1 (recovery model) + SS-INV-1/6/7 + INT M3 §4.6.
- **Mechanism:** the in-memory publish is NEVER treated as durable truth
  (`:216`); the crash window between durable chain advancement and in-memory SSS
  materialization is closed by recovery re-deriving `Fold(canonical history)` — full
  replay (PR R1) or verified checkpoint + continuation (PR R2).
- **Separation banked:** INT-OPEN-1 = LOGICAL commit-coincidence; PR-OPEN-1 =
  CRASH/cold-start convergence. M3 needs NO crash-atomic coupling to the coins write
  (`:219-222`). ✔

### (7) ATMP's optional SSS check is advisory and never participates in publication

- **Governing contract:** ATMP-OPEN-1/2, ATMP-INV, ATMP-NOGO-1/2, §3.1 of ATMP spec.
- **Mechanism:** the mempool-time SSS check reads a possibly-stale VIEW; it may reject
  early (conservative) but its accept carries no consensus meaning, and it is NOT part
  of `Δ_B` staging or the ConnectTip publish path. The authoritative check remains
  `LitenyxConnectSharedState` staging inside ConnectBlock.
- **Verdict:** SATISFIABLE; ATMP is strictly in the policy plane, orthogonal to M3
  publication. ✔

### 1.1 Joint result

```
All seven conditions are SIMULTANEOUSLY SATISFIABLE by the frozen contracts:
  (1)(2)(3)  ← INT M3 R1/R2/R3  + SS-INV-4        [INT-Q4 = impl verification]
  (4)        ← SS-INV-5 + M3 §4.4
  (5)        ← RPC-OPEN-1 + SS-INV-3 (+ RPC-OPEN-2 presentation)
  (6)        ← PR-OPEN-1 + SS-INV-1/6/7 + M3 §4.6
  (7)        ← ATMP-OPEN-1/2 (advisory, non-publishing)
No contract requires another to be weakened. No new daemon-level contradiction found.
```

---

## 2. Contradiction scan (did any later decision break an earlier C11 surface?)

| Original C11 surface | Post-critique decision touching it | Contradiction? |
| --- | --- | --- |
| S1 ordering P4→P5→P6 | M3 moves VISIBILITY only, not check order (INT-NOGO-2) | NO |
| S2 disconnect symmetry | M3 §4.4 aligns visibility, preserves RevertSpend | NO |
| S3 activation composition | untouched by any later spec | NO |
| S4 failure atomicity (the GAP) | M3 R1–R3 close it (logical); PR closes crash | RESOLVED-BY-DESIGN, not contradicted |
| S5 fail-closed reconstruction | PR-OPEN-1 recovery re-derives fold; fail-closed preserved | NO |
| S6 no-bypass (single path) | RPC-OPEN-1 removes writer path (strengthens); INT-Q2/G-INT-3 forbid second reader | NO — strengthened |
| P7 status (not integrated) | DA-OPEN-1 OPEN; G-INT-4 reserves post-P6 slot | NO — still isolated |

No later decision contradicts a C11 surface. Two surfaces (S4, S6) are STRENGTHENED.

---

## 3. Phase-7 separation (held explicit, per directive)

```
┌───────────────────────────────────────────────────────────────┐
│ P7 Pure Engine          = PROVEN (770496e, 18/18 KATs)         │
│ DA-OPEN-1               = OPEN (drain-entry trigger; §non-close)│
│ P7 Production Integration= NOT YET DEFINED                      │
│                            (G-INT-4 reserves the post-P6 slot;  │
│                             gets its own atomicity/rollback +    │
│                             DrainCommitment recovery review)     │
└───────────────────────────────────────────────────────────────┘
```

The seven-way check deliberately covers ONLY P2/P4/P5/P6 production composition. P7
adds no ConnectBlock hook today, so it introduces no new atomicity obligation now.
When integrated it MUST re-run this reconciliation (G-INT-4 + G-PR-4): a
consensus-visible `DrainCommitment` would be a SECOND consensus-relevant recoverable
object and would re-open the "isolated vs systemic" recovery question (Component 9
caveat).

---

## 4. INT-OPEN-1 classification

```
┌───────────────────────────────────────────────────────────────┐
│ INT-OPEN-1 = SPECIFIED / IMPLEMENTATION-PENDING                │
│                                                                │
│  • DESIGN: complete & frozen (M3, R1–R3), consistent with     │
│    SS-INV-1..7, RPC-OPEN-1/2, PR-OPEN-1, ATMP-OPEN-1/2.        │
│  • NOT RESOLVED: no code exists; SS-INV-4 is satisfied         │
│    BY DESIGN, not yet BY IMPLEMENTATION.                        │
│  • VERIFICATION REMAINING (impl-time, not design):             │
│      – INT-Q4: last failure-capable ConnectTip step (vendored  │
│        tree) → exact publish hook point.                        │
│      – INT-Q1/Q2/Q3: hook shape, API ergonomics, reindex/IBD   │
│        per-block publish (all subordinate to R1–R3).           │
└───────────────────────────────────────────────────────────────┘
```

This is the honest classification requested: **SPECIFIED / IMPLEMENTATION-PENDING** —
NOT "resolved." The label distinguishes a frozen, contradiction-free design from a
delivered fix.

---

## 5. Disposition

```
Component 11 RECONCILED against all post-critique contracts.
  • Seven production-composition conditions are SIMULTANEOUSLY SATISFIABLE by the
    frozen contracts (INT M3 R1–R3, SS-INV-1..7, RPC-OPEN-1/2, PR-OPEN-1,
    ATMP-OPEN-1/2); no contract weakens another.
  • Contradiction scan: NONE. Surfaces S4 and S6 are strengthened.
  • Phase 7 kept explicitly separate: PureEngine PROVEN | DA-OPEN-1 OPEN |
    Production Integration NOT YET DEFINED (G-INT-4 reserves the slot).
  • INT-OPEN-1 = SPECIFIED / IMPLEMENTATION-PENDING (design frozen; SS-INV-4 met by
    design; INT-Q1..Q4 are implementation-time verification, INT-Q4 load-bearing).

Remaining MAJOR architectural uncertainty = DA-OPEN-1 (drain-entry trigger).
The SharedSpendSet/daemon work is now an IMPLEMENTATION-AND-VERIFICATION track
governed by the frozen contracts, not an open design question.

No frozen invariant reopened; no code; P4/P5/P6 pure engines untouched.
Live OPEN design boundaries: DA-OPEN-1 (only major architectural one remaining);
  SSC-OPEN-1/PR-OPEN-1 now specified (implementation-pending);
  XCT/ATMP/RPC OPEN items specified.
```
