# Litenyx ATMP-OPEN-1 / ATMP-OPEN-2 — Advisory Admission Policy — Spec v0.1 (spec-first; no code)

> STATUS: ATMP-OPEN-1/2 were recorded OPEN by the ecosystem critique (Component 8).
> This document performs **ATMP source reconciliation first** — mapping the actual
> fields a transaction carries at `AcceptToMemoryPool` time against every frozen
> authority predicate — then classifies each predicate into
> **admission-determinable / block-context-only / not-applicable**, and only the
> first category is permitted to become early-rejection policy. No frozen invariant
> is reopened; no source is changed; `MempoolPolicy ≠ ConsensusAuthority` is
> preserved. Honors ATMP-NOGO-1..3, G-ATMP-1..4, and the XCT-OPEN-2 result.

---

## 0. The precision this spec must hold (from the directive)

> "Re-routed spend" MUST NOT imply that a transaction itself carries a freely
> selectable destination lane. Admission must first determine which routing/authority
> context is actually available from the concrete transaction format (G-ATMP-4:
> no synthesizing block-time-only context).

Everything below is subordinate to that precision.

---

## 1. ATMP source reconciliation (evidence)

### 1.1 What a transaction carries at admission time

| Field present at ATMP | Litenyx-specific? | Anchor |
| --- | --- | --- |
| Inputs = `CTxIn.prevout {hash, n}` (outpoints) | No — vanilla | `LITENYX_validation.cpp:60-61` reads `txin.prevout.hash/n` |
| Outputs = `CTxOut` amounts (`CAmount`) + scripts | No — vanilla | XCT-OPEN-2 recon R10 (`LITENYX_sharedstate.h:26-33`: no amount field added) |
| A lane / chainId / `PersistentChainId` field | **NO — ABSENT on the transaction** | see §1.2 |

### 1.2 The load-bearing fact: lane is a BLOCK property, not a transaction property

```
The chainId (lane) is carried in the AUX HEADER of the confirming BLOCK, never in
the transaction:

    LitenyxConnectSharedState(block): nChainId = block.nyx_aux.chainId   (:54)
    ... record spend as  outpoint -> block.nyx_aux.chainId               (:72)
```

- `chainId` lives in `nyx_aux` (the block aux extension), keyed by wire-format
  version V1/V2/V3 (`LITENYX_types.h:35-49`). It is a **block** field.
- The spent-set entry `outpoint → chainId` (`LITENYX_sharedstate.h:43-74`) takes its
  `chainId` from the **block** that confirms the tx (`:72`), NOT from the tx.
- Therefore the confirming lane is **assigned by block construction/selection**, not
  freely chosen inside the transaction. A tx does not "point at" a destination lane.

```
┌────────────────────────────────────────────────────────────────────┐
│ RECONCILIATION RESULT:                                               │
│ At ATMP time a transaction exposes ONLY: outpoints, amounts, scripts.│
│ The lane / PersistentChainId that will confirm it is UNKNOWN until a │
│ block is constructed. "Destination lane" is NOT a transaction field. │
└────────────────────────────────────────────────────────────────────┘
```

This is exactly why the XCT-OPEN-2 "re-routed spend" ontology must NOT be read as
"tx picks a lane": the re-routing is a **block-time** placement of an ordinary
single-ledger spend, not a tx-carried selector.

---

## 2. Predicate classification (the ATMP-OPEN-1 gate answered)

> For each transaction/operation class, which P5/P6/P7 and XCT predicates are
> determinable from canonical state at admission time?

Classification vocabulary: **AD** = admission-determinable · **BC** = block-context-only
· **NA** = not applicable to the current tx format.

