# Litenyx Phase 7 — Draining Authority Specification v0.1

> **Increment 1 (this document): the `DRAINING` operational-capability mode.**
> Frozen against `phase6-green @ a95507f` (the exact tested, compiled-and-exercised
> Phase-6 execution-authority checkpoint).
>
> This is a **specification increment only** — no engine, no daemon hook, no RPC,
> no patch. It freezes the consensus contract that Phase-7 implementation must
> prove, mirroring the proven Phase-4/5/6 discipline: *specification establishes
> authority; implementation proves it (pure, KAT-covered); daemon integration
> makes it non-bypassable and CI proves it compiled-and-exercised.*
>
> **v0.2 R-REP delta (this revision):** the `DrainCommitment` logical shape
> `{PersistentChainId, DrainStartHeight}`, drain **completion** (= actual Phase-5
> retirement), **independence** from merge, and **edge-only eligibility** are now
> FROZEN (§4.4). Grounded in two findings about the frozen engine: retirement is
> emergent/highest-lane/OBS-boundary (F-A), and the topology commitment carries no
> merge-intent (F-B) — so no committed `RetireHeight` and no topology-derived entry
> are representable without reopening Phase 4/5.
>
> **OPEN (deliberately NOT frozen here):** the autonomous drain **EMITTER** /
> `DrainDecisionEngine` (§4.4.5) and the concrete `DrainCommitment` byte layout.
> Discretionary emission is REJECTED; an autonomous emitter needs a
> consensus-reproducible pre-transition pressure signal that the frozen controller
> does not currently expose. This increment freezes the **state-machine,
> capability-projection, precedence, representation shape, completion, and
> validation contract** so the emitter can be designed later without reopening any
> frozen Phase-4/5/6 surface.

---

## 0. The single question this increment answers

> **Given a canonical tip at height `h`, its already-validated Phase-5
> `ChainIdLifecycleState L_h`, and its already-derived Phase-6
> `ExecutionAuthorityResult`, MAY a still-authoritative identity be placed into a
> committed, deterministic "winding-down" window in which it may SETTLE existing
> execution but may NOT ORIGINATE new binding — and how does that window
> compose with the frozen authority without changing it?**

Phase 5 proves *what identities exist* (`{Active, Retired, Nonexistent}`).
Phase 6 proves *what execution authority those identities possess*
(`{AUTHORIZED, REVOKED, UNKNOWN}` + `{MayRoute, MayBind}`). Phase 7 introduces a
new committed state that decides *whether an already-`AUTHORIZED` identity's
operational capabilities are narrowed to settle-only*, WITHOUT reopening either
frozen layer.

```text
Phase-5 Lifecycle Truth  →  Phase-6 Authority Projection  →  + Phase-7 DrainCommitment  →  Effective Capability
   {Active,Retired,          {AUTHORIZED,REVOKED,UNKNOWN}       (new committed state)        {EffectiveMayBind,
    Nonexistent}              {MayRoute, MayBind}                                             EffectiveMayRoute}
```

with the permanent, extended separation:

```text
TopologyLaneId  ≠  PersistentChainId (Identity)  ≠  ExecutionAuthorityState  ≠  OperationalCapabilityMode
```

> **The decisive resolution (from Phase-6 §3.1/A7).** `DRAINING` **cannot** be
> derived from the frozen `{Active, Retired, Nonexistent}` domain, because the
> merge fold retires a `PersistentChainId` atomically inside a single `G` fold at
> the observation boundary — there is no consensus-visible "bound but winding
> down" window in Phase-5. Phase 7 therefore introduces `DRAINING` as **NEW,
> separately-committed consensus state** (`DrainCommitment`), NOT by reinterpreting
> Phase 5 and NOT by expanding the Phase-6 enum.

---

## 1. Locked principles (carried, NOT reopened)

Inherited verbatim; Phase 7 MUST NOT reopen any of these.

- **INVARIANT L0/L1/L2/L3 (Phase 5, LOCKED).** Lanes are bounded reusable
  positions; `PersistentChainId`s are persistent, never recycled; binding
  uniqueness; dense non-recycling allocation (`nextChainId` retirement oracle);
  fail-closed exhaustion. Phase 7 does not touch these.
