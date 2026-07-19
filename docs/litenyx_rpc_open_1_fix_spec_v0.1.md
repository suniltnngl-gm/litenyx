# Litenyx RPC-OPEN-1 — Ungated SharedSpendSet Mutation Fix — Spec v0.1

> **Status: SPEC-FIRST ONLY.** No Phase-2 code changes. This spec designs the fix
> for RPC-OPEN-1 AGAINST the frozen doctrine
> (`litenyx_sharedspendset_doctrine_v0.1.md`, esp. SS-INV-3) and the Component-10
> finding (`litenyx_ecosystem_critique_v0.1.md`, F-RPC-1). It is narrow by design:
> make PRODUCTION SharedSpendSet mutation via RPC impossible, and decide the fate
> of the regtest test affordance.

## 0. The frozen acceptance condition

```
Δ SSS (production)  <=>  successful canonical connect/disconnect        [SS-INV-3]
```

Corollary (doctrine §9, SS-INV-3): RPC mutation of the logical set is non-canonical
and impermissible on ANY production network; any debug affordance is regtest-gated
and cannot exist on main/test. This spec makes that corollary enforceable in code
terms (spec-level).

## 1. The defect restated (from Component 10, F-RPC-1)

`litenyx-rpc.patch` registers five `litenyx` RPCs. Exactly ONE mutates the canonical
SharedSpendSet:

| RPC | Modes | Touches SharedSpendSet? | Mutating? |
| --- | --- | --- | --- |
| `testlitenyxsharedstate` | record / revert / query | **YES — the real global singleton** | **record, revert = MUTATE** |
| `testlitenyxtopology` | status/observe/tick/reset | advisory tracker only (not SSS) | mutates tracker, not SSS |
| `testlitenyxtopoauthority` | regime/expected/decide | no (pure derivation) | no |
| `testlitenyxlifecycle` | regime/expected/decide | no (pure derivation) | no |
| `testlitenyxexecauthority` | regime/resolve/resolveid | no (pure derivation) | no |

The load-bearing defect:

- `testlitenyxsharedstate` mode `record` -> `LitenyxRecordSharedSpend(txid,n,chainId)`
  (`litenyx-rpc.patch:96`) and mode `revert` -> `LitenyxRevertSharedSpend(txid,n)`
  (`litenyx-rpc.patch:111`). These are the **same functions `ConnectBlock` /
  `DisconnectBlock` use on the same process-global singleton** — an exogenous write
  to the canonical logical set with NO canonical transition. Direct SS-INV-3
  violation.
- **Gating is by COMMENT ONLY.** The doc-comment says "regtest-only"
  (`litenyx-rpc.patch:45`), but `RegisterMiningRPCCommands`
  (`litenyx-rpc.patch:527-529`) registers `commands[]` UNCONDITIONALLY on every
  network. Nothing in code restricts `record`/`revert` to regtest. On main/test a
  client with RPC access can inject/erase spends in the canonical set.

