# Litenyx ChainId Lifecycle Consensus Specification — v0.1

**Status:** DRAFT (docs-only, first Phase 5 deliverable).
**Branch:** `phase5-chainid-authority` (rooted at merged `main @ 686b4d3`).
**Read-only recovery anchors (UNTOUCHED):** `phase2-green @ f5ff75b`,
`phase3-green @ b5574cb`, `phase4-green @ dee0771`.

This document freezes *definitions and contracts only*. It introduces NO engine
code, NO daemon hook, and NO `ConnectBlock`/`DisconnectBlock` change. Per the
Phase-4 lesson (below), nothing here may be tagged green until its
consensus-critical implementation is compiled into the production daemon path and
exercised there in CI.

> **Permanent acceptance invariant (carried from Phase 4, LOCKED):** unit proofs
> and regtests are INSUFFICIENT unless the actual daemon integration path
> containing the new consensus code is compiled and exercised in CI. A phase
> cannot be declared green on test-green alone.

---

## 0. Phase 5 is an identity layer ABOVE frozen Phase 4 topology authority

Phase 5 does **not** reinterpret or modify Phase 4's frozen `TopologyState`
semantics (topology spec §3, §4). `phase4-green @ dee0771` remains valid
**byte-for-byte and behavior-for-behavior**. Phase 5 adds a separate persistent
execution-domain identity *on top of* the frozen topology authority.

```text
Phase 4  Topology Authority
    Active topology positions / lanes
    bounded: [0 .. LITENYX_TOPO_MAX_CHAINS)
    reusable after merge
              │
              │ Phase 5 binding (pure, derived from the SAME canonical history)
              ▼
Phase 5  ChainId Authority
    Persistent execution-domain identities
    monotonically allocated · never recycled
```

### 0.1 Terminology reconciliation (FROZEN)

The value Phase 4 serializes as an element of `TopologyState.activeChainIds` is,
for Phase 5 purposes, a **`TopologyLaneId`** (a *position*). Phase 4 code and
serialization are NOT renamed or changed; this is a conceptual clarification.

| Term | Owner | Domain | Recycling | Meaning |
|------|-------|--------|-----------|---------|
| **`TopologyLaneId`** (= Phase-4 `chainId`) | Phase 4 (FROZEN) | `[0, LITENYX_TOPO_MAX_CHAINS)` | **reusable** after merge | a bounded execution *position* in the current topology |
| **`ChainId`** (a.k.a. `ExecutionChainId`) | Phase 5 (this spec) | monotonic `uint32`, unbounded in principle | **never recycled** | a persistent execution-*domain identity* with a birth and (optional) retirement |

> **INVARIANT L0 (LOCKED).** Topology lanes are bounded, reusable execution
> positions. ChainIds are persistent, non-recycled execution-domain identities.
> **Reuse of a topology lane after retirement MUST allocate a NEW `ChainId`.**
> A retired `ChainId` MUST NEVER transition back to `Active`.

### 0.2 Illustrative lifecycle (NORMATIVE example)

```text
Topology starts (N=2):
  Lane 0 → ChainId 0
  Lane 1 → ChainId 1
  nextChainId = 2

Split (N: 2 → 3):
  Phase 4 activates Lane 2
  Phase 5 allocates ChainId 2      (nextChainId: 2 → 3)
  Lane 2 → ChainId 2

Merge (N: 3 → 2):
  Phase 4 retires Lane 2
  Phase 5 retires ChainId 2 PERMANENTLY

Later split (N: 2 → 3):
  Phase 4 may activate Lane 2 AGAIN     ← lane reuse allowed (Phase 4 rule)
  Phase 5 allocates ChainId 3           ← ChainId 2 is NOT reused
  Lane 2 → ChainId 3                    (nextChainId: 3 → 4)
```

### 0.3 The single invariant Phase 5 must establish

> **Given authoritative `TopologyState_h` (Phase 4) and identical committed chain
> history, every honest node MUST derive exactly the same `ChainIdLifecycleState`
> at height `h`, and a transaction/block asserting an execution `ChainId` that is
> not a valid active execution domain at `h` is CONSENSUS-INVALID (once Phase 5
> enforcement is active).**

### 0.4 Corollary — path independence (LOCKED, extends topology spec §0.1)

