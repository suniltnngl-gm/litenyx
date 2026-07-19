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

---

# Component 3 — Topology Authority

**Central question:** Does Topology Authority uniquely bind every accepted block
to the topology state derived from its canonical ancestry?

Four surfaces examined: **(A) canonical state encoding/hash**, **(B)
regime/activation semantics**, **(C) canonical reconstruction**, **(D)
enforcement integration**.

**Verdict:** This is one of the strongest boundaries in the stack. The pipeline

```
Canonical History -> Deterministic Fold -> Canonical TopologyState
  -> 13-byte Commitment -> Regime Verification
```

holds end-to-end. The authoritative state is derived purely from canonical
ancestor bodies; the block's asserted commitment is only ever *compared* against
the independently-derived expected value, never fed back into derivation. All
required invariants (below) hold against source. No frozen invariant is
violated; the only items recorded are minor doctrine guardrails.

## Source anchors

- `litenyx/LITENYX_topology_authority.h:121-150` — `D_v1` / `M_c_v1` /
  single-downscale (version-pinned math).
- `:198-236` — `LitenyxTopologyState` value + explicit 13-byte LE serialization.
- `:238-340` — self-contained SHA256d + `LitenyxTopologyStateHash` (KAT-locked).
- `:383-409` — `LitenyxVerifyTopologyCommitment` (three-regime verdict table).
- `:420-520` — pure boundary transition + `LitenyxCalculateExpectedTopologyFromChain`
  (canonical-chain fold; internal sort proves path-independence).
- `litenyx/LITENYX_validation.cpp:128-187` — enforcement integration.
- `cpp_reference/test/test_litenyx_topology_authority.cpp:364-448` — determinism,
  IBD==live, disconnect/reconnect identity, reorg prefix identity.

## Surface A — canonical state encoding / hash (PASS)

- **Field-by-field preimage, explicit widths, not struct memory.**
  `LitenyxSerializeTopologyState` writes `nVersion|nHeight|nN|nLastTransition` as
  fixed little-endian bytes via a `put32` lambda into a `[13]` buffer (`:223-236`)
  — no `memcpy` of the struct, so platform padding/endianness cannot leak. All
  validators hash exactly the same 13 semantic bytes.
- **Version pins interpretation AND math.** `nVersion` is the first hashed field;
  the header states changing `D_v1`/`M_c_v1`/downscale "REQUIRES a new nVersion +
  activation height" (`:49-51`). The hash preimage therefore commits to the
  version under which it was computed — a v1 hash cannot silently coexist with
  altered v1 math without changing the committed bytes.
- **Single hash domain.** The commitment value IS the frozen `TopologyStateHash`;
  no separate domain/tag is introduced (`:342-352`), KAT-locked SHA256d.

### F-TA-1 (FINDING, minor) — `nVersion` pins interpretation but not the math *implementation*
The version field guarantees any change to `D_v1`/`M_c_v1`/downscale must bump
`nVersion` to remain honest, and the KAT-locked hash makes a silent constant
change detectable via divergent hashes. However, nothing *mechanically* prevents
a future edit from altering `LITENYX_DEMAND_SCALE`/`LITENYX_MAX_BLOCK_WEIGHT`
while leaving `nVersion=1` — the guarantee is by KAT + doctrine, not by type. See
G-TA-1.

## Surface B — regime / activation semantics (PASS)

Frozen verdict table (`:374-407`), all fail-closed:

| regime | absent | correct | mismatch |
|---|---|---|---|
| PreDerivation | Valid | (present->Invalid, premature) | Invalid |
| SoftAdvisory | Valid | Valid | **AdvisoryMismatch** |
| HardAuthority | **Invalid** | Valid | **Invalid** |

- **Soft-advisory containment invariant HOLDS:**
  `AdvisoryMismatch -> diagnostics only` and `AdvisoryMismatch -/-> persistent
  alternative consensus truth`. In `SoftAdvisory`, a mismatch returns
  `AdvisoryMismatch`, which enforcement maps to `LogPrintf(...) + return true`
  (`validation.cpp:175-181`). The block is accepted, but **nothing about the
  asserted (wrong) commitment is stored or fed forward** — see Surface C.
- **Activation structural validity is enforced:** `IsValid()` couples
  both-disabled, forbids `hDerive==0`, requires `hDerive <= hTopology` (`:82-88`);
  DISABLED is an exact `== 0xFFFFFFFF` sentinel test, not "large height" (`:54-58`).

### F-TA-2 (FINDING) — Soft->Hard transition cannot resurrect tolerated mismatches
Decisive check for the flagged risk: a mismatch accepted during `SoftAdvisory`
does **not** become an alternative ancestor state used by later `HardAuthority`
reconstruction. Reason (proven by construction, Surface C): the expected state at
any height is *recomputed from canonical block bodies*
(`LitenyxCalculateExpectedTopologyFromChain`), which reads only `(chainId,
GetBlockWeight)` per ancestor and **never reads any block's asserted
`topologyCommitment`**. The soft-mode mismatch left the (correct) canonical
ancestry unchanged; when the chain crosses into hard authority, reconstruction
re-derives the *correct* expected state from that same ancestry. The tolerated
wrong commitment has no persistence surface. Classified: invariant HOLDS.

## Surface C — canonical reconstruction (PASS)

- **Fold input is canonical-only.** `LitenyxCalculateExpectedTopologyFromChain`
  consumes `std::vector<LitenyxCommittedBlock>{chainId, blockWeight}` indexed by
  height, walks `OBS_WINDOW` boundaries in ascending order, aggregates `M_c_v1`
  using the N active *at that boundary* (itself derived from earlier boundaries),
  and applies the frozen controller (`:499-520`). No tracker, cache, mempool, or
  asserted commitment is an input.
- **Path-independence is proven, not asserted.** `LitenyxCalculateExpectedTopology`
  sorts boundaries so the result is a function of the *set* of boundaries, not
  insertion order (`:464-483`); KATs assert IBD==live at every boundary,
  disconnect/reconnect identity, and reorg prefix identity
  (`test:382-448`). Satisfies spec §0.1 "IBD/sequential/reorg yield byte-identical
  hash."
- **The §5.6 cache-independence invariant is explicit:** deleting every optional
  cache/index must not change the result — it is the definition of authoritative
  topology (`:497-498`).

## Surface D — enforcement integration (PASS, fail-closed)

`LitenyxCheckTopologyCommitment` (`validation.cpp:128-187`):

1. Regime from frozen per-network activation.
2. PreDerivation fast path: a present V2 commitment is rejected as premature.
3. **Derive `expected` from canonical chain alone** via `LitenyxBuildCanonicalBlocks`
   (reads ancestor bodies from disk) + `LitenyxCalculateExpectedTopologyFromChain`.
4. Pure verify; map verdict to accept / advisory-log / `state.Invalid`.

- **Fail-closed on unreadable history (decisive check):** a pruned/missing
  ancestor body yields `litenyx-topo-reconstruct-unavailable`
  (`validation.cpp:158-163`) — an explicit deterministic validation failure. There
  is **no fallback** to tracker state, cached advisory state, the asserted block
  commitment, or a default topology. This is the strongest form of the
  fail-closed property.

### F-TA-3 (FINDING, integration boundary) — reconstruction cost is O(height) per validated block
`LitenyxBuildCanonicalBlocks` walks `pindexPrev` to genesis reading every ancestor
body on each `CheckTopologyCommitment` call (`validation.cpp:113-122`). Correct
and canonical, but O(height) disk reads per block with no memoization. This is a
**performance/DoS-surface** observation, not a consensus-correctness issue; any
future optimization MUST preserve the §5.6 invariant that caches cannot change the
result (G-TA-2).

## Central-question resolution

YES. Every accepted block is bound to the topology state derived from its
canonical ancestry: enforcement derives `expected` from ancestor bodies alone and
requires the block's committed hash to equal `LitenyxExpectedTopologyCommitment(expected)`
under hard authority; the asserted commitment is never an input to derivation;
missing history fails closed. The four-surface pipeline is intact.

## Guardrails (doctrine-level; no code change now)

- **G-TA-1** — The v1 reconstruction constants (`LITENYX_DEMAND_SCALE`,
  `LITENYX_MAX_BLOCK_WEIGHT`, `LITENYX_CONTROLLER_DOWNSCALE`) and the `D_v1`/`M_c_v1`
  math are part of the `nVersion=1` consensus identity. Any change REQUIRES a new
  `nVersion` + activation height; it may never ship while still claiming version 1.
  (KAT-locked hash makes violations detectable; this guardrail makes the rule
  explicit.)
- **G-TA-2** — Any performance optimization of canonical reconstruction (e.g. a
  topology index/cache to avoid the O(height) walk) MUST be provably equal to the
  pure fold: deleting the cache must not change the result (spec §5.6).
- **G-TA-3** — `AdvisoryMismatch` must remain diagnostics-only and must never be
  persisted or fed into any later reconstruction; expected state is ALWAYS
  recomputed from canonical ancestry, never from a stored/asserted commitment.

## Disposition

All four surfaces PASS; the central question resolves YES. F-TA-1 (math pinned by
KAT/doctrine not type), F-TA-2 (soft->hard cannot resurrect mismatches — invariant
holds), and F-TA-3 (O(height) reconstruction cost — perf/DoS, not correctness) are
recorded. Guardrails G-TA-1..3 recorded. No frozen invariant reopened. Proceed to
Component 4 (ChainId Lifecycle).

---

# Component 4 — ChainId Lifecycle

**Central question:** Is `L_h` a pure, path-independent fold that assigns each
active lane exactly one PersistentChainId, with permanent retirement and no
identity reuse?

**Verdict:** YES, with high confidence. The lifecycle fold layers directly on the
frozen Phase-4 topology fold (semantic N-identity, not a second derivation),
enforces the contiguous lane domain via a coherence oracle checked before AND
after every transition, and grounds permanent retirement in monotone `nextChainId`
plus the L2 status oracle. The critical `LaneReuse permitted / PersistentChainIdReuse
forbidden` distinction holds by construction. The within-history vs. cross-branch
`nextChainId` distinction is respected. No frozen invariant is violated.

## Source anchors

- `litenyx/LITENYX_chainid_lifecycle.h:104-123` — `L_h` state
  `{nVersion, nextChainId, activeBindings[], lastLifecycleHeight}` + `operator==`.