Scope note (from Component 10): the risk is LOCAL consensus-state integrity (this
node's SharedSpendSet), not a network consensus break — but SS-INV-1/3 make even a
local exogenous mutation impermissible, because the local set must equal
`Fold(canonical history)` at all times.

## 2. What SS-INV-3 requires (the target contract)

```
(P1) On main and test, NO RPC may cause Δ SSS. (record/revert must be unreachable.)
(P2) The query path (read-only observation) is permissible on any network — it is
     an RPCObservation, never a mutation, and RPCObservation != ConsensusAuthority.
(P3) Any surviving mutation affordance exists ONLY under regtest, and its existence
     must be ENFORCED in code (not by comment), fail-closed if the network cannot be
     confirmed as regtest.
```

## 3. The design question

```
Should regtest SharedSpendSet mutation remain an explicitly isolated live-singleton
test affordance, or be replaced by a synthetic/non-live test surface that never
touches the production singleton on any network?
```

This is the one real decision. Everything else (gating query vs mutate, fail-closed)
follows from SS-INV-3 directly.

## 4. Option comparison

Criteria: SS-INV-3 guarantee strength, blast radius if misconfigured, test-coverage
retained, complexity, consistency with the "single source of truth / no second
writer path" guardrail (G-INT-3 / RPC-NOGO).

### O1 — Keep record/revert, add runtime regtest gate (Params().NetworkIDString()=="regtest")
- P1: satisfied IF the gate is correct and fail-closed.
- Blast radius: a future refactor or misread of the chain params re-exposes a LIVE
  writer to the canonical singleton. The dangerous capability still EXISTS in the
  binary on main/test; only a runtime branch stands between it and the singleton.
- Test coverage: unchanged (still drives the real singleton on regtest).
- Complexity: LOW.
- Verdict: acceptable minimum, but retains a live production-singleton writer in the
  shipped binary — weakest defense-in-depth.

### O2 — Compile-time exclude the mutating modes outside regtest builds
- P1: satisfied strongly — record/revert not present in main/test binaries.
- Blast radius: near-zero on release builds; but regtest and release often share one
  binary in this lineage (network chosen at runtime), so a pure compile-time switch
  may not map cleanly to "regtest run". Risk of either losing regtest coverage or
  needing a separate build.
- Verdict: strong isolation but awkward against runtime-selected networks.

### O3 — Replace the live-singleton mutation with a SYNTHETIC test surface
- record/revert no longer touch the production singleton at all. The Phase-2
  invariant (`Spend(U,A) => !Spend(U,B)`, reorg rollback) is exercised against a
  LOCAL throwaway `SharedSpendSet` instance constructed inside the RPC, mirroring
  the KAT/pure-engine testing style used for P4/P5/P6 RPCs (which already create NO
  process-local state).
- P1: satisfied by CONSTRUCTION on every network — there is no code path from any
  RPC to `Δ (production SSS)`. Even if the RPC is reachable on main, it cannot
  mutate canonical state.
- Blast radius: zero (no live writer exists).
- Test coverage: retains the invariant proof; loses only "exercise the exact global
  singleton instance" — but that instance is the same class, and its integration
  with ConnectBlock is now covered by the INT-OPEN-1 stage/publish path + daemon
  harness, not by an RPC poking the singleton.
- Consistency: matches the other four litenyx RPCs, which are already
  synthetic/derivation-only and "NOT a consensus dependency."
- Complexity: MEDIUM (construct a local instance; keep query semantics).
- Verdict: **recommended** — strongest SS-INV-3 guarantee (no production writer path
  exists at all), consistent with the established RPC pattern.

### O4 — Remove testlitenyxsharedstate record/revert entirely
- P1: trivially satisfied.
- Test coverage: LOSES the Phase-2 RPC-level invariant exercise unless replaced.
- Verdict: viable only if the invariant is covered elsewhere; O3 keeps coverage at
  similar cost, so O3 dominates O4.

## 5. Recommended design (spec-level, no code)

**Adopt O3 (synthetic test surface) as the primary fix, with O1's runtime
regtest-gate as a belt-and-suspenders guard on any residual affordance.**

### 5.1 Mutation elimination (P1)
- `testlitenyxsharedstate` `record`/`revert` MUST NOT call
  `LitenyxRecordSharedSpend` / `LitenyxRevertSharedSpend` on the production global
  singleton. They operate on a LOCAL `SharedSpendSet` instance created per request
  (or per session), proving the invariant without exogenous canonical mutation.
- No RPC anywhere retains a path to `Δ (production SSS)`. This is the SS-INV-3
  guarantee stated as an absence-of-path property, verifiable by inspection.

### 5.2 Query path (P2)
- `query` mode remains, and MAY read the production singleton (read-only
  observation). It is an RPCObservation and never mutates. This preserves the
  ability to inspect live canonical state for diagnostics.
- (Open: whether `query` should read the production singleton or the same local
  instance — see RPC-Q2.)

### 5.3 Enforced gating + fail-closed (P3)
- Any affordance that could still reach ANY mutation (should O1 be retained for the
  local instance, or for symmetry) MUST be gated by an ENFORCED runtime check
  `Params().NetworkIDString() == "regtest"`, throwing `RPC_MISC_ERROR` otherwise —
  fail-closed. Comment-only gating (the current defect) is prohibited.
- The gate must fail closed if the network cannot be positively confirmed as
  regtest.

### 5.4 The other four RPCs
- `testlitenyxtopoauthority`, `testlitenyxlifecycle`, `testlitenyxexecauthority`:
  UNCHANGED — pure derivation, no SSS, "NOT a consensus dependency" already true.
- `testlitenyxtopology` observe/tick/reset mutate the ADVISORY tracker, not the
  SharedSpendSet, and the tracker is non-authoritative (Component 2). Out of scope
  for RPC-OPEN-1 (SharedSpendSet-specific); noted for a separate hardening pass if
  desired, but it cannot violate SS-INV-3 (does not touch SSS).

## 6. Interaction with frozen invariants (cross-check)

| Invariant | Interaction | Result |
| --- | --- | --- |
| SS-INV-1 (truth) | production singleton keeps == Fold(history); no RPC writes it | preserved |
| SS-INV-2 (ordering) | unaffected (no canonical mutation via RPC) | preserved |
| SS-INV-3 (history-only) | no RPC path to Δ production SSS; the target | SATISFIED (by absence-of-path) |
| SS-INV-4 (atomicity) | unaffected; INT-OPEN-1 owns commit boundary | orthogonal |
| SS-INV-5 (reorg) | unaffected | orthogonal |
| SS-INV-6/7 (recovery/ckpt) | unaffected | orthogonal |

## 7. Guardrail compliance

- **RPC-NOGO / G-INT-3 (single writer path)** — O3 removes the SECOND writer path to
  the canonical singleton entirely; only ConnectBlock/DisconnectBlock (via the
  INT-OPEN-1 stage/publish) mutate it. Satisfied.
- **Preserve diagnostics** — read-only `query` retained (P2). No loss of observation.
- **Consistency** — mutating RPC now matches the derivation-only pattern of the
  Phase 4/5/6 RPCs.

## 8. Open sub-questions deferred to implementation

- **RPC-Q1** — O3 local instance lifetime: per-request (stateless, simplest, but
  multi-call record->query sequences won't persist) vs per-session/static-local
  (persists across calls for a test sequence). The Phase-2 harness's usage pattern
  decides this; must NOT be the production singleton either way.
- **RPC-Q2** — whether `query` reads the production singleton (live diagnostics) or
  the O3 local instance (self-contained test). If both are wanted, `query` may take
  a target selector; production target stays read-only.
- **RPC-Q3** — confirm `RegisterMiningRPCCommands` registration cannot be made
  conditional cleanly (runtime network not known at registration in this lineage);
  if so, the runtime gate (5.3) is the correct enforcement layer, not registration.
- **RPC-Q4** — decide whether to ALSO apply the enforced regtest gate to
  `testlitenyxtopology` observe/tick/reset for symmetry (does not affect SS-INV-3;
  hygiene only).

## 9. Disposition

RPC-OPEN-1's fix is specified as **O3 — replace the live-singleton mutation in
`testlitenyxsharedstate` record/revert with a synthetic LOCAL `SharedSpendSet`
surface, retain read-only `query`, and enforce a fail-closed regtest gate on any
residual affordance.** This satisfies SS-INV-3 as an ABSENCE-OF-PATH property: after
the fix no RPC on any network can produce `Δ (production SharedSpendSet)`, so the
canonical set changes IFF a canonical transition occurs. The current defect —
comment-only "regtest-only" gating over an unconditionally registered live writer —
is eliminated at the strongest level (the writer path ceases to exist), consistent
with the derivation-only pattern already used by the Phase 4/5/6 RPCs. No Phase-2
code written; no invariant reopened. Next in sequence: PR-OPEN-1 design against
SS-INV-6/7 (recovery convergence + fail-closed + checkpoint binding).