`ChainIdLifecycleState_h` is a pure function of the ordered committed block
sequence `[B_0 .. B_h]` of the active chain — never of arrival order, timing, or
transiently-held branches. After a reorg or IBD, deriving the lifecycle from the
winning chain MUST produce EXACTLY the same active `LaneId → ChainId` bindings,
retirement set, and `nextChainId` as a node that connected those blocks
sequentially in real time.

```text
LiveReplay(chain) == IBDReplay(chain)
Undo(Transition_h) → LifecycleState_{h-1}
```

A reorg across a split OR merge boundary MUST reconstruct exactly the same active
`ChainId` set as fresh replay.

---

## 1. Locked principles (carried from Phase 2/3/4)

1. **1 Blockchain family + 1 Currency + 1 Global Monetary State + N Parallel
   Chains.** ChainId lifecycle changes only how persistent execution-domain
   identities are *derived, committed, and validated*; it never creates a new
   currency or a second UTXO universe.
2. **Shared UTXO.** The spent-set remains global and single across all execution
   domains, before/during/after any split, merge, creation, or retirement.
3. **`ConsensusCore != RuntimePolicy != WalletPolicy`.** ChainId lifecycle
   derivation is a ConsensusCore behavior. No node-local heuristic, mempool
   state, wall clock, RPC observation, or process-local counter may influence
   the authoritative lifecycle.
4. Phase-2 shared-spend invariant survives for ALL valid execution domains:
   `Spend(U, C_i) => NOT Spend(U, C_j)` for `i != j`, where `C` now denotes a
   persistent `ChainId`.
5. **Consensus fails closed; observation fails safe.** A lifecycle/execution
   mismatch (once active) MUST `return false`. Any observational bookkeeping
   exception MUST remain contained by `catch(...)` and MUST NOT affect validity.
6. **Phase 4 is frozen.** Phase 5 MUST NOT change Phase-4 serialization, hashing,
   genesis KAT, `D_v1`/`M_c_v1`/`F`, activation constants, or the `LitenyxAuxHeader`
   V1/V2 wire framing. Phase 5 carries its own commitment (§6) and its own
   activation (§8).

---

## 2. Architecture: lifecycle is derived, execution is validated

```text
Committed Chain History
        │
        ▼
Phase 4 Pure Topology Function  ──►  TopologyState T_{h-1}, T_h   (FROZEN)
        │
        ▼
Phase 5 Pure Lifecycle Function
   L_h = G(L_{h-1}, T_{h-1}, T_h, h)          [§4]
        │
        ├── ChainIdLifecycleState L_h  (active LaneId→ChainId, retired set, nextChainId)
        │
        ├── commit hash of L_h  ──► verify vs block commitment  [§6]  → mismatch: return false
        │
        ▼
ValidateExecutionContext(L_h, chainId, h, version) → ValidatedExecutionContext | Invalid  [§5]
        │
        └── (Phase 5 → Phase 6 authority boundary; Phase 6 CONSUMES, never rediscovers)
```

**Rule:** the Phase-4 transition (`F`, `LitenyxTopoDecide`/`Apply`, boundary
timing) is REUSED, not re-derived. Phase 5 reads the `(T_{h-1} → T_h)` delta and
translates lane activations/retirements into ChainId allocations/retirements. It
never re-computes demand, windows, or split/merge decisions.

---

## 3. Canonical `ChainIdLifecycleState`

`ChainIdLifecycleState` (`L_h`) is the authoritative persistent-identity state at
height `h`. It is serialized canonically and committed per block (§6).

| Field | Type | Meaning |
|-------|------|---------|
| `nVersion` | `uint16` | lifecycle-state schema/epoch version (starts at 1) |
| `nextChainId` | `uint32` | monotonic allocation counter; the next `ChainId` to hand out; only ever increases |
| `activeBindings` | ordered list of `(LaneId: uint8, ChainId: uint32)` | the active `TopologyLaneId → ChainId` map at `h`, sorted ascending by `LaneId` |
| `lastLifecycleHeight` | `uint32` | canonical height of the last applied allocation/retirement (0 if none) |

**Rationale.** `activeChainCount` (Phase-4 `N_h`) alone can no longer derive the
authoritative ChainId set (invariant L0): a merge-then-split reuses a `LaneId`
but MUST NOT reuse a `ChainId`. The lifecycle therefore binds the full active
`LaneId → ChainId` map plus the monotonic `nextChainId`, all reconstructible from
canonical history.