- **Phase-5 lifecycle domain is FROZEN (byte-for-byte).** `LitenyxChainIdStatus`
  remains exactly `{Active, Retired, Nonexistent}`. The lifecycle commitment bytes
  and the `G` merge fold are UNCHANGED. **Phase 7 adds no field to the Phase-5
  commitment.** (This is the direct consequence of choosing a *separate* drain
  commitment over extending `L_h`.)
- **INVARIANT A1–A7 (Phase 6, LOCKED).** The execution-authority projection is a
  pure total function of frozen Phase-5 status; the authoritative lane is
  exclusively the `L_h` binding; capabilities are pure functions of
  `(state, lane-agreement)` and fail closed; `ExecutionAuthorityState` is never
  serialized/hashed. **A7 in particular is now DISCHARGED, not violated:** Phase 6
  deferred `DRAINING` to "new, separately-committed consensus state with its own
  KATs" — this document defines exactly that.
- **Phase-6 `ExecutionAuthorityResult` is FROZEN.** Its enum
  (`{AUTHORIZED, REVOKED, UNKNOWN}`), its codes (`Ok/Unknown/Revoked/WrongLane/
  Premature/Malformed`), and its fields are byte- and semantics-frozen. **Phase 7
  MUST NOT add a `DRAINING` code or any field to it.**
- **Enforcement ordering (Phase 6, LOCKED).** `ConnectBlock → Phase-5 Lifecycle
  → Phase-6 Execution Authority → remaining validation`. Phase 7 enforcement, when
  later gated, appends STRICTLY AFTER the Phase-6 execution-authority check and
  consumes its result.

### 1.1 Terminology (FROZEN for this increment)

| Phase-7 name | Definition | Owner | Committed? |
|--------------|------------|-------|------------|
| `DrainCommitment` | new committed consensus object marking a `PersistentChainId` as draining from some height/window | Phase 7 (this doc; **representation OPEN**) | **YES (new)** |
| `IsDraining(c, h)` | pure predicate: is `PersistentChainId c` under a valid drain commitment in force at height `h`? | Phase 7 | derived from the commitment |
| `OperationalCapabilityMode` | `{NORMAL, DRAINING}` — a mode of an already-`AUTHORIZED` identity, NOT an authority state | Phase 7 | n/a (derived) |
| `DrainCapabilityProjection` | Phase-7 result `{drainStatus, effectiveMayBind, effectiveMayRoute}` | Phase 7 | n/a (derived) |

> **INVARIANT D0 (LOCKED).** `DRAINING` is an **operational-capability mode**, NOT
> a fourth identity-authority state. `ExecutionAuthorityState` remains exactly
> `{AUTHORIZED, REVOKED, UNKNOWN}`. An identity is "draining" iff it is
> simultaneously Phase-5 `Active`, Phase-6 `AUTHORIZED`, and subject to a valid
> `DrainCommitment` in force at `h`.

---

## 2. Authority inputs (the ONLY authoritative inputs)

Phase 7 consumes, and MUST NOT reconstruct:

1. `L_h` — frozen Phase-5 lifecycle state (identity truth).
2. The frozen Phase-6 `ExecutionAuthorityResult` for the asserted route
   (authority + `{MayRoute, MayBind}`).
3. The `DrainCommitment` set in force at height `h` (NEW; the only new input).

Explicitly **NOT** inputs in this increment (deferred, §11): `SlotId`, `W_t`,
monetary/VEA, ATMP/mempool policy, routing tables, DB persistence, XCT, and any
drain *cancellation* mechanism.

---

## 3. The `DRAINING` composition model (derived overlay)

Phase 7 does **not** modify Phase 6. It composes a monotonic capability
restriction ON TOP of the frozen Phase-6 result:

```text
DrainCapabilityProjection(Phase6Result, DrainCommitment, h)
    consumes  Phase6Result  (never reconstructs or mutates it)
    consumes  IsDraining(c, h)
    returns   { drainStatus, effectiveMayBind, effectiveMayRoute }
```

### 3.1 Effective capability matrix (FROZEN)