- `:128-138` — genesis `L_0` (lane i -> ChainId i, `nextChainId = MIN_CHAINS`).
- `:145-187` — explicit LE serialization + reused frozen SHA256d hash.
- `:195-210` — `LitenyxLifecycleStateCoherent` (the structural oracle).
- `:221-274` — `LitenyxAdvanceChainIdLifecycle` (SPLIT/MERGE/HOLD fold G).
- `:278-293` — `LitenyxChainIdStatus` + `LitenyxClassifyChainId` (L2 oracle).
- `:344-378` — `LitenyxCalculateExpectedLifecycleFromChain` (canonical fold).
- `litenyx/LITENYX_validation.cpp:191-256` — enforcement integration.

## Surface 1 — state + serialization + coherence oracle (PASS)

- **Canonical single encoding.** `activeBindings` kept ascending by laneId;
  serialization is explicit LE with a length byte and per-binding `laneId|chainId`
  (`:145-176`); hash reuses the frozen Phase-4 SHA256d (same platform-independence
  guarantee as G-TA surface A).
- **Coherence oracle is the structural backbone.** `LitenyxLifecycleStateCoherent`
  enforces, for expected count `Nexp`: `Nexp in [MIN,MAX]`,
  `len(activeBindings)==Nexp`, `laneId[i]==i` (contiguous `{0..Nexp-1}` ascending),
  every `chainId < nextChainId` (L2 domain), and chainId uniqueness (L1 bijection)
  (`:198-208`). This is checked on the incoming state (`:233`) AND on every output
  (`:246,257,273`) — no transition can produce an incoherent `L_h`.

## Surface 2 — SPLIT/MERGE fold + lane-vs-identity reuse (PASS; ABA foundation)

- **SPLIT** binds the new highest lane `Nprev -> nextChainId`, then `nextChainId++`
  (`:249-257`). **MERGE** retires exactly lane `Ncur == Nprev-1` (highest active),
  erases its binding, and leaves `nextChainId UNCHANGED` (`:260-273`).
- **Decisive `LaneReuse vs. IdentityReuse` check — HOLDS by construction:**
  a merged lane number can reappear on a later SPLIT, but it is bound to the
  then-current `nextChainId`, never to the retired ChainId (which is `< nextChainId`
  and unbound => permanently `Retired` by the L2 oracle). The sequence
  `Lane 2 -> ChainId 2 -> Retired -> Lane 2 -> ChainId 3` is exactly what the fold
  produces. This is the ABA foundation consumed by Phase 6/7 and is independently
  exercised by Phase-7 D-K14. Classified:
  `LaneReuse permitted AND PersistentChainIdReuse forbidden`.
- **`nextChainId` as permanent retirement memory — sufficient.** No explicit
  retired-set is stored; permanence rests on the triad
  `nextChainId monotone-nondecreasing + ActiveIds < nextChainId + NewId = nextChainId`.
  Within one canonical history this triad holds: `nextChainId` only ever `+= 1` on
  SPLIT and is untouched on MERGE/HOLD (`:255,271,244-246`); the coherence oracle
  guarantees `ActiveIds < nextChainId`; SPLIT sets `NewId = nextChainId` before
  incrementing. Therefore a retired id can never be re-allocated within a history.
- **L3 exhaustion fail-closed:** SPLIT at `nextChainId == 0xFFFFFFFF` returns false
  rather than wrapping/recycling (`:250-251`).

### F-CL-1 (FINDING, distinction preserved) — within-history monotonicity vs. cross-branch comparison
`nextChainId` monotonicity is a **within-canonical-history** invariant, not a
global one. A reorg legitimately reconstructs an earlier prefix with a *lower*
`nextChainId`; since `L_h` is a pure fold of the (new) canonical prefix, this is
correct re-derivation, NOT identity reuse — the abandoned branch's higher
`nextChainId` and its retirements existed only on that abandoned branch. The
implementation supports this precisely because it stores NO cross-branch state:
`LitenyxCalculateExpectedLifecycleFromChain` always folds from `L_0` over the
current canonical blocks (`:353-376`), so branch state never leaks. Classified:
correct; the guard is "no path regresses `nextChainId` *within one history*,"
which SPLIT/MERGE/HOLD structurally satisfy.

## Surface 3 — canonical reconstruction + N-source identity (PASS)

- **Path-independent fold from `L_0`.** `LitenyxCalculateExpectedLifecycleFromChain`
  walks `OBS_WINDOW` boundaries ascending, reconstructing each window from
  canonical `(chainId, weight)` and folding G (`:356-374`). No tracker/cache/
  arrival-order input.
- **Decisive `N_h^{Phase4} == N_h^{Phase5}` check — SEMANTIC IDENTITY, not two
  derivations:** Phase 5 does not consume a separately-supplied Phase-4 value nor
  re-implement the controller; it advances the *actual frozen Phase-4 engine
  inline* — same `LitenyxReconstructMcV1Window` + `LitenyxDeriveTopologyAtBoundary`
  (`:364-365`) — and feeds the resulting `Nprev -> Ncur` scalar into G (`:357,367,371`).
  There is exactly one topology derivation; the lifecycle fold is a strict
  identity-layer *over* it. Any `|d|>1` or non-boundary delta is rejected by G
  (`:236,239`), so an impossible topology smuggled into the chain fails closed.

## Surface 4 — enforcement integration (PASS, fail-closed)

`LitenyxCheckLifecycleCommitment` (`validation.cpp:191-256`): regime from the
frozen INDEPENDENT Phase-5 activation; PreDerivation rejects a premature V3
commitment; else derive expected `L_h` from canonical chain via the SAME
`LitenyxBuildCanonicalBlocks` Phase 4 uses, then pure-verify.

- **Two distinct fail-closed exits:** unreadable/pruned history ->
  `litenyx-lifecycle-reconstruct-unavailable` (`:223-228`); an impossible folded
  transition -> `litenyx-lifecycle-derivation-invalid` (`:230-235`). No fallback to
  tracker/cache/asserted-commitment/default.
- **Soft-advisory containment mirrors G-TA-3:** advisory mismatch is log-only
  (`:245-250`); expected `L_h` is always recomputed from ancestry, so a soft-mode
  mismatch cannot persist into later hard-authority derivation.

## Surface 5 — Phase-4 coupling / contiguous lane domain (PASS)

- **Strongest coherence condition `|activeBindings_h| == N_h` HOLDS** and is
  stronger still: the oracle requires the domain be the exact contiguous prefix
  `{0..N_h-1}` ascending (`:199,204`). Sparse bindings are structurally
  impossible.
- **MERGE-victim determinism is well-founded:** because the domain is a contiguous
  prefix, "retire the highest active lane `== Ncur == Nprev-1`" (`:260-261`) is
  unambiguous. The concern that sparse bindings could make the highest-lane rule
  ambiguous does not arise — coherence forbids sparsity before and after every fold.

## Central-question resolution

YES. `L_h = LifecycleFold(TopologyFold(CanonicalHistory))`:

```
CanonicalHistory -> TopologyFold(N_h) -> LifecycleFold(L_h)
  -> { activeBindings (bijection over {0..N_h-1}), nextChainId (monotone) }
```

Each active lane maps to exactly one chainId (L1 bijection, oracle-enforced);
retirement is permanent (L2 oracle over monotone `nextChainId`, no recycle, L3
fail-closed); lane reuse is permitted while identity reuse is forbidden; the fold
is pure and path-independent; and Phase 5 shares one topology derivation with
Phase 4 (semantic N-identity). Phase 5 adds persistent identity semantics over
Phase 4's reusable-lane topology, exactly as intended.

## Guardrails (doctrine-level; no code change now)

- **G-CL-1** — `nextChainId` monotonicity is a WITHIN-CANONICAL-HISTORY invariant.
  Reorg re-derivation to a lower `nextChainId` on a different canonical prefix is
  correct and MUST NOT be "fixed" by persisting cross-branch retirement memory;
  `L_h` must always be re-folded from `L_0` over the current canonical chain.
- **G-CL-2** — The contiguous-prefix lane domain `{0..N_h-1}` is load-bearing for
  the deterministic MERGE victim. No future change may permit sparse
  `activeBindings`; the coherence oracle must remain gating on every fold I/O.
- **G-CL-3** — Permanent retirement rests on the triad (`nextChainId` monotone +
  `ActiveIds < nextChainId` + `NewId = nextChainId`). Any allocation scheme that
  breaks the triad (e.g. reuse of a freed id, non-`nextChainId` allocation) reopens
  the ABA hazard and is forbidden without a new lifecycle `nVersion`.

## Disposition

All five surfaces PASS; the central question resolves YES. F-CL-1 (within-history
vs. cross-branch — correct as designed) recorded. Guardrails G-CL-1..3 recorded.
No frozen invariant reopened. Proceed to Component 5 (Execution Authority) — where
the Phase-5 -> Phase-6 boundary (`LitenyxValidateExecutionContext` /
`LitenyxClassifyChainId`) becomes the authority projection, already partially
mapped during the Phase-7 critique.

---

# Component 5 — Execution Authority

**Central question:** Does the authority projection deny by default, deriving
MayRoute/MayBind solely from `L_h` with a closed, unambiguous failure taxonomy,
never granting capability the lifecycle layer does not support?

**Non-escalation invariant (the crux):**

```
Capabilities_P6(x, L_h)  ⊆  CapabilitiesPermittedBy_P5(x, L_h)
```

**Verdict:** YES. Phase 6 is a strict projection of Phase-5 lifecycle truth: it
reconstructs nothing of its own, wraps the frozen `LitenyxValidateExecutionContext`
as the sole classifier, projects a total/injective state map, and enforces a
closed F1-F5 precedence with structural/regime guards evaluated BEFORE any
lifecycle-derived verdict. The enforcement hook is ordered strictly after Phase 5
and is non-bypassable. As anticipated, the highest-value output here is a set of
semantic guardrails for future consumers (especially the MayBind capability-vs-
transition distinction), not defects in the current pure projection.

## Source anchors

- `litenyx/LITENYX_execution_authority.h:38-65` — activation (regtest {600,800},
  strictly after Phase-5 {200,400}).
