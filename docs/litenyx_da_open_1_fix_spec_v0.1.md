# Litenyx DA-OPEN-1 — Drain-Entry Causal Fact — Spec v0.1

> **Status: SPEC-FIRST ONLY.** No Phase-7 code, no serialization, no daemon hook,
> no recovery. Designed AGAINST the frozen Phase-7 engine
> (`LITENYX_draining_authority.h` @ `770496e`), spec v0.2
> (`litenyx_draining_authority_spec_v0.1.md`), and the Component-6 finding
> (`litenyx_ecosystem_critique_v0.1.md`, Surface B / DA-OPEN-1). The FIRST gate is
> the existing-state sufficiency audit (§1). No mechanism/serialization is
> discussed until the audit resolves.
>
> **CORRECTION (post-review, latching audit).** An earlier draft of this spec
> claimed the MERGE decision is decided at `h_obs` and EXECUTED ~COOLDOWN blocks
> later, yielding a pre-transition window. **That is FALSE.** Verified against
> `LitenyxDeriveTopologyAtBoundary` (`LITENYX_topology_authority.h:420-443`): the
> decrement `N -> N-1` is applied IMMEDIATELY at the boundary where MERGE is
> decided, and `nLastTransition = TransitionHeight(h_obs)` is a FORWARD-DATED
> COOLDOWN FLOOR for the NEXT decision — NOT a scheduled execution of THIS one. The
> Phase-5 retirement fold runs at the SAME boundary (`LITENYX_chainid_lifecycle.h
> :356-373`). Therefore decision, decrement, and retirement are COINCIDENT at one
> boundary `h`; there is NO pre-transition determinacy and `MergeDecided` is NOT a
> latched pre-transition fact. Sections below are corrected accordingly; DA-OPEN-1
> is downgraded from RESOLVED to PARTIALLY ADVANCED. The reconstructibility
> breakthrough is preserved; the "makes drain REQUIRED before retirement" claim is
> withdrawn.

## 0. The gate question (frozen)

```
What existing consensus-visible fact makes an ELIGIBLE edge identity REQUIRED to
ENTER draining (not merely eligible to drain)?
```

No-go boundaries preserved throughout (DA-NOGO-1..3, G-DA-1..4):

```
No invented MERGE_INTENT                     (DA-NOGO-1 / F-B)
No committed or derived RetireHeight          (DA-NOGO-2 / F-A, D9, D-K13)
No assumed Phase7DrainPressureSnapshot        (DA-NOGO-3 — necessity must be proven)
No discretionary validator/operator emission  (§4.4.5)
No modification of frozen P4/P5/P6 semantics  (D4 / G-DA-1)
```

Additional matured constraint: **do NOT equate ChainCountController pressure with
drain entry by inheritance.** Any link must be DEMONSTRATED from canonical state,
not assumed from earlier topology-pressure ideas.

## 1. GATE — Existing-State Sufficiency Audit

Strict audit across the frozen P4/P5/P6 surfaces + the locked controller, to decide
which of three outcomes holds:
(a) existing canonical FACT sufficient -> define `RequiredToDrain` directly;
(b) existing OBSERVATIONS sufficient but a new deterministic Phase-7 DERIVATION is
    required (may avoid new committed state);
(c) existing state INSUFFICIENT -> prove the information deficit before any new
    commitment.

### 1.1 Candidate facts inventory

