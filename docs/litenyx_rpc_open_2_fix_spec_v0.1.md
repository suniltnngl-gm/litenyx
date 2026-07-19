# Litenyx RPC-OPEN-2 — Presentation-Provenance Contract — Spec v0.1 (spec-first; no code)

> STATUS: RPC-OPEN-2 was recorded OPEN by the ecosystem critique (Component 10,
> F-RPC-4, G-RPC-3/4). This document closes it as a **presentation-provenance**
> contract: every Litenyx RPC field is labelled with its source class so a consumer
> can never mistake advisory/runtime state for canonical truth, an asserted lane for
> authoritative identity, a hypothetical (synthetic-chain) projection for real block
> context, or a confirmed-spend query for mempool state. It touches NO consensus
> behavior and preserves the already-specified RPC-OPEN-1 mutation-gating fix. No
> frozen invariant is reopened; no source is changed here.

---

## 0. The central question (from the directive)

> Can an RPC consumer distinguish canonical truth, derived authority,
> advisory/runtime state, and block-context-only projections without ambiguity?

Today: **NO**, in two concrete places (§2). This spec makes it **YES** by attaching
an explicit provenance label to every field.

---

## 1. The four provenance classes

```
CANONICAL    — a fact of committed chain history (or its deterministic fold);
               the single source of consensus truth.
DERIVED      — a pure re-derivation/projection FROM canonical history (P4/P5/P6
               folds). Authoritative in the same sense as canonical; labelled
               distinctly because it is computed, not stored.
ADVISORY     — ephemeral runtime/tracker state; NON-consensus; may drift; MUST
               NEVER be read as canonical.
HYPOTHETICAL — a projection over a SYNTHETIC or SUPPLIED block context (e.g. the
               test RPCs' synthetic canonical chain), not the node's real chain
               state. Correct only relative to the stated context.
```

Mapping to the frozen recovery model (Component 9): CANONICAL/DERIVED = "persisted
canonical / deterministically reconstructed"; ADVISORY = "ephemeral runtime state";
HYPOTHETICAL is an RPC-only presentation category (synthetic input).

---

## 2. Field reconciliation (actual RPC outputs, with the two ambiguities)

### 2.1 The `nChains` collision (F-RPC-4, the primary hazard)

```
testlitenyxtopology status    -> nChains = TopologyTracker::Instance().Chains()   ADVISORY
testlitenyxtopoauthority expected -> nChains = expected.nN                        DERIVED (canonical N_h)
```

Two different RPCs emit a field with the IDENTICAL name `nChains` and OPPOSITE
provenance (`litenyx-rpc.patch:175` advisory vs `:281` derived). An operator reading
advisory `nChains` as canonical `N_h` is the exact F-RPC-4 mis-read. Same collision
for `lastTransition` (`:176` advisory).

### 2.2 The asserted-vs-authoritative lane (F-RPC-3 — already GOOD, must stay)

```
testlitenyxexecauthority resolve
  input:  asserted lane (params[3])
  output: chainId/laneId = AUTHORITATIVE from L_h  (:490-491)
          mayRoute/mayBind = DERIVED decision       (:488-489)
  BUT the whole result is over buildChain(height) = a SYNTHETIC canonical chain
      (:423-425, :479) => HYPOTHETICAL context, not the node's real chain.
```

F-RPC-3 correctly surfaces authoritative identity, never the claim, as authority.
The residual RPC-OPEN-2 gap: the result is HYPOTHETICAL (synthetic chain) yet the
response carries no field saying so. A consumer could read `mayRoute:true` as a
statement about a real transaction — which, per the ATMP reconciliation, is
impossible (lane is block context, not a tx field). See §4.

### 2.3 Full field inventory and target labels