- `:67-108` — `ExecutionAuthorityState`, `ExecutionAuthorityCode` (F1-F5), result.
- `:110-121` — `LitenyxProjectAuthorityState` (total projection).
- `:138-198` — `LitenyxResolveExecutionAuthority` (precedence + capabilities).
- `:200-236` — `LitenyxResolveExecutionAuthorityForLane` (adapter + sentinel).
- `litenyx/LITENYX_validation.cpp:264-335` — enforcement integration.
- `deploy/patches/litenyx-validation.patch:49-85` — ConnectBlock ordering.

## Surface 1 — projection totality (PASS)

`LitenyxProjectAuthorityState` (`:112-121`) maps `Active->AUTHORIZED`,
`Retired->REVOKED`, `Nonexistent->UNKNOWN` over the frozen 3-value
`LitenyxChainIdStatus`, with an unreachable fail-closed `return UNKNOWN`. There is
no fourth state (the header explicitly notes "no DRAINING member (deferred)",
`:69`), which is the Phase-7 D0 anchor: `DRAINING` never became a 4th authority
state. Total and injective on the lifecycle status domain.

## Surface 2 — failure precedence (PASS, operational not merely enum-ordered)

`LitenyxResolveExecutionAuthority` (`:138-198`) evaluates in this ORDER, each
returning immediately:

1. **F5 Malformed** — `laneId >= TOPO_MAX_CHAINS`, BEFORE any lifecycle lookup,
   with `state=UNKNOWN` set explicitly (`:154-158`).
2. **F4 Premature** — `regime == PreDerivation`, still before projection (`:162-166`).
3. **Projection** — only now is `LitenyxValidateExecutionContext` consulted;
   `Unknown`/`Revoked` (`:175-181`) then `Ok`/`WrongLane` by lane agreement
   (`:184-196`).

- **Decisive check — malformed/premature cannot leak a misleading lifecycle
  verdict:** because steps 1-2 return before `LitenyxValidateExecutionContext` is
  ever called, a malformed lane or dormant regime yields `Malformed`/`Premature`
  with `state=UNKNOWN` set as a guard value, NOT a lifecycle-derived
  `UNKNOWN/REVOKED/WrongLane`. Precedence is proven by control flow, not by enum
  numbering.
- **Mutually exclusive & exhaustive:** every path returns exactly one code; the
  enforcement `switch` (`validation.cpp:320-334`) has a `default` fail-closed to
  `malformed`, so no code is unhandled.

## Surface 3 — capability asymmetry (PASS)

- `mayBind = true` on any AUTHORIZED identity, ANY lane (`:185`); `mayRoute`
  additionally requires exact lane agreement `ctx.laneId == laneId` (`:186-189`).
- **WrongLane** sets neither `authorized` nor `mayRoute`, keeps `mayBind=true`, and
  surfaces the AUTHORITATIVE lane (`r.laneId = ctx.laneId`, `:194`).
- **Non-escalation holds:** every capability requires AUTHORIZED, i.e. Phase-5
  `Active`. A `Retired`/`Nonexistent` identity gets `mayBind=mayRoute=false`
  (result default, never set true off the non-Active paths). Thus
  `Capabilities_P6 ⊆ CapabilitiesPermittedBy_P5` by construction — P6 can never
  grant a capability for an identity P5 does not classify Active.

### F-EA-1 (FINDING, HIGH-VALUE guardrail) — MayBind is a capability predicate, not a binding-transition authority
`mayBind=1` means "this AUTHORIZED identity is permitted to bind under the frozen
Phase-6 model" — it is a READ-ONLY predicate over `L_h`. It does NOT, and must
never be read to, authorize mutation of `activeBindings`. Phase 5
(`LitenyxAdvanceChainIdLifecycle`) remains the SOLE owner of lifecycle/identity
mutation, driven by canonical topology deltas — not by any Phase-6 result. If a
future consumer treated `mayBind` as "this operation may mutate bindings," Phase 6
would silently become a lifecycle-mutation authority, violating the layering.
Recorded as G-EA-1.

```
MayBind = capability predicate   ≠   binding-transition authority
```

## Surface 4 — WrongLane safety (PASS)

The claimed lane never becomes authoritative through error handling. On WrongLane
the engine OVERWRITES the echoed claim with the authoritative bound lane
(`r.laneId = ctx.laneId`, `:194`) purely as diagnostic/routing output;
`mayRoute` stays false, so no routing capability is granted on the claimed lane.
The claimed lane is only ever an input to the agreement test, never promoted.

## Surface 5 — adapter sentinel (PASS; depends on the P5 retirement triad)

`LitenyxResolveExecutionAuthorityForLane` (`:219-236`) looks up the
PersistentChainId bound to the asserted lane in `L_h`; if unbound it asserts the
sentinel `assertedChainId = L.nextChainId` (`:228`).

- **Decisive check — sentinel cannot collide with an active or retired identity:**
  by the Phase-5 retirement triad (proven in Component 4), every existing id is
  `< nextChainId`. `LitenyxClassifyChainId(L, nextChainId)` therefore hits
  `chainId >= nextChainId -> Nonexistent` (`chainid_lifecycle.h:288`) with
  certainty. An unbound lane deterministically resolves to `Unknown` (F1), never
  colliding with an Active or Retired id. This is exactly the cross-component
  dependency you flagged, and it holds because Surface 2/Component-4 guarantee
  `ActiveIds ∪ RetiredIds ⊂ [0, nextChainId)`.

## Surface 6 — enforcement composition (PASS, non-bypassable, consume-only)

- **Ordering (from the ConnectBlock hook patch):**
  `ConnectSharedState (patch:49) -> CheckTopologyCommitment (58) ->
  CheckLifecycleCommitment (72) -> CheckExecutionAuthority (85)`. P6 runs strictly
  after P5; a block cannot route around it.

```
ConnectBlock -> P5 LifecycleCheck -> P6 AuthorityCheck -> RemainingValidation
```

- **Consume-only.** `LitenyxCheckExecutionAuthority` reuses the SAME
  `LitenyxBuildCanonicalBlocks` + `LitenyxCalculateExpectedLifecycleFromChain`
  (`validation.cpp:285-294`) — it does not build a parallel lifecycle model — then
  resolves the lane and maps the pure code to a consensus verdict (`:300-334`).
- **Fail-closed** on unreadable history (`litenyx-exec-reconstruct-unavailable`)
  and impossible derivation (`litenyx-exec-derivation-invalid`); SoftAdvisory is
  log-only (`:305-314`), HardAuthority rejects every non-Ok code distinctly.
- **Activation ordering, no gap:** `H_exec_derive >= H_cid_derive` and
  `H_exec_enforce >= H_cid_enforce` (`:38-40`), regtest {600,800} strictly after
  Phase-5 {200,400}. There is no height at which P6 enforces authority against
  lifecycle semantics that are not yet themselves authoritative.

## Central-question resolution

YES. The architecture resolves exactly as:

```
CanonicalHistory -> P4 Topology -> P5 Lifecycle -> P6 AuthorityProjection
  -> { MayBind, MayRoute }
```

Deny-by-default (all capabilities require AUTHORIZED==Active); capabilities are a
strict subset of P5-permitted (non-escalation holds); F1-F5 is closed,
mutually-exclusive, exhaustive, with structural/regime guards ahead of any
lifecycle verdict; the adapter sentinel provably classifies Nonexistent via the
P5 triad; enforcement is ordered after P5, non-bypassable, and consume-only.

## Guardrails (doctrine-level; no code change now)

- **G-EA-1** — `mayBind` is a capability PREDICATE over `L_h`, never authority to
  mutate `activeBindings`. Lifecycle mutation is owned EXCLUSIVELY by Phase 5
  driven by canonical topology deltas. No consumer may treat a Phase-6 result as a
  binding-transition trigger.
- **G-EA-2** — A WrongLane result's surfaced authoritative lane is
  diagnostic/routing information only; the claimed lane must never be promoted to
  authoritative through error handling or retry.
- **G-EA-3** — The adapter's unbound-lane sentinel (`nextChainId`) is correct ONLY
  while the Phase-5 retirement triad holds (G-CL-3). Any change to allocation that
  breaks the triad also breaks this fail-closed classification.
- **G-EA-4** — Phase 6 activation must remain at/after Phase 5
  (`H_exec_* >= H_cid_*`); no future retuning may create a height where authority is
  enforced against non-authoritative lifecycle semantics.

## Disposition

All six surfaces PASS; the central question and non-escalation invariant resolve
YES. The single high-value finding F-EA-1 (capability-vs-transition) is captured as
G-EA-1. Guardrails G-EA-1..4 recorded. No frozen invariant reopened. Proceed to
Component 6 (Draining Overlay) — re-examined here as an ecosystem component,
consuming the frozen boundary at 770496e with the emitter/provenance path still
explicitly OPEN.

---

# Component 6 — Draining Overlay

**Central question:** Does Phase 7 remain a strictly monotonic capability
restriction over Phase 6, without acquiring lifecycle, topology, or
commitment-creation authority?

**Layered non-escalation invariant (the crux):**

```
Capabilities_P7(x) ⊆ Capabilities_P6(x) ⊆ CapabilitiesPermittedBy_P5(x)
```

**Verdict — two-part:**
- **Frozen/proven overlay: MATURE.** The pure engine at `770496e` is a strict
  monotonic restriction over the frozen Phase-6 result, keyed on identity for ABA
  safety, with completion subordinated to actual Phase-5 retirement. It acquires
  no lifecycle/topology/commitment-creation authority. D-K1..D-K18 green (C++11 +
  C++20).
- **Input boundary: intentionally INCOMPLETE (OPEN, not a defect).** The causal
  fact that would make an eligible edge identity *required* to enter DRAINING is
  undefined; the autonomous emitter / provenance predicate is deferred. The
  system is designed-incomplete at exactly this seam.

## Source anchors

- `litenyx/LITENYX_draining_authority.h:81-97` — `DrainCommitment` (identity-keyed)
  + `OperationalCapabilityMode {NORMAL, DRAINING}`.
- `:99-119` — `DrainCapabilityProjection`.
- `:127-136` — `LitenyxIsDraining` (D9 completion = Phase-5 Active).
- `:183-208` — `ValidateDrainCommitmentSemantics` (D11 + semantics-half of D12).
- `:220-242` — `LitenyxProjectDrainCapability` (D0/D1/D2 monotonic overlay).
- `:248-259` — `LitenyxResolveDrainForLane` (full compose).
- `cpp_reference/test/test_litenyx_draining_authority.cpp` — D-K1..D-K18.

