# Litenyx Phase 6 — Execution Authority Specification v0.1

> **Increment 1 (this document): lane ↔ `PersistentChainId` execution authority.**
> Frozen against `main @ cbb5cca` (the Phase-5 integration baseline; the exact
> tested pre-merge implementation checkpoint is `phase5-green @ 0a2bddb`).
>
> This is a **specification increment only** — no engine, no daemon hook. It
> freezes the consensus contract that Phase-6 implementation must prove, mirroring
> the proven Phase-4/Phase-5 discipline: *specification establishes authority;
> implementation proves it; daemon integration makes it non-bypassable.*

---

## 0. The single question this increment answers

> **Given a canonical tip at height `h` and its already-validated
> `ChainIdLifecycleState L_h` (Phase 5, frozen), which `PersistentChainId` has
> exactly which execution capabilities on which `TopologyLaneId`?**

Phase 5 proves *what identities exist and their lifecycle*. Phase 6 decides *what
execution authority those already-proven identities possess*. Phase 6 MUST NEVER
rediscover topology or lifecycle; it consumes the frozen Phase-5 outputs and
projects an **execution-authority state**, then a **capability matrix**, from them.

```text
Phase-5 Lifecycle State (frozen)  →  Phase-6 Authority Projection  →  Capability Matrix
```

with the permanent separation:

```text
TopologyLaneId  ≠  PersistentChainId (Identity)  ≠  ExecutionAuthorityState
```

---

## 1. Locked principles (carried, not reopened)

These are inherited verbatim and **MUST NOT** be reopened by Phase 6:

- **INVARIANT L0 (Phase 5 §0.1, LOCKED).** Topology lanes are bounded, reusable
  execution *positions*. `PersistentChainId`s are persistent, non-recycled
  execution-domain identities. Reuse of a topology lane after retirement MUST
  allocate a NEW `PersistentChainId`. A retired `PersistentChainId` MUST NEVER
  transition back to active.
- **INVARIANT L1/L2/L3 (Phase 5 §3.2/§3.3/§5.3, LOCKED).** Binding uniqueness,
  dense non-recycling allocation (`nextChainId` is a sufficient retirement
  oracle), and fail-closed exhaustion. Phase 6 does not touch these.
- **Path independence (Phase 5 §0.4, LOCKED).** `L_h` is a pure function of the
  ordered committed block sequence. Any Phase-6 projection inherits this and adds
  no arrival-order, timing, or branch dependence.
- **P5-I1 Identity Isolation (architecture doc).** Persistent consensus state is
  keyed exclusively by `PersistentChainId`, **never solely** by `TopologyLaneId`.
  Phase 6 authority MUST key capability on `PersistentChainId`; a `TopologyLaneId`
  is only the *route*, never the authority subject.

### 1.1 Terminology reconciliation (FROZEN)

> **`PersistentChainId`** is the Phase-6 specification name for the non-recycled
> `ChainId` / `ExecutionChainId` authority object already established by the
> Phase-5 implementation frozen at `cbb5cca`. Phase 6 does **not** alter its
> representation (`uint32`), allocation (dense monotonic `nextChainId`),
> derivation (folding `G` over topology deltas), or persistence semantics
> (never recycled). Any future branch-hash-derived representation is **outside
> this increment**.

| Phase-6 name | Phase-5 object (frozen) | Owner | Recycling |
|--------------|-------------------------|-------|-----------|
| `TopologyLaneId` | Phase-4 `chainId` / lane position | Phase 4 (FROZEN) | reusable |
| `PersistentChainId` | `ChainId` / `ExecutionChainId` (`uint32`) | Phase 5 (FROZEN) | never |
| `LaneBinding` | one entry of `L_h.activeBindings` | Phase 5 (FROZEN) | n/a |
| `ExecutionAuthorityState` | **NEW — Phase-6 projection** | Phase 6 (this doc) | n/a (derived) |