| # | Canonical surface | What it provides | Pre-transition? | Consensus-reproducible? |
| --- | --- | --- | --- | --- |
| F1 | Committed `LitenyxTopologyState` `{nVersion,nHeight,nN,nLastTransition}` (`LITENYX_topology_authority.h:198-218`) | current N, last transition height | N only AFTER decrement | yes, but carries NO intent (F-B) |
| F2 | Controller MERGE decision `A < HYST_LOW ∧ N > MIN` (`LITENYX_topology.h:126-131`) | the merge PRESSURE decision | **YES — decided at h_obs** | **YES — see 1.2** |
| F3 | Deferred transition height `TransitionHeight(h_obs)` = first OBS_WINDOW boundary ≥ `h_obs+COOLDOWN` (`LITENYX_topology.h:136`) | WHEN a decided merge is recorded | YES (a future height) | yes (pure fn of h_obs) |
| F4 | Phase-5 merge fold: retire `retiredLane = Ncur = Nprev-1` (`LITENYX_chainid_lifecycle.h:260-273`) | WHICH identity retires (the highest lane) | YES (deterministic) | yes |
| F5 | Phase-5 `L_h` active bindings; edge = `AuthoritativeLane == N_h-1` (`LITENYX_draining_authority.h:158-166`) | the CURRENT edge identity | YES | yes |
| F6 | Phase-6 authority AUTHORIZED for the edge identity | eligibility precondition | YES | yes |

### 1.2 What IS established — reconstructibility (breakthrough, retained)

The controller decision consumes an observation vector `obs` (`M_c` per lane,
`LITENYX_topology.h:64-74`). This vector is **not** advisory-tracker-only: the
consensus authority path reconstructs it FROM CANONICAL BLOCK BODIES via
`LitenyxReconstructMcV1Window` (`LITENYX_topology_authority.h:173-183`), feeding
`LitenyxDeriveTopologyAtBoundary` / `LitenyxCalculateExpectedTopologyFromChain`
(`:420-443,499`). Block weights are canonical `GetBlockWeight`
(`LITENYX_validation.cpp:111,121`). So `A` and hence the MERGE decision are a PURE
FUNCTION of canonical block bodies over the window `[h-W+1, h]`.

```
ESTABLISHED: MergeDecided(h) := ( LitenyxTopoDecide(Reconstruct(bodies,h), N, h, lastT)
                                  == MERGE )   is CANONICALLY RECONSTRUCTIBLE.
```

This alone refines the spec v0.2 F-B wording (see §1.6 reconciliation): a
deterministic merge FACT is reconstructible from canonical bodies with no new
committed state. That part of the breakthrough stands.

### 1.3 What is NOT established — no pre-transition latching (the load-bearing correction)

The review question was whether `MergeDecided(h_obs) => MergeExecuted(h_transition)`
with a gap between them. **It does not, because there is no gap:**

- `LitenyxDeriveTopologyAtBoundary` (`LITENYX_topology_authority.h:434-441`): when
  `d != HOLD` and `newN != prev.nN`, it sets `next.nN = newN` **at `hObs` itself**.
  The decrement is IMMEDIATE at the decision boundary.
- `next.nLastTransition = LitenyxTopoTransitionHeight(hObs)` (`:439`) is a
  FORWARD-DATED value used ONLY by the cooldown guard of the NEXT decision
  (`LitenyxTopoDecide`: `since = h_obs - lastTransition; if since < COOLDOWN return
  HOLD`, `LITENYX_topology.h:118-122`). It is a COOLDOWN FLOOR, not a scheduled
  execution of the current merge.
- The Phase-5 retirement fold runs at the SAME boundary `h`
  (`LITENYX_chainid_lifecycle.h:356-373`): `LitenyxDeriveTopologyAtBoundary(...,h)`
  then `LitenyxAdvanceChainIdLifecycle(Nprev, Ncur, h)` retires the highest lane in
  the SAME iteration.
- No latched "pending merge" survives across boundaries. Idle pressure at an earlier
  boundary that is suppressed by cooldown is RE-EVALUATED FRESH later; earlier
  idleness does NOT imply a later merge.

```
CONSEQUENCE:  decision  ≡  decrement  ≡  retirement,   all COINCIDENT at boundary h.
              MergeDecided(h)  ∧  Active(id_edge, h)   is CONTRADICTORY for the
              retiring edge identity — it is Retired within the SAME fold at h.
```

So `MergeDecided` is a canonical **pressure/decision fact**, but it is COINCIDENT
with retirement, NOT a **pre-transition** determinant. An `id` "required to drain"
at `h` on the strength of `MergeDecided(h)` would be the very identity that has
ALREADY retired at `h` — draining would begin and be moot in the same fold. This is
the failure mode the review anticipated.

