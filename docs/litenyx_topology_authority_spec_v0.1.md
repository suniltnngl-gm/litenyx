# Litenyx Topology Authority Consensus Specification — v0.1

*Status: DRAFT (Phase 4 substrate). Promotes the Phase-3 dynamic chain-count
control from OBSERVATIONAL/planning state to CONSENSUS-AUTHORITATIVE state. This
document extends `litenyx_topology_spec_v0.1.md` (Phase 3) and is ONLY valid
once the Phase-3 acceptance gate is GREEN and frozen at tag `phase3-green`
(commit `b5574cb`). Phase 4 work branches from `b5574cb` via
`phase4-topology-authority`; `phase3-green` remains a read-only recovery
anchor.*

Phase 4 makes topology **reproducible from committed chain history alone**, binds
a topology commitment into each block, and validates it fail-closed in
`ConnectBlock`. It does **not** promote the Phase-3 `LitenyxTopologyTracker` into
consensus; instead it extracts the deterministic transition mathematics into a
NEW pure consensus function and leaves the tracker as a non-authoritative
observer.

Phase 4 deliberately DEFERS actual `chainId` lifecycle *enforcement* (rejecting
txs on not-yet-created or retired lanes) to a later phase, to keep the Phase-4
consensus delta minimal and auditable. Phase 4 establishes the authoritative
topology state that such enforcement will later depend on.

---

## 0. The single invariant Phase 4 must establish

> **Given identical committed chain history, every honest node MUST derive
> exactly the same topology state `T_h`, and a block whose topology commitment
> does not match the derived `T_h` is CONSENSUS-INVALID.**

Everything below exists to make that statement true and testable.

### 0.1 Corollary — path independence (LOCKED)

> **Topology derivation MUST NOT depend on the order in which a node received
> blocks.** After a reorg or initial block download (IBD), deriving `T_h` from
> the winning chain MUST produce EXACTLY the same `TopologyState` (and therefore
> the same `TopologyStateHash`) as a node that connected those blocks
> sequentially in real time.

Formally, `T_h` is a pure function of the ordered committed block sequence
`[B_0 .. B_h]` of the active chain — never of arrival order, timing, or which
competing branches a node transiently held. This is what makes the commitment
safe to enforce (§9) and rollback deterministic (§7).

---

## 1. Locked principles (carried from Phase 2/3)

1. **1 Blockchain family + 1 Currency + 1 Global Monetary State + N Parallel
   Chains.** Topology authority changes only how `N_h` / the active `chainId`
   set is *derived and committed*; it never creates a new currency or a second
   UTXO universe.
2. **Shared UTXO.** The spent-set remains global and single across all N chains,
   before, during, and after any topology change.
3. **ConsensusCore != RuntimePolicy != WalletPolicy.** Topology derivation is a
   ConsensusCore behavior. No node-local heuristic, mempool state, wall clock,
   RPC observation, or process-local counter may influence authoritative
   topology.
4. Phase-2 invariant survives for ALL valid N:
   `Spend(U, C_i) => NOT Spend(U, C_j)` for `i != j`.
5. **Consensus fails closed; observation fails safe.** (Phase-3 boundary,
   preserved.) A consensus-topology mismatch MUST `return false`. An
   observational tracker exception MUST remain contained by `catch(...)` and
   MUST NOT affect validity.

---

## 2. Architecture: two separate paths

```text
Committed Chain History
        │
        ▼
Pure Consensus Topology Function      LitenyxTopologyTracker
   T_h = F(T_{h-1}, C_h)                     │  (observational)
        │                                    │  Observe()/Tick()
        ▼                                    │  exception → catch(...) allowed
Expected Topology State T_h                  │
        │                                    └── telemetry / planning only
        ├── verify against Block Topology Commitment
        │        mismatch → return false
        ▼
Consensus-valid topology
        │
        ├── (FUTURE) chainId lifecycle enforcement
        ├── shared-state rules (Phase 2)
        └── (FUTURE) receipt routing / execution lanes
```

**Rule:** the Phase-3 transition math (`LitenyxTopoDecide`, `LitenyxTopoApply`,
`LitenyxTopoTransitionHeight`, `LitenyxTopoAggregateLoad`) is REUSED by the pure
consensus function. The tracker CALLS the same math but is never itself the
source of authoritative state.

---

## 3. Canonical `TopologyState`

