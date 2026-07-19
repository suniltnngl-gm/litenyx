# Litenyx DA-OPEN-1 — Drain-Entry Causal Fact — Spec v0.1

> **Status: SPEC-FIRST ONLY.** No Phase-7 code, no serialization, no daemon hook,
> no recovery. Designed AGAINST the frozen Phase-7 engine
> (`LITENYX_draining_authority.h` @ `770496e`), spec v0.2
> (`litenyx_draining_authority_spec_v0.1.md`), and the Component-6 finding
> (`litenyx_ecosystem_critique_v0.1.md`, Surface B / DA-OPEN-1). The FIRST gate is
> the existing-state sufficiency audit (§1). No mechanism/serialization is
> discussed until the audit resolves.

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

### 1.2 The decisive finding — the MERGE decision is pre-transition AND canonically reproducible

The spec v0.2 F-B framing said there is "no ready-made pre-transition committed
pressure state." The audit REFINES this with implementation evidence:

- The controller decision consumes an observation vector `obs` (`M_c` per lane,
  `LITENYX_topology.h:64-74`). This vector is **not** advisory-tracker-only: the
  consensus authority path reconstructs it FROM CANONICAL BLOCK BODIES via
  `LitenyxReconstructMcV1ForWindow` (`LITENYX_topology_authority.h:173-183`, feeding
  `LitenyxDeriveNextTopologyState` / `LitenyxCalculateExpectedTopologyFromChain`,
  `:428-434,499`). Block weights are canonical `GetBlockWeight`
  (`LITENYX_validation.cpp:111,121`). So `A` and hence the MERGE decision are a PURE
  FUNCTION of canonical block bodies over the window `[h_obs-W+1, h_obs]`.
- The decision is taken at the OBSERVATION boundary `h_obs`, but the transition is
  RECORDED at `TransitionHeight(h_obs)` = first OBS_WINDOW boundary ≥ `h_obs +
  COOLDOWN` (`LITENYX_topology.h:112-122,136`). With `OBS_WINDOW=100`,
  `COOLDOWN=200`, this is a gap of at least COOLDOWN blocks.

Therefore a **pre-transition, consensus-reproducible merge-pressure fact EXISTS**:
at boundary `h_obs`, every validator can independently compute
`MergeDecided(h_obs) := LitenyxTopoDecide(reconstructed obs, N, h_obs, lastT) ==
MERGE`, strictly before the transition it will cause. This is NOT a new committed
state (DA-NOGO-3 respected) — it is a DERIVATION over already-canonical block
bodies, exactly the derivation P4/P5/P6 already perform.

### 1.3 Which identity — no prediction beyond the frozen fold

If a MERGE is recorded, F4 makes it DETERMINISTIC that the retired identity is the
one on the highest lane at the transition. The frozen eligibility (F5/F6, spec
§4.4.4) already restricts drain to the CURRENT edge identity (`AuthoritativeLane ==
N_h-1`). Between `h_obs` (merge decided) and the transition, the edge identity is
knowable from `L_h`. No MERGE_INTENT and no RetireHeight are needed: we do not
predict WHEN retirement lands (that stays emergent, D9/DA-NOGO-2); we observe that a
merge has been DECIDED and that the structurally-removable identity is the current
edge.

### 1.4 Gate verdict — OUTCOME (b)

```
Existing OBSERVATIONS are SUFFICIENT. No new committed state is required. A NEW,
PURE, DETERMINISTIC Phase-7 DERIVATION over already-canonical block bodies + L_h
+ Phase-6 authority can define RequiredToDrain(id, h). Outcome (c) is REFUTED;
outcome (a) is nearly true but the fact is a DERIVATION, not a single stored field.
```

Consequence for the no-gos: DA-NOGO-3 is HONORED (no new snapshot/committed
surface); DA-NOGO-1/2 are HONORED (no intent, no RetireHeight — we key on the
DECIDED-merge derivation + edge, not on a stored retirement schedule). The
ChainCountController link is DEMONSTRATED, not inherited: it is the merge DECISION,
recomputed from canonical bodies, and it is connected to drain entry ONLY through
the edge-identity eligibility the frozen engine already requires.

### 1.5 Information-deficit note (why outcome (c) fails)