| Phase-6 authority | drain in force | `EffectiveMayBind` | `EffectiveMayRoute` | mode |
|-------------------|----------------|-------------------:|--------------------:|------|
| `AUTHORIZED` (lane agrees) | no  | `MayBind_P6` (1) | `MayRoute_P6` (1) | `NORMAL` |
| `AUTHORIZED` (lane agrees) | yes | `0`              | `MayRoute_P6` (1) | `DRAINING` |
| `AUTHORIZED` (wrong lane)  | yes | `0`              | `MayRoute_P6` (0) | `DRAINING` (still no route: Phase-6 denied it) |
| `REVOKED`                  | (irrelevant) | `0`     | `0`               | `NORMAL` (drain has no meaning) |
| `UNKNOWN`                  | (irrelevant) | `0`     | `0`               | `NORMAL` (drain has no meaning) |

> **INVARIANT D1 — Monotonic restriction (LOCKED).** Phase 7 may only REMOVE
> capabilities; it may NEVER manufacture one Phase 6 denied. Formally:
>
> ```text
> EffectiveMayBind  = MayBind_P6  ∧ ¬IsDraining(c, h)
> EffectiveMayRoute = MayRoute_P6                      # drain never changes route grant
> ```
>
> Consequently `{EffectiveMayBind, EffectiveMayRoute} ⊆ {MayBind_P6, MayRoute_P6}`.
> DRAINING can revoke `MayBind`; DRAINING can NEVER grant `MayRoute` (or `MayBind`)
> that Phase 6 did not already grant.

> **INVARIANT D2 — Phase-6 denial dominates drain (LOCKED).** A `DrainCommitment`
> has operational meaning ONLY when the underlying identity is `Active ∧
> AUTHORIZED`. `REVOKED + DrainCommitment` stays `REVOKED`; `UNKNOWN +
> DrainCommitment` stays `UNKNOWN`. Drain never resurrects, softens, or reorders a
> Phase-6 denial.

### 3.2 P7-DRAIN-ROUTE (FROZEN semantic rule)

> **P7-DRAIN-ROUTE.** While a `PersistentChainId` is subject to a valid drain
> commitment in force at `h` AND remains Phase-5 `Active` and Phase-6
> `AUTHORIZED`, it SHALL retain the Phase-6-derived `MayRoute` capability
> **exclusively on its authoritative `L_h` lane**, while `EffectiveMayBind` SHALL
> be `0`. DRAINING SHALL NEVER grant routing authority not already provided by
> Phase 6 (in particular a wrong-lane assertion stays denied). Upon Phase-5
> retirement the identity becomes `REVOKED` and routing authority ceases
> atomically.

This gives `DRAINING` its distinct meaning — **settle-only** — separating it from
`REVOKED` (which forbids both bind and route):

```text
ACTIVE   / AUTHORIZED / NORMAL     : MayBind=1, MayRoute=1   (admit + settle)
DRAINING / AUTHORIZED / DRAINING   : MayBind=0, MayRoute=1   (settle only)
RETIRED  / REVOKED                 : MayBind=0, MayRoute=0   (neither)
```

---

## 4. The drain state machine

### 4.1 States and the one-way transition (FROZEN)

```text
        set DrainCommitment            Phase-5 atomic retirement (merge fold)
NORMAL ───────────────────────▶ DRAINING ─────────────────────────────────▶ RETIRED
(Active,AUTHORIZED)             (Active,AUTHORIZED,               (Retired, REVOKED)
                                DrainCommitment)
```

> **INVARIANT D3 — One-way (LOCKED).** The lifecycle of a drained identity is
> `NORMAL → DRAINING → RETIRED` only. There is **NO** `DRAINING → NORMAL`
> transition (no cancellation) in this increment. A drain commitment, once in
> force for `c`, remains in force until `c` is retired by the frozen Phase-5 merge
> fold, after which it has no operational relevance (D2).

> **INVARIANT D4 — Phase-5 retirement is UNCHANGED (LOCKED).** Phase 7 does NOT
> redefine or delay the merge fold. At the retirement boundary Phase 5 still sees
> exactly `Active → Retired`. The drain window is a committed *precondition/
> overlay that exists BEFORE* the existing atomic retirement; it does not sit
> inside the fold and cannot alter `L_h`, `nextChainId`, or bindings.
>
> ```text
> DrainCommitment  →  DrainWindow  →  [ existing Phase-5 atomic retirement ]
> ```

### 4.2 Who may enter DRAINING (FROZEN preconditions)

A `DrainCommitment` for `c` is *operationally in force* at `h` iff ALL hold:

1. `ClassifyChainId(L_h, c) == Active` (Phase-5 truth), AND
2. the Phase-6 projection of `c` at `h` is `AUTHORIZED`, AND
3. a valid `DrainCommitment` for `c` is in force at `h` per `IsDraining(c, h)`.

If (1) or (2) fail, the commitment is inert (D2). A commitment referencing a
`Retired`/`Nonexistent` `c` MUST be treated as inert, never as an error that
affects block validity beyond the frozen Phase-6 decision. Termination of the
draining mode is governed exclusively by **P7-DRAIN-COMPLETE** (§4.4.2): it ends
iff Phase 5 stops classifying `c` as `Active`.

### 4.3 Determinism & path independence (FROZEN)

> **INVARIANT D5 (LOCKED).** `IsDraining(c, h)` and `DrainCapabilityProjection`
> are PURE, fail-closed functions of committed inputs only (the drain commitment
> set + `L_h` + Phase-6 result). They inherit Phase-5/6 path independence: no
> arrival-order, timing, wall-clock, mempool, or branch dependence. Recomputable
> identically at any height from canonical data alone.

### 4.4 R-REP resolution (v0.2 delta) — representation, completion, entry, emission

This delta RESOLVES the parts of R-REP that the frozen protocol can support, and
records — with evidence — which part must remain OPEN and why. It is grounded in
two findings about the FROZEN engine:

> **FINDING F-A (Phase-5 fold, `LITENYX_chainid_lifecycle.h:260-273`).** Retirement
> is emergent, not schedulable: MERGE retires the identity bound to the HIGHEST
> active lane (`retiredLane = Ncur = Nprev-1`), ONLY when topology decrements `N`,
> ONLY at an `OBS_WINDOW` boundary. `Active → Retired` happens inside ONE fold. A
> committed `RetireHeight` therefore cannot be guaranteed to coincide with the
> actual Phase-5 retirement without modifying the frozen fold (forbidden by D4).

> **FINDING F-B (Phase-4 commitment, `LITENYX_topology_authority.h:198-218`;
> controller `LITENYX_topology.h:102-131`).** The committed `LitenyxTopologyState`
> carries ONLY `{nVersion,nHeight,nN,nLastTransition}` — NO decision enum, NO
> merge target, NO "merge intent" distinct from the `N` decrement. The MERGE
> decision (`A < HYST_LOW && N > MIN_CHAINS`) is reproducible but is computed at
> the SAME boundary the transition is recorded. There is therefore NO pre-existing
> committed "this identity will retire" signal to derive drain entry from, and
> adding one would reopen the frozen Phase-4 commitment.

#### 4.4.1 FROZEN — representation

```text
DrainCommitment = { PersistentChainId id ; uint32 DrainStartHeight }
```

- Keyed on `PersistentChainId` (NEVER on `TopologyLaneId`) — mandatory for ABA
  safety (a reused lane later bound to a different identity MUST NOT inherit a
  historical drain commitment; P5-I1 / A3).
- `DrainStartHeight` MUST be an `OBS_WINDOW` boundary (`% OBS_WINDOW == 0`), so
  entry aligns with the only heights at which topology/lifecycle can change.
- Committed as NEW Phase-7 consensus state. Byte layout / carrier field remains a
  mechanical follow-on (§11) but MUST NOT modify the Phase-5 lifecycle commitment
  bytes or the Phase-4 topology commitment (D4 / F-B).

#### 4.4.2 FROZEN — completion (P7-DRAIN-COMPLETE)

> **P7-DRAIN-COMPLETE (LOCKED).** DRAINING terminates operationally **iff** the
> frozen Phase-5 lifecycle no longer classifies the committed `PersistentChainId`
> as `Active`. Phase 7 SHALL NOT independently schedule, trigger, predict, or
> declare retirement. There is **no committed `RetireHeight`.**
>
> ```text
> IsDraining(id, H) ⇔  ValidDrainCommitment(id) ∧ H ≥ DrainStartHeight
>                                                ∧ ClassifyChainId(L_H, id) == Active
> ```
>
> Consequently, when the emergent Phase-5 merge retires `id`
> (`Active → Retired`), Phase 6 becomes `REVOKED` and `IsDraining` becomes false
> automatically — `DRAINING → REVOKED (0,0)`, with NO Phase-7 completion event and
> NO commitment-deletion mechanism (the commitment may persist in history but
> loses operational meaning).

