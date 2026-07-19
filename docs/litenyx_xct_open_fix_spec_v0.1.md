# Litenyx XCT-OPEN-1 / XCT-OPEN-2 Fix Spec — v0.1 (spec-first; no code)

> STATUS: OPEN design boundaries carried forward from the ecosystem critique
> (Component 7). This document SPECIFIES the future Routing/XCT transition contract
> (XCT-OPEN-1) and records the value-conservation problem (XCT-OPEN-2). It elevates
> NO mechanism to a mature Litenyx principle (G-XCT-4). No frozen invariant is
> reopened; no source is changed.

---

## 0. Frozen premises consumed (not re-derived)

| Premise | Source (verified) |
| --- | --- |
| `mayRoute = AUTHORIZED ∧ lane == L_h binding` (pure fold, exact-lane) | `LITENYX_execution_authority.h:186-196` |
| Non-active identity fails closed: `Unknown` (nonexistent) / `Revoked` (retired) | `LITENYX_execution_authority.h:175-182` |
| Block carries only a lane position; PersistentChainId is looked up on `L_h` alone; no new carrier state | `LITENYX_execution_authority.h:200-214` |
| `effMayRoute = p6.mayRoute` — drain NEVER grants nor revokes route | `LITENYX_draining_authority.h:239` |
| `effMayBind = p6.mayBind ∧ ¬draining`; `draining = AUTHORIZED ∧ isDraining` | `LITENYX_draining_authority.h:231-238` |
| Route capability is consumed ONLY as a block accept/reject gate; no code effects a route | `LITENYX_validation.cpp:300-334` (critique Deliverable 1) |
| Routing/XCT TRANSITION mechanism is ABSENT (explicit out-of-scope) | `execution_authority_spec §100-101`, `draining_authority_spec:126`, `chainid_lifecycle_spec:700` |

**Consequence:** the mature stack proves what a consumer MAY do; it proves NO
consumer exists. XCT is that (absent) consumer. This spec constrains it; it does not
build it.

---

## 1. The frozen independence rule (load-bearing)

Per the DA-OPEN-1 disposition (`docs/litenyx_da_open_1_fix_spec_v0.1.md`), the
following is FROZEN for all XCT work:

```
XCT correctness MUST NOT depend on DA-OPEN-1 being resolved.
```

Formally, with `DrainCommitment` denoting a canonically-valid drain fact for the
identity at the execution point:

```
DrainCommitment present  ⇒  XCT consumes P7 effective authority (effMayRoute)
DrainCommitment absent    ⇒  XCT uses P6-derived effective authority (mayRoute)
                             ≠  XCT blocked
```

### 1.1 Why this is already SOUND against the frozen engine

This is not a new rule imposed on Phase 7 — it is exactly how the frozen overlay
already degrades:

```
isDraining = false
  ⇒ draining      = false                 (D2, :232)
  ⇒ effMayRoute   = p6.mayRoute           (:239, unconditional)
  ⇒ effMayBind    = p6.mayBind            (:238, since ¬draining)
  ⇒ effective authority  ≡  P6 authority
```

So "no drain commitment" is not a missing input that stalls XCT; it is the engine's
identity element. XCT that consumes `eff*` automatically sees P6 authority when no
drain exists. **XCT-DEP-2 (consume, never reconstruct) and the independence rule are
therefore satisfied by the SAME action:** consume `eff*`.

### 1.2 What XCT MUST NOT infer from `DrainCommitment` absence

Absence of a drain commitment means "no drain overlay applies," NOT "draining is
proven impossible / forbidden here." DA-OPEN-1 remains OPEN: the trigger provenance
is unresolved. XCT MUST treat absence as the NORMAL-mode default, never as a positive
assertion about future drain semantics (mirrors DA C-B non-closure).

---

## 2. XCT-OPEN-1 — the transition contract

> What canonical transition contract must a future Routing/XCT consumer satisfy so
> that an execution permitted by `effMayRoute = true` respects boundaries (a)–(e)?

The five boundaries are RESTATED here as the contract's obligations. They are the
outward projection of the negative-authority discipline proven inward for Phase 7;
they are REQUIREMENTS on a not-yet-designed layer, not proven properties.

### 2.1 The five non-crossable boundaries (contract obligations)

```
XCT  ⇏  BindingMutation           (a)
XCT  ⇏  LifecycleMutation         (b) / (c-retirement)
XCT  ⇏  TopologyMutation          (implicit in a/b; N and lane map are P4/P5 only)
XCT  ⇏  IdentityReuse / ABA       (c-lane-reuse)
XCT  ⇏  UnauthorizedValueCreation (d) spend invariant / (e) issuance-burn
```

### 2.2 Contract clauses (spec-first; each maps to a boundary)

- **XCT-C1 (consume-only authority; boundary a,b).** An XCT transition takes
  `effMayRoute` as a *given input* and performs a transfer ONLY when it is `true`.
  It MUST NOT compute, weaken, or re-derive any routing/binding permission
  (XCT-DEP-2, G-XCT-2). Re-derivation reopens every escalation surface Components 4–6
  closed.

- **XCT-C2 (PersistentChainId end-to-end; boundary c-ABA).** The transition MUST be
  keyed on `PersistentChainId` from source to destination, never on `laneId`. Lanes
  are reused across lifecycle (`L_h` lane→id lookup, exec-auth `:200-214`); keying on
  a lane admits the ABA hazard (G-XCT-3, inherits G-CL-3/G-DA-3).