`TopologyState` (`T_h`) is the authoritative topology at height `h`. It is
serialized canonically and committed per block.

| Field | Type | Meaning |
|-------|------|---------|
| `nVersion` | `uint16` | topology-state schema/epoch version (starts at 1) |
| `nChains` (`N_h`) | `uint8` | active chain count, `MIN_CHAINS <= N_h <= TOPO_MAX_CHAINS` |
| `activeChainIds` | ordered set of `uint8` | the exact lanes that exist at `h` |
| `lastTransitionHeight` | `uint32` | canonical height of the last applied transition (0 if none) |

**Rationale (per review):** committing `N_h` alone is insufficient — `N_h = 8`
does not say *which* eight lanes exist across a history of splits and merges.
The commitment therefore binds a hash of the full canonical `TopologyState`, not
just `N_h`.

### 3.1 Canonical serialization

`SerializeTopologyState(T)` is a fixed byte layout (little-endian, no padding):

```
nVersion (2) || nChains (1) || len(activeChainIds) (1) ||
activeChainIds[0..len-1] (1 each, ascending) || lastTransitionHeight (4)
```

`TopologyStateHash(T) = SHA256(SHA256(SerializeTopologyState(T)))` (double-SHA256,
matching Bitcoin/Dogecoin convention). Ascending-sorted `activeChainIds`
guarantees a single canonical encoding for a given set.

---

## 4. Authoritative transition function `F`

```text
T_h = F(T_{h-1}, C_h)
```

- `T_{h-1}` is the authoritative topology committed by the parent block
  (genesis seeds `T_0 = { version=1, nChains=MIN_CHAINS,
  activeChainIds={0,1}, lastTransitionHeight=0 }`).
- `C_h` is the **consensus input set** for height `h` (see §5). It contains ONLY
  data deterministically derivable from committed block(s).

`F` is pure: `F(T_{h-1}, C_h)` always yields the same `T_h`. It:
1. accumulates the per-chain measurement(s) from `C_h` into the current window,
2. at an observation boundary (`h % OBS_WINDOW == 0`), computes
   `LitenyxTopoDecide(...)` on the window and applies `LitenyxTopoApply(...)`,
   updating `nChains`, `activeChainIds`, and `lastTransitionHeight`,
3. otherwise returns `T_{h-1}` unchanged (advancing only window accumulation).

Split adds the lowest unused `chainId` in `[0, TOPO_MAX_CHAINS)`; merge retires
the highest active `chainId`. (Deterministic lane selection — LOCKED for v0.1;
alternatives are FUTURE.)

---

## 5. `M_c` classification — THE gating decision

Phase 4 CANNOT proceed to enforcement until every input in `C_h` is proven to be
a pure function of committed chain history. Node-local, wall-clock, mempool, or
RPC inputs are DISALLOWED.

> **Determinism is necessary but NOT sufficient.** A metric derived from
> committed data is *reproducible*, but that alone does not make it a *good*
> permanent consensus signal. v0.1 therefore specifies the `M_c` **interface and
> invariants** and marks the exact **production formula as TBD**, to be selected
> and version-locked before `H_topology`.

### 5.1 Derivation pipeline (LOCKED shape; formula slot TBD)

```text
Committed Block Data
        │
        ▼
Canonical Demand Function  D(block)        ← formula TBD before H_topology
        │
        ▼
Per-Block Demand Sample    M_b ∈ [0,100]
        │
        ▼
Deterministic Historical Window            ← [h_obs-W+1, h_obs], committed only
        │
        ▼
Per-Chain Measurement      M_c ∈ [0,100]
        │
        ▼
Pure TopologyTransition(T_prev, M_c, height)
        │
        ▼
Expected T_h
```

This shape lets the Phase-3 `nTx>0` proxy be swapped for the final canonical
`D(block)` **without touching the topology controller** — only `D` changes.

### 5.2 `M_c` interface / invariants (LOCKED for v0.1)

Any admissible `D(block)` MUST satisfy:

- **(I1) Purity.** `D` is a function of committed block data only. No mempool,
  wall clock, RPC, or process-local counter.
- **(I2) Range.** `M_b = D(block) ∈ [0, 100]` (normalized demand pressure),
  matching the controller's existing `M_c` contract.
- **(I3) Path independence.** `D` depends only on the block's own committed
  contents, so windowing over `[B_0..B_h]` is order-independent (§0.1).