#### 4.4.3 FROZEN — asymmetry (drain ⇏ merge, merge ⇏ drain)

> **P7-DRAIN-INDEPENDENCE (LOCKED).**
> `DrainCommitment ⇏ MERGE` and `MERGE ⇏ prior DrainCommitment`. Draining MAY
> precede retirement; retirement does NOT depend on draining. Phase 7 SHALL NOT
> make "must have drained first" a merge-validity rule, as that would let Phase 7
> invalidate an otherwise-valid frozen Phase-4/5 transition (reopening the fold).
> Because only the highest lane ever retires, a drained non-edge identity MAY
> remain `DRAINING` for an unbounded, topology-dependent period. This is accepted,
> not a defect (the unavoidable consequence of preserving frozen topology).

#### 4.4.4 FROZEN — eligibility (edge-only, Model B) computed from CURRENT state

> **P7-DRAIN-ELIGIBLE (LOCKED).** A `DrainCommitment` for `id` is *admissible* at
> boundary `H` iff, from CURRENT canonical state alone:
> ```text
> ActivationValid(H) ∧ (H % OBS_WINDOW == 0)
>   ∧ ClassifyChainId(L_H, id) == Active
>   ∧ Phase6Authority(id, H) == AUTHORIZED
>   ∧ AuthoritativeLane(L_H, id) == N_H - 1      # current highest active lane
>   ∧ ¬ExistingDrainCommitment(id)
> ```
> This uses the *current* highest active lane (`N_H − 1`), derived from `L_H`, and
> makes NO prediction that a merge will occur. It only requires that IF an identity
> enters settle-only mode, it is the one occupying the structurally removable edge.
> Eligibility is validated against `L_H`; it needs NO topology "intent" signal.

#### 4.4.5 OPEN — autonomous emission (P7-DRAIN-EMITTER)

Who/what actually EMITS an admissible `DrainCommitment` is **deliberately LEFT
OPEN**, for a decisive evidence-based reason:

> Discretionary emission (miner/pool/operator/RPC/governance) is REJECTED: an
> actor able to commit `DrainCommitment(id)` can force `MayBind: 1 → 0` on an
> otherwise `Active ∧ AUTHORIZED` identity — a consensus-visible capability denial
> too powerful to be discretionary.

> An autonomous, non-discretionary emitter would require a deterministic,
> **consensus-reproducible structural-pressure signal that exists BEFORE the `N`
> transition.** Per F-B, the frozen controller's MERGE pressure (`A < HYST_LOW`)
> is reproducible but is evaluated at the SAME boundary the transition is recorded
> (the only deferral is the cooldown-dated transition height, not a carried-forward
> committed intent). There is thus **no ready-made pre-transition committed
> pressure state** to key emission on WITHOUT introducing new committed topology
> state (reopening Phase 4) or risking validator disagreement.

> **DECISION (FROZEN).** Emission remains OPEN. This increment freezes the
> representation, completion, independence, and eligibility so the emitter can be
> designed later WITHOUT reopening any frozen surface. Until then Phase 7 defines a
> committed `DrainCommitment` and its capability projection but NOT an autonomous
> creation authority.

#### 4.4.6 FROZEN — validation contract (P7-DRAIN-EMITTER / P7-DRAIN-VALIDATE)

Whatever emitter is later chosen MUST satisfy:

> **P7-DRAIN-EMITTER (LOCKED).** A `DrainCommitment` SHALL NOT be valid merely
> because a discretionary actor proposed it. It is valid ONLY if every validator
> can independently reproduce the same `(id, DrainStartHeight)` from canonical
> consensus-visible state.

> **P7-DRAIN-VALIDATE (LOCKED).**
> ```text
> Valid(C, H) ⇔ C == DrainDecisionEngine(CanonicalState_H)
>              ∧ P7-DRAIN-ELIGIBLE(C.id, C.DrainStartHeight)
> ```
> If `RecomputeDrainDecision(CanonicalState_H) ≠ C`, the commitment is invalid
> (fail closed). `DrainDecisionEngine` is the OPEN §4.4.5 component; its output
> domain and admissibility are already frozen here.