- **XCT-C3 (retirement invalidation; boundary c-retirement).** A transition that was
  capability-valid at height `h₀` MUST NOT remain valid past the retirement of its
  `PersistentChainId`. Because retirement is COINCIDENT with the merge decision
  (DA-OPEN-1 §1.3: `MERGE_Decision_h ≡ N_Decrement_h ≡ Edge_Retirement_h`), a
  retired identity resolves to `Revoked` (`exec-auth :178-181`) and `effMayRoute`
  becomes `false` at the SAME boundary. XCT MUST re-evaluate `effMayRoute` at the
  execution height, not cache a stale `h₀` verdict.

- **XCT-C4 (spend-invariant subordination; boundary d).** Any XCT settlement that
  moves value MUST pass through the canonical cross-chain shared-spend invariant
  (SS-INV-1..7, doctrine v0.1) and MUST NOT introduce a private spend path.
  DEPENDENCY: XCT-DEP-1 / SSC-OPEN-1 (persistence/reconstruction of the shared spend
  set is OPEN). See §4.

- **XCT-C5 (issuance-burn conservation; boundary e).** The transition MUST NOT
  create, duplicate, or implicitly bridge value. Topology mutation is proven
  value-neutral (`topology_spec:203-205`), but the settlement accounting rule itself
  is UNSPECIFIED — this is XCT-OPEN-2 (§3). XCT-C5 states the PROHIBITION; the
  mechanism is deferred.

### 2.3 Contract shape (informal type; no code)

```
XCT_Transition(
    src:  PersistentChainId,          -- C2: never a lane
    dst:  PersistentChainId,          -- C2
    eff:  EffectiveAuthority @ h_exec -- C1/C3: consumed, re-evaluated at h_exec
    val:  ValueMovement               -- C4/C5: spend-invariant + conservation bound
) -> Settled | Rejected(fail-closed)
```

Fail-closed default: any clause unsatisfied ⇒ `Rejected`. This inherits the whole
stack's fail-closed discipline (A4/A5, D-* fail-closed, SS-INV-6).

---

## 3. XCT-OPEN-2 — value conservation (recorded OPEN, not derived)

> What is the exact cross-chain settlement accounting rule under which an XCT
> transition conserves value consistent with the issuance/burn model
> (burn-not-bridge)?

- **What is PROVEN:** topology mutation (split/merge, `N`) does not itself mint or
  burn (`topology_spec:203-205`, `consensus_spec:59`). Confirmation is a routing
  property; spent-state is global.
- **What is NOT proven / deliberately NOT derived here:** the concrete accounting
  rule (debit/credit, burn semantics, receipt structure) for a cross-chain settle.
  Deriving more than the prohibition (boundary e / XCT-C5) from frozen evidence would
  overreach (G-XCT-4).
- **Disposition:** XCT-OPEN-2 stays OPEN. This spec records it and binds it to
  XCT-C5; it does NOT propose an accounting mechanism.

---

## 4. Dependencies (frozen)

| Dep | Statement | Blocks XCT? |
| --- | --- | --- |
| **XCT-DEP-1 / SSC-OPEN-1** | Shared-spend invariant proven behaviourally (`test_litenyx.cpp:74`), persistence/reconstruction OPEN | XCT-C4 (value-moving settle) MUST resolve or explicitly inherit it |
| **XCT-DEP-2** | Consume `P5→P6→P7→effMayRoute`; never reconstruct a weaker permission | Hard: violation reopens Components 4–6 |
| **DA-OPEN-1** | Autonomous drain-entry trigger OPEN | **CONDITIONAL, NOT a blocker** — see §1; XCT correct with drain absent |

---

## 5. Guardrails (doctrine-level; forward-looking; no code)

- **G-XCT-1** — `mayRoute`/`effMayRoute` are capability PREDICATES; truth is not a
  routing action having occurred (reinforces G-EA-1/G-DA-1).
- **G-XCT-2** — Any XCT consumer MUST consume frozen effective authority (XCT-DEP-2);
  MUST NOT reconstruct a private/weaker routing permission.
- **G-XCT-3** — XCT transitions MUST be keyed on `PersistentChainId` end-to-end;
  lane-keying reopens ABA (inherits G-CL-3/G-DA-3).
- **G-XCT-4** — No XCT mechanism proposal is a mature Litenyx principle until
  XCT-OPEN-1 (contract) AND XCT-OPEN-2 (value conservation) are answered spec-first.
- **G-XCT-5 (new; independence)** — XCT correctness MUST NOT be coupled to DA-OPEN-1.
  `DrainCommitment` absent ⇒ P6-derived effective authority, never a block (§1).

---

## 6. Disposition

XCT-OPEN-1 is ADVANCED to a spec-first CONTRACT (clauses XCT-C1..C5 over the five
frozen boundaries), sound against the frozen overlay's identity behaviour and the
DA-OPEN-1 independence rule. XCT-OPEN-2 remains OPEN (accounting mechanism not
derived; prohibition-only via XCT-C5). Dependencies XCT-DEP-1/2 recorded; DA-OPEN-1
carried as CONDITIONAL, not a blocker. No frozen invariant reopened; no mechanism
elevated to principle (G-XCT-4). No code.

```
XCT-OPEN-1 = SPEC-FIRST CONTRACT DRAFTED (C1..C5)
XCT-OPEN-2 = OPEN (value-conservation accounting deliberately not derived)
DA-OPEN-1  = OPEN, but XCT-independent (G-XCT-5 proven sound §1.1)
Live OPEN boundaries carried forward: SSC-OPEN-1, DA-OPEN-1, XCT-OPEN-2
```