## Surface A — frozen/proven overlay (MATURE)

- **D0 — no 4th authority state.** `LitenyxOperationalCapabilityMode` is a MODE of
  an already-AUTHORIZED identity, not a member of `LitenyxExecutionAuthorityState`;
  the Phase-6 enum/result is echoed unchanged (`state = p6.state`, `:225`).
  Cross-checked by Component 5 Surface 1 ("no DRAINING member").
- **D1 — monotonic restriction.** `effMayBind = mayBind_P6 && !draining`;
  `effMayRoute = mayRoute_P6` (`:238-239`). Drain can only REMOVE bind; it never
  touches route. `AUTHORIZED + DRAINING -> (MayBind=0, MayRoute=1)` is exactly
  D-K2.
- **D2 — Phase-6 denial dominance.** `draining = authorized && isDraining`
  (`:231-232`); for REVOKED/UNKNOWN the mode is forced NORMAL and both caps stay
  at the Phase-6 (denied) values (D-K4/D-K5). Drain never manufactures a route on
  a P6-denied identity (D-K3).
- **Non-escalation proven:** `effMayBind` and `effMayRoute` are each a boolean AND/
  identity of the P6 value, so `{Eff} ⊆ {P6}` always (D-K6 exhaustively checks all
  state x draining combinations). Composed with Component 5's
  `Capabilities_P6 ⊆ CapabilitiesPermittedBy_P5`, the full chain
  `P7 ⊆ P6 ⊆ P5-permitted` holds.
- **D9 — completion only via actual Phase-5 retirement.** `LitenyxIsDraining`
  returns Active-classification AND present AND `h >= start` (`:132-135`); the
  instant the frozen merge fold retires the identity, draining ceases with no
  Phase-7 completion event and no commitment-deletion mechanism (D-K7 one-way,
  D-K12 complete-on-retire).
- **D8 / ABA — identity-keyed.** `DrainCommitment` keys on `chainId`
  (PersistentChainId), never a lane; a reused lane later bound to a different
  identity cannot inherit a historical commitment (D-K14, which depends on the
  Component-4 retirement triad).
- **Cannot restore a P6-denied capability:** there is no code path where `draining`
  sets any capability to true; it only ANDs bind toward false. Confirmed by the
  subset property above.

## Surface A negative-authority properties (all HOLD)

```
DrainCommitment  ⇏  MERGE                 (D-K16)
DRAINING         ⇏  ScheduledRetirement   (D9; no RetireHeight, D-K13)
Phase7           ⇏  LifecycleMutation     (engine takes L by const&, D-K8)
Phase7           ⇏  TopologyMutation      (no topology surface consumed/produced)
```

- **`DrainCommitment ⇏ MERGE`:** the overlay has no topology authority; an
  identity may remain DRAINING indefinitely while Active regardless of height
  (D-K16). Drain does not force N down.
- **`DRAINING ⇏ ScheduledRetirement`:** there is NO `RetireHeight` and no committed
  completion height; `DrainCommitment` carries only `{present, chainId,
  drainStartHeight}` (`:81-90`, D-K13). Retirement is emergent from Phase 5, not
  scheduled by Phase 7.
- **`Phase7 ⇏ LifecycleMutation`:** every entry point takes `L` as
  `const LitenyxChainIdLifecycleState&`; D-K8 asserts byte-identical `L` before/
  after consulting the overlay.
- **`Phase7 ⇏ TopologyMutation`:** the engine neither reads a mutable topology
  object nor emits one; it consumes only the frozen P6 result + `L_h` classifier.
- **`merge ⇏ prior drain`** (D10 converse) also holds: an identity may retire with
  no prior commitment (D-K17).

## Surface B — OPEN provenance seam (catalogued, NOT solved)

The pure engine proves the **semantic validation half** of commitment handling:
`ValidateDrainCommitmentSemantics` decides admissibility against canonical state
alone (regime derived, start OBS_WINDOW-aligned, identity Active + AUTHORIZED,
edge-only D11) — this is the currently-representable half of D12 (D-K15, D-K18).

It does NOT prove **canonical provenance/reproduction**: that every validator
would independently reproduce `(id, drainStartHeight)` from canonical state (the
full D12 statement). That is deferred with the emitter.

### DA-OPEN-1 (the missing causal fact)
> What canonical, consensus-visible fact makes an eligible edge identity REQUIRED
> to ENTER draining (not merely eligible to drain)?

Constraints inherited from the frozen findings F-A / F-B (spec v0.2), preserved
here so the OPEN question is not re-answered with superseded proposals:

- **DA-NOGO-1** — Do NOT reintroduce `MERGE_INTENT`: F-B established topology
  commitment carries only `{nVersion,nHeight,nN,nLastTransition}`; there is no
  merge-intent distinct from N-decrement without reopening Phase 4.
- **DA-NOGO-2** — Do NOT reintroduce a committed/derived `RetireHeight`: F-A
  established retirement is emergent (highest lane, only on N-decrement, only at an
  OBS_WINDOW boundary), so no `RetireHeight` is committed. D9/D-K13 depend on this.
- **DA-NOGO-3** — Do NOT assume a `Phase7DrainPressureSnapshot` (or any new
  committed surface) as the answer. It was a proposal, not a mature principle; its
  necessity is itself part of DA-OPEN-1 and must be justified, not presumed.

Resolution order (unchanged from the frozen plan): DA-OPEN-1 must be answered
SPEC-FIRST before any emitter code, daemon hook, or commitment serialization.

## Central-question resolution

YES for the overlay. Phase 7 is a strictly monotonic capability restriction over
Phase 6 and acquires no lifecycle, topology, or commitment-creation authority; all
four negative-authority properties hold, and the layered subset invariant
`P7 ⊆ P6 ⊆ P5-permitted` is proven. The system is intentionally incomplete only
at its INPUT boundary:

```
   ?                DrainCommitment        Pure P7 Overlay         Effective
(OPEN canonical  ->  (input fact)      ->    (PROVEN,         ->   Capabilities
 entry fact,          identity-keyed)         monotonic)
 DA-OPEN-1)
```

## Guardrails (doctrine-level; no code change now)

- **G-DA-1** — Phase 7 is capability-restriction ONLY. No Phase-7 code may mutate
  `activeBindings`, `nextChainId`, topology `N`, or create/serialize committed
  state. (Reinforces G-EA-1: even `effMayBind` is a predicate, and drain only
  narrows it.)
- **G-DA-2** — Completion is defined EXCLUSIVELY as actual Phase-5 retirement
  (D9). No `RetireHeight`, no scheduled retirement, no Phase-7 completion event
  may be introduced (DA-NOGO-2).
- **G-DA-3** — `DrainCommitment` MUST remain keyed on PersistentChainId; keying on
  a lane reopens the ABA hazard (depends on G-CL-3 / retirement triad).
- **G-DA-4** — A true return from `ValidateDrainCommitmentSemantics` is NOT
  autonomous provenance. Until DA-OPEN-1 + the emitter are resolved, no consumer
  may treat semantic admissibility as proof the commitment would be canonically
  reproduced (full D12).

## Disposition

Overlay (Surface A) is MATURE; the central question and layered non-escalation
invariant resolve YES; all four negative-authority properties hold. The provenance
seam (Surface B) is recorded as OPEN design boundary DA-OPEN-1 with no-go
constraints DA-NOGO-1..3 — explicitly NOT a defect in the pure overlay. Guardrails
G-DA-1..4 recorded. No frozen invariant reopened; emitter/provenance remains
deferred as agreed. This completes the frozen dependency stack
(Components 1-6). Next: outward into the broader ecosystem, beginning with
Component 7 (Routing / XCT).

# Component 7 — Routing / XCT

**Nature of this component (read first).** This is the first step OUTWARD from the
frozen consensus-authority stack. Unlike Components 1-6, the subject here is
largely NOT implemented: the frozen stack is consensus-AUTHORITY, not TRANSPORT.
The critique is therefore architectural — it classifies what exists versus what is
design-space, and frames the required authority contract for a hypothetical future
consumer WITHOUT presenting any proposed mechanism as a mature Litenyx principle.

**Central question:** For an execution the frozen stack has already permitted
(`effMayRoute = true`), what canonical transition contract must a future
Routing/XCT consumer satisfy so that acting on that permission cannot escalate
authority, survive retirement, bypass the cross-chain spend invariant, or create
value the protocol forbids?

**Verdict — three-part:**

```
Routing Capability: IMPLEMENTED  |  Routing/XCT Transition: ABSENT  |  Future Authority Contract: XCT-OPEN-1
```

The maturity distinction is the whole point of this component:

```
MayRoute = Capability Predicate  ≠  Routing/XCT Transition Authority
```

`mayRoute`/`effMayRoute` is a READ of authority (a pure boolean), surfaced
read-only over RPC and consumed only as a block accept/reject gate. NO code
anywhere effects a route. DRAINING preserving `effMayRoute` establishes what an
eventual settle consumer MAY do; it does NOT prove such a consumer exists.

## Deliverable 1 — Surface classification (reconnaissance; unchanged)

```
IMPLEMENTED  |  PARTIAL/SCAFFOLD  |  SPEC-ONLY  |  ABSENT
```

| Surface | Class | Anchor |
| --- | --- | --- |
| `mayRoute` capability predicate (Phase-6) | IMPLEMENTED | `LITENYX_execution_authority.h:95,188` (`AUTHORIZED && exact-lane`, pure fold) |
| `effMayRoute` monotone passthrough (Phase-7) | IMPLEMENTED | `LITENYX_draining_authority.h:239` (`effMayRoute = p6.mayRoute`, never altered) |
| Read-only route inspection RPC | IMPLEMENTED (regtest, non-consensus) | `deploy/patches/litenyx-rpc.patch:415,488,513` ("creates NO process-local state, NOT a consensus dependency") |
| Route-capability enforcement (as a gate, not transport) | IMPLEMENTED | `LITENYX_validation.cpp:300-334` (consumes decision to accept/reject a block; performs no routing action) |
| Routing/XCT transition authority (actual cross-lane/cross-chain transfer mechanism) | ABSENT | no source file; `XCT` appears only as an explicit out-of-scope exclusion (`execution_authority_spec §100-101`, `draining_authority_spec:126`, `chainid_lifecycle_spec:700`) |
| Cross-chain receipts / execution-lane routing payloads | SPEC-ONLY (FUTURE) | `topology_authority_spec_v0.1.md:88,664,687` ("(FUTURE) receipt routing / execution lanes") |
| Persistent shared spent-set as cross-chain settle substrate | PARTIAL/SCAFFOLD | `test_litenyx.cpp:74` (invariant proven) but persistence unproven — the pre-existing SSC-OPEN-1 |