> **REQUIREMENT R-REP (FROZEN, retained).** Any concrete representation/emitter
> MUST keep `IsDraining(c, h)` a pure, deterministic, path-independent predicate
> over committed data (D5); MUST NOT alter the Phase-5 commitment domain or merge
> fold (D4/F-A); MUST NOT touch the Phase-4 topology commitment (F-B); MUST NOT add
> a code/field to the frozen Phase-6 result (D0); and MUST ship KATs discharging §9.

---

## 5. Capability semantics (what the flags authorize)

Same routing-validity meaning as Phase 6 §5 (no monetary/attribution semantics):

| Capability | Meaning (this increment) |
|------------|--------------------------|
| `EffectiveMayRoute` | a transaction/block MAY assert this `(lane, PersistentChainId)` as its execution route at `h` |
| `EffectiveMayBind`  | this `PersistentChainId` MAY be treated as accepting NEW binding/origination at `h` |

Boundary-height semantics inherited verbatim from Phase-5 §5.2 / Phase-6 §5: a
block at height `h` is evaluated against `L_h` and the drain set in force at `h`;
no `h` vs `h+1` divergence.

---

## 6. Activation behavior (staged, independent — mirrors Phase 4/5/6)

Phase 7 gets its OWN independent staged activation, layered strictly later than
Phase 6, so a block reaches drain enforcement only after passing the frozen
execution-authority check.

```text
PreDerivation      h < H_drain_derive    : dormant; legacy behavior; no drain effect
SoftAdvisory  H_drain_derive ≤ h < H_drain_enforce : report-but-accept
HardAuthority      h ≥ H_drain_enforce   : fail-closed enforcement of D1/D2/P7-DRAIN-ROUTE
```

- **OPEN (activation heights):** `H_drain_derive`, `H_drain_enforce` per network.
  MUST satisfy `H_drain_derive ≥ H_exec_enforce` on every network (Phase 7 strictly
  after Phase 6), and mainnet MUST be DISABLED in this increment.
- Regtest/test get explicit windows for CI; **mainnet DISABLED** (production-path
  proven without premature mainnet activation, matching Phase 6).

> **INVARIANT D6 — layering (LOCKED).** `H_drain_enforce > H_exec_enforce >
> H_cid_enforce > H_topology_enforce` on any network where they are enabled. A
> block cannot reach drain enforcement without first satisfying topology,
> lifecycle, and execution authority.

---

## 7. Enforcement integration (later-gated; contract frozen now)

When implemented (a SEPARATE gated increment), enforcement MUST:

- run in `ConnectBlock` STRICTLY AFTER `LitenyxCheckExecutionAuthority`, outside
  try/catch, consuming its result;
- CONSUME `DrainCapabilityProjection`; NEVER re-derive Phase-5/6;
- in `HardAuthority`, reject a block that asserts NEW binding/origination for a
  draining identity (`EffectiveMayBind == 0`) with a distinct reason
  (`litenyx-drain-maybind-denied`), while ACCEPTING settle-only routing permitted
  by `EffectiveMayRoute`;
- be exercised on the real compiled path by a regtest-only RPC driving the SAME
  compiled `DrainCapabilityProjection` (compiled-and-exercised, §10.11 discipline).

> **INVARIANT D7 — non-bypassable, non-authoritative (LOCKED).** The drain hook
> MUST NOT become a second authority/lifecycle engine. It ONLY applies the frozen
> monotonic projection (D1) to the frozen Phase-6 result. It reconstructs no
> topology, lifecycle, lane, allocation, or authority.

---

## 8. Reorg semantics

`IsDraining(c, h)` is a pure function of canonical committed data at `h`; a reorg
that changes the canonical chain changes the drain set exactly as it changes
`L_h` — deterministically, with no residual process-local drain state. Any future
implementation MUST hold no drain state outside recomputation from canonical data
(mirrors Phase-3 tracker discipline: observational bookkeeping, if any, MUST NOT
affect validity).

---

## 9. Known-Answer Tests (KATs) — MANDATORY acceptance targets

Representation-independent; each MUST be discharged by the pure engine and
re-exercised on the compiled daemon/RPC path once implemented.