`ExecutionAuthorityState` is a *derived* projection — it is NOT serialized, NOT
committed, and does NOT enter any hash domain. It is recomputable at any height
purely from the frozen `L_h`.

---

## 2. Authority inputs (the ONLY authoritative inputs)

The Phase-6 authority decision at height `h` is a pure function of exactly these
already-consensus-visible inputs. No other input is authoritative; introducing
any is a spec violation.

| Input | Source | Frozen by |
|-------|--------|-----------|
| `L_h` — `ChainIdLifecycleState` at `h` | Phase-5 `LitenyxCalculateExpectedLifecycleFromChain` | Phase 5 `cbb5cca` |
| `h` — the block's own height | canonical chain | Phase 4 |
| asserted `PersistentChainId` | the transaction/block claim under test | this doc |
| asserted `TopologyLaneId` (the route) | the transaction/block claim under test | this doc |
| `consensusVersion` / network activation | staged activation table (§6) | this doc |

> **INVARIANT A0 (LOCKED).** The authority decision reads NOTHING that is not
> derivable from the canonical chain up to and including `h`. No mempool state,
> no tracker/cache, no wall-clock, no peer input, no branch above `h`. This is
> the Phase-4/5 "derivable from canonical chain ALONE" invariant, carried.

Explicitly **NOT** inputs in this increment (deferred, §8): `SlotId`, `W_t`
wallet-count controller, attribution / obligation accounting, cross-chain
transfer (XCT), monetary/VEA quantities, and any state-migration semantics.

---

## 3. The `ExecutionAuthorityState` projection (Phase-6, derived)

Phase 5 projects, for any `PersistentChainId` against `L_h`, exactly three
lifecycle statuses (frozen `LitenyxChainIdStatus`):

```text
Active       — bound to a lane in L_h.activeBindings
Retired      — < L_h.nextChainId but not bound (permanent; L2 oracle)
Nonexistent  — >= L_h.nextChainId (not yet created)
```

Phase 6 derives `ExecutionAuthorityState` **only** from that frozen status. In
this increment the projection is deliberately total and injective:

| Phase-5 lifecycle status | Phase-6 `ExecutionAuthorityState` |
|--------------------------|-----------------------------------|
| `Active`                 | `AUTHORIZED`                      |
| `Retired`                | `REVOKED`                         |
| `Nonexistent`            | `UNKNOWN`                         |

> **INVARIANT A1 (LOCKED).** The projection is a pure total function of the
> frozen Phase-5 status alone. It adds NO new consensus-visible information and
> changes NO Phase-5 commitment. `ExecutionAuthorityState` is never serialized
> or hashed.

### 3.1 `DRAINING` — explicitly DEFERRED (not silently introduced)

A `DRAINING` capability tier would mean "an identity that still exists but is
winding down and may only settle, not originate." Phase 6 does **not** introduce
it in this increment, for a decisive reason grounded in the frozen model:

> In the frozen Phase-5 engine a merge boundary retires a `PersistentChainId`
> **atomically inside a single `G` fold** at the observation boundary. There is
> **no intermediate, consensus-visible window** in which an identity is "bound but
> winding down": at height `h` a ChainId is either `Active` (bound in `L_h`) or,
> from `h` onward, `Retired`. A `DRAINING` state therefore **cannot be defined
> deterministically from already-consensus-visible Phase-5 information** without
> introducing new committed state (a retirement-intent epoch, a drain window,
> etc.), which would expand the Phase-5 lifecycle commitment domain.

> **DECISION (FROZEN).** `DRAINING` is **DEFERRED**. It MUST NOT be added to the
> Phase-5 lifecycle projection to support Phase-6 capabilities. If a future
> increment defines a drain window, it MUST do so as new, separately-committed
> consensus state with its own KATs — never by reinterpreting the frozen
> `{Active, Retired, Nonexistent}` domain.

---

## 4. Deterministic `PersistentChainId → LaneBinding` resolution

Given `L_h` and an asserted `(PersistentChainId c, TopologyLaneId l)` route, the
authority resolution is:

```text
ResolveExecutionAuthority(L_h, c, l, h, consensusVersion) -> ExecutionAuthorityDecision
```

Resolution rules (all pure, fail-closed):

1. Compute the frozen Phase-5 status `s = ClassifyChainId(L_h, c)` and project
   `ExecutionAuthorityState` per §3.
2. If `s != Active` → the decision is `Denied` with the projected state
   (`REVOKED` for retired, `UNKNOWN` for nonexistent). The asserted lane `l` is
   irrelevant; a non-active identity has no lane.
3. If `s == Active`, look up the UNIQUE binding `(l*, c)` in
   `L_h.activeBindings` (uniqueness guaranteed by L1). The **bound** lane `l*` is
   authoritative.
   - If `l == l*` → `Authorized{ chainId=c, laneId=l*, height=h }`.
   - If `l != l*` → `Denied(WrongLane)` — the identity is authorized but the
     claimed route contradicts the authoritative binding.

> **INVARIANT A2 (LOCKED).** The authoritative lane for an `AUTHORIZED`
> `PersistentChainId` is EXCLUSIVELY the lane it is bound to in `L_h` — never the
> lane the transaction claims. A claim is validated *against* the binding; it can
> never *establish* it. This is the operational form of `LaneId ≠ Identity`.

> **INVARIANT A3 (LOCKED).** Because a lane is reusable (L0), a
> `(TopologyLaneId, PersistentChainId)` pair is only meaningful *at a specific
> `h` against `L_h`*. The same lane number bound to a different `PersistentChainId`
> at a different height is a DIFFERENT authority. Resolution therefore always
> takes `L_h` and never memoizes lane→identity across heights.

This resolution is exactly a thin, total wrapper over the frozen Phase-5
`LitenyxValidateExecutionContext` (which already returns
`{valid, chainId, laneId, status}` for the active case). Phase-6 adds the
explicit lane-agreement check and the authority-state projection; it introduces
no new lifecycle logic.

---

## 5. Capability matrix (transaction / block routing validity)

Capabilities are decided **strictly** from `ExecutionAuthorityState` and the
lane-agreement result. In this increment two capabilities are defined; both are
routing-validity predicates (no monetary/attribution semantics, §8).

| Capability | Meaning (this increment) |
|------------|--------------------------|
| `MayRoute` | a transaction/block may assert this `(lane, PersistentChainId)` as its execution route |
| `MayBind`  | this `PersistentChainId` may be treated as an active execution domain at `h` |

| `ExecutionAuthorityState` | lane agreement | `MayRoute` | `MayBind` | Decision |
|---------------------------|----------------|-----------|-----------|----------|
| `AUTHORIZED`              | `l == l*`      | ✅ yes    | ✅ yes    | `Authorized` |
| `AUTHORIZED`              | `l != l*`      | ❌ no     | ✅ yes    | `Denied(WrongLane)` |
| `REVOKED` (retired)       | (n/a)          | ❌ no     | ❌ no     | `Denied(Revoked)` |
| `UNKNOWN` (nonexistent)   | (n/a)          | ❌ no     | ❌ no     | `Denied(Unknown)` |

> **INVARIANT A4 (LOCKED).** Every capability is a pure function of
> `(ExecutionAuthorityState, lane-agreement)`. No capability is ever granted to a
> non-`AUTHORIZED` identity. `MayRoute` additionally requires exact lane
> agreement. Both fail closed.

Boundary-height semantics are inherited verbatim from Phase-5 §5.2: a block at
height `h` is evaluated against `L_h` (no `h` vs `h+1` divergence between
activation and retirement). An identity activated at boundary `h` is `AUTHORIZED`
for use *in block `h`*; one retired at boundary `h` is `REVOKED` *in block `h`*
and forever after.

---

## 6. Activation behavior (staged, independent)

Phase-6 enforcement is staged with its own activation table, INDEPENDENT of
Phase 4 and Phase 5, reusing the frozen three-regime model
(`PreDerivation` / `SoftAdvisory` / `HardAuthority`) exactly as Phase 5 did.