| Predicate | Class | Justification |
| --- | --- | --- |
| Single-ledger UTXO validity (`Σin = Σout+fee`, scripts, standardness) | **AD** | pure function of the tx + `CCoinsView`; no lane needed; inherited Dogecoin |
| SharedSpendSet global-unspent check (`¬LitenyxIsSharedSpent(outpoint)`) | **AD (advisory only)** | computable from the current spent-set VIEW at admission (`sharedstate.h`); BUT see §3.1 caveat — the *authoritative* check is at `ConnectBlock` (`validation.cpp:56-66`) |
| `PersistentChainId` of the confirming lane | **BC** | requires `block.nyx_aux.chainId` (§1.2) — unknown pre-block |
| `mayBind` (P6) | **BC** | `LitenyxResolveExecutionAuthority` needs `(chainId, lane, L_h)` at height `h` (`execution_authority.h:186`); lane is block-context |
| `mayRoute` (P6) | **BC** | same; requires lane agreement with `L_h` binding at `h` |
| `WrongLane` (F3) | **BC** | explicitly a block/execution-context property (critique §Caveat, `execution_authority.h:191-196`) |
| `effMayRoute` / `effMayBind` (P7) | **BC** | overlay on P6 (`draining_authority.h:238-239`); inherits BC from P6 + drain fact |
| `IsDraining` / `DrainCommitment` (P7) | **BC / NA** | DA-OPEN-1 trigger OPEN; not tx-carried; not knowable at admission |
| Topology `N_h` / lifecycle status (P4/P5) | **BC** | pure function of committed history to `h`; every engine header declares "NO mempool input" |
| XCT-C1..C5 predicates | **BC** | all consume block-context authority + spend-set at `h` |
| XCT-INV-VALUE V1 (`Σin=Σout+fee`) | **AD** | = single-ledger UTXO validity above |
| XCT-INV-VALUE V2 (total_supply) | **NA at ATMP** | a per-transition/topology property, not a per-tx admission check |
| XCT-INV-VALUE V3 (spend uniqueness) | **AD (advisory)** | = SharedSpendSet check above |
| XCT-INV-VALUE V4 (no second carrier) | **AD (by absence)** | tx format has no second carrier field to admit; trivially holds |

### 2.1 Summary

```
ADMISSION-DETERMINABLE (may become policy):
  • Single-ledger UTXO validity (inc. V1)
  • SharedSpendSet global-unspent VIEW check (advisory; V3)
  • V4 no-second-carrier (holds by absence)

BLOCK-CONTEXT-ONLY (MUST NOT be early-rejection policy — G-ATMP-4):
  • PersistentChainId, mayBind, mayRoute, WrongLane, effMayRoute/effMayBind,
    IsDraining, N_h, lifecycle status, all XCT-C1..C5, V2

NOT APPLICABLE (current tx format):
  • Any lane/destination selector on the transaction (does not exist)
```