- **(I4) Replay equivalence.** Re-deriving `M_b` during IBD/reorg yields the
  identical value produced at original connection.
- **(I5) Version-gated.** The chosen formula is pinned to `TopologyState.nVersion`
  (§3); changing it requires a version bump + a new activation height (§8).

### 5.3 Phase-3 proxy — CLASSIFIED, not frozen

Phase 3 used `M_c = (block has ≥1 non-coinbase tx) ? 50 : 0` in `validation.cpp`.

- **Classification: Phase-3 SYNTHETIC DETERMINISTIC PROXY.** It satisfies (I1),
  (I2), (I3), (I4) — every node reproduces it from `block.vtx` — but it is a
  coarse presence flag, not a demand metric. It is **explicitly NOT** frozen as
  the Phase-4 production `D`. It remains valid ONLY as the observational tracker
  feed and during the pre-derivation regime (§8).

### 5.4 Candidate production metrics (analysis; selection TBD before `H_topology`)

| Candidate | Committed? | Assessment |
|-----------|-----------|------------|
| **Block occupancy / weight** | yes | *SELECTED — see §5.5 for the FROZEN `D_v1`.* Represents consumed execution capacity; directly maps to "demand pressure." Illustrative form: `floor(SCALE * GetBlockWeight / MAX_BLOCK_WEIGHT)`. |
| Transaction count | yes | Deterministic but treats one tiny tx like one maximum-cost tx; poor demand fidelity. Rejected as primary. |
| Fee totals / fee density | yes | Committed, but measures *market conditions* as much as demand; conflates price with load. NOT the primary signal (may be a FUTURE secondary/telemetry input only). |

**Provisional lead:** block occupancy/weight (satisfies I1–I5 and best models
capacity). It is NOT locked in v0.1; the final `D` is selected, justified, and
version-pinned in a spec revision **before** `H_topology`.

### 5.5 Canonical Demand Function — `D_v1` (FROZEN)

**Canonical Demand Function Version 1: normalized consensus block-weight
occupancy, represented using bounded fixed-point integer arithmetic.**

`D_v1` is the pinned production formula selected per §13 step 2. It is stateless
(a pure function of one block) and satisfies invariants (I1)–(I5).

#### 5.5.1 The four pinned items

**(1) Exact canonical weight function.**
`D_v1` MUST call the SAME consensus weight computation the underlying chain
already enforces — Dogecoin's `GetBlockWeight(const CBlock&)`
(`src/primitives/block.h`), checked against `MAX_BLOCK_WEIGHT` in `ConnectBlock`
(`ContextualCheckBlock`, `GetBlockWeight(block) > MAX_BLOCK_WEIGHT`). NO
topology-specific reinterpretation of block size/weight is permitted. The bound
is the consensus constant `MAX_BLOCK_WEIGHT = 4000000` (`src/consensus/consensus.h`).

**(2) `DEMAND_SCALE`.**
```
DEMAND_SCALE = 10000        // fixed-point, 0..10000 == 0.00%..100.00%
```
`M_b ∈ [0, DEMAND_SCALE]`. (This SUPERSEDES the earlier 0..100 controller range;
see §5.5.5 for the controller-scaling reconciliation.)

**(3) Rounding behavior.**
Truncation toward zero (integer floor), integer-only, no floating point:
```
D_v1(block) = floor( GetBlockWeight(block) * DEMAND_SCALE / MAX_BLOCK_WEIGHT )
```
Evaluated as 64-bit integer arithmetic. `GetBlockWeight(block)` is
`0 <= w <= MAX_BLOCK_WEIGHT` for any valid connected block, so
`D_v1 ∈ [0, DEMAND_SCALE]` with no clamp required; a defensive
`clamp(_, 0, DEMAND_SCALE)` is nonetheless applied to tolerate future
constant changes. Intermediate product `w * DEMAND_SCALE` fits int64
(`4e6 * 1e4 = 4e10 << 2^63`).

**(4) Activation / version semantics.**
`D_v1` is bound to `TopologyState.nVersion == 1` (§3). Changing the demand
formula REQUIRES a new `nVersion` and a new activation height (§8). Under staged
activation: pre-derivation uses no demand; the soft and hard regimes both use
`D_v1` for `nVersion == 1`.

#### 5.5.2 Excluded inputs (NORMATIVE)

