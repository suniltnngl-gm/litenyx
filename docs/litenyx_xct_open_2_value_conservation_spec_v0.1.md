# Litenyx XCT-OPEN-2 — Value-Conservation Accounting — Spec v0.1 (spec-first; no code)

> STATUS: XCT-OPEN-2 (value conservation) was recorded OPEN by the ecosystem
> critique (Component 7) and `litenyx_xct_open_fix_spec_v0.1.md` §3. This document
> follows the mandated path — **monetary-model reconnaissance → accounting ontology
> → conservation invariant → contract** — and does NOT select a bridge mechanism
> before the evidence establishes the intended ontology. No frozen invariant is
> reopened; no source is changed; no mechanism is elevated to a mature principle
> (G-XCT-4). SSC-OPEN-1 is CONSUMED as a dependency (XCT-C4 / XCT-DEP-1), not
> reopened.

---

## 0. The four orthogonal boundaries (kept separate throughout)

```
Authority        : effMayRoute        — may this execution route on this lane?
Identity         : PersistentChainId  — which canonical identity owns the lane?
Spend uniqueness : SharedSpendSet     — is this outpoint globally unspent?
Value conservation: XCT-OPEN-2         — does value movement create/duplicate/lose coin?
```

A valid route permission does NOT prove value ownership. A globally-unique spend
does NOT by itself prove conservation. This spec addresses ONLY the fourth boundary,
consuming the other three as given.

---

## 1. Monetary-model reconnaissance (evidence, verbatim anchors)

| # | Finding | Anchor |
| --- | --- | --- |
| R1 | **ONE UTXO universe, ONE currency, shared across chains.** "two parallel chains sharing ONE UTXO universe and ONE currency." | `LITENYX_types.h:6-8` |
| R2 | Per-chain params "select validation parameters ONLY; they **never bind a transaction to a ledger or change the currency**." | `LITENYX_types.h:20-21` |
| R3 | Central invariant `Spend(U, Chain_A) ⇒ ¬Spend(U, Chain_B)`, enforced by "a SINGLE global shared spent-set, **not per-chain ledgers**." | `LITENYX_types.h:11-13` |
| R4 | "Chain confirmation is a *routing* property; spent-state is *global*." | `consensus_spec_v0.1.md:59` |
| R5 | "1 Blockchain family + 1 Currency + 1 Global Monetary State + N Parallel Chains … never creates a new currency or a second UTXO universe." | `chainid_lifecycle_spec_v0.1.md:122-127` |
| R6 | Topology safety gate clause 1: **`total_supply` before == `total_supply` after; split/merge touches only routing/N, never coin issuance.** | `topology_spec_v0.1.md:202-203` |
| R7 | Safety gate clauses 2–4: shared spent-set byte-identical across a transition; every spent outpoint stays spent; cross-chain exclusion preserved for all `i≠j`. | `topology_spec_v0.1.md:204-211` |
| R8 | Issuance / subsidy / halving: **ABSENT in Litenyx** — inherited unchanged from Dogecoin; explicitly FUTURE. | recon §2 (`consensus_spec:112-119`, `execution_authority_spec:347`) |
| R9 | Burn mechanism: **ABSENT as code**; "burn-not-bridge" exists only as a prohibition. | recon §3 |
| R10 | Value = the vanilla Dogecoin UTXO with `CAmount`; Litenyx adds NO value/amount field. `LitenyxOutPoint{hash,n}` has no amount; spent-set is `outpoint → chainId`. | `LITENYX_sharedstate.h:26-33,43-74` |
| R11 | Spend recording enforces global-unspent-before-mutate; coinbase excluded; reorg reverts. | `LITENYX_validation.cpp:56-88` |

---

## 2. Accounting ontology (derived from evidence, not chosen)

### 2.1 The candidate ontologies and which the evidence admits

The mandated classification was: *movement of an existing canonical UTXO*,
*burn-and-representation*, *lock-and-release*, or *something else*.