## Deliverable 2 — Maturity guardrail confirmed from source

Confirmed directly: `mayRoute` is (1) computed as a pure boolean, (2) surfaced
read-only over RPC, (3) consumed only as an accept/reject block gate. There is no
transition. The Phase-7 settle-only reading is exact: `effMayRoute = p6.mayRoute`
defines what a future consumer MAY do and proves NO such consumer is implemented.

## XCT-OPEN-1 (future transition authority contract)

> What canonical transition contract must a future Routing/XCT consumer satisfy so
> that an execution permitted by `effMayRoute = true`
> (a) routes only through the authoritative `PersistentChainId` and lane
>     established by Phase 5/6,
> (b) cannot create or mutate lifecycle bindings,
> (c) cannot remain valid across retirement or acquire authority through lane
>     reuse,
> (d) cannot bypass the canonical cross-chain spend invariant, and
> (e) cannot create, duplicate, or implicitly bridge value contrary to the
>     protocol's issuance/burn invariants?

**Five clean authority boundaries a real XCT layer MUST NOT cross:**

```
XCT  ⇏  BindingMutation
XCT  ⇏  LifecycleMutation
XCT  ⇏  TopologyMutation
XCT  ⇏  IdentityReuse / ABA
XCT  ⇏  UnauthorizedValueCreation
```

These are the outward projection of the same negative-authority discipline proven
inward for Phase 7 (Component 6 Surface A) — carried forward as REQUIREMENTS on a
not-yet-designed layer, not as proven properties.

## Dependencies

### XCT-DEP-1 — SSC-OPEN-1
Any XCT design relying on the global shared-spend invariant (boundary (d)) must
resolve or explicitly inherit its deterministic reconstruction contract. The
cross-chain double-spend exclusion is proven behaviourally (`test_litenyx.cpp:74`)
but its persistence/reconstruction remains OPEN as SSC-OPEN-1 (Component 1).

### XCT-DEP-2 — Effective Authority (consume, never reconstruct)
The transition MUST consume the effective authority available at its execution
point and MUST NEVER independently reconstruct a weaker routing permission:

```
P5 → P6 → P7 (if applicable) → effMayRoute
```

A consumer that re-derives its own routing permission (rather than consuming the
frozen result) reopens every escalation surface Components 4-6 closed.

### DA-OPEN-1 — conditional upstream dependency (NOT an XCT blocker)
A future XCT consumer depends on Phase-7 effective capability ONLY WHEN a valid
drain commitment exists. The unresolved mechanism that causes an identity to
ENTER draining (DA-OPEN-1, Component 6) is therefore a CONDITIONAL upstream
dependency, not a precondition of core XCT transition semantics:

> Resolution of autonomous drain-entry provenance (DA-OPEN-1) is required before
> DRAINING can become fully enforced in production, but XCT transition semantics
> MAY be specified independently against a supplied, canonically-valid
> effective-authority result. XCT correctness MUST NOT be coupled to the
> intentionally-unresolved Phase-7 provenance problem.

## Value-conservation model — classified OPEN (not derived)