`D_v1` MUST NOT incorporate: fees, mempool backlog, wall-clock/latency, peer
observations, local CPU/disk pressure, or hash rate. These MAY feed the
observational planner (`LitenyxTopologyTracker`) but can NEVER influence
authoritative topology unless independently committed and consensus-defined in a
future version.

#### 5.5.3 "Accepted demand" caveat (documented, accepted for v1)

Block occupancy measures **accepted** demand, not total demand: a full block
signals sustained pressure but cannot distinguish "slightly oversubscribed" from
"100× oversubscribed" (the excess lives in uncommitted mempool, which is
correctly excluded). This is ACCEPTABLE for v1 because topology transitions
respond to *sustained saturation over a window* (hysteresis + cooldown), not to
instantaneous or uncommitted demand.

#### 5.5.4 Three separate concepts (LOCKED separation)

```text
D_v1(block)   per-block demand function     stateless, 0..DEMAND_SCALE
     ↓
M_c           canonical aggregation over committed samples in the window
     ↓
T_h           resulting authoritative topology
```

`D_v1` is stateless. Windowing/aggregation into `M_c` is a SEPARATE consensus
concept (the deterministic window of §5.1), and `T_h` derivation is a third.
This separation is what makes future versioning safe: a new demand formula
changes only `D`, not the aggregation or the transition machinery.

#### 5.5.5 `M_c_v1` aggregation + controller-boundary scaling (FROZEN)

**Decision: preserve full 0..`DEMAND_SCALE` precision through the ENTIRE window
aggregation, then perform ONE deterministic downscale (floor ÷ 100) at the
controller input boundary.** Per-sample downscaling is FORBIDDEN (it discards
precision at every block).

**The frozen controller contract (unchanged).** `LITENYX_topology.h` consumes,
per chain `c`, a `LitenyxChainObservation.M_c` in 0..100, aggregates to
`A = mean_c(M_c)` (`LitenyxTopoAggregateLoad`, integer floor mean), then decides
with the STRICT inequalities `A > HYST_HIGH (80)` → SPLIT, `A < HYST_LOW (20)` →
MERGE, else HOLD (`LitenyxTopoDecide`, lines 122/124). NONE of this changes.

**The pinned pipeline (per chain `c`, over its committed window `W`):**
```text
D_v1(block_i)                                 0..DEMAND_SCALE  (10000)
        ↓  full-precision window aggregation
M_c_v1(c) := floor( sum_{i in W} D_v1(block_i) / W )    0..DEMAND_SCALE
        ↓  SINGLE downscale at controller boundary
ControllerInput.M_c(c) := floor( M_c_v1(c) / 100 )      0..100
        ↓
LitenyxTopoDecide( {ControllerInput.M_c(c)}, N_h, h_obs, lastTransition )
```

- `W` = canonical sample count for the applicable committed-history window
  (`LITENYX_TOPOLOGY_OBS_WINDOW`, deterministic; §5.1). `W >= 1` always.
- Only `M_c_v1(c)` is downscaled — NOT individual `D_v1` samples, NOT the
  post-mean aggregate `A`. Each chain's window value crosses the 0..100 boundary
  exactly once, before entering the existing `LitenyxChainObservation`.
- **Strict-inequality boundary preserved.** Because `HYST_HIGH=80` with `A > 80`,
  a chain does not push toward SPLIT until its downscaled `M_c(c)` reaches 81
  (i.e. `M_c_v1 >= 8100`); e.g. `M_c_v1=8099 → floor(8099/100)=80 → 80 > 80 =
  false`. This exactly mirrors frozen Phase-3 behavior and is NORMATIVE — do NOT
  reinterpret `HYST_HIGH=80` as `>= 80`.
- Effective conceptual thresholds: `HYST_LOW=20 ≈ M_c_v1 2000`,
  `HYST_HIGH=80 ≈ M_c_v1 8000`, without rewriting the controller.

**Overflow bound (NORMATIVE).** All aggregation uses `int64`.
`sum_{i in W} D_v1 <= W * DEMAND_SCALE`. For the max permitted window this is far
below `2^63` (`10000 * DEMAND_SCALE = 1e8 << 2^63`), so no overflow for any
consensus-permitted `W`. Implementations MUST use a width that guarantees
`sum(D_v1)` cannot overflow for the maximum permitted window.