```
burn-and-representation   presupposes ≥2 value-domains (burn on A, mint on B)
lock-and-release          presupposes ≥2 value-domains (escrow on A, release on B)
movement between ledgers  presupposes ≥2 ledgers
```

**All three presuppose more than one ledger / value-domain.** R1–R5 establish there
is exactly ONE: one UTXO universe, one currency, one global monetary state; chains
are **routing lanes over a single ledger** (R2, R4). Therefore:

```
┌────────────────────────────────────────────────────────────────────┐
│ ONTOLOGY (forced by R1–R7):                                          │
│                                                                      │
│ An XCT "cross-chain transfer" is NOT a bridge. It is a PLAIN spend   │
│ of an existing canonical UTXO on the single global ledger, whose     │
│ acceptance/routing lane differs from a prior confirmation lane.      │
│                                                                      │
│ Value NEVER crosses a value-domain boundary, because there is only   │
│ one value-domain. "Cross-chain" is a routing adjective, not a value  │
│ adjective.                                                            │
└────────────────────────────────────────────────────────────────────┘
```

This is "something else" in the classification: **re-routed single-ledger spend**,
not any of the three bridge archetypes. No bridge mechanism is selected because the
evidence shows no bridge is needed or admissible (selecting one would fabricate a
second value-domain, violating R1/R5/R6).

### 2.2 Why this is stronger than the topology burn doctrine, as required

The topology doctrine proves TOPOLOGY MUTATION is value-neutral (R6:
`total_supply` invariant across split/merge). The required XCT invariant is stronger
because it must also bind ordinary VALUE MOVEMENT (a transfer), not just structural
N-changes. The ontology delivers that strength by REDUCTION: since XCT value
movement is an ordinary UTXO spend on the one ledger, it is already fully bound by
the existing conservation machinery (Dogecoin `CAmount` input==output+fee, plus the
global spend-set), with NO new value path introduced.

---

## 3. The conservation invariant (XCT-OPEN-2 answer)

### 3.1 Statement

```
┌────────────────────────────────────────────────────────────────────┐
│ XCT-INV-VALUE (v0.1):                                                │
│                                                                      │
│   For any XCT transition T over the single global ledger:           │
│                                                                      │
│   (V1) Σ inputs(T)  ==  Σ outputs(T)  +  fee(T)      -- inherited    │
│        [Dogecoin CAmount conservation; unchanged]                    │
│                                                                      │
│   (V2) total_supply  unchanged by T                 -- R6/R8        │
│        [T mints nothing; issuance path untouched]                    │
│                                                                      │
│   (V3) every input outpoint ∈ SharedSpendSet-unspent BEFORE T,      │
│        and becomes spent EXACTLY ONCE, on EXACTLY ONE lane          │
│        [SS-INV-1..7; R3/R11]                                         │
│                                                                      │
│   (V4) T introduces NO value carrier other than the canonical       │
│        UTXO — no representation token, no wrapped/mirrored coin,     │
│        no second amount field                     -- R10 (DA-NOGO-  │
│                                                       style absence) │
│                                                                      │
│  ⇒  XCT  ⇏  UnauthorizedValueCreation                               │
│  ⇒  XCT  ⇏  Duplication  ⇒  XCT  ⇏  AmbiguousOwnership              │
└────────────────────────────────────────────────────────────────────┘
```

### 3.2 The conservation equation

Because there is one ledger, the equation is the *inherited* single-ledger equation
made explicit across lanes:

```
    total_supply(after T)  =  total_supply(before T)
    Σ inputs(T)            =  Σ outputs(T) + fee(T)
    spent(U, lane_i after T)  ⇒  ¬spendable(U, lane_j)  for all j     (SS-INV via R3/R7)
```

There is deliberately **no** cross-domain term (no `mint_B`, no `escrow_A`) because
no second domain exists. The absence of that term IS the conservation guarantee.

### 3.3 Why each of V1–V4 is discharged by existing evidence, not new code