**Only the AD row is admissible as early-rejection policy.** Every authority/routing
predicate is BC because the lane is a block property (§1.2). This is the precise,
evidence-grounded reason the earlier caveat ("some outcomes only knowable at
block-validation time") holds — now proven from the transaction format, not asserted.

---

## 3. The advisory ATMP architecture (ATMP-OPEN-1 disposition)

```
        ┌─────────────── ADMISSION (advisory; policy only) ───────────────┐
        │  Single-Ledger UTXO Validity                                     │
        │      +  SharedSpendSet global-unspent VIEW (advisory)            │
        │      +  Admission-Available Authority Context (= NONE today)     │
        │  ─────────────────────────────────────────────────────────────  │
        │                    → Advisory ATMP Decision                      │
        └─────────────────────────────────────────────────────────────────┘
                                    ‖  (no coupling)
        ┌─────────────── CONSENSUS (authoritative; late) ─────────────────┐
        │  Canonical Block Context → P5 → P6 → P7 → XCT-C1..C5            │
        │                    → Consensus Validation                        │
        └─────────────────────────────────────────────────────────────────┘

HARD INVARIANT:   ATMPAccept(tx)  ⇏  ConsensusValid(tx)
```

- "Admission-Available Authority Context" is currently the **empty set** (§2.1): no
  authority predicate is AD. So today an advisory ATMP layer would filter ONLY on
  single-ledger validity + the spend-set view — it adds NO authority anticipation.
- If a FUTURE transaction format ever exposes a lane/identity selector, the SAME
  classification MUST be re-run before any predicate migrates AD; G-ATMP-4 forbids
  synthesizing it earlier.

### 3.1 SharedSpendSet advisory check — the one subtlety

The spend-set check is AD in the sense that the view is readable at admission, but it
is **advisory, not authoritative**:

- The authoritative global-unspent check runs in `LitenyxConnectSharedState`
  (`validation.cpp:56-66`) at block connect, against the canonical spent-set at `h`.
- A mempool-time view can be STALE (a conflicting spend may connect first). Therefore
  an ATMP spend-set check may reject early (conservative) but its ACCEPT carries no
  consensus meaning (ATMP-NOGO-1). It MUST re-defer to `ConnectBlock` (ATMP-NOGO-2 /
  G-ATMP-2). This satisfies the "reject more conservatively, never bypass" shape of
  ATMP-OPEN-2.

---

## 4. ATMP-OPEN-2 answered — the non-authority guarantee

The invariant that guarantees advisory-only, one-directional behaviour:

```
┌────────────────────────────────────────────────────────────────────┐
│ ATMP-INV (v0.1) — monotone conservatism:                            │
│                                                                      │
│   (P1)  ATMPReject(tx)  ⇒  node relay/mining POLICY only            │
│         (NOT a new consensus-invalidity rule)                        │
│   (P2)  ATMPAccept(tx)  ⇏  ConsensusValid(tx)                       │
│   (P3)  ConnectBlock re-derivation is ALWAYS authoritative and is   │
│         NEVER substituted by a cached mempool decision              │
│   (P4)  ATMP may only make the admitted set SMALLER than or equal   │
│         to what ConnectBlock would accept — never larger            │
│                                                                      │
│  ⇒  MempoolPolicy ≠ ConsensusAuthority  (preserved, not weakened)   │
└────────────────────────────────────────────────────────────────────┘
```

- **P1 is emphasized per directive:** ATMP rejection is **node relay/mining policy**,
  not consensus invalidity. A tx rejected by one node's ATMP policy may still be
  perfectly consensus-valid and mined by another node; nothing about consensus
  changes. This keeps ATMP strictly in the policy plane.
- **P4 (monotone conservatism)** is the formal ATMP-OPEN-2 answer: advisory checks may
  only subtract from the candidate set. Because the only AD predicates are
  single-ledger validity (already a superset gate) and a *conservative* spend-set
  view (rejects only on observed conflict), acceptance can never manufacture validity.

---

## 5. What the XCT-OPEN-2 result simplifies (directive confirmation)

Because value conservation reduced to single-ledger accounting (XCT-INV-VALUE), ATMP
needs **none** of the following, and this spec introduces none:

```
ATMP needs NO:  bridge-state · mint/burn receipt · lock proof · second-ledger
                balance check · wrapped/mirrored-coin accounting
```

Ordinary UTXO conservation remains the monetary foundation. Litenyx-specific
admission logic concerns ONLY (a) spend uniqueness (advisory spend-set view) and
(b) whatever routing authority is knowable before block context — which §2 proves is
currently NONE. This is the maximal simplification the resolved accounting permits.

---

## 6. No-gos and guardrails (all preserved; one added)

| Rule | Status in this spec |
| --- | --- |
| ATMP-NOGO-1 (accept ⇏ consensus valid) | upheld (ATMP-INV P2) |
| ATMP-NOGO-2 (no stale read bypasses ConnectBlock) | upheld (P3; §3.1) |
| ATMP-NOGO-3 (consume frozen eff-authority; never reconstruct) | upheld — and moot today (no AD authority predicate to consume) |
| G-ATMP-1 (policy only) | upheld (P1) |
| G-ATMP-2 (ConnectBlock authoritative) | upheld (P3) |
| G-ATMP-3 (consume, not reconstruct) | upheld |
| G-ATMP-4 (no synthesized block-time context) | **central** — §1.2/§2 classification enforces it structurally |
| **G-ATMP-5 (new) — Reclassification discipline** | No predicate may migrate from BC to AD without re-running §2 against the THEN-CURRENT concrete transaction format. A future format change cannot silently promote an authority predicate into early policy. |

---

## 7. Disposition

```
ATMP-OPEN-1 = ANSWERED (scoped): the admission-determinable set today is
  {single-ledger UTXO validity, advisory SharedSpendSet-unspent view, V4-by-absence}.
  ALL authority/routing predicates (PersistentChainId, mayBind, mayRoute, WrongLane,
  effMayRoute, IsDraining, N_h, lifecycle, XCT-C1..C5, V2) are BLOCK-CONTEXT-ONLY,
  because the lane lives in block.nyx_aux, not the transaction (validation.cpp:54,72).
  Admission-Available Authority Context = ∅ today.

ATMP-OPEN-2 = ANSWERED: ATMP-INV (P1 policy-not-consensus, P2 accept⇏valid,
  P3 ConnectBlock authoritative, P4 monotone conservatism) ⇒ MempoolPolicy ≠
  ConsensusAuthority preserved. ATMP rejection is node relay/mining policy, NOT a
  consensus-invalidity rule.

Simplification banked from XCT-OPEN-2: no bridge-state / receipt / lock / second-
  ledger balance. New guardrail G-ATMP-5 (reclassification discipline).
  No frozen invariant reopened; no mechanism elevated to principle (G-ATMP-4/G-XCT-4);
  no code.

Live OPEN boundaries carried forward: SSC-OPEN-1, DA-OPEN-1.
Outward next (as directed): RPC presentation as a CONSUMER of these semantics.
```