| Regime | Condition | Behavior |
|--------|-----------|----------|
| `PreDerivation` | `h < H_exec_derive` | authority is dormant; a present execution-authority assertion under test is premature → invalid; absence is valid |
| `SoftAdvisory`  | `H_exec_derive <= h < H_exec_enforce` | decisions are computed and surfaced; a `Denied` is advisory (warn, accept) — never fatal |
| `HardAuthority` | `h >= H_exec_enforce` | `Denied` is CONSENSUS-INVALID; fail closed |

Dependency (FROZEN): `H_exec_derive >= Phase-5 H_cid_derive` and
`H_exec_enforce >= Phase-5 H_cid_enforce`. Execution authority can never enforce
before the identities it authorizes are themselves under lifecycle enforcement.

Concrete per-network values are **FROZEN at the engine/hook step** (not here),
following the Phase-5 pattern (regtest crosses both boundaries cheaply in one CI
run; mainnet DISABLED as a deliberate future decision). The `DISABLED` sentinel
is the shared `0xFFFFFFFF` used by Phase 4/5.

---

## 7. Failure taxonomy (FROZEN classifications)

All failures are DISTINGUISHABLE for diagnostics but ALL fail closed in
`HardAuthority`. In `SoftAdvisory` each is emitted as an advisory of the same
name (warn, accept).

| # | Failure | Condition | Regime effect |
|---|---------|-----------|---------------|
| F1 | `Denied(Unknown)` | asserted `PersistentChainId >= L_h.nextChainId` (nonexistent) | premature/unknown identity |
| F2 | `Denied(Revoked)` | asserted `PersistentChainId < nextChainId` but unbound (retired) | permanently revoked identity |
| F3 | `Denied(WrongLane)` | identity `AUTHORIZED` but asserted lane `!= L_h` bound lane | route contradicts authoritative binding |
| F4 | `Denied(Premature)` | authority asserted while regime is `PreDerivation` | asserted before activation |
| F5 | `Denied(Malformed)` | asserted `PersistentChainId`/lane fails structural bounds for its epoch (`lane >= TOPO_MAX_CHAINS`, or `chainId` out of `uint32` framing) | malformed assertion |

> **INVARIANT A5 (LOCKED).** The failure taxonomy is closed: every denied
> decision maps to exactly one of F1–F5. `Revoked` (F2) and `Unknown` (F1) are
> distinguishable via the `nextChainId` oracle (identical to Phase-5 §5.1) but
> both fail closed. No failure silently downgrades to acceptance in
> `HardAuthority`.

---

## 8. Reorg semantics

Phase-6 authority holds NO independent persistent state — `ExecutionAuthorityState`
is a pure projection of `L_h`, and `L_h` is re-derived from the canonical chain by
the frozen Phase-5 engine. Therefore:

> **INVARIANT A6 (LOCKED).** After any reorg or IBD, re-deriving `L_h` for the
> winning chain and re-projecting the authority yields EXACTLY the same
> capability decision for every `(PersistentChainId, lane, h)` as a node that
> connected the blocks sequentially. There is no Phase-6 undo step, no cache to
> roll back, and no "stale generation" window: a decision at height `h` depends
> ONLY on the canonical prefix `[B_0 .. B_h]`.

**Stale-generation / lane-reuse across reorg (the ABA case).** Because a lane is
reusable (L0) but a `PersistentChainId` is never recycled, a reorg that replaces a
merge-then-split history changes *which `PersistentChainId` a lane is bound to* at
a given height. The authority decision correctly follows the winning `L_h`: the
same `(lane, height)` may resolve to a different `PersistentChainId` — and a claim
carrying the losing branch's `PersistentChainId` for that lane resolves to
`Denied(WrongLane)` or `Denied(Revoked)` against the winning `L_h`. This is the
consensus-correct outcome and requires no special handling beyond re-derivation.

---