- **V1** — Dogecoin `CAmount` balance is unchanged and untouched (R8, R10). XCT
  consumes it; it does not re-implement it. (Mirrors XCT-DEP-2: consume, never
  reconstruct.)
- **V2** — total_supply invariance is already a locked safety-gate property (R6) and
  issuance is out of XCT scope (R8).
- **V3** — spend uniqueness is the frozen SharedSpendSet doctrine (SS-INV-1..7);
  XCT-C4 subordinates to it; SSC-OPEN-1 supplies the persistence contract as a
  DEPENDENCY (§5).
- **V4** — the no-second-carrier requirement is satisfied by ABSENCE (R10: no
  Litenyx amount/representation field exists). Introducing one would be a new
  committed-state decision requiring separate doctrine — explicitly NOT taken here,
  and explicitly prohibited by XCT-C5.

---

## 4. Relationship to XCT-OPEN-1 (contract clauses)

XCT-INV-VALUE completes XCT-C5 (issuance-burn conservation), which previously stated
only the prohibition. It also constrains XCT-C4 (spend-invariant subordination):

- XCT-C5 mechanism = **none needed**: conservation is inherited single-ledger
  accounting; the correct "mechanism" is the refusal to add a value carrier (V4).
- XCT-C4 remains a hard dependency on SS-INV-1..7 + SSC-OPEN-1 for spend uniqueness
  (V3).

No change to XCT-C1/C2/C3 (authority/identity/retirement); those are the other three
orthogonal boundaries (§0).

---

## 5. Dependencies (consumed, not reopened)

| Dep | Role in XCT-OPEN-2 | Status |
| --- | --- | --- |
| **SSC-OPEN-1 / XCT-DEP-1** | Supplies deterministic persistence/reconstruction of SharedSpendSet, on which V3 (spend uniqueness) rests | OPEN upstream; CONSUMED here per user directive — addressed spec-first by doctrine v0.1 + INT/RPC/PR specs; NOT redesigned from XCT |
| **XCT-DEP-2** | V1 (CAmount) consumed, never reconstructed | frozen |
| **DA-OPEN-1** | none — value conservation is independent of drain-entry provenance (orthogonal boundary, §0) | OPEN but irrelevant to XCT-OPEN-2 |

---

## 6. Guardrails touched

- **G-XCT-4** — this spec answers XCT-OPEN-2 spec-first; it does NOT elevate a bridge
  mechanism (none exists / none admissible). XCT may now be discussed as a
  single-ledger re-routed spend, but a *transport implementation* remains future.
- **G-XCT-5** — unaffected (DA independence preserved).
- **New: G-XCT-6 (no second value carrier)** — No Litenyx layer may introduce a
  value/amount/representation field parallel to the canonical UTXO. Cross-chain value
  movement is a single-ledger spend re-routed by lane; any wrapped/mirrored coin
  reintroduces duplication and ambiguous ownership (violates V4). This is a permanent
  negative-authority guardrail, the value-domain analogue of DA-NOGO-1.

---

## 7. Disposition

```
XCT-OPEN-2 = ANSWERED spec-first.
  Ontology (forced, not chosen): cross-chain transfer = re-routed spend on the
    SINGLE global ledger; no bridge, no second value-domain.
  Invariant: XCT-INV-VALUE (V1–V4)  ⇒  XCT ⇏ UnauthorizedValueCreation,
    strictly stronger than the topology burn doctrine (binds transfers, not only N).
  Conservation equation carries NO cross-domain term — its absence is the guarantee.
  Mechanism: NONE introduced; conservation is inherited + refusal to add a carrier.
Dependencies: SSC-OPEN-1 CONSUMED (not reopened), XCT-DEP-2 consumed, DA-OPEN-1
  orthogonal. New guardrail G-XCT-6. No frozen invariant reopened; no code.

Live OPEN boundaries carried forward: SSC-OPEN-1 (upstream, consumed), DA-OPEN-1.
Outward next (as directed): ATMP / RPC presentation as CONSUMERS of the now-defined
  single-ledger re-routed-spend semantics.
```