| KAT | Setup | Assert |
|-----|-------|--------|
| D-K1 `NORMAL-passthrough` | `Active`+`AUTHORIZED`, no drain | `mode=NORMAL`, `EffectiveMayBind=1`, `EffectiveMayRoute=1` (== Phase-6) |
| D-K2 `DRAIN-settle-only` | `Active`+`AUTHORIZED`+drain in force, lane agrees | `mode=DRAINING`, `EffectiveMayBind=0`, `EffectiveMayRoute=1` |
| D-K3 `DRAIN-no-manufacture-route` | `AUTHORIZED` wrong-lane (Phase-6 `MayRoute=0`) + drain | `EffectiveMayRoute=0` (drain never grants route) |
| D-K4 `DRAIN-inert-on-REVOKED` | `Retired`/`REVOKED` + drain commitment present | unchanged `REVOKED`, `(0,0)`; mode NORMAL/inert (D2) |
| D-K5 `DRAIN-inert-on-UNKNOWN` | `Nonexistent`/`UNKNOWN` + drain commitment present | unchanged `UNKNOWN`, `(0,0)` (D2) |
| D-K6 `monotonic-subset` | any state, drain on/off | `{Eff} ⊆ {P6}` always; no capability manufactured (D1) |
| D-K7 `one-way` | drain in force then Phase-5 retirement at boundary | `DRAINING → REVOKED`; never `DRAINING → NORMAL` (D3) |
| D-K8 `retirement-unchanged` | merge fold at boundary `h` | Phase-5 still `Active(h-1) → Retired(h)`; `L_h`, `nextChainId`, bindings identical to Phase-5-only (D4) |
| D-K9 `path-independent` | recompute `IsDraining`/projection twice, and across IBD vs sequential | identical results (D5) |
| D-K10 `activation-regime` | boundaries at `H_drain_derive`/`H_drain_enforce`; mainnet | exact regime transitions; mainnet DISABLED at all heights (§6) |
| D-K11 `layering` | evaluate at heights below/above each enforce | drain enforced only after exec authority enforced (D6) |
| D-K12 `complete-on-P5-retire` | drain in force, then MERGE retires `id` | `IsDraining` becomes false the moment `ClassifyChainId(L_H,id)≠Active`; no Phase-7 completion event (P7-DRAIN-COMPLETE) |
| D-K13 `no-RetireHeight` | any drain commitment | commitment carries `{id, DrainStartHeight}` only; no committed retire height (§4.4.1) |
| D-K14 `ABA-key-on-id` | lane `L` drains as ChainId `X`, later retired, lane `L` reused by ChainId `Y` | historical commitment for `X` NEVER applies to `Y` (keyed on `PersistentChainId`) |
| D-K15 `eligible-edge-only` | assert drain for a non-edge Active identity (lane ≠ `N_H−1`) | inadmissible (P7-DRAIN-ELIGIBLE); edge identity admissible |
| D-K16 `drain⇏merge` | drain in force, topology load stays high | `N` never forced down; identity may stay `DRAINING` unbounded (P7-DRAIN-INDEPENDENCE) |
| D-K17 `merge⇏drain` | MERGE retires an identity with no prior drain | valid; retirement does not require a prior drain (P7-DRAIN-INDEPENDENCE) |
| D-K18 `validate-reproduce` | tamper `(id, DrainStartHeight)` vs canonical recompute | `Valid(C,H)` false unless `C == DrainDecisionEngine(CanonicalState_H)` (P7-DRAIN-VALIDATE) |

---

## 10. Frozen invariants (summary, LOCKED at `a95507f`)