## 9. Frozen invariants (summary, LOCKED at `cbb5cca`)

| ID | Invariant |
|----|-----------|
| A0 | Authority reads nothing not derivable from the canonical chain up to `h`. |
| A1 | `ExecutionAuthorityState` is a pure total projection of frozen Phase-5 status; never serialized/hashed; changes no Phase-5 commitment. |
| A2 | The authoritative lane for an `AUTHORIZED` identity is exclusively its `L_h` binding; a claim is validated against, never establishes, the binding. |
| A3 | A `(lane, PersistentChainId)` pair is only meaningful at a specific `h` against `L_h`; never memoized across heights. |
| A4 | Every capability is a pure function of `(ExecutionAuthorityState, lane-agreement)`; never granted to a non-`AUTHORIZED` identity; fails closed. |
| A5 | Failure taxonomy is closed (F1–F5); `Revoked`/`Unknown` distinguishable; no silent downgrade in `HardAuthority`. |
| A6 | Reorg/IBD re-derivation reproduces identical decisions; no Phase-6 undo/cache; decision depends only on `[B_0 .. B_h]`. |
| A7 | `DRAINING` is deferred; MUST NOT be added to the Phase-5 projection; any future drain window is separately-committed new state with its own KATs. |
| A8 | `TopologyLaneId ≠ PersistentChainId (Identity) ≠ ExecutionAuthorityState` — three distinct domains, never conflated. |

---

## 10. Known-Answer Tests (KATs) — MANDATORY acceptance targets