**Consistency invariants (NORMATIVE, checked at derivation and validation):**
- `activeBindings` LaneId set == `T_h.activeChainIds` (the Phase-4 lane set).
  Phase 5 active identity is a *relabeling* of the frozen Phase-4 lane set, never
  a different cardinality.
- Every `ChainId` in `activeBindings` is `< nextChainId`.
- `activeBindings` is sorted ascending by `LaneId`; each `LaneId` appears once.
- `nextChainId` is monotonic non-decreasing across `h`.
- No `ChainId` ever appears in two distinct `activeBindings` across all heights
  once retired (persistence of identity; §4).

### 3.1 Canonical serialization

`SerializeLifecycleState(L)` is a fixed byte layout (little-endian, no padding):

```
nVersion (2) || nextChainId (4) || len(activeBindings) (1) ||
  for each binding ascending by LaneId: LaneId (1) || ChainId (4) ||
lastLifecycleHeight (4)
```

`LifecycleStateHash(L) = SHA256(SHA256(SerializeLifecycleState(L)))`
(double-SHA256, matching Bitcoin/Dogecoin + topology spec §3 convention). The
self-contained hasher (`litenyx_detail::double_sha256`) from Phase 4 is REUSED.
Ascending-by-`LaneId` ordering guarantees a single canonical encoding.

> A genesis KAT (`LifecycleStateHash(L_0)`) MUST be pinned when serialization is
> frozen in the engine step, exactly as the topology genesis KAT was pinned.

---

## 4. Authoritative lifecycle function `G` (pure)

```text
L_h = G(L_{h-1}, T_{h-1}, T_h, h)
```

- `L_{h-1}` is the authoritative lifecycle committed by the parent block.
  Genesis seeds:
  `L_0 = { version=1, nextChainId = MIN_CHAINS,
           activeBindings = { (lane 0 → ChainId 0), (lane 1 → ChainId 1) },
           lastLifecycleHeight = 0 }`
  (i.e. the initial `MIN_CHAINS` lanes map to ChainIds `0..MIN_CHAINS-1`).
- `T_{h-1}`, `T_h` are the FROZEN Phase-4 topology states (topology spec §3/§4).

`G` is pure: `G(L_{h-1}, T_{h-1}, T_h, h)` always yields the same `L_h`. It:

1. Computes the lane delta `Δ = T_h.activeChainIds − T_{h-1}.activeChainIds`:
   - **Added lanes** `A = T_h \ T_{h-1}` (a split activated these lanes).
   - **Removed lanes** `R = T_{h-1} \ T_h` (a merge retired these lanes).
2. **Retirement first, then allocation** (deterministic ordering, LOCKED):
   - For each `lane ∈ R` (ascending): remove `(lane → C)` from `activeBindings`;
     mark `C` retired permanently. `C` is NEVER returned to `activeBindings`.
   - For each `lane ∈ A` (ascending): allocate `ChainId = nextChainId`, append
     `(lane → nextChainId)`, then `nextChainId += 1`.
3. If `Δ` is non-empty, set `lastLifecycleHeight = h`; otherwise `L_h = L_{h-1}`
   unchanged.

> **Allocation determinism (LOCKED).** When a single boundary adds multiple
> lanes, ChainIds are allocated in ascending-`LaneId` order. This makes the
> `LaneId → ChainId` assignment a pure function of the topology delta.

> **No standalone creation/retirement.** In v0.1, ChainId creation and retirement
> occur ONLY as a consequence of a Phase-4 lane activation/retirement. There is
> no independent "create chain" or "retire chain" transaction. (Reserved as
> FUTURE; see §11.)

---

## 5. Execution-context validation (Phase 5 → Phase 6 authority boundary)

```text
ValidationResult<ValidatedExecutionContext>
ValidateExecutionContext(
    const ChainIdLifecycleState& lifecycle,   // L_h, authoritative
    uint32_t chainId,                          // asserted execution ChainId
    uint32_t height,                           // h
    uint32_t consensusVersion);                // for staged/versioned rules
```

`ValidateExecutionContext` is PURE and is the SOLE Phase 5 → Phase 6 authority
boundary. Phase 6 MAY consume a `ValidatedExecutionContext` later but MUST NEVER
rediscover topology or lifecycle. A `ValidatedExecutionContext` carries at least
`{ chainId, laneId, height }` and is constructible ONLY via this function.