**Three separate concepts, restated with scaling:**
```text
D_v1        per-block demand           stateless          0..DEMAND_SCALE
M_c_v1      per-chain window aggregate  full precision     0..DEMAND_SCALE
(boundary)  floor(M_c_v1 / 100)         single downscale   0..100
A / T_h     controller mean + decision  frozen Phase-3     0..100 -> topology
```
This is version-pinned to `TopologyState.nVersion == 1`; changing any of
`D_v1`, `M_c_v1`, or the downscale REQUIRES a new `nVersion` + activation height.

### 5.6 Canonical-chain reconstruction (FROZEN — Phase 4B)

**Source of truth: the canonical blockchain ALONE.** The authoritative `M_c_v1`
inputs are reconstructed on demand from committed block data (`CBlockIndex` +
block bodies) by walking the active chain — NEVER from the observational
`LitenyxTopologyTracker`, and NEVER from a persisted topology cache.

**INVARIANT (LOCKED, extends §0.1):**
> The authoritative topology is derivable from the canonical blockchain alone.
> Deleting every optional topology cache/index MUST NOT change consensus results.

**Reconstruction (per boundary height `h`, `h % OBS_WINDOW == 0`):**
```text
active chain (CBlockIndex)
        ↓  walk window [h-W+1, h]   (W = OBS_WINDOW = 100)
per block: (chainId, GetBlockWeight(block))
        ↓  D_v1(GetBlockWeight)  per block, bucketed by chainId
per-chain samples
        ↓  M_c_v1(c) = floor(mean of chain c's D_v1 samples)   [§5.5.5]
mcV1ByChain   (index == chainId; size == N active AT boundary h)
        ↓
LitenyxDeriveTopologyAtBoundary(T_{h-1}, h, mcV1ByChain)
```

- **Zero-block chains** in the window aggregate to `M_c_v1 = 0` (idle),
  identical to `M_c_v1({})`. Blocks whose `chainId >= N_active` are ignored.
- **N is the count active AT the boundary** (itself derived from earlier
  boundaries), so reconstruction and forward derivation use one consistent `N`.
- **Path-independence (§0.1):** reconstruction reads canonical heights in order;
  it never depends on block arrival/connection order. IBD, live connect,
  restart, disconnect/reconnect, and reorg all reduce to deriving from the same
  canonical prefix → identical `T_h` and `TopologyStateHash`.
- **Engine functions:** `LitenyxReconstructMcV1Window(windowBlocks, nActive)`
  and `LitenyxCalculateExpectedTopologyFromChain(state, chainBlocks, tip)` — pure,
  standalone-proven (Phase 4B tests B1–B4).

**Acceleration index (DEFERRED, not Phase 4B).** A dedicated persisted
`height → per-chain weight` index MAY be added LATER purely as a *rebuildable
acceleration* structure, with an explicit `indexed == reconstructed` test. It
MUST NOT become the source of consensus truth.

**Pruning (NORMATIVE consideration before activation).** Canonical
reconstruction requires the block bodies (or at least each block's `chainId` +
`GetBlockWeight`) within `[h-W+1, h]`. A validly pruned node may lack historical
bodies. Therefore, BEFORE any network sets `H_topology` (i.e. before enabling the
hard regime on a prunable network), the minimal per-block topology inputs
(`chainId`, block weight) MUST be persisted in rebuildable consensus metadata
(e.g. in `CBlockIndex`) so reconstruction never depends on prunable bodies. For
Phase 4B (regtest/testnet, full nodes) this is satisfied by present block data;
the pruning-safe metadata is a Phase 4B(3)/pre-mainnet requirement, tracked in
§12.

---

### 5.7 Topology commitment carrier & verification (FROZEN — Phase 4B(3))

The commitment binds a block to the topology the network independently derives.
The AuxHeader **carries** the commitment; it NEVER **defines** authoritative
topology (nodes derive `T_h` from canonical history per §5.6). The carried value
is NEVER an input to derivation.

**Value (FROZEN).** The commitment IS the frozen `TopologyStateHash(T_h)` (§3).
NO separate hash domain is introduced: the canonical 13-byte serialization,
`nVersion` field, `SHA256d` definition, and genesis KAT already establish the
consensus identity, so a second hash would only add surface immediately before
enforcement.

```text
expectedState → CanonicalSerialize13 → SHA256d → TopologyStateHash
aux.topologyCommitment == TopologyStateHash(expectedState)
```

KAT (genesis state, `nVersion=1`):

```text
TopologyStateHash(genesis) = 71667e04205a7150268d09b82c13849ddd2d187cbf73f5d83b2aecea693bfc09
```