### 1.4 Gate verdict — OUTCOME (b*), NOT full resolution

```
Existing OBSERVATIONS are SUFFICIENT to RECONSTRUCT a deterministic merge fact with
NO new committed state (breakthrough retained). But that fact is COINCIDENT with
retirement, so it does NOT by itself establish a canonical fact that makes an
identity REQUIRED to drain BEFORE it retires. DA-OPEN-1 is PARTIALLY ADVANCED, not
resolved. Outcome (c) is NOT refuted for the specific 'pre-retirement requirement'
question: there is an INFORMATION-TIMING deficit (see §1.5).
```

### 1.5 Information-deficit proof (the residual, honestly stated)

- Reconstructible pre-`h`: idle PRESSURE readings (`A < HYST_LOW`) at boundaries
  during cooldown — but these are NOT decisions (suppressed to HOLD) and do NOT
  determine that a merge will execute at the next post-cooldown boundary (fresh
  re-evaluation may not be idle).
- NOT reconstructible before `h`: the FACT that a merge WILL execute at `h`. It is
  only known AT `h`, coincident with the retirement it causes.
- Therefore the specific quantity DA-OPEN-1 asks for — a canonical fact, available
  BEFORE retirement, that makes the edge identity REQUIRED to drain — has a genuine
  timing deficit under the frozen controller. It is not an ABSENCE of the merge
  fact; it is that the merge fact does not exist earlier than the retirement.