### 5.1 Fail-closed validation matrix (once Phase 5 enforcement is active)

| Case | Condition | Result |
|------|-----------|--------|
| **Valid active** | `chainId` is bound to some `laneId` in `L_h.activeBindings` | `ValidatedExecutionContext{chainId, laneId, h}` |
| **Not yet created** | `chainId >= L_h.nextChainId` | Invalid — premature/unknown ChainId |
| **Retired** | `chainId < L_h.nextChainId` and NOT in `activeBindings` | Invalid — retired ChainId, never re-usable |
| **Malformed** | `chainId` fails structural bounds for its epoch/version | Invalid — malformed |
| **Boundary height** | evaluate against `L_h` for the block's own height `h` (see §5.2) | per rows above |

> A "retired" ChainId and a "not yet created" ChainId are BOTH invalid but
> DISTINGUISHABLE (retired `< nextChainId`, uncreated `>= nextChainId`). The
> distinction is diagnostic; both fail closed.

### 5.2 Boundary-height semantics (FROZEN)

Phase 5 inherits Phase-4 boundary timing with NO off-by-one divergence:

> **Block at height `h` is validated against `L_h`** — the lifecycle state
> derived by applying `G` for height `h` (i.e. including any transition whose
> boundary is `h`). Because Phase 4 commits `T_h` *in block `h`*, a lane
> activated at boundary `h` yields a `ChainId` that is **valid for use starting
> in block `h`** (the same block that commits it). Likewise a lane retired at
> boundary `h` makes its `ChainId` invalid for use **in block `h`** and forever
> after. Retirement and activation are governed by the identical rule; there is
> no separate `h` vs `h+1` treatment for the two directions.