The burn doctrine (`topology_spec_v0.1.md:203-205`, "split/merge touches only
routing/N, never coin issuance"; `consensus_spec_v0.1.md:59` "chain confirmation
is a routing property; spent-state is global") proves that TOPOLOGY MUTATION does
not itself mint or burn value. It does NOT by itself fully specify a future XCT
value-conservation / cross-chain settlement accounting rule. Deriving more than
this from the frozen evidence would overreach.

### XCT-OPEN-2 (value conservation)
> What is the exact cross-chain settlement accounting rule under which an XCT
> transition conserves value consistent with the issuance/burn model (burn-not-
> bridge)? Recorded OPEN; boundary (e) states the PROHIBITION, not the mechanism.

## Guardrails (doctrine-level; no code change; forward-looking)

- **G-XCT-1** — `mayRoute`/`effMayRoute` are capability predicates. No layer may
  treat their truth as a routing action having occurred (reinforces G-EA-1 /
  G-DA-1).
- **G-XCT-2** — Any XCT consumer MUST consume frozen effective authority
  (XCT-DEP-2) and MUST NOT reconstruct a private/weaker routing permission.
- **G-XCT-3** — An XCT transition MUST be keyed on `PersistentChainId` end-to-end;
  lane-keying reopens the ABA hazard (inherits G-CL-3 / G-DA-3).
- **G-XCT-4** — No XCT mechanism proposal may be presented as a mature Litenyx
  principle until XCT-OPEN-1 (contract) and XCT-OPEN-2 (value conservation) are
  answered spec-first.

## Disposition

Routing CAPABILITY is IMPLEMENTED and mature; Routing/XCT TRANSITION authority is
ABSENT — the correct, expected result for a consensus-authority stack that is not
(yet) a transport/settlement layer. The mature authority stack and the not-yet-
designed transport layer are cleanly separated. Recorded: XCT-OPEN-1 (five
boundaries), XCT-OPEN-2 (value conservation), dependencies XCT-DEP-1/2 and the
CONDITIONAL DA-OPEN-1, guardrails G-XCT-1..4. No frozen invariant reopened; no
proposed mechanism elevated to principle.

Live OPEN design boundaries now carried forward: SSC-OPEN-1, DA-OPEN-1, XCT-OPEN-1,
XCT-OPEN-2. Next: continue outward (e.g. RPC surface / mempool-ATMP boundary /
daemon integration) as directed.

# Component 8 — Mempool / ATMP Boundary

**Nature of this component.** Second step outward. The subject is the
pre-consensus transaction-admission path (ATMP / mempool) and whether it enforces
or safely anticipates the authority the canonical `ConnectBlock` path ultimately
enforces — without becoming an independent source of consensus truth.

**Central question:** Does pre-consensus transaction admission enforce or safely
anticipate the same authority constraints that ConnectBlock ultimately enforces,
without becoming an independent source of consensus truth?

**Verdict — three-part:**

```
ATMP Authority Hook: ABSENT | Consensus Safety: PRESERVED | Early Authority Filtering: OPEN
```

**Key architectural result:** Litenyx currently has LATE canonical enforcement
without pre-consensus authority anticipation. Whether early filtering is useful can
be decided later — after Routing/XCT and transaction semantics exist — without
weakening the mature rule:

```
MempoolPolicy ≠ ConsensusAuthority
```

## Deliverable 1 — Surface classification (reconnaissance)

```
IMPLEMENTED  |  PARTIAL/SCAFFOLD  |  SPEC-ONLY  |  ABSENT
```

| Surface | Class | Anchor |
| --- | --- | --- |
| ATMP / `AcceptToMemoryPool` litenyx hook | ABSENT | only `DisconnectBlock` + `ConnectBlock` hunks are patched (`deploy/patches/litenyx-validation.patch:14,39`); no ATMP hunk anywhere |
| Mempool consumption of `PersistentChainId` | ABSENT | no pre-consensus surface reads identity |
| Mempool consumption of Phase-5 lifecycle status | ABSENT | every engine header declares "NO mempool input" |
| Mempool consumption of `mayRoute` / `mayBind` | ABSENT | predicates computed only in `ConnectBlock` (`LITENYX_validation.cpp:300`) |
| Mempool consumption of Phase-7 effective capabilities | ABSENT | Phase-7 engine unhooked entirely (KAT-only) |
| `txmempool.h` include in validation | INCIDENTAL (core context, not authority) | `litenyx-validation.patch:6` is a context line — pre-existing core include, not a `+` addition |
| Consensus-purity exclusion of mempool as an authority input | IMPLEMENTED (as a stated invariant) | `execution_authority_spec:95`, `topology_authority_spec:198,276`, `chainid_lifecycle_spec:129` — mempool MUST NOT feed authority |

## Deliverable 2 — Maturity principle: source-confirmed

```
MempoolPolicy ≠ ConsensusAuthority
```

Reconnaissance yields the STRONGEST form of this principle: the litenyx mempool
authority layer does not exist, so it CANNOT be a source of consensus truth. All
authority is derived exclusively in `ConnectBlock` from committed chain history.
There is no stale-mempool bypass risk because no mempool decision participates in
authority at all — the invariant holds vacuously and strongly.

## Central-question resolution (answered by absence)

ATMP neither enforces NOR anticipates the authority constraints — because no
litenyx ATMP hook exists. This is:

- **Consensus-safe.** The canonical `ConnectBlock` path is the sole, unbypassed
  authority (Components 3-6). Late canonical enforcement is intact.
- **Operationally unanticipated (precisely stated).** ATMP has no Litenyx-specific
  early authority filtering. Therefore, TO THE EXTENT that a future transaction or
  operation exposes sufficient identity/lane semantics before block construction,
  invalid or stale authority-targeted operations MAY consume mempool/relay
  resources before canonical `ConnectBlock` enforcement rejects the resulting
  execution context. The exact exposure depends on the future
  transaction/routing/binding format.

**Caveat preserved (do not overclaim).** Because reconnaissance established only
that ATMP does not CONSUME these authority concepts, we have NOT proven ATMP could
meaningfully classify every case. In particular, `WrongLane` is a
block/execution-context property, not necessarily a transaction-admission
property; some authority outcomes are only knowable at block-validation time.

```
ATMP admission            relay + retain          ConnectBlock reject
(no early filtering)  ->  (resource use)     ->   (canonical, late)
```

Wasted bandwidth, stale-tx accumulation, and DoS pressure are OPERATIONAL
vulnerabilities, not consensus defects.

## OPEN questions (recorded, not answered)

### ATMP-OPEN-1 (advisory admission policy — scoped to determinable classes)
> For transaction or operation classes whose authority requirements are
> determinable from canonical state at admission time, should ATMP apply a
> read-only advisory projection of the applicable Phase-5/6/7 effective authority
> to reject clearly inadmissible operations early, strictly as policy?

(Scoping deliberately avoids assuming every future transaction maps directly to
`mayBind` or `mayRoute`.)

### ATMP-OPEN-2 (the non-authority guarantee)
> If ATMP-OPEN-1 is pursued, what invariant guarantees the advisory check can only
> reject MORE conservatively and can NEVER cause acceptance to imply consensus
> validity, nor let a stale mempool decision bypass `ConnectBlock`?

**No-go constraints:**

- **ATMP-NOGO-1** — mempool acceptance MUST NEVER imply consensus validity
  (`MempoolPolicy ≠ ConsensusAuthority`).
- **ATMP-NOGO-2** — a stale/cached mempool authority read MUST NEVER bypass
  canonical `ConnectBlock` re-derivation (purity invariant I1).
- **ATMP-NOGO-3** — an advisory ATMP check MUST consume the frozen
  effective-authority result (XCT-DEP-2 pattern: `P5→P6→P7→eff`), never
  reconstruct its own weaker permission.

## Guardrails (doctrine-level; no code change; forward-looking)

- **G-ATMP-1** — ATMP filtering, if introduced, is POLICY ONLY; acceptance carries
  no consensus meaning (maps ATMP-NOGO-1).
- **G-ATMP-2** — `ConnectBlock` re-derivation is always authoritative; no cached
  mempool decision may substitute for it (maps ATMP-NOGO-2).
- **G-ATMP-3** — an advisory check consumes the frozen effective authority; it
  never reconstructs a private/weaker permission (maps ATMP-NOGO-3).
- **G-ATMP-4 — Context Availability.** ATMP SHALL NOT invent, predict, or
  synthesize execution context that is only knowable at block-validation time
  merely to perform an early authority check. This prevents a future policy layer
  from fabricating a lane or execution context and accidentally becoming a shadow
  authority engine.

## Disposition

```
ATMP Authority Hook: ABSENT | Consensus Safety: PRESERVED | Early Authority Filtering: OPEN
```

The absence is consensus-correct: the entire authority stack is deliberately
mempool-independent and `MempoolPolicy ≠ ConsensusAuthority` holds vacuously and
strongly. The gap is purely operational (early-rejection / DoS-resistance),
correctly framed as advisory-policy OPEN questions (ATMP-OPEN-1/2) rather than a
missing authority layer. Recorded: ATMP-OPEN-1/2, ATMP-NOGO-1..3, G-ATMP-1..4. No
frozen invariant reopened; no proposed mechanism elevated to principle.

Live OPEN design boundaries now carried forward: SSC-OPEN-1, DA-OPEN-1, XCT-OPEN-1,
XCT-OPEN-2, ATMP-OPEN-1, ATMP-OPEN-2. Next: continue outward (RPC surface /
persistence-recovery / daemon integration) as directed.

# Component 9 — Persistence / Recovery

**Nature of this component.** Third step outward, and the most foundational: it
revisits the most significant unresolved finding (SSC-OPEN-1) and tests whether
the otherwise-mature pure-fold stack survives real node lifecycle events. If
canonical state cannot be reliably recovered, every higher-level daemon
integration inherits that weakness even when its pure engine is correct.

**Central question:** After restart, reindex, crash recovery, or reorg, does every
Litenyx consensus-relevant state converge to exactly the state implied by
canonical history?

```
Restart + Reindex + CrashRecovery + Reorg  ->  State(CanonicalHistory)
```

**Scope distinction (held throughout this component):**

```
Litenyx component-state convergence   vs.   daemon canonical-history recovery
```

Litenyx does not itself prove arbitrary daemon crash recovery. It proves that
GIVEN a correctly recovered canonical chain/index and the required block bodies,
its component state converges. Claims below are scoped accordingly.

**Verdict — three-part:**

```
P4/P5/P6      : Deterministically reconstructed GIVEN canonical history
P7 Overlay    : Stateless; future commitment recovery NOT YET DEFINED (with DA-OPEN-1)
P2 SharedSet  : Cold-start / rebuild convergence OPEN (SSC-OPEN-1 -> PR-OPEN-1)
```

```
No systemic mutable-state recovery problem identified | P2 recovery contract OPEN | P7 future commitment recovery NOT YET DEFINED
```

## Deliverable 1 — Per-family recovery-model classification

```
Persisted Canonical State  |  Deterministically Reconstructed State  |  Ephemeral Runtime State
```

| State family | Model | Anchor |
| --- | --- | --- |
| SharedSpendSet (Phase 2) | Ephemeral Runtime State | in-memory `unordered_map`; NO Serialize/Unserialize, NO disk, NO LoadFromDisk (`LITENYX_sharedstate.h:71-74`); mutated only via `RecordSpend`/`RevertSpend` on Connect/Disconnect |
| TopologyState (Phase 4) | Deterministically Reconstructed | "from CANONICAL CHAIN HISTORY ALONE … never from any persisted topology cache … DisconnectBlock needs NO undo" (`LITENYX_validation.h:43-56`, `LITENYX_topology_authority.h:489`) |
| ChainId Lifecycle (Phase 5) | Deterministically Reconstructed | "Re-derivation from the canonical prefix means DisconnectBlock needs NO lifecycle undo" (`LITENYX_validation.h:71-80`) |
| Execution Authority (Phase 6) | Deterministically Reconstructed (stateless projection) | pure fold over reconstructed `L_h`; "Re-derivation means DisconnectBlock needs NO undo" (`LITENYX_validation.h:95-110`) |
| Draining overlay (Phase 7 engine) | Stateless (N/A today) | engine-only, no committed state, no persistence surface (Component 6) |
| `DrainCommitment` recovery (Phase 7 future) | NOT YET DEFINED | representation/provenance OPEN (DA-OPEN-1); recovery model reviewable only once it becomes consensus-visible |
| TopologyTracker (advisory) | Ephemeral Runtime State (non-consensus) | singleton + `Reset()` trapdoor (Component 2 F-TOPO-3) |

**Critical clarification.** `LitenyxSerialize{Lifecycle,Topology}State` are the
COMMITMENT-HASH PREIMAGES (`LITENYX_chainid_lifecycle.h:145-187` -> `double_sha256`),
NOT a persisted state store. They compute the consensus digest committed in the
block and feed reconstruction VERIFICATION — not disk recovery.

## Deliverable 2 — Convergence, per family (scoped)

| Family | Convergence claim |
| --- | --- |
| Topology / Lifecycle / ExecAuthority | GIVEN the same recovered canonical history and required block bodies, each deterministically converges to the same state WITHOUT component-specific undo or persisted caches. (Not a claim that the Litenyx layer proves arbitrary crash recovery — that depends on daemon canonical-history recovery first.) |
| P7 pure overlay | Stateless: nothing to recover TODAY. `DrainCommitment` recovery is NOT YET DEFINED and will require its own review once representation/provenance exist. |
| SharedSpendSet | Reorg reversibility established IN-RUN (`RecordSpend`/`RevertSpend` symmetry, `sharedstate.h:19-24`, `test_litenyx.cpp:74`). Cold-start (restart / reindex / crash) reconstruction NOT proven — this is SSC-OPEN-1. |

The three reconstructed families satisfy the convergence equation by construction
(pure fold), CONDITIONAL on daemon canonical-history recovery. The SharedSpendSet
cold-start dimension is the sole component-level gap.

## Deliverable 3 — Is SSC-OPEN-1 isolated or systemic?

```
SSC-OPEN-1 is an ISOLATED Phase-2 recovery gap, not a systemic pattern
```

Every Phase-4/5/6 family is reconstructed-from-canonical-history (convergent by
construction given canonical recovery); Phase-7 is stateless today. ONLY the
SharedSpendSet holds consensus-relevant state (`outpoint -> chainId`, enforcing the
cross-chain double-spend invariant) with no serialization / no disk store AND no
proven cold-start reconstruction contract. Its reorg safety IS established; the gap
is specifically the restart / reindex / crash rebuild dimension.

Caveat carried forward: this "isolated today" result must be re-checked if and when
`DrainCommitment` becomes genuinely consensus-visible — a second consensus-relevant
persisted/recoverable object would then exist (see the P7 row above).

## The open question (SSC-OPEN-1, sharpened)

### PR-OPEN-1 (recovery model for SharedSpendSet) — refines SSC-OPEN-1
> What canonical recovery model guarantees that `SharedSpendSet` after startup,
> reindex, crash recovery, and canonical reorganization is EXACTLY equivalent to
> the deterministic fold of canonical multi-chain spend history, while preserving
> reorg reversibility and operating under the protocol's pruning assumptions?

Candidate models to be EVALUATED later (not prescribed here): persistence with
reorg-consistent flush/rollback; full canonical replay on startup/reindex;
authenticated checkpoint + verified replay. The requirement is convergence +
reversibility + pruning-compatibility, not a specific implementation.

**No-go constraints:**

- **PR-NOGO-1** — the chosen model MUST NOT make the spend-set an IRREVERSIBLE
  global flag; reorg-reversibility (`RevertSpend` symmetry) must survive it
  (`sharedstate.h:24`).
- **PR-NOGO-2** — do NOT introduce a persisted cache for the reconstructed
  families (Topology/Lifecycle/ExecAuthority); their purity is a proven asset
  (`validation.h:45,55`), and a cache would create a second, corruptible source of
  truth.
- **PR-NOGO-3** — any recovery mechanism that requires historical block bodies
  MUST explicitly define its compatibility with pruning (it must NOT be presumed
  that reconstruction necessarily requires all bodies). The hooks already
  fail-closed on "reconstruction inputs unavailable / pruned body"
  (`LITENYX_validation.cpp:159,224`); recovery must define pruned-node behavior.

## Guardrails (doctrine-level; no code change)

- **G-PR-1** — SharedSpendSet recovery, whatever the model, preserves reorg
  reversibility (maps PR-NOGO-1).
- **G-PR-2** — reconstructed pure families remain cache-free; canonical
  re-derivation stays the single source of truth (maps PR-NOGO-2).
- **G-PR-3** — recovery is pruning-aware: any body-dependent mechanism defines its
  behavior on a pruned node explicitly (maps PR-NOGO-3).
- **G-PR-4** — if `DrainCommitment` becomes consensus-visible, its
  persistence/reconstruction model gets a dedicated recovery review before
  activation; the P7 engine's current statelessness must not be read as a standing
  recovery guarantee for a future committed object.

## Disposition

```
No systemic mutable-state recovery problem identified | P2 recovery contract OPEN | P7 future commitment recovery NOT YET DEFINED
```

The mature pure-fold stack (P4/P5/P6) converges to canonical-history state under
the full lifecycle GIVEN daemon canonical recovery — no component-specific undo, no
caches. Phase-7's overlay is stateless today, with `DrainCommitment` recovery
explicitly NOT YET DEFINED (tied to DA-OPEN-1) rather than vacuously convergent.
The sole component-level gap is the Phase-2 SharedSpendSet cold-start
reconstruction contract, sharpened as PR-OPEN-1 and confirmed ISOLATED. Recorded:
PR-OPEN-1, PR-NOGO-1..3, G-PR-1..4. No frozen invariant reopened; no implementation
choice prematurely prescribed.

Live OPEN design boundaries now carried forward: SSC-OPEN-1 (== PR-OPEN-1),
DA-OPEN-1, XCT-OPEN-1, XCT-OPEN-2, ATMP-OPEN-1, ATMP-OPEN-2. Next: continue outward
(RPC surface / daemon integration) as directed.

# Component 10 — RPC Surface

**Central question:** Does every Litenyx RPC remain an inspection/test interface
over canonical authority, without becoming a mutation path, alternate authority
source, or consensus dependency?

```
RPCObservation ≠ ConsensusAuthority
```

**Verdict — split three ways (after daemon-framework verification):**

```
P4/P5/P6 Authority RPCs   : Exemplary inspection/test
TopologyTracker Mutators  : Operational / advisory risk
SharedSpendSet Mutators   : LOCAL consensus-state integrity risk (ungated)
```

## Daemon-framework gating verification (decisive precondition)

The base daemon is NOT vendored; it is cloned at build time from
`github.com/dogecoin/dogecoin` (`deploy/Makefile:44`, `--depth 1`). Its RPC
dispatch is standard Bitcoin/Litecoin-lineage `rpc/server.cpp`. Verified behavior
of the generic framework:

- The `CRPCCommand` `okSafeMode` field (set `true` for all 5 litenyx RPCs,
  `litenyx-rpc.patch:38-42`) is a PERMISSIVENESS flag ("allowed during safe
  mode"), NOT a network restriction.
- The RPC category (`"litenyx"`) is a `help`-grouping label with NO dispatch-gating
  effect.
- In this lineage, regtest-restricted methods enforce it INSIDE the RPC body
  (explicit `Params().MineBlocksOnDemand()` / network check). There is NO generic
  registration-level "test command -> regtest-only" gate.

The litenyx RPC bodies contain NO such runtime check: `Params()` appears only in
`litenyx-validation.patch`, never in `litenyx-rpc.patch`. Therefore the decisive
question resolves:

```
A non-regtest daemon CAN dispatch the mutating Litenyx RPC methods.
```

The Python harness invokes them only under `-regtest`
(`tests/regtest/test_litenyx_splitmerge.py`), but that is TEST CONVENTION, not
enforcement. Per the maturity rule (configuration convention < consensus-adjacent
safety boundary), the mutators are treated as UNGATED. F-RPC-1 is retained.

## Deliverable 1 — Per-RPC classification

```
read-only inspection  |  regtest/test driver  |  state mutation  |  production operational
```

| RPC (mode) | Class | Consensus fns? | Local state | Path to `ConnectBlock`? |
| --- | --- | --- | --- | --- |
| `testlitenyxsharedstate query` | read-only inspection | yes (`IsSharedSpent`/`ConfirmingChain`) | reads real singleton | no (read) |
| `testlitenyxsharedstate record`/`revert` | STATE MUTATION | yes (`Record`/`RevertSharedSpend`) | MUTATES real global spent-set | **YES — writes the exact set `ConnectBlock` consumes** |
| `testlitenyxtopology status` | read-only inspection | yes (tracker) | reads advisory singleton | no |
| `testlitenyxtopology observe`/`tick`/`reset` | STATE MUTATION | yes (`Observe`/`Tick`/`Reset`) | MUTATES advisory tracker | advisory only (non-consensus, Component 2) |
| `testlitenyxtopoauthority regime/expected/decide` | regtest/test driver | yes (pure engine) | NONE | no (pure, synthetic chain) |
| `testlitenyxlifecycle regime/expected/decide` | regtest/test driver | yes (pure engine) | NONE | no |
| `testlitenyxexecauthority regime/resolve/resolveid` | regtest/test driver | yes (pure engine + adapter) | NONE | no |

The three AUTHORITY RPCs (Phase 4/5/6) are exemplary: they drive the SAME compiled
functions `ConnectBlock` uses, over a synthetic canonical chain, with NO
process-local state — true inspection/test surfaces that cannot feed consensus.

## Deliverable 2 — Load-bearing invariant, tested per class

```
RPCObservation ≠ ConsensusAuthority
```

- **HOLDS** for P4/5/6 authority RPCs (pure, stateless, synthetic input).
- **HOLDS in consensus terms** for topology mutators — only because the tracker is
  advisory (Component 2). Does NOT hold in operator-perception terms (RPC-OPEN-2).
- **VIOLATED locally** by sharedstate `record`/`revert`: they mutate the REAL
  consensus-relevant global spent-set, with no runtime gate. Global protocol
  consensus RULES are unchanged, but a single node's acceptance BEHAVIOR can be
  perturbed:

```
RPCMutation -> SharedSpendSet -> ConnectBlockDecision
```

An accidental or malicious `record` can cause a node to reject an otherwise-valid
block (outpoint appears already globally spent); a `revert` can drop a spend
canonical history says is recorded — until state happens to be repaired by
subsequent canonical activity. This is a LOCAL consensus-state integrity risk,
strictly more serious than the advisory-tracker mutators, and classified
separately.

## Findings

### F-RPC-1 (FINDING, RETAINED — no runtime network gate)
All 5 RPCs are "regtest-only" IN COMMENTS ONLY. No runtime guard
(`Params().NetworkIDString()` / `MineBlocksOnDemand()`) exists in any body; the
generic Dogecoin framework imposes none via registration/category/`okSafeMode`.
On a main/test daemon these methods are dispatchable.

### F-RPC-2 (FINDING — mutators reachable via RPC)
The Component-2 `Reset()` trapdoor is RPC-reachable (`testlitenyxtopology reset`),
as are `observe`/`tick` and sharedstate `record`/`revert`. Tracker impact is
advisory; `record`/`revert` transiently desynchronize the live spent-set from
canonical history between blocks (see the LOCAL integrity risk above).

### F-RPC-3 (FINDING, GOOD — claimed vs. authoritative presentation)
`testlitenyxexecauthority resolve` surfaces BOTH the asserted lane AND the
authoritative `chainId`/`laneId` from `L_h` (`litenyx-rpc.patch:490-491`), never
presenting the claimed lane as authority. Correct discipline; preserve it.

### F-RPC-4 (FINDING — advisory tracker mistakable for canonical topology)
`testlitenyxtopology status` returns `nChains`/`lastTransition` from the ADVISORY
tracker, while canonical topology is the pure re-derivation
(`testlitenyxtopoauthority expected`). Naming does not signal the advisory vs.
canonical distinction; an operator could read advisory `nChains` as canonical
`N_h`.

## OPEN questions

### RPC-OPEN-1 (production gating contract)
> Before any non-regtest deployment, what gating guarantees the mutating RPCs
> (`testlitenyxsharedstate record/revert`, `testlitenyxtopology
> observe/tick/reset`) are UNREACHABLE — a runtime regtest guard, compile-time
> exclusion (`#ifdef`), or removal — so no operator/RPC action can write
> consensus-relevant singleton state on main/test?

### RPC-OPEN-2 (advisory/canonical presentation contract)
> What RPC-response/naming contract ensures advisory tracker state (`nChains`) can
> never be read as canonical topology (`N_h`) — e.g. explicit `"advisory": true` /
> `"source": "tracker|canonical"` fields?

**No-go constraints:**

- **RPC-NOGO-1** — no RPC result may become an input to `ConnectBlock` authority;
  canonical re-derivation stays the sole source (extends PR-NOGO-2).
- **RPC-NOGO-2** — inspection RPCs MUST NOT present a CLAIMED lane/identity as
  authoritative (preserve F-RPC-3).
- **RPC-NOGO-3** — a "regtest-only" designation MUST be enforced at
  runtime/compile time, NEVER by comment/convention alone (closes F-RPC-1).

## Guardrails (doctrine-level)

- **G-RPC-1** — Litenyx RPCs are read-only by default; any mutator is gated
  (runtime or compile-time) to regtest (maps RPC-NOGO-3, RPC-OPEN-1).
- **G-RPC-2** — no RPC output feeds `ConnectBlock`; consensus stays
  re-derivation-only (maps RPC-NOGO-1).
- **G-RPC-3** — advisory state is explicitly labelled advisory in every response
  (maps RPC-OPEN-2, F-RPC-4).
- **G-RPC-4** — authoritative vs. claimed identity/lane is always distinguished in
  output (maps RPC-NOGO-2, preserves F-RPC-3).
- **G-RPC-EXT-1 (external dependency)** — the daemon RPC framework provides NO
  network gate for these commands; the gate MUST be added by Litenyx (do not rely
  on `okSafeMode`/category/harness convention).

## Disposition

```
P4/P5/P6 Authority RPCs: Exemplary | TopologyTracker Mutators: Operational/advisory | SharedSpendSet Mutators: LOCAL consensus-state integrity risk (ungated)
```

The authority RPCs are model inspection surfaces (same engine, synthetic input, no
local state, cannot feed consensus) — `RPCObservation ≠ ConsensusAuthority` holds
cleanly for them. Daemon-framework verification confirms NO generic gating, so the
mutating RPCs are genuinely reachable outside regtest: the topology mutators are an
operational/advisory risk, and the SharedSpendSet mutators are a LOCAL
consensus-state integrity risk (RPCMutation -> SharedSpendSet ->
ConnectBlockDecision). None alter global protocol consensus rules or create an
alternate authority source. Recorded: F-RPC-1..4, RPC-OPEN-1/2, RPC-NOGO-1..3,
G-RPC-1..4 + G-RPC-EXT-1. No frozen invariant reopened.

Live OPEN design boundaries now carried forward: SSC-OPEN-1 (== PR-OPEN-1),
DA-OPEN-1, XCT-OPEN-1, XCT-OPEN-2, ATMP-OPEN-1, ATMP-OPEN-2, RPC-OPEN-1, RPC-OPEN-2.
Next: Component 11 — Daemon Integration (production composition:
ConnectBlock -> SharedState -> TopologyAuthority -> LifecycleAuthority ->
ExecutionAuthority -> RemainingValidation; rejection atomicity, ordering, disconnect
symmetry, activation boundaries, fail-closed reconstruction, intentional Phase-7
hook absence).

# Component 11 — Daemon Integration

**Central question:** Does daemon integration preserve every component's frozen
authority boundary, with correct ordering, atomic failure, reorg symmetry, and no
bypass path?

**Verdict:**

```
Ordering: CORRECT | Reorg symmetry: CORRECT | Fail-closed: CORRECT | Bypass: NONE
Cross-phase rejection atomicity: GAP (INT-OPEN-1) | P7 production enforcement: NOT YET INTEGRATED (by design)
```

The composition is sound on five of six surfaces. The exception is a real
cross-phase atomicity gap in which `SharedSpendSet` mutates BEFORE a later
Litenyx check rejects, WITHOUT an explicit rollback on that failure path. This is
an integration finding linking Components 1/9/10 — NOT a reason to reopen
P4/P5/P6, whose pure engines remain correct.

## Surface 1 — ConnectBlock ordering (executable sequence)

Actual order (`deploy/patches/litenyx-validation.patch:43-111`), matching spec
`topology_authority_spec:502-514`:

```
1. LitenyxCheckAuxHeader           -> return false on failure
2. LitenyxConnectSharedState       -> COMMITS spends into the global set; false on double-spend
3. LitenyxCheckTopologyCommitment  -> return false on Invalid   [runs AFTER spends committed]
4. LitenyxCheckLifecycleCommitment -> return false on Invalid   [runs AFTER spends committed]
5. LitenyxCheckExecutionAuthority  -> return false on Hard non-Ok [runs AFTER spends committed]
6. TopologyTracker Observe/Tick    -> try/catch contained (advisory, non-consensus)
```

Layering is correct: each authority check runs STRICTLY AFTER its upstream truth
is authoritative (P4 before P5 before P6), closing the "omit the downstream field
to route around the upstream check" bypass (spec §6.2 / §9.1). Consensus-critical
steps 2-5 are OUTSIDE any try/catch; only the advisory tracker (6) is contained.

**But step 2 mutates before steps 3-5 can reject.** `LitenyxConnectSharedState`
records spends into the process-global singleton (`LITENYX_validation.cpp:68-74`)
and returns true; a subsequent Invalid at step 3/4/5 does `return false` with NO
`LitenyxDisconnectSharedState` on that path. See Surface 4.

## Surface 2 — Disconnect symmetry

Correctly asymmetric BY DESIGN:

- **SharedSpendSet** requires explicit undo: `DisconnectBlock` calls
  `LitenyxDisconnectSharedState` (`litenyx-validation.patch:20`), which
  `RevertSpend`s exactly this block's inputs (`LITENYX_validation.cpp:78-88`) —
  the mirror of `ConnectSharedState`.
- **P4/P5/P6** require NO component-specific undo: they reconstruct from the
  canonical prefix, so a reorg re-derives the correct state automatically
  (`validation.h:56,80,110`; Component 9). Confirmed consistent.

This symmetry is correct for the NORMAL reorg path (a block that DID connect, then
gets disconnected). It does NOT cover the FAILED-connect path (Surface 4).

## Surface 3 — Activation composition

Regime ordering holds: each phase selects its own frozen per-network activation
and derives its regime independently; enforcement runs in dependency order
(P4->P5->P6). No downstream authority is enforced before its upstream truth is
authoritative. Phase 7 is explicitly OUTSIDE this production sequence (Surface 6).
Main is DISABLED for all phases (frozen facts), so HardAuthority composition is
exercised on regtest/test only.

## Surface 4 — Failure atomicity (the high-value finding)

Target property:

```
ConnectBlock Reject  =>  No persistent/ephemeral consensus-relevant mutation
                         (or: deterministic rollback of any earlier mutation)
```

- **Phase-2 self-atomicity: HOLDS.** `LitenyxConnectSharedState` is
  check-ALL-then-commit-ALL (`LITENYX_validation.cpp:56-75`): if Phase 2 itself
  rejects, it committed nothing. (Already recorded as Component-1 "what is
  proven".)
- **Cross-phase atomicity: GAP.** Once Phase 2 returns true, the spends ARE in the
  singleton. If step 3/4/5 then returns false, the ConnectBlock hunk performs NO
  `RevertSpend`. Rollback would only occur if the daemon calls `DisconnectBlock`
  for the failed block — but in the Bitcoin/Dogecoin lineage a block whose
  `ConnectBlock` returns false is marked invalid and its `CCoinsViewCache` is
  DISCARDED (never flushed); `DisconnectBlock` is NOT called for a block that never
  successfully connected. Critically, `LitenyxSharedSpendSet` is a SEPARATE
  process-global singleton, NOT part of the discarded CoinsView cache, so its
  leaked spends survive the failure.

**Consequence (LOCAL, not global-consensus):**

```
ConnectSharedState commit  ->  later Litenyx reject  ->  leaked spends persist in singleton
```

A block that passes Phase 2 but fails P4/P5/P6 leaves its inputs marked globally
spent. A later valid block spending those same outpoints could then be wrongly
rejected as a cross-chain double spend, until the singleton is repaired (restart /
reindex rebuild — itself OPEN, PR-OPEN-1). This is the same LOCAL consensus-state
integrity surface as the RPC mutators (Component 10), reached here via the
internal pipeline rather than RPC. It does NOT change global protocol rules and
does NOT reopen P4/P5/P6.

Severity note: reachability depends on a block that is Phase-2-valid yet
P4/P5/P6-invalid at an ACTIVE (Hard) regime. Main is DISABLED, so this is
currently a regtest/test-regime exposure — but it is a genuine composition defect,
not a comment-level concern.

## Surface 5 — Fail-closed reconstruction

Correct. When reconstruction inputs are unavailable (pruned/unreadable body),
`LitenyxBuildCanonicalBlocks` returns false (`LITENYX_validation.cpp:116-118`) and
the hooks fail CLOSED with distinct reasons (`:159,224,287`) — they never fall
back to tracker state, RPC-mutated state, the asserted commitment, defaults, or a
weaker authority. Verified across P4/P5/P6. (Pruning support itself remains a
pre-mainnet requirement; main is DISABLED.)

## Surface 6 — Bypass-path reconnaissance

Clean. Every production call site of the Litenyx hooks was enumerated
(grep across the repo): the five enforcement hooks appear ONLY inside the single
`ConnectBlock` hunk, and `LitenyxDisconnectSharedState` ONLY inside the
`DisconnectBlock` hunk. No alternate validation/import/reindex path invokes them
separately — reindex/IBD/import all funnel through `ConnectBlock`, so they
traverse the same enforcement composition. No second, weaker path exists.

## Phase-7 status (NOT a defect)

```
P7 PureEngine = PROVEN  |  P7 ProductionEnforcement = NOT YET ACTIVATED/INTEGRATED
```

Consistent with the frozen `770496e` boundary and DA-OPEN-1: there is
deliberately no Phase-7 ConnectBlock hook. Recorded as designed-incomplete, not a
composition gap.

## OPEN question

### INT-OPEN-1 (cross-phase rejection atomicity)
> What mechanism guarantees that a block committing `SharedSpendSet` spends in
> step 2 but rejected by a later Litenyx check (step 3/4/5) leaves NO leaked spends
> in the singleton — e.g. (a) reorder so `ConnectSharedState` runs LAST among the
> Litenyx checks, (b) a scoped commit that is rolled back on any subsequent
> `return false`, or (c) make the spend-set part of the discarded CoinsView batch
> rather than a standalone singleton?

**No-go constraints:**
- **INT-NOGO-1** — the fix MUST preserve Phase-2 self-atomicity and reorg
  reversibility (does not regress `RevertSpend` symmetry; extends PR-NOGO-1).
- **INT-NOGO-2** — the fix MUST NOT weaken the P4->P5->P6 ordering or the
  fail-closed / no-bypass properties (Surfaces 1/5/6).
- **INT-NOGO-3** — do NOT solve it by wrapping consensus-critical steps in
  try/catch (steps 2-5 must stay outside try/catch).

## Guardrails (doctrine-level)

- **G-INT-1** — any Litenyx state mutation inside `ConnectBlock` is either the LAST
  consensus-critical step or is transactionally rolled back on any later
  `return false`.
- **G-INT-2** — enforcement ordering (P4->P5->P6, each after its upstream truth)
  is invariant; new phases insert in dependency order.
- **G-INT-3** — every enforcement hook remains reachable from exactly one
  production path (`ConnectBlock`); no second/weaker validation path is introduced.
- **G-INT-4** — Phase-7 production integration, when it comes, is added at the
  frozen ordering position AFTER P6 and gets its own atomicity/rollback review
  (ties DA-OPEN-1, G-DA-*).

## Disposition

```
Ordering CORRECT | Reorg symmetry CORRECT | Fail-closed CORRECT | Bypass NONE | Cross-phase atomicity GAP (INT-OPEN-1) | P7 NOT YET INTEGRATED
```

Daemon integration composes the individually-correct engines with correct
dependency ordering, correct reorg symmetry for connected blocks, fail-closed
reconstruction, and a single unbypassed enforcement path. The one real defect is
cross-phase rejection atomicity: `SharedSpendSet` commits before P4/P5/P6 can
reject, with no rollback on the failed-connect path, producing a LOCAL
consensus-state integrity exposure that links Components 1/9/10 (recorded as
INT-OPEN-1, currently regtest/test-regime given main DISABLED). Phase-7 production
enforcement is intentionally not integrated (frozen `770496e` / DA-OPEN-1), not a
defect. Recorded: INT-OPEN-1, INT-NOGO-1..3, G-INT-1..4. No frozen invariant
reopened; P4/P5/P6 pure engines untouched.

Live OPEN design boundaries now carried forward: SSC-OPEN-1 (== PR-OPEN-1),
DA-OPEN-1, XCT-OPEN-1, XCT-OPEN-2, ATMP-OPEN-1, ATMP-OPEN-2, RPC-OPEN-1, RPC-OPEN-2,
INT-OPEN-1. This completes the outward ecosystem traversal (Components 1-11).