There is NO information deficit for ENTRY. The only thing genuinely NOT derivable
pre-transition is the exact retirement HEIGHT (F-A: emergent inside one fold under
cooldown/boundary interaction). But entry does not need the retirement height —
`DrainStartHeight` is an OBS_WINDOW boundary chosen at/after the decided merge, and
completion is subordinate to actual Phase-5 retirement (D9). So the deficit that
DID motivate leaving emission OPEN (a pre-transition COMMITTED intent) is dissolved:
the pressure fact is reproducible without committing anything.

## 2. Minimal canonical fact (the answer to the gate)

```
MergeDecided(h) := ( LitenyxTopoDecide(ReconstructObs(bodies, h), N_h, h, lastT_h)
                     == MERGE )                                   # pure over canonical bodies
```

This is the minimal canonical, consensus-visible fact that distinguishes "eligible"
from "required": an identity is REQUIRED to enter draining only in a window where a
MERGE has actually been DECIDED (idle pressure `A < HYST_LOW`), i.e. the structural
contraction that will remove the edge is already in motion.

## 3. RequiredToDrain (deterministic predicate)

```
RequiredToDrain(id, h) :=
    ActivationValid_P7(h)                                   # §6 drain regime derived
  ∧ (h % OBS_WINDOW == 0)                                    # boundary-aligned
  ∧ MergeDecided(h)                                          # §2 canonical merge-pressure fact
  ∧ ClassifyChainId(L_h, id) == Active                       # P5 (frozen)
  ∧ Phase6Authority(id, h) == AUTHORIZED                     # P6 (frozen)
  ∧ AuthoritativeLane(L_h, id) == N_h - 1                    # edge-only (frozen §4.4.4)
  ∧ ¬ExistingDrainCommitment(id)
```

Relationship to the frozen `P7-DRAIN-ELIGIBLE` (spec §4.4.4):

```
RequiredToDrain(id, h)  ==  P7-DRAIN-ELIGIBLE(id, h)  ∧  MergeDecided(h)
```

i.e. REQUIRED is strictly ELIGIBLE plus the newly-established canonical merge-decision
fact. Eligibility is a necessary-but-insufficient condition (an edge identity is
always eligible); the merge decision is the causal fact that promotes eligibility to
requirement. This DEMONSTRATES (not inherits) the controller↔drain link: no merge
decided => not required, even though still eligible.

- **Purity / determinism (D5):** every term is a pure function of canonical block
  bodies + frozen P4/P5/P6 engines. Path-independent, fail-closed.
- **No frozen surface touched (D4/G-DA-1):** `MergeDecided` REUSES the existing
  reconstruction; it adds no committed field and does not alter P4/P5/P6.
- **No RetireHeight / MERGE_INTENT (DA-NOGO-1/2):** requirement keys on the DECIDED
  merge derivation + current edge, never on a stored intent or schedule.

## 4. DrainCommitment provenance (closing the D12 statement)

With `RequiredToDrain` established, the deferred provenance predicate (spec §4.4.5/
§4.4.6, `ReproduceDrainCommitment` / `DrainDecisionEngine`) becomes DEFINABLE
without new committed state:

```
DrainDecisionEngine(CanonicalState_h) :=
    { DrainCommitment(id, DrainStartHeight = h)
      | id such that RequiredToDrain(id, h) }               # deterministic, possibly empty
```

- **Determinism:** because `RequiredToDrain` selects at most the single current edge
  identity (`AuthoritativeLane == N_h-1` is unique, L1), the engine's output at any
  boundary `h` is a deterministic set of size 0 or 1. Every validator reproduces it
  identically from canonical bodies.
- **P7-DRAIN-VALIDATE (spec §4.4.6) now satisfiable:**
  ```
  Valid(C, h) ⇔ C ∈ DrainDecisionEngine(CanonicalState_h) ∧ P7-DRAIN-ELIGIBLE(C.id, h)
  ```
  A commitment is valid IFF every validator reproduces exactly `(id, h)` — the full
  D12 statement, no longer only its semantics half.
- **Non-discretionary (§4.4.5):** emission is not proposed by any actor; it is the
  deterministic image of `DrainDecisionEngine` over canonical state. A discretionary
  proposal that does not equal the engine output is invalid (fail closed). This
  eliminates the "actor forces MayBind 1->0" hazard that justified rejecting
  discretionary emission.