These are frozen here and MUST be reproduced byte/decision-for-decision by both
the pure engine and the daemon-exercised regtest path (the "compiled-and-
exercised" gate, inherited from Phase-5 §10.11). Each KAT is a fixed canonical
synthetic chain, a height `h`, an asserted `(PersistentChainId, lane)`, and the
expected `ExecutionAuthorityState` + capability decision, evaluated in
`HardAuthority` unless the row is a regime case.

| KAT | Scenario | Asserted `(chainId, lane)` | Expected state | Expected decision |
|-----|----------|-----------------------------|----------------|-------------------|
| K1 `ACTIVE-bound` | genesis `L_0`: lane 0 → ChainId 0 | `(0, 0)` | `AUTHORIZED` | `Authorized` (`MayRoute`+`MayBind`) |
| K2 `ACTIVE-bound (2nd)` | genesis `L_0`: lane 1 → ChainId 1 | `(1, 1)` | `AUTHORIZED` | `Authorized` |
| K3 `WRONG-LANE` | after split (lane 2 → ChainId 2 active) assert ChainId 2 on lane 1 | `(2, 1)` | `AUTHORIZED` | `Denied(WrongLane)` (`MayBind` yes, `MayRoute` no) |
| K4 `RETIRED` | split then merge retires ChainId 2 permanently | `(2, <any>)` | `REVOKED` | `Denied(Revoked)` |
| K5 `NONEXISTENT` | at `L_0`, assert a ChainId `>= nextChainId` | `(9, 0)` | `UNKNOWN` | `Denied(Unknown)` |
| K6 `ABA / stale-generation` | merge-then-split so lane 2 → ChainId 3; assert the OLD ChainId 2 on lane 2 | `(2, 2)` | `REVOKED` | `Denied(Revoked)` |
| K7 `ABA new identity valid` | same chain as K6; assert the NEW ChainId 3 on lane 2 | `(3, 2)` | `AUTHORIZED` | `Authorized` |
| K8 `MALFORMED lane` | assert lane `>= TOPO_MAX_CHAINS` (e.g. 8) for an active ChainId | `(0, 8)` | (n/a) | `Denied(Malformed)` |
| K9 `PRE-ACTIVATION` | `h < H_exec_derive`, assert any authority | `(0, 0)` present | (dormant) | `Denied(Premature)` |
| K10 `SOFT advisory` | `H_exec_derive <= h < H_exec_enforce`, a wrong-lane claim | `(2, 1)` | `AUTHORIZED` | advisory `WrongLane` → **accept** |
| K11 `POST-activation hard` | `h >= H_exec_enforce`, same wrong-lane claim as K10 | `(2, 1)` | `AUTHORIZED` | `Denied(WrongLane)` → **reject** |
| K12 `path-independence` | derive decision for K3/K4 via sequential vs IBD replay | — | identical | identical decision (A6) |

Exact numeric commitment/state vectors for the synthetic chains are pinned at the
**engine step** (as Phase 5 pinned its genesis/derivation KATs after the header
existed); this document freezes the *scenarios and expected decisions* so the
engine cannot silently change them.

---

## 11. Scope boundary (what this increment does NOT do)

- Does NOT introduce `SlotId`, `W_t` wallet-count controller, attribution, or
  obligation accounting.
- Does NOT introduce cross-chain transfer (XCT), monetary quantities, VEA, reward,
  supply, or any value-flow semantics.
- Does NOT introduce state-migration semantics, drain windows, or any new
  committed/hashed consensus state. `ExecutionAuthorityState` is a pure
  projection, never serialized.
- Does NOT introduce a new `PersistentChainId` derivation (e.g. branch-hash
  identity); it consumes the frozen Phase-5 representation unchanged.
- Does NOT add `DRAINING` (deferred, §3.1/A7).
- Does NOT modify Phase-4 or Phase-5 code, serialization, hashing, KATs, or
  activation constants. `phase4-green`, `phase5-green` remain byte-identical.

---

## 12. Milestone boundary

```text
phase5-green @ 0a2bddb        (read-only anchor: tested pre-merge checkpoint)
main @ cbb5cca                (Phase-5 integration baseline; THIS spec frozen here)
    │  Persistent, non-recycled PersistentChainId lifecycle (frozen)
    │  Pure ValidateExecutionContext → Phase 5→6 authority boundary (frozen)
    ▼
Phase 6 — Increment 1 (this spec)
    │  ExecutionAuthorityState projection (AUTHORIZED/REVOKED/UNKNOWN)
    │  Deterministic PersistentChainId → LaneBinding resolution
    │  Capability matrix (MayRoute/MayBind); closed failure taxonomy F1–F5
    │  Staged independent activation; pure reorg re-derivation
    │  DRAINING deferred (A7)
    ▼
Phase 6+ (FUTURE increments)
    SlotId · W_t wallet-count controller · attribution · XCT · monetary/VEA
    · (only if defined) separately-committed drain window
```

---

## 13. Implementation sequencing (mirrors Phase-4/5 discipline)

1. **This commit — documentation only.** No code; `ConnectBlock` untouched.
   Freezes inputs (§2), the projection (§3), resolution (§4), capability matrix
   (§5), activation (§6), failure taxonomy (§7), reorg semantics (§8), invariants
   A0–A8 (§9), and the KAT scenarios (§10).
2. **Pure enforcement engine + KATs — no hook.** Implement, as
   standalone-testable pure code, a thin projection over the frozen Phase-5
   `LitenyxValidateExecutionContext`: `ExecutionAuthorityState` projection →
   `ResolveExecutionAuthority` (lane-agreement) → capability matrix → failure
   taxonomy. Pin the numeric KAT vectors for K1–K12.
3. **Daemon hook.** Only after (2) is green, wire the authority resolution into
   `ConnectBlock` behind staged independent activation (§6), ordered strictly
   AFTER the Phase-5 lifecycle-commitment check. Pure re-derivation makes any
   `DisconnectBlock` undo unnecessary (§8).
4. **Compiled-and-exercised gate.** Prove the hook compiles into `dogecoind` and
   is exercised on the real consensus path in CI (regtest crossing
   `H_exec_derive` and `H_exec_enforce`) via a regtest-only RPC driving the SAME
   compiled engine — the Phase-5 §10.11 pattern.
5. **Tag `phase6-green`** ONLY when the full gate is GREEN, branching from and
   merging back into `main` while leaving all prior green tags untouched.