This choice keeps `ValidateExecutionContext` consistent with the committed
`LifecycleStateHash` for the same block (no state can be "committed but not yet
usable").

---

## 6. Lifecycle commitment carrier & validation order

### 6.1 Carrier (to be frozen in the engine step; scheme LOCKED here)

Phase 5 commits `LifecycleStateHash(L_h)`. To preserve the FROZEN Phase-4
`LitenyxAuxHeader` V1/V2 framing byte-for-byte, Phase 5 introduces a NEW wire
version rather than mutating V2:

- **`V3` (`LITENYX_AUX_MAGIC_V3`)** = V2 layout + a trailing 32-byte
  `lifecycleCommitment`. `HasLifecycleCommitment() := IsV3()` (structural, not a
  zero sentinel), mirroring the Phase-4 `HasTopologyCommitment() := IsV2()`
  ruling. V1/V2 blocks remain byte-identical and carry no lifecycle bytes.

> The exact byte length of V3 and its KAT are pinned when the carrier is
> implemented. No Phase-4 struct field changes; `magic` remains the sole
> wire-version discriminator (Phase-4 RULING 2, preserved).

### 6.2 Validation order in `ConnectBlock` (ENFORCEMENT DEFERRED to a later step)

When implemented, Phase 5 enforcement is CONSENSUS-CRITICAL and MUST NOT be
wrapped in `try/catch`. It runs AFTER the frozen Phase-4 topology commitment
check, deriving `L_h` from canonical history alone:

```
... (Phase 2 shared-state) ...
... (Phase 4 LitenyxCheckTopologyCommitment)     [FROZEN, must pass first]
Phase 5:
  1. regime = LitenyxChainIdActivationRegimeAt(h)          [§8]
  2. derive T_{h-1}, T_h from canonical chain (reuse Phase-4 reconstruction §5.6)
  3. L_h = G(L_{h-1}, T_{h-1}, T_h, h)                     [pure, §4]
  4. verify block's lifecycle commitment vs LifecycleStateHash(L_h)  [regime §8]
  5. for each execution assertion in the block:
        ValidateExecutionContext(L_h, chainId, h, version)  → Invalid ⇒ return false
... (observational bookkeeping, if any) → catch(...) contained
```

`L_{h-1}` is itself derived from the canonical prefix (never a process-local
cache/tracker), preserving path-independence (§0.4). No process-local lifecycle
state means `DisconnectBlock` needs NO lifecycle undo: reorg rollback is
automatic via re-derivation of the new canonical prefix (mirrors the Phase-4
DisconnectBlock ruling).

---

## 7. Rollback / reorg semantics (`DisconnectBlock`)

Because `L_h` is a pure function of the canonical prefix (§0.4, §6.2), authority
rollback across ANY split/merge/creation/retirement boundary is deterministic via
re-derivation. Phase 5 creates NO process-local lifecycle state, so there is no
imperative lifecycle undo to perform.

Required invariants:
```
LiveReplay(chain) == IBDReplay(chain)             (byte-identical LifecycleStateHash at every h)
Undo(Transition_h) → LifecycleState_{h-1}
Reorg across split/merge → identical active ChainId set as fresh replay
```

Any observational lifecycle bookkeeping (if later added) fails contained via
`catch(...)` and MUST NOT affect validity.

---

## 8. Activation semantics (staged, INDEPENDENT of Phase 4)

Phase 5 enforcement MUST NOT be assumed to activate at Phase-4 `H_topology`.
ChainId lifecycle enforcement has its OWN per-network, height-indexed activation
carried in `Consensus::Params`, following the exact Phase-4 scheme (topology spec
§8):

```text
consensus.nLitenyxChainIdDeriveHeight    // H_cid_derive
consensus.nLitenyxChainIdEnforceHeight   // H_cid_enforce
```

Three regimes, gated by two heights:

| Regime | Height range | Commitment | Derivation | Validation |
|--------|--------------|-----------|-----------|------------|
| Pre-derivation | `h < H_cid_derive` | absent | none | legacy behavior |
| Soft / advisory | `H_cid_derive <= h < H_cid_enforce` | present, computed + logged | `L_h` derived | mismatch/invalid-context WARNED, NOT rejected |
| Hard / authoritative | `h >= H_cid_enforce` | MANDATORY | `L_h` derived | mismatch/missing/invalid-context → `return false` |

Rules (NORMATIVE, mirroring Phase 4 §8.1):
- **Disabled sentinel.** `H_cid_derive == LITENYX_CHAINID_ACTIVATION_DISABLED`
  (a NAMED "never" sentinel) ⇒ dormant on all heights. Test `== DISABLED`, never
  `h >= someHugeNumber`.
- **Mainnet = DISABLED in Phase 5.** Enabling mainnet is a deliberate future
  release decision.
- **Ordering.** When enabled, `0 < H_cid_derive <= H_cid_enforce`.
- **Both-disabled coupling.** `H_cid_derive == DISABLED ⇒ H_cid_enforce == DISABLED`.
- **Dependency on Phase 4.** Phase 5 derivation requires authoritative topology,
  so a network MUST NOT set `H_cid_derive` earlier than its Phase-4 `H_derive`
  (validated at params construction). Enforcement (`H_cid_enforce`) SHOULD be at
  or after Phase-4 `H_topology`.

Concrete per-network values are pinned when the engine + hook are implemented
(regtest chosen to cross both boundaries cheaply in CI; mainnet DISABLED).

---

## 9. Failure semantics (hard regime, `h >= H_cid_enforce`)

The following are CONSENSUS-INVALID and MUST propagate `return false`:

1. **Missing lifecycle commitment** where mandatory.
2. **Malformed lifecycle commitment** (bad length / version / unsortable
   bindings / `ChainId >= nextChainId` in `activeBindings`).
3. **Incorrect lifecycle**: `commitment != LifecycleStateHash(G(...))`.
4. **Lane/identity divergence**: `activeBindings` LaneId set `!= T_h.activeChainIds`.
5. **Invalid execution context**: any asserted `chainId` that is not a valid
   active execution domain per §5.1 (not-yet-created, retired, or malformed).
6. **Non-monotonic `nextChainId`** or a recycled `ChainId` (violates L0).

Observational bookkeeping failures remain contained and MUST NOT invalidate a
block.

---

## 10. Phase-5 acceptance gate (must be GREEN to tag `phase5-green`)

Extends the Phase 4 gate. ALL prior gates (Phase 1/2/3/4) MUST still pass. New
required proofs:

1. **Golden vectors**: pinned `LifecycleStateHash` for `L_0` and for a scripted
   split/merge/split history (exercising lane reuse ⇒ new ChainId).
2. **Persistence (L0)**: a merge-then-split history NEVER reuses a `ChainId`;
   retired ChainIds never return to `activeBindings`; `nextChainId` strictly
   increases at each allocation.
3. **Lane/identity coherence**: at every `h`, `activeBindings` LaneId set equals
   the frozen Phase-4 `T_h.activeChainIds`.
4. **Boundary semantics (§5.2)**: a ChainId activated at boundary `h` is valid
   in block `h`; a ChainId retired at boundary `h` is invalid in block `h`.
5. **Fail-closed matrix (§5.1)**: valid-active accepted; not-yet-created,
   retired, and malformed all rejected in the hard regime.
6. **Soft regime tolerates**: the same invalid context in the soft window is
   accepted but logged.
7. **Purity (I1/I2)**: derivation uses ONLY committed block data (no
   mempool/time/RPC); varying node-local state yields identical
   `LifecycleStateHash`.
8. **Path independence / IBD (§0.4)**: `LiveReplay == IBDReplay` — byte-identical
   `LifecycleStateHash` at every height regardless of block arrival order.
9. **Reorg restores authority**: disconnecting across a split/merge boundary and
   re-deriving yields exactly `L_{parent}` and the same active ChainId set as
   fresh replay.
10. **Boundary preserved**: consensus checks fail closed; an injected
    observational exception does NOT invalidate a block.
11. **COMPILED-AND-EXERCISED (LOCKED)**: the Phase-5 consensus code is compiled
    into `dogecoind` via the production patch path AND exercised on the real
    consensus path in CI (regtest crossing `H_cid_derive` and `H_cid_enforce`),
    not merely in standalone proofs. Without this, `phase5-green` MUST NOT be
    tagged.

---

## 11. Scope boundary (what Phase 5 does NOT do)

- Does NOT introduce Phase 6 `SlotId`, `W_t` (wallet-count controller),
  attribution, or obligation accounting.
- Does NOT add standalone "create chain" / "retire chain" transactions; ChainId
  birth/retirement is derived SOLELY from Phase-4 lane activation/retirement in
  v0.1 (independent lifecycle transactions are FUTURE).
- Does NOT change Phase-4 serialization, hashing, genesis KAT, `D_v1`/`M_c_v1`/`F`,
  activation constants, or V1/V2 aux framing.
- Does NOT add cross-chain receipts, execution-lane routing payloads, or
  `AttributedTxContext` provenance accounting beyond producing the
  `ValidatedExecutionContext` boundary object (attribution consumption is
  Phase 6+).
- Does NOT alter block size, reward, supply, or wallet controllers.

---

## 12. Milestone boundary

```text
phase4-green @ dee0771   (read-only anchor)
    │  Pure consensus topology authority (LaneId positions, reusable)
    ▼
phase5-green             (this spec)
    │  Persistent, non-recycled ChainId lifecycle derived from topology deltas
    │  Committed + hashed authoritative ChainIdLifecycleState (V3 carrier)
    │  Pure ValidateExecutionContext → Phase 5→6 authority boundary
    │  Fail-closed validation (staged, independent activation)
    ▼
Phase 6+
    SlotId · W_t wallet-count controller · attribution · obligation accounting
```

---

## 13. Implementation sequencing (mirrors the proven Phase-4 discipline)

1. **This commit — documentation only.** No code; `ConnectBlock` untouched.
   Freezes L0, the LaneId/ChainId reconciliation (§0), `ChainIdLifecycleState`
   (§3), `G` (§4), `ValidateExecutionContext` (§5), staged activation (§8), and
   the acceptance gate (§10).
2. **Pure engine + golden vectors — no hook.** Implement `L_0`, `SerializeLifecycleState`
   / `LifecycleStateHash` (pin genesis KAT), `G`, and `ValidateExecutionContext`
   as standalone-testable pure code. Land golden-vector, persistence,
   lane-coherence, and path-independence proofs FIRST.
3. **Carrier.** Add the `V3` lifecycle-commitment carrier (§6.1) with framing +
   KAT proofs; V1/V2 remain byte-identical.
4. **Daemon hook.** Only after (2)+(3) are green, wire Phase-5 derivation +
   commitment verification + `ValidateExecutionContext` into `ConnectBlock`
   (§6.2) behind staged, independent activation (§8). Re-derivation makes
   `DisconnectBlock` lifecycle-undo unnecessary (§7).
5. **Compiled-and-exercised gate.** Prove the hook compiles into `dogecoind` and
   is exercised on the real consensus path in CI (§10 item 11).
6. **Tag `phase5-green`** ONLY when the full gate (§10) — including item 11 — is
   GREEN, branching from and merging back into `main` while leaving all prior
   green tags untouched.