> **Scope line.** This spec RESOLVES the causal fact (`RequiredToDrain`) and shows
> the provenance predicate is now definable. It does NOT yet freeze the emitter's
> operational form (autonomous recompute at each boundary vs a carried commitment
> whose provenance is re-checked), nor the byte layout, hook, or recovery — those
> are the follow-on increment, now UNBLOCKED because the input fact exists.

## 5. Interaction with frozen invariants / no-gos (cross-check)

| Constraint | Interaction | Result |
| --- | --- | --- |
| DA-NOGO-1 (no MERGE_INTENT) | uses recomputed MERGE DECISION, not a stored intent field | honored |
| DA-NOGO-2 (no RetireHeight) | entry keys on decided-merge + edge; completion stays emergent (D9) | honored |
| DA-NOGO-3 (no new snapshot) | `MergeDecided` is a derivation over existing bodies; zero new committed state | honored |
| §4.4.5 (no discretionary emission) | emission == deterministic engine image; discretionary != engine is invalid | honored |
| D4 / G-DA-1 (no frozen mutation) | reuses P4 reconstruction; adds/serializes nothing | honored |
| D0 (DRAINING is a mode) | unchanged; this fixes ENTRY provenance only | honored |
| D9 / G-DA-2 (completion = P5 retirement) | untouched; retirement height still not needed/committed | honored |
| G-DA-3 (identity-keyed) | `RequiredToDrain`/engine key on PersistentChainId | honored |
| controller-link demonstrated | REQUIRED = ELIGIBLE ∧ MergeDecided; no-merge => not required | demonstrated, not inherited |

## 6. Open sub-questions deferred to the follow-on increment

- **DA-Q1** — Emitter operational form: recompute `DrainDecisionEngine` at each
  boundary (stateless, no carrier) vs commit a carrier whose provenance is
  re-validated. The causal fact supports BOTH; pick in the serialization increment.
- **DA-Q2** — `MergeDecided` reconstruction cost at scale: it reuses the P4 window
  reconstruction (already on the consensus path), so cost is bounded by existing
  behavior; confirm no additional full-chain walk beyond P4's window.
- **DA-Q3** — Cooldown/boundary interaction edge cases: a merge decided at `h_obs`
  but the edge identity changes before `DrainStartHeight` — the edge-only + Active
  re-check at the evaluated boundary already fail-closes; enumerate against KATs.
- **DA-Q4** — Whether `DrainStartHeight` must equal the decision boundary `h` or may
  be a later boundary before the transition; both are eligible, choose in follow-on.
- **DA-Q5 (from PR-W1)** — if a future phase adds concurrent same-height per-lane
  blocks, both `MergeDecided` reconstruction and edge uniqueness must be revisited
  under the extended SS-INV-2 order.

## 7. Disposition

DA-OPEN-1's gate is RESOLVED at **audit outcome (b): existing canonical observations
are sufficient; no new committed state is required.** The minimal canonical fact is
`MergeDecided(h)` — the frozen ChainCountController's MERGE decision, RECONSTRUCTED
from canonical block bodies (the same reconstruction P4/P5/P6 use), which is BOTH
pre-transition (decided at `h_obs`, recorded ≥ COOLDOWN later) AND
consensus-reproducible. This refutes the earlier "no pre-transition pressure state"
framing WITHOUT reopening Phase-4: it is a derivation, not a commitment. From it,
`RequiredToDrain(id, h) = P7-DRAIN-ELIGIBLE(id, h) ∧ MergeDecided(h)` gives the
causal fact that promotes eligibility to requirement, keyed on the current edge
identity (unique, L1) and PersistentChainId (ABA-safe). This makes
`DrainDecisionEngine` deterministic (output size 0/1) and the full D12
`P7-DRAIN-VALIDATE` provenance statement satisfiable, dissolving the hazard that
justified rejecting discretionary emission. All of DA-NOGO-1..3, §4.4.5, D0/D4/D9,
and G-DA-1..4 are honored; the controller↔drain relationship is DEMONSTRATED, not
inherited. No frozen surface is reopened. The emitter operational form,
serialization, daemon hook, and Phase-7 recovery remain the follow-on increment —
now UNBLOCKED because the drain-entry causal fact exists and is canonical.
