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

---

# Component 2 — Topology

**Central question:** Is topology a pure, path-independent value folded
deterministically from canonical history with a single source of truth, or does
the runtime `LitenyxTopologyTracker` singleton constitute a second, ephemeral
representation? Critique is split into two layers:

```
Topology Consensus Value   vs.   Topology Runtime Tracker
```

**Decisive test:** `TrackerState != ConsensusTruth` (unless the frozen spec says
otherwise).

**Verdict:** The two layers are cleanly separated. The **consensus value is a
pure, path-independent fold** over canonical chain data; the **tracker's
authoritative reads are consumed by RPC/telemetry ONLY and never by any
consensus enforcement path**. Two nodes with identical canonical history but
divergent tracker histories CANNOT derive a different consensus `N_h`, because
enforcement recomputes topology from the chain and never consults the tracker.
The singleton is therefore a **runtime-maturity issue, not a second source of
consensus truth**. No frozen invariant is violated.

## Source anchors

- `litenyx/LITENYX_topology.h:102-153` — pure controller (`LitenyxTopoDecide`,
  `LitenyxTopoTransitionHeight`, `LitenyxTopoApply`); explicitly "does NOT mutate
  state"; decision is a pure function of `(obs, N_h, h_obs, lastTransitionHeight)`.
- `litenyx/LITENYX_topology_authority.h:198-215` — `LitenyxTopologyState`
  `{nVersion, nHeight, nN, nLastTransition}` value type with `operator==`.
- `litenyx/LITENYX_topology_authority.h:499-518` — `LitenyxCalculateExpectedTopologyFromChain`
  (pure fold; the consensus source of truth).
- `litenyx/LITENYX_validation.cpp:98-124` — `LitenyxBuildCanonicalBlocks` (reads
  ancestor bodies from disk; canonical chain data ONLY; fail-closed on pruned body).
- `litenyx/LITENYX_validation.cpp:156-186` — enforcement derives `expected` from
  the chain and verifies the commitment; tracker never referenced.
- `litenyx/LITENYX_topology_tracker.{h,cpp}` — stateful singleton with
  authoritative `m_nChains`/`m_lastTransition`/`m_transitions`.
- `deploy/patches/litenyx-rpc.patch:175-204` — the ONLY readers of
  `Tracker::Instance().Chains()/LastTransition()` (RPC/telemetry).

## Layer A — Topology Consensus Value (mature)

- **Pure fold / path-independent.** `LitenyxCalculateExpectedTopologyFromChain`
  folds `Genesis()` over the canonical (chainId, weight) sequence; KATs assert
  IBD==live, prefix determinism, and branch independence
  (`test_litenyx_topology_authority.cpp:368-445`). This satisfies the "identical
  canonical history -> identical `N_h`" requirement independent of runtime state.
- **Deterministic transition semantics.** Observation window exact and
  boundary-aligned `[h_obs-W+1, h_obs]`; cooldown uses signed arithmetic to avoid
  unsigned wrap (`LITENYX_topology.h:118-122`); bounds `MIN_CHAINS(2) <= N <=
  TOPO_MAX_CHAINS(8)`, MERGE can never collapse to N=1; `S` (imbalance) is
  telemetry-only and excluded from decisions.
- **Reconstruction is fail-closed.** A pruned/missing ancestor body yields
  `litenyx-topo-reconstruct-unavailable` rather than a guess (`validation.cpp:158-163`).

## Layer B — Topology Runtime Tracker (maturity issue, non-authoritative)

The tracker maintains an independently-mutated `m_nChains` via an
ordering/boundary-dependent path (`Connect`/`Tick`/`Finalize`). Considered in
isolation it is NOT a pure fold:

### F-TOPO-1 (FINDING) — tracker N is order/boundary-dependent, not a fold
`Finalize` mutates `m_nChains`/`m_lastTransition`/`m_transitions` in place from
window accumulators; `Connect` finalizes only when `height % OBS_WINDOW == 0`.
The tracker's N depends on the live block-processing order and window state, not
purely on canonical history. **Non-consensus** because enforcement never reads it.

### F-TOPO-2 (FINDING) — asymmetric rollback vs. the pure fold
`Disconnect` rolls back only if a transition was recorded at exactly that height
and recomputes `prev` by scanning `m_transitions` (`tracker.cpp:18-40`); the
window comment concedes accumulators are "rebuilt on the next Connect pass." This
disconnect/reconnect path is not provably identical to re-folding from history —
a divergence surface that exists ONLY in the advisory tracker, never in
enforcement.

### F-TOPO-3 (FINDING) — singleton + `Reset()` trapdoor (F-SSC-2/3 pattern recurs)
`Instance()` process-global with an unguarded `Reset()` reachable from the
regtest RPC path (`rpc.patch:201`). Same ownership-divergence and reset-trapdoor
maturity issues catalogued for the shared-state core; here strictly contained to
telemetry because consensus ignores the tracker.

### F-TOPO-4 (FINDING) — constructor-vs-Reset init asymmetry (latent crash note)
The header documents that the real block path never calls `Reset()` first, so the
constructor must pre-size accumulators or the first `Accumulate()` would index
empty vectors; the regtest path masks this by always issuing `reset`
(`tracker.h:28-35`, `98-106`). A defensive-coding smell, not a consensus issue.

## Decisive-test resolution

`TrackerState != ConsensusTruth`, AND consensus never consults `TrackerState`.
Enforcement (`LitenyxCheckTopologyCommitment`) derives `expected` purely from the
chain and verifies the block's committed topology against it; the tracker's
`Chains()/LastTransition()` are read only by RPC. Therefore divergent tracker
histories cannot cause consensus `N_h` divergence. Classified as **runtime
maturity**, not consensus risk.

## Guardrails (doctrine-level; no code change now)

- **G-TOPO-1** — `LitenyxTopologyTracker` is ADVISORY/TELEMETRY only. No
  consensus code may read `Chains()`/`LastTransition()` or otherwise treat tracker
  state as authoritative; the authoritative topology is EXCLUSIVELY
  `LitenyxCalculateExpectedTopologyFromChain`.
- **G-TOPO-2** — `Tracker::Reset()` is a test-harness affordance only, never a
  production consensus transition (mirrors G-SSC-2).
- **G-TOPO-3** — Any future need for persistent topology state MUST derive from
  the pure fold, not by promoting the tracker to authoritative.

## Disposition

Layer A is mature and satisfies the central question. Layer B findings
(F-TOPO-1..4) are runtime-maturity items contained by the pure-fold enforcement
boundary; none reopens a frozen invariant. Guardrails G-TOPO-1..3 recorded.
Proceed to Component 3 (Topology Authority) — where the fold, the 13-byte
`TopologyStateHash` serialization, and the commitment-verification regimes will
be examined directly.