| RPC (mode) | Field(s) | Class | Anchor |
| --- | --- | --- | --- |
| `sharedstate query` | `spent`, `confirming_chain` | **CANONICAL** (confirmed-spend state) | `:126-127` |
| `sharedstate record`/`revert` | `accepted`, `all_accepted` | mutation ack (see RPC-OPEN-1) | `:100,104` |
| `topology status` | `nChains`, `lastTransition` | **ADVISORY** | `:175-176` |
| `topology status` | `minChains`, `maxChains` | CANONICAL constant | `:177-178` |
| `topology observe/tick/reset` | `observed`,`nChains`,`reset` | **ADVISORY** (mutator) | `:187-204` |
| `topoauthority expected` | `nChains`(=`nN`), `commitment` | **DERIVED** | `:279-281` |
| `topoauthority regime`/`decide` | `regime`, `verdict` | **DERIVED**/HYPOTHETICAL | `:270,295` |
| `lifecycle expected` | `activeChains`,`nextChainId`,`commitment` | **DERIVED** | `:373-376` |
| `execauthority resolve`/`resolveid` | `code,state,authorized,mayRoute,mayBind,laneId,chainId` | **DERIVED** decision over **HYPOTHETICAL** context | `:485-517` |
| all `regime` fields | `regime` | context descriptor | many |

---

## 3. The presentation-provenance contract (RPC-OPEN-2 answer)

```
┌────────────────────────────────────────────────────────────────────┐
│ RPC-INV-PROV (v0.1):                                                 │
│                                                                      │
│  Every Litenyx RPC response field carries an explicit provenance    │
│  descriptor such that:                                               │
│                                                                      │
│  (C1) Each object states  "source": one of                         │
│         "canonical" | "derived" | "advisory" | "hypothetical".      │
│  (C2) ADVISORY objects additionally carry  "advisory": true  and    │
│        MUST NOT reuse a field NAME that a CANONICAL/DERIVED object   │
│        uses for a different-provenance value (resolves the nChains   │
│        collision, §2.1).                                             │
│  (C3) HYPOTHETICAL objects state the CONTEXT they were computed      │
│        against  (e.g. "context":"synthetic-chain", plus the height   │
│        / net that defined it).                                       │
│  (C4) Any object surfacing a lane keeps the F-RPC-3 discipline:      │
│        an ASSERTED lane is labelled as input/claim; the             │
│        AUTHORITATIVE PersistentChainId/lane is labelled authoritative│
│        and never conflated.                                         │
│                                                                      │
│  ⇒ a consumer can classify every field with NO out-of-band knowledge│
└────────────────────────────────────────────────────────────────────┘
```

### 3.1 Concrete disambiguation of the collision (illustrative shape, not code)

```
topology status            ->  { "source":"advisory", "advisory":true,
                                 "trackerChains": N, "trackerLastTransition": T }
topoauthority expected     ->  { "source":"derived",
                                 "canonicalN": N_h, "commitment": ... }
```

The field is RENAMED by provenance (`trackerChains` vs `canonicalN`) so the two can
never be confused even if a consumer ignores `"source"`. (C2 requires at minimum the
label; renaming is the stronger, recommended form.)

### 3.2 The ATMP-derived semantic guardrail (directive)

Because the ATMP reconciliation PROVED lane assignment is block context (a
transaction carries no lane), the following is frozen:

```
┌────────────────────────────────────────────────────────────────────┐
│ RPCTransactionView  ⇏  { PersistentChainId, mayRoute, effMayRoute,  │
│                          WrongLane }                                 │
│                                                                      │
│  A per-transaction RPC view MUST NOT emit routing-authority fields   │
│  as if the transaction selected/possessed a lane. Those fields MAY   │
│  appear ONLY in an RPC that is explicitly evaluating a HYPOTHETICAL  │
│  or ACTUAL block context, and MUST be labelled with that context     │
│  (C3).                                                               │
└────────────────────────────────────────────────────────────────────┘
```

This is why the existing `execauthority resolve` output is acceptable but must gain
a `"context":"synthetic-chain"` marker: it is a block-context evaluator, not a
transaction view. It must never be mistaken for "this tx may route on lane X."