| ID | Invariant |
|----|-----------|
| D0 | `DRAINING` is an operational-capability mode, NOT a 4th authority state; `ExecutionAuthorityState` stays `{AUTHORIZED,REVOKED,UNKNOWN}`. |
| D1 | Monotonic restriction: `EffMayBind = MayBind_P6 ∧ ¬IsDraining`; `EffMayRoute = MayRoute_P6`; `{Eff} ⊆ {P6}`. |
| D2 | Phase-6 denial dominates drain; drain is meaningful only for `Active ∧ AUTHORIZED`. |
| D3 | One-way `NORMAL → DRAINING → RETIRED`; no cancellation this increment. |
| D4 | Phase-5 lifecycle domain, commitment bytes, and merge fold UNCHANGED; drain is a committed precondition BEFORE the atomic retirement. |
| D5 | `IsDraining`/projection are pure, fail-closed, path-independent over committed data. |
| D6 | Activation strictly later than Phase 6: `H_drain_enforce > H_exec_enforce`; mainnet DISABLED. |
| D7 | Enforcement is non-bypassable AND non-authoritative: applies the frozen projection only; reconstructs nothing. |
| D8 | `DrainCommitment = {PersistentChainId, DrainStartHeight}`, keyed on identity (ABA-safe), `DrainStartHeight` on an `OBS_WINDOW` boundary; no committed `RetireHeight` (§4.4.1). |
| D9 | P7-DRAIN-COMPLETE: draining ends iff Phase 5 stops classifying `id` as `Active`; Phase 7 never schedules/predicts/declares retirement (§4.4.2). |
| D10 | P7-DRAIN-INDEPENDENCE: `DrainCommitment ⇏ MERGE` and `MERGE ⇏ prior drain`; unbounded drain for non-edge identities is accepted (§4.4.3). |
| D11 | P7-DRAIN-ELIGIBLE (edge-only, Model B): admissible only for the current highest active lane `N_H−1`, computed from `L_H`, no future-merge prediction (§4.4.4). |
| D12 | P7-DRAIN-EMITTER/VALIDATE: no discretionary emission; a commitment is valid only if every validator reproduces it from canonical state; autonomous emitter is OPEN (§4.4.5/§4.4.6). |
| R-REP | Any `DrainCommitment` representation/emitter MUST satisfy D0/D4/D5/D8–D12 and ship KATs §9. |

---

## 11. Scope boundary (what this increment does NOT do)

- FREEZES the `DrainCommitment` logical shape `{PersistentChainId, DrainStartHeight}`,
  completion, independence, and edge-only eligibility (§4.4, v0.2), but does NOT
  fix the concrete byte layout / carrier field (mechanical follow-on).
- Does NOT define the autonomous drain EMITTER / `DrainDecisionEngine` — OPEN
  (§4.4.5), pending a consensus-reproducible pre-transition pressure signal;
  discretionary emission is explicitly REJECTED.
- Does NOT implement any engine, daemon hook, RPC, or patch (spec-only).
- Does NOT add `DRAINING` to `ExecutionAuthorityState` or any field/code to the
  frozen Phase-6 `ExecutionAuthorityResult`.
- Does NOT modify the Phase-5 lifecycle commitment, `LitenyxChainIdStatus`, or the
  merge fold.
- Does NOT define drain CANCELLATION (`DRAINING → NORMAL`).
- Does NOT touch ATMP/mempool policy, routing tables, DB persistence, `SlotId`,
  XCT, monetary/VEA, or broader execution semantics.
- Does NOT enable mainnet.

---

## 12. Milestone boundary

```text
┌─ Phase 4 — Topology authority (FROZEN) ───────────────────────────────────┐
│  Phase 5 — ChainId lifecycle: {Active,Retired,Nonexistent} (FROZEN)        │
│  Phase 6 — Execution authority: {AUTHORIZED,REVOKED,UNKNOWN} + capabilities│
│            (FROZEN, phase6-green @ a95507f)                                 │
│  Phase 7 (THIS SPEC) — DRAINING as separate committed DrainCommitment      │
│            {id, DrainStartHeight}; monotonic capability overlay;           │
│            settle-only; one-way; completion = Phase-5 retirement;          │
│            edge-only eligibility; autonomous EMITTER OPEN; mainnet DISABLED │
└────────────────────────────────────────────────────────────────────────────┘
```

---

## 13. Implementation sequencing (mirrors Phase-4/5/6 discipline)

1. **(this doc)** Freeze the DRAINING state-machine + capability-projection +
   precedence model, and the R-REP v0.2 delta (representation shape, completion,
   independence, eligibility, validation contract) against `a95507f`. No code.
2. Resolve the remaining OPEN item — the autonomous drain EMITTER /
   `DrainDecisionEngine` (§4.4.5) — in a follow-up spec delta, only if/when a
   consensus-reproducible pre-transition pressure signal is established. Fix the
   concrete `DrainCommitment` byte layout there too.
3. Pure engine + KATs (§9, D-K1..D-K18) under C++11/C++20 — proven in isolation.
4. Daemon hook (after Phase-6 check) + regtest-only RPC driving the SAME compiled
   projection — compiled-and-exercised.
5. Green CI → permanent `phase7-green` checkpoint; prior green tags immutable.
