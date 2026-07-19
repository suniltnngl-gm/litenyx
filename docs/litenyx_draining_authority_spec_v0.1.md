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
> **OPEN (deliberately NOT frozen here):** the exact byte representation, fields,
> and *duration* semantics of the `DrainCommitment` (see §4.4 / §11). This
> increment freezes the **state-machine, capability-projection, and precedence
> model** so that the representation can be chosen next without reopening any of
> the frozen Phase-5/6 surfaces.

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
affects block validity beyond the frozen Phase-6 decision.

### 4.3 Determinism & path independence (FROZEN)

> **INVARIANT D5 (LOCKED).** `IsDraining(c, h)` and `DrainCapabilityProjection`
> are PURE, fail-closed functions of committed inputs only (the drain commitment
> set + `L_h` + Phase-6 result). They inherit Phase-5/6 path independence: no
> arrival-order, timing, wall-clock, mempool, or branch dependence. Recomputable
> identically at any height from canonical data alone.

### 4.4 `DrainCommitment` representation — **OPEN (NOT frozen here)**

The following are DELIBERATELY LEFT OPEN for the next increment; only the
*requirements* on whatever representation is chosen are frozen:

- **OPEN:** exact fields (e.g. `drainStartHeight`, `drainUntilHeight`,
  retirement-intent marker, per-binding flag, external commitment structure).
- **OPEN:** where it is committed (block aux field vs a new committed structure
  resolved alongside `L_h`) — but it MUST be committed consensus state, and it
  MUST NOT modify the frozen Phase-5 lifecycle commitment bytes.
- **OPEN:** duration/window semantics and what consensus event, if any, other than
  Phase-5 retirement bounds the window.

> **REQUIREMENT R-REP (FROZEN).** Whatever representation is chosen MUST make
> `IsDraining(c, h)` a pure, deterministic, path-independent predicate over
> committed data (D5); MUST NOT alter the Phase-5 commitment domain or the merge
> fold (D4); MUST NOT add a code/field to the frozen Phase-6 result (D0/A-frozen);
> and MUST come with KATs discharging §9.

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
| R-REP | Any `DrainCommitment` representation MUST satisfy D0/D4/D5 and ship KATs §9. |

---

## 11. Scope boundary (what this increment does NOT do)

- Does NOT choose the `DrainCommitment` byte representation, fields, or duration
  (§4.4 OPEN).
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
│  Phase 7 (THIS SPEC) — DRAINING as separate committed DrainCommitment +    │
│            monotonic capability overlay; settle-only; one-way;             │
│            representation OPEN; mainnet DISABLED                            │
└────────────────────────────────────────────────────────────────────────────┘
```

---

## 13. Implementation sequencing (mirrors Phase-4/5/6 discipline)

1. **(this doc)** Freeze the DRAINING state-machine + capability-projection +
   precedence model against `a95507f`. No code.
2. Resolve §4.4 OPEN items (representation/duration) in a follow-up spec delta.
3. Pure engine + KATs (§9) under C++11/C++20 — proven in isolation.
4. Daemon hook (after Phase-6 check) + regtest-only RPC driving the SAME compiled
   projection — compiled-and-exercised.
5. Green CI → permanent `phase7-green` checkpoint; prior green tags immutable.