### 3.3 XCT/drain terminology restraint

No Litenyx RPC may expose `effMayRoute`, `IsDraining`, `DrainCommitment`, or XCT
transition terminology in a way implying transaction-level routing selection.
`effMayRoute`/`IsDraining` are BLOCK-CONTEXT-ONLY (ATMP §2) and DA-OPEN-1 leaves
drain provenance OPEN; presenting them per-transaction would fabricate a capability
the stack does not offer. If ever surfaced, they are HYPOTHETICAL/block-context and
labelled per C3.

---

## 4. What this does NOT change (scope fences)

- **No consensus behavior.** RPC-INV-PROV is purely presentational: labels and field
  names in responses. `RPCObservation ≠ ConsensusAuthority` (RPC-NOGO-1) is
  untouched; no output feeds `ConnectBlock` (G-RPC-2).
- **RPC-OPEN-1 preserved.** The mutation-gating contract (`rpc_open_1_fix_spec_v0.1`)
  is orthogonal and unchanged; this spec adds only provenance labelling. A mutator's
  gating is still required (RPC-NOGO-3, G-RPC-1).
- **F-RPC-3 preserved and generalised** into C4.
- **SharedSpendSet query is CANONICAL confirmed-spend, not mempool** — labelled
  `"source":"canonical"`; consistent with ATMP §3.1 (the authoritative spend check is
  `ConnectBlock`, and the query reflects committed state, never mempool).

---

## 5. No-gos and guardrails (preserved; two added)

| Rule | Status |
| --- | --- |
| RPC-NOGO-1 (no RPC output feeds ConnectBlock) | upheld (§4) |
| RPC-NOGO-2 (never present claimed lane as authoritative) | upheld / strengthened (C4) |
| RPC-NOGO-3 (regtest gating enforced, not comment) | untouched (RPC-OPEN-1) |
| G-RPC-1..2 | untouched |
| G-RPC-3 (advisory labelled advisory) | **fulfilled by C1/C2** |
| G-RPC-4 (authoritative vs claimed distinguished) | **fulfilled by C4** |
| G-RPC-EXT-1 | untouched |
| **G-RPC-5 (new) — Provenance completeness** | Every Litenyx RPC field MUST be classifiable into exactly one of {canonical, derived, advisory, hypothetical}; no field may ship without a determinable class (C1). |
| **G-RPC-6 (new) — No transaction-level routing authority** | Routing-authority fields (`PersistentChainId`, `mayRoute`, `effMayRoute`, `WrongLane`, `IsDraining`) MUST NOT be presented in a per-transaction view; only in an explicitly-labelled block-context (hypothetical/actual) evaluator (§3.2). |

---

## 6. Disposition

```
RPC-OPEN-2 = ANSWERED (presentation-provenance contract RPC-INV-PROV, C1–C4).
  Central question resolves YES: every field classifiable as
  canonical | derived | advisory | hypothetical with no out-of-band knowledge.
  Primary hazard closed: the nChains advisory/derived collision (F-RPC-4) via
  provenance label + recommended rename (trackerChains vs canonicalN).
  F-RPC-3 preserved+generalised (C4). SharedSpendSet query = CANONICAL confirmed-
  spend, never mempool. New semantic guardrail (from ATMP): RPCTransactionView ⇏
  {PersistentChainId, mayRoute, effMayRoute, WrongLane} unless explicitly a
  labelled block context. XCT/drain terminology restrained (§3.3).
  New guardrails G-RPC-5/6. RPC-OPEN-1 mutation fix preserved & orthogonal.
  No consensus behavior touched; no frozen invariant reopened; no code.

Live OPEN boundaries carried forward: SSC-OPEN-1 (==PR-OPEN-1), DA-OPEN-1.
Outward next (as directed): Component 11 — Daemon Integration (production
  composition / rejection atomicity / ordering / disconnect symmetry).
```