This deficit is exactly what the frozen spec §4.4.5 already suspected ("no
ready-made PRE-TRANSITION committed pressure state"); the audit CONFIRMS it for
determinacy while CORRECTING it for reconstructibility (§1.6).

### 1.6 Reconciliation with the frozen Phase-7 record (DA-NOGO-1 intact)

Two distinct statements must not be conflated:

```
(S1) "No committed MERGE_INTENT FIELD exists."          -> STILL TRUE (F-B). DA-NOGO-1 intact.
(S2) "No deterministic pre-transition merge FACT can be reconstructed." -> IMPRECISE.
      Corrected: a deterministic merge fact IS reconstructible, but only AT the
      transition boundary (coincident), NOT strictly before it.
```

- DA-NOGO-1 is preserved: we introduce no `MERGE_INTENT` field and add no committed
  state. The reconstructed `MergeDecided` is a derivation, not a commitment.
- The spec v0.2 §4.4.5 conclusion (emission OPEN) is UPHELD, now with a sharper
  reason: it is not that the merge decision is unreconstructible, but that it is not
  available BEFORE the retirement it triggers, so it cannot ground a "required to
  drain first" entry without either (i) accepting a coincident/after-the-fact drain
  (operationally moot for the retiring edge), or (ii) a NEW pre-transition signal
  (which DA-NOGO-3 forbids assuming and whose necessity is now PROVEN by §1.5).

## 2. Canonical fact established (reconstructibility) — and its timing limit

```
MergeDecided(h) := ( LitenyxTopoDecide(ReconstructObs(bodies, h), N_h, h, lastT_h)
                     == MERGE )                                   # pure over canonical bodies
```

`MergeDecided(h)` is a genuine canonical, consensus-visible fact — reconstructible
from block bodies with NO new committed state. **But (§1.3) it is COINCIDENT with the
merge decrement and the edge retirement at the SAME boundary `h`.** It therefore
CANNOT distinguish "eligible" from "required-BEFORE-retirement": at `h`, the edge
identity that `MergeDecided(h)` points at is already `Retired`. `MergeDecided` is thus
a valid derivation but the WRONG TIMING to answer the gate as originally posed.

## 3. RequiredToDrain — WITHDRAWN as a pre-retirement entry rule

The earlier formulation

```
RequiredToDrain(id, h)  ?=  P7-DRAIN-ELIGIBLE(id, h)  ∧  MergeDecided(h)
```

is **WITHDRAWN**. It is ill-timed: `MergeDecided(h) ∧ Active(id_edge, h)` is
contradictory (§1.3), so the predicate would either be vacuously false for the
retiring edge, or — if evaluated one boundary earlier without `MergeDecided` — reduce
to bare eligibility (no requirement at all). Neither yields a canonical fact that
makes drain REQUIRED before retirement.

Three honestly-scoped candidate directions remain (NOT selected here; each needs its
own justification against the no-gos):

- **C-A (accept coincident/near-retirement drain).** Treat drain as operationally
  meaningful only in the window leading up to `h` using idle PRESSURE (not decision).
  Requires proving a pressure threshold that is both reconstructible pre-`h` AND does
  not misfire (cooldown re-evaluation breaks determinacy — §1.5). Currently unproven.
- **C-B (accept the unbounded/consequence-only drain, D10).** Do NOT make drain
  REQUIRED at all; keep it a capability MODE that, if entered, monotonically
  restricts, with completion = emergent retirement (D9). This ACCEPTS that "required
  to drain" may have no canonical pre-retirement trigger — consistent with the frozen
  engine, which never claimed one. Lowest risk; leaves emission OPEN as v0.2 did.
- **C-C (introduce a new pre-transition signal).** Its NECESSITY is now PROVEN
  (§1.5), but DA-NOGO-3 forbids ASSUMING it; adopting it is a deliberate new-committed-
  state decision that reopens the "reopen Phase-4?" question and must be escalated,
  not made at fix time.

## 4. DrainCommitment provenance — advanced, not closed

`DrainDecisionEngine` / full D12 provenance CANNOT be declared satisfiable, because it
depends on a `RequiredToDrain` that is now withdrawn (§3). What the audit DID buy:

- The reconstructibility half is proven: any decision predicate over canonical bodies
  IS reproducible by every validator (the mechanical prerequisite for D12).
- The remaining gap is purely the CHOICE of trigger timing (C-A/C-B/C-C), not a
  mechanical inability to reproduce it.

So D12 moves from "semantics-half only, mechanism unknown" to "semantics-half proven
+ reproduction-mechanism proven; TRIGGER SEMANTICS still OPEN." That is real progress
without overclaiming.

## 5. Interaction with frozen invariants / no-gos (cross-check)

| Constraint | Interaction | Result |
| --- | --- | --- |
| DA-NOGO-1 (no MERGE_INTENT) | S1 upheld (no field); `MergeDecided` is a derivation, not a committed intent (§1.6) | honored |
| DA-NOGO-2 (no RetireHeight) | no retire height introduced; completion stays emergent (D9) | honored |
| DA-NOGO-3 (no new snapshot) | none assumed; C-C's necessity proven but NOT adopted here | honored |
| §4.4.5 (no discretionary emission) | emission stays OPEN; no discretionary path added | honored |
| D4 / G-DA-1 (no frozen mutation) | reuses P4 reconstruction; adds/serializes nothing | honored |
| D0 (DRAINING is a mode) | unchanged | honored |
| D9 / G-DA-2 (completion = P5 retirement) | untouched | honored |
| D10 (unbounded drain permitted) | C-B leans on this; consequence now explicit, not accidental | consistent |
| G-DA-3 (identity-keyed) | any future predicate keys on PersistentChainId | honored |
| controller-link | shown to be COINCIDENT (not pre-transition); link demonstrated but ill-timed for entry | corrected |

## 6. Open sub-questions carried forward

- **DA-Q1 (was the gate; now the residual)** — Choose the drain-entry trigger among
  C-A (pre-`h` pressure, must first PROVE a determinate reconstructible threshold
  despite cooldown re-evaluation), C-B (no required trigger; consequence-only drain
  under D10), or C-C (new pre-transition signal — necessity proven §1.5, adoption is
  an escalated new-committed-state decision, DA-NOGO-3). **No option is selected.**
  C-B is a valid architectural DISPOSITION but MUST NOT be recorded as CLOSING
  DA-OPEN-1: doing so would convert "no canonical trigger currently exists" into a
  positive doctrine that draining never needs one. All three remain legitimate
  future choices pending their own justification.
- **DA-Q2** — If C-A is pursued: is there ANY canonical pre-`h` quantity that
  determines a merge at `h` under the cooldown/fresh-re-evaluation semantics (§1.5)?
  Current analysis says NO; a rigorous impossibility proof (or counterexample) should
  be produced before C-A is entertained.
- **DA-Q3** — `MergeDecided` reconstruction cost reuses the P4 window reconstruction
  (already on the consensus path); confirm no additional full-chain walk.
- **DA-Q4 (from PR-W1)** — future concurrent same-height per-lane blocks would break
  edge uniqueness AND the coincidence analysis; revisit under extended SS-INV-2.

## 7. Disposition

DA-OPEN-1 is **PARTIALLY ADVANCED, NOT resolved.** The load-bearing latching audit
requested in review CORRECTS an error in the prior draft:
`LitenyxDeriveTopologyAtBoundary` (`LITENYX_topology_authority.h:434-441`) applies the
`N -> N-1` decrement IMMEDIATELY at the boundary where MERGE is decided, and
`nLastTransition = TransitionHeight(h_obs)` is a forward-dated COOLDOWN FLOOR for the
NEXT decision — NOT a scheduled execution of the current one. The Phase-5 retirement
fold runs at the SAME boundary. Hence:

```
MergeDecided(h)  ≢  a pre-transition determinant;
decision ≡ decrement ≡ retirement are COINCIDENT at h, so
MergeDecided(h_obs) ⇏ (a window in which the edge is Active-but-required-to-drain).
```

The `RequiredToDrain = ELIGIBLE ∧ MergeDecided` formulation is **WITHDRAWN** (§3):
it would target an identity already `Retired` in the same fold. What SURVIVES and is
valuable: **`MergeDecided(h)` is a genuine canonical, consensus-reconstructible merge
fact requiring NO new committed state** — this REFINES the frozen record by
distinguishing S1 ("no committed `MERGE_INTENT` field" — still TRUE, DA-NOGO-1 intact)
from S2 ("no reconstructible merge fact" — corrected: reconstructible, but only
COINCIDENT with retirement, not before). The residual is a genuine information-TIMING
deficit (§1.5), which UPHOLDS the spec v0.2 decision to leave emission OPEN, now with
a proven reason.

Net: the reconstructibility breakthrough is banked; the unbounded/consequence-only
nature of drain (D10) is now an EXPLICIT accepted consequence rather than an accident;
and the true remaining question is a TRIGGER-TIMING choice (C-A / C-B / C-C, DA-Q1),
not a mechanical reproducibility gap. No frozen surface reopened; no new committed
state introduced; DA-NOGO-1..3, §4.4.5, D0/D4/D9/D10, G-DA-1..4 all honored.

**Precise disposition (frozen for downstream):**

```
DA-OPEN-1 = OPEN
  | Existing-state sufficiency audit          = COMPLETE
  | Pre-retirement deterministic trigger      = NOT FOUND
  | Proven result: current P4/P5 canonical state contains NO deterministic
    pre-retirement MERGE fact, because
        MERGE_Decision_h  ≡  N_Decrement_h  ≡  Edge_Retirement_h
    and earlier pressure observations are neither latched nor guaranteed to
    survive fresh evaluation at h.
  | C-A / C-B / C-C remain legitimate future choices; NONE selected.
```

Because DA-OPEN-1 is OPEN (not merely partially resolved), the Component-7 note that
XCT carries DA-OPEN-1 as a CONDITIONAL upstream dependency remains in force. The
downstream rule is frozen as: **XCT correctness MUST NOT depend on DA-OPEN-1 being
resolved** — an XCT consumer may consume an already-established P7 effective
capability if one exists, but MUST remain correct (using P6-derived effective
authority) when no drain commitment exists at all.
