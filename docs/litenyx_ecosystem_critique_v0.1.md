# Litenyx Ecosystem Critique — v0.1

*Status: LIVING AUDIT RECORD. This document captures component-by-component
critique findings and open design questions against the frozen Litenyx stack.
It is a **record of findings**, NOT a specification and NOT a set of
implementation directives. Nothing here reopens a frozen invariant. Items marked
`OPEN` are consensus-design questions to be answered spec-first, later.*

## Baseline

- Pure-engine boundary frozen at commit `770496e` (Phase-7 draining overlay;
  D-K1..D-K18 green under C++11 and C++20).
- `phase6-green @ a95507f` and all prior green tags immutable.
- The Phase-7 emitter / provenance problem is explicitly **OPEN** and separate
  from the proven drain-overlay semantics.

## Maturity hierarchy (evaluation order of authority)

When findings conflict, the more mature artifact wins:

1. **Frozen implementation & specifications** (compiled-and-exercised; green tags).
2. **Later locked decisions** (agreed, not yet fully implemented).
3. **Verified evidence** (source-grounded findings).

Superseded exploratory conclusions MUST NOT be reintroduced as assumptions.

## Traversal order

Frozen dependency order, then outward into the broader ecosystem:

```
Shared-State Core -> Topology -> Topology Authority -> ChainId Lifecycle
  -> Execution Authority -> Draining Overlay
  -> Routing/XCT -> Mempool/ATMP -> Persistence/Recovery
  -> Mining & Hash Distribution -> Controller System
  -> Reward/Fee Economics -> Wallet/User Layer
  -> Governance & Long-Term Attack Analysis
```

## Finding classification

- `FINDING` — a source-grounded structural observation.
- `OPEN` — an unresolved consensus-design question to be answered spec-first.
- `GUARDRAIL` — a doctrine-level constraint to preserve without changing code now.

---

# Component 1 — Shared-State Core

**Central question:** Does every downstream consensus decision consume one
canonical state representation, with deterministic transitions and no alternate
reconstruction path?

**Verdict:** Partially. The spend-set is single-sourced and reorg-safe at block
granularity, but it is a runtime in-memory singleton with no serialization, no
formally established replay-determinism contract, and an ownership model that
diverges from the pure-fold discipline used by every later phase. It is the
least mature core in the frozen stack despite being Phase 2. No frozen invariant
is violated; all items below are boundary/maturity findings.

## Source anchors

- `litenyx/LITENYX_sharedstate.h:43` — `LitenyxSharedSpendSet` (sole `m_spent` map).
- `litenyx/LITENYX_sharedstate.cpp:5-19` — `RecordSpend` / `RevertSpend` (only mutators).
- `litenyx/LITENYX_sharedstate.h:61-64` — `Reset()`.
- `litenyx/LITENYX_sharedstate.h:66-69` — `Instance()` singleton.
- `litenyx/LITENYX_validation.cpp:52-88` — connect (two-phase commit) / disconnect.
- `litenyx/LITENYX_sharedstate.cpp:27-32` — `ConfirmingChain` (stored asserted lane).

## What is proven

- One set, one invariant, single mutation path, symmetric reorg revert, and no
  alternate reader path in code (all consumers use the four free functions).
- `LitenyxConnectSharedState` is check-all-then-commit-all, so a rejected block
  leaks no partial records (block-granular transition atomicity).

## Findings

### F-SSC-1 (FINDING; drives SSC-OPEN-1) — ephemeral canonical state, no formal reconstruction contract
`m_spent` is a pure runtime `std::unordered_map` with no serialize/deserialize
and no persistence. The canonical state is **derived**, never committed or
checkpointed, and is rebuilt only by implicit ConnectBlock replay. The problem
is **not** two competing sources of truth today; it is an **ephemeral canonical
state whose deterministic reconstruction contract has not been formally
established**.

### F-SSC-2 (FINDING) — ownership-model divergence (singleton vs. pure fold)
Every later phase is a pure function of an explicit `L_h` value
(path-independent, standalone-testable). The shared-state core is a
process-global mutable singleton (`Instance()`). This is the one place where
"the canonical state" is an object with identity and lifetime rather than a
value folded from history — the most likely origin of a future
second-source-of-truth bug, because the downstream "recompute from `L_h`" mental
model does not apply here.

### F-SSC-3 (FINDING) — `Reset()` is an unguarded global trapdoor
`Reset()` clears the entire canonical set with no activation gate and no history
awareness, and is reachable from the regtest RPC path. It exists for test
convenience but is a live consensus-mutating entrypoint on the singleton.

### F-SSC-4 (FINDING) — stored `chainId` is advisory, not authoritative
`RecordSpend` stores `block.nyx_aux.chainId` (the asserted lane), recorded
without Phase-6 execution-authority resolution. `ConfirmingChain` therefore
returns a claimed lane, not an authoritative `PersistentChainId`. Inert today
(RPC-only), but a stored fact that looks authoritative and is not; keying future
logic on it would reintroduce the ABA/lane-reuse hazard Phase 7 avoids.

### F-SSC-5 (FINDING; folds into SSC-OPEN-1's proof obligation) — atomicity is per-block, not per-reorg
Two-phase commit guarantees atomicity within one `ConnectBlock`. It does not
address multi-chain reorg disconnect/connect ordering; correctness relies on the
daemon replaying the exact inverse ordering, which lives in the patched daemon
outside the frozen pure core — asserted-by-integration, not proven-by-KAT.

## Open design question

### SSC-OPEN-1
> Is `LitenyxSharedSpendSet` defined as **(A)** persistent/committed canonical
> consensus state, or **(B)** derived canonical state reconstructed exclusively
> by deterministic folding of canonical multi-chain history? If (B), the protocol
> must specify the canonical replay ordering and prove that restart, reindex, and
> multi-chain reorganization converge to the identical spend-set state.

Proof obligation (absorbs F-SSC-5):

```
Restart + Reindex + Single-chain reorg + Multi-chain reorg
  -> identical canonical SharedSpendSet
```

Do not implement serialization before answering SSC-OPEN-1.

## Guardrails (doctrine-level; no code change now)

- **G-SSC-1** — No whole-set consensus fold may depend on `unordered_map`
  iteration order. (Benign today because only point lookups exist; a latent trap
  for any future whole-set fold, e.g. a committed spend-set hash or a
  drain-pressure snapshot.)
- **G-SSC-2** — `Reset()` must never be considered a valid production consensus
  transition; it is a test-harness affordance only.
- **G-SSC-3** — `ConfirmingChain` must remain informational only; the stored
  lane claim must never substitute for authoritative `PersistentChainId` identity.

## Disposition

None of F-SSC-1..F-SSC-5 requires reopening a frozen invariant. The most
important item is F-SSC-1, tracked as SSC-OPEN-1. Guardrails G-SSC-1..3 are
recorded at the doctrine level. Proceed to Component 2 (Topology).