**Carrier & wire framing (FROZEN).** `magic` in `LitenyxAuxHeader` doubles as the
**wire-format version**; layout is keyed on it so V0/V1 byte streams are
preserved **byte-for-byte** (there is no outer length delimiter around `nyx_aux`,
which is serialized mid-stream in `CBlockHeader`, so mid-struct field insertion
is forbidden):

| magic | version | layout |
|-------|---------|--------|
| `0` | V0 (non-Litenyx-aware) | existing fields; **no** topology bytes |
| `LITENYX_AUX_MAGIC_V1` (`"LYXX"`) | Phase 2/3 | existing fields; **no** topology bytes |
| `LITENYX_AUX_MAGIC_V2` (`"LYY2"`) | Phase 4 | existing fields **+ 32-byte** `topologyCommitment` |

Serialization reads `magic` first, then conditionally reads the trailing 32
bytes ONLY when `magic == V2`:

```text
V1: [magic][chainId][eventHeight][auxAnchor][splitVector][reserved]            = 56 bytes
V2: [magic][chainId][eventHeight][auxAnchor][splitVector][reserved][commit32]  = 88 bytes
```

Because `magic` is read first, the parser knows the exact byte-width of `nyx_aux`
BEFORE decoding subsequent block fields — the boundary is unambiguous. Layout is
NOT height-dependent: wire parsing answers *"is a commitment present?"*;
activation (§8) answers *"is its presence/absence/value valid at this height?"*.

**Presence is STRUCTURAL, not value-based.** `HasTopologyCommitment() := IsV2()`.
A V2 header with an all-zero commitment is **present** (and simply mismatches
unless zero is the expected hash). Absence == V0/V1 (no bytes on the wire). This
eliminates the zero-sentinel ambiguity. Predicates:

```text
HasKnownMagic() := IsV1() || IsV2()   // recognized Litenyx format
HasTopologyCommitment() := IsV2()     // structural presence
```

Existing `HasMagic()`/`SetMagic()` are retained as V1 aliases, with `HasMagic()`
deliberately redefined to recognize ANY known format (V1 **or** V2) so a V2
header is never misclassified as non-Litenyx by legacy call sites.

**Verifier (FROZEN, PURE).** `LitenyxVerifyTopologyCommitment(regime, present,
commitment, expected) → {Valid, Invalid, AdvisoryMismatch}`. No I/O, no globals.
Separation of concerns: presence = wire format; value = 32 bytes; validity =
regime + expected hash. Outcome table (matches §8/§9):

| Regime | absent (V0/V1) | V2 + match | V2 + mismatch |
|--------|----------------|------------|---------------|
| Pre-derivation | Valid | Invalid (premature) | Invalid (premature) |
| Soft / advisory | Valid | Valid | **AdvisoryMismatch** (warn, not invalid) |
| Hard / authoritative | **Invalid** | Valid | **Invalid** |

The daemon maps `Valid → accept`, `Invalid → return false`, `AdvisoryMismatch →
log only`. This 4B(3) deliverable is PURE; wiring the verdict into `ConnectBlock`
is 4B(4) (§6, §13).

---

## 6. Validation order (`ConnectBlock`)

The authoritative topology check is inserted with a STRICT ordering so it fails
closed and never depends on observational state:

```
1. (existing Dogecoin) contextual + script checks
2. LitenyxCheckAuxHeader(block, prev)              → false on failure
3. LitenyxConnectSharedState(block)                → false on failure   (Phase 2)
4. T_h = CalculateExpectedTopology(T_{h-1}, C_h)   → pure, no I/O
5. VerifyTopologyCommitment(block, T_h)            → false on mismatch  (Phase 4)
6. (FUTURE) chainId lifecycle enforcement using T_h
7. persist/index T_h for this block                (deterministic rollback)
8. LitenyxTopologyTracker::Observe()/Tick()        → catch(...) contained (telemetry)
```

Steps 4–5 are consensus-critical: NO `try/catch` may wrap them. Step 8 (the
Phase-3 tracker) remains inside its observational `catch(...)` boundary.

Pre-activation (§8) behavior for steps 4–5 is defined by the activation rule.

---

## 7. Rollback semantics (`DisconnectBlock`)

Authoritative topology rollback MUST restore `T_{h-1}` from **indexed consensus
state**, not by imperatively reversing a split/merge:

```
1. read the persisted T_{parent} from the topology index (keyed by block hash / height)
2. restore authoritative topology to T_{parent}
3. (existing Phase 2) LitenyxDisconnectSharedState(block)
4. LitenyxTopologyTracker::Disconnect(...)          → catch(...) contained (telemetry)
```

Because `T_h` is committed and indexed, a reorg across any split/merge boundary
deterministically restores the prior authoritative topology with no dependence
on the observational tracker.

---

## 8. Activation semantics (FROZEN — per-network, height-indexed)

Staged activation (per Phase-4 decision) de-risks by proving network-wide
determinism before enforcement. Two per-network heights gate three regimes:

| Regime | Height range | Commitment | Derivation | Validation |
|--------|--------------|-----------|-----------|------------|
| Pre-derivation | `h < H_derive` | absent | none; `T_h = T_0` fixed | legacy behavior |
| Soft / advisory | `H_derive <= h < H_topology` | present, computed + logged | authoritative `T_h` derived + indexed | mismatch WARNED, NOT rejected |
| Hard / authoritative | `h >= H_topology` | MANDATORY | authoritative `T_h` derived + indexed | mismatch/missing → `return false` |

### 8.1 Per-network parameters (FROZEN scheme; values pinnable per network)

Activation is a **per-network consensus parameter**, carried in
`Consensus::Params` (NOT a compile-time global), exactly as other soft/hard forks
are. Two fields:

```text
consensus.nLitenyxTopoDeriveHeight    // H_derive
consensus.nLitenyxTopoTopologyHeight  // H_topology
```

Semantics (NORMATIVE):

- **Disabled state (explicit).** A network is DISABLED when
  `H_derive == LITENYX_TOPO_ACTIVATION_DISABLED`. When disabled, ALL heights are
  Pre-derivation: no derivation, no commitment, legacy behavior — the authority
  engine is dormant. `LITENYX_TOPO_ACTIVATION_DISABLED` is a NAMED sentinel
  meaning "never," NOT a large-but-reachable height. Implementations MUST test
  `== DISABLED`, never `h >= someHugeNumber`.
- **Mainnet = DISABLED in Phase 4.** Mainnet ships with BOTH heights set to
  `LITENYX_TOPO_ACTIVATION_DISABLED`. Enabling mainnet is a DELIBERATE, separate
  release decision (a future spec revision + params change), never an accidental
  side effect of a placeholder height becoming reachable.
- **Ordering invariant.** When enabled, `0 < H_derive <= H_topology`. A network
  MUST NOT set `H_topology` without `H_derive`. `H_derive == H_topology` is
  permitted (no soft window) but discouraged outside tests.
- **Both-disabled coupling.** If `H_derive == DISABLED` then `H_topology` MUST
  also be `DISABLED` (no hard enforcement without derivation). Validated at
  params construction.

### 8.2 Concrete per-network values (FROZEN for Phase 4)

| Network | `H_derive` | `H_topology` | Rationale |
|---------|-----------|--------------|-----------|
| **regtest** | `100` | `300` | Low, deterministic; exercises all three regimes + both boundaries fast in CI. Aligns to `OBS_WINDOW=100` boundaries; `H_topology - H_derive = 200 = COOLDOWN`. |
| **testnet** | `500` | `1500` | Enabled early on a throwaway network for multi-node soft-window confirmation before any mainnet consideration. |
| **mainnet** | `DISABLED` | `DISABLED` | Explicitly OFF in Phase 4; enabled only by a future deliberate release. |

`LITENYX_TOPO_ACTIVATION_DISABLED` is defined in `LITENYX_types.h` /
`LITENYX_topology_authority.h`. Regtest values are chosen so replay/reorg tests
can cross `H_derive-1/H_derive` and `H_topology-1/H_topology` cheaply.

### 8.3 Notes

- The soft window (`H_derive..H_topology`) lets multiple nodes confirm identical
  `TopologyStateHash` before enforcement flips on.
- Activation heights are **deployment policy** around the (already-proven) pure
  mechanism; they do NOT alter `D_v1`/`M_c_v1`/`F`. Changing the *mechanism*
  requires a `nVersion` bump (§3, §5.5); changing *when it enforces* requires
  only new per-network heights.

---

## 9. Failure semantics (hard regime, `h >= H_topology`)

The following are CONSENSUS-INVALID and MUST propagate `return false`:

1. **Missing commitment** where mandatory.
2. **Malformed commitment** (bad length / version / unsortable chainId set).
3. **Incorrect transition**: `commitment != TopologyStateHash(CalculateExpectedTopology(...))`.
4. **Out-of-bounds `N_h`** (`< MIN_CHAINS` or `> TOPO_MAX_CHAINS`).
5. **(FUTURE, next phase)** unknown active `chainId`, premature lane use, or
   retired-lane use.

Observational tracker failures (§6 step 8, §7 step 4) remain contained and MUST
NOT invalidate a block.

---

## 10. Phase-4 acceptance gate (must be GREEN to tag `phase4-green`)

Extends the Phase-3 9/9 suite. All prior gates MUST still pass. New required
tests:

1. **Determinism**: two independent derivations from the SAME committed history
   yield identical `TopologyStateHash` at every height.
2. **Commitment round-trip**: `Serialize → Hash → Verify` accepts the correct
   commitment and rejects every single-bit mutation.
3. **Mismatch rejects**: a block carrying a wrong topology commitment is
   rejected (`return false`) in the hard regime.
4. **Soft regime tolerates**: the same wrong commitment in the soft window is
   accepted but logged.
5. **Reorg restores authority**: disconnecting across a split/merge boundary
   restores `T_{parent}` from the index (not from the tracker).
6. **`M_c` purity (I1/I2)**: derivation uses ONLY committed block data (no
   mempool / time / RPC); proven by a test that varies node-local state and
   asserts an identical `TopologyStateHash`.
7. **Path independence (§0.1, I3/I4)**: derive `T_h` by (a) sequential
   connection and (b) IBD/reorg re-derivation from the same winning chain;
   assert byte-identical `TopologyStateHash` at every height regardless of block
   arrival order.
8. **Boundary preserved**: consensus checks fail closed; an injected tracker
   exception does NOT invalidate a block.

---

## 11. Scope boundary (what Phase 4 does NOT do)

- Does NOT enforce `chainId` lifecycle at the tx level (FUTURE / Phase 5).
- Does NOT add cross-chain receipts or execution-lane routing (FUTURE).
- Does NOT freeze the production `M_c`/`D` formula; v0.1 locks only the `M_c`
  interface/invariants (§5.2). The canonical `D` is selected and version-pinned
  in a spec revision before `H_topology`.
- Does NOT alter block size, reward, supply, or wallet-count controllers
  (remain FUTURE).
- Does NOT promote `LitenyxTopologyTracker` into consensus.

---

## 12. Milestone boundary

```text
phase3-green @ b5574cb   (read-only anchor)
    │  Deterministic topology OBSERVATION on real blocks
    ▼
phase4-green             (this spec)
    │  Pure consensus topology function
    │  Committed + indexed authoritative TopologyState
    │  Fail-closed commitment validation (staged activation)
    │  Tracker remains observational
    ▼
Phase 5+
    chainId lifecycle enforcement · cross-chain receipts · dynamic lanes
```

---

## 13. Implementation sequencing (deliberately incremental)

1. **This commit — documentation only.** No code changes; `ConnectBlock`
   untouched. Establishes the spec and the `M_c` interface/invariants.
2. **Select canonical `D`. — DONE (§5.5).** `D_v1` FROZEN as normalized
   consensus block-weight occupancy (`GetBlockWeight`/`MAX_BLOCK_WEIGHT`,
   `DEMAND_SCALE=10000`, floor rounding, pinned to `nVersion=1`).
   Activation semantics FROZEN (§8): per-network `H_derive`/`H_topology`,
   regtest `100/300`, testnet `500/1500`, mainnet DISABLED sentinel.
3. **Pure consensus function — no `ConnectBlock` hook.** Implement, as pure
   standalone-testable code, the now-FROZEN chain: `D_v1` (§5.5), `M_c_v1`
   full-precision aggregation + single controller-boundary downscale (§5.5.5),
   `TopologyState` serialization/hash (§3), and `CalculateExpectedTopology`
   (reusing the frozen `LitenyxTopoDecide`). Land replay-equivalence and
   path-independence tests (§10.1, §10.7) FIRST.
4. **Add authoritative validation hook.** Only after (3) is green, wire
   `VerifyTopologyCommitment` into `ConnectBlock` (§6) behind staged activation
   (§8) and the topology index into `DisconnectBlock` (§7).
5. **Tag `phase4-green`** when the full gate (§10) is GREEN.
