# Litenyx INT-OPEN-1 / M3 Implementation Map v0.1

Status: **SPEC-TRACK ARTIFACT — pre-code.** Binds the frozen M3 contract
(`docs/litenyx_int_open_1_fix_spec_v0.1.md`, R1–R3) and the VERIFIED vendored
attach points (dogecoin **v1.14.9** `src/validation.cpp` `ConnectTip` 2313–2361,
`ActivateBestChainStep` 2440–2509) to the EXACT Litenyx source locations that must
change. No code is written here. This map is the last artifact before
implementation; it exists so the diff is mechanical and every edit traces to a
frozen invariant.

Governing docs (all frozen / accepted):
- SharedSpendSet doctrine SS-INV-1..7 (`litenyx_sharedspendset_doctrine_v0.1.md`).
- INT-OPEN-1 spec R1–R3, INT-Q1..Q5 resolved (`litenyx_int_open_1_fix_spec_v0.1.md`).
- Component-11 reconciliation (`litenyx_component11_reconciliation_v0.1.md`) —
  Surface 6 no-bypass; single reader path.

---

## 1. The defect this map fixes (restated, source-anchored)

`LITENYX_validation.cpp:52` `LitenyxConnectSharedState` is called from inside
`ConnectBlock` (hook order per `deploy/patches/litenyx-validation.patch:43–111`),
which runs at `ConnectTip` line **2333**. Today it commits spends DIRECTLY into the
live singleton:

- `LITENYX_validation.cpp:68–74` calls `LitenyxRecordSharedSpend(...)` →
  `LitenyxSharedSpendSet::Instance().RecordSpend(...)` (`LITENYX_sharedstate.h:66,49`).

Per INT-Q4, after `ConnectBlock` succeeds there remains a failure-capable step —
`FlushStateToDisk` at `ConnectTip:2348` — that can `return false`. If it fails, the
UTXO `view` is discarded (scope 2331–2344) BUT the live shared set has ALREADY been
mutated. Result: a block that never becomes canonical can leave spends in the live
set. This violates **R1** (candidate delta must be invisible through ConnectBlock),
**R3** (failed connect ⇒ live set bit-for-bit unchanged), and **SS-INV-4**.

The disconnect path (`LITENYX_validation.cpp:78`) does NOT rescue this: `ConnectTip`
does not call `DisconnectBlock` on a `FlushStateToDisk` failure — it just returns
false. So there is no inverse applied for the leaked commit.

---

## 2. Design shape (from INT-Q1/Q2, no new reader path)

Introduce a **candidate delta** `Δ_B` = the ordered set of outpoints a block would
spend, staged but NOT applied to the live singleton, with:

- **R1 invisibility:** `Δ_B` is NOT consulted by `IsSpent`/`LitenyxIsSharedSpent`.
  The live singleton remains the **sole reader surface** (G-INT-3 / RPC-NOGO /
  Component-11 single-reader). Advisory ATMP view, RPC provenance, everything reads
  only the live set. `Δ_B` is write-only until publish.
- **R2 publish-on-success:** `Δ_B` is applied to the live singleton exactly once,
  at the `ConnectTip` non-failure-capable tail (post-2348; at/after `UpdateTip`).
- **R3 discard-by-default:** `Δ_B` has scoped (RAII) lifetime. Any early
  `return`/exit before publish destroys `Δ_B` with ZERO effect on the live set.
  Discard is via scope exit, NOT try/catch on consensus-critical steps
  (INT-NOGO-3).

`Δ_B` must still perform the **intra-block global-double-spend CHECK** during
`ConnectBlock` (so an invalid block is rejected at the right place), but that check
reads the LIVE set (as today, `LITENYX_validation.cpp:61`) plus the not-yet-published
`Δ_B` for within-block/within-batch coherence — it does NOT write the live set.

---

## 3. Attach points (exact source edits)

### 3.1 `LITENYX_sharedstate.h` / `.cpp` — add staging type (INT-Q2)

Add a scoped candidate-delta type, e.g. `LitenyxCandidateSpendDelta`:

- Holds an ordered `std::vector<std::pair<LitenyxOutPoint,uint8_t>>` (insertion
  order preserved for deterministic publish/KAT).
- `bool StageSpend(op, chainId)`:
  - reject if `LitenyxSharedSpendSet::Instance().IsSpent(op)` (live conflict), OR
  - reject if `op` already staged in this `Δ_B` (within-block/batch double spend);
  - else append. Returns false on reject (mirrors current `RecordSpend` contract at
    `LITENYX_sharedstate.h:49`), WITHOUT touching the singleton.
- `void Publish()`: apply every staged entry via the existing
  `LitenyxSharedSpendSet::Instance().RecordSpend(...)`. Called exactly once, only
  from the verified success tail. `RecordSpend` remains the SINGLE writer into the
  live map (SS-INV mutation locus unchanged).
- Destructor: no-op on the live set (discard-by-default). No global registration —
  the object is stack/scope owned (R3, INT-Q2).

Do NOT expand the reader API. `IsSpent`/`LitenyxIsSharedSpent` stay exactly as-is
(`LITENYX_sharedstate.h:56,79`) — the delta is invisible to readers (R1).

### 3.2 `LITENYX_validation.cpp:52` `LitenyxConnectSharedState` — split into stage + no live write

Refactor so the function called at `ConnectBlock`-time (2333) performs only:
1. the Phase-1 global-double-spend CHECK (current lines 56–66) reading the live set
   AND the current-block staging delta (for within-block coherence), and
2. **staging** each spend into `Δ_B` (replacing the direct `LitenyxRecordSharedSpend`
   writes at current lines 68–74).

It MUST NOT call `LitenyxRecordSharedSpend` (no live mutation). Signature gains a
`Δ_B` out-parameter (or the block-scoped delta is threaded through the ConnectBlock
hook context). The hook order in `litenyx-validation.patch` is UNCHANGED; only the
effect of the SharedState step changes from "commit" to "stage".

### 3.3 `ConnectBlock` hook context — own `Δ_B` for the block's lifetime

`Δ_B` is created per block being connected and owned by the `ConnectTip` connect
scope so that:
- on `ConnectBlock` failure (`ConnectTip:2335` → return error), `Δ_B` is destroyed
  with the `view` (scope 2331–2344) — nothing leaked (R3);
- on `FlushStateToDisk` failure (`ConnectTip:2348` → return false), `Δ_B` is
  destroyed before publish — nothing leaked (R3, the INT-Q4 window closed);
- `Δ_B.Publish()` is invoked only at the tail (post-2348), see 3.4.

Because Litenyx cannot edit `ConnectTip` directly except via patch, the ownership is
realized by attaching `Δ_B` to the ConnectBlock hook state that the patch already
threads, and publishing via a new tail hook (3.4). The precise C++ ownership vehicle
(explicit local vs. patched member) is the only remaining implementation choice; both
satisfy §2 provided discard-by-default holds.

### 3.4 `ConnectTip` success tail — publish hook (INT-Q1/Q4)

Add a Litenyx publish call at the verified non-failure-capable tail, i.e. AFTER
`FlushStateToDisk` succeeds (`ConnectTip:2348`), at/after `UpdateTip`
(`ConnectTip:2353–2360`), via `deploy/patches/litenyx-validation.patch`. This call
is `Δ_B.Publish()` for the just-connected block. It is the ONLY place the live set is
mutated on connect. One publish per `ConnectTip` = one publish per connected block,
consistent with the `ActivateBestChainStep` per-block loop (INT-Q3,
`ActivateBestChainStep:2469`).

### 3.5 Disconnect path — unchanged inverse (INT-Q2 inverse)

`LITENYX_validation.cpp:78` `LitenyxDisconnectSharedState` stays as the inverse: on
`DisconnectTip`/`DisconnectBlock` it calls `LitenyxRevertSharedSpend` for the block's
spends (`LITENYX_sharedstate.h:53`). Since publish now happens only for blocks that
completed `ConnectTip`, every live-set entry has a matching connected block to revert
— restoring the connect/disconnect symmetry described in `LITENYX_sharedstate.h:19–24`.
No change required, but a test must confirm publish/revert pairing (see §4).

### 3.6 IBD / reindex / import (INT-Q3) — no extra work

All these funnel through `ActivateBestChainStep`'s per-block `ConnectTip` loop
(2469). Because stage+publish is bound to a single `ConnectTip`, batch connection
gets one stage/publish cycle per block automatically. No path-specific code.

### 3.7 Makefile pin (INT-Q5) — prerequisite

Before building against these attach points, pin `deploy/Makefile:44` from unpinned
`master --depth 1` to `--branch v1.14.9` (or a fixed commit) so the built
`ConnectTip` is the verified 2313–2361 structure. This is a build-hygiene edit, not
an M3 design change.

---

## 4. Test obligations (must accompany the code, KAT-style)

1. **R3 / SS-INV-4 (core):** simulate `ConnectBlock(B)=success` then
   `FlushStateToDisk=fail` ⇒ assert live set `== ` pre-connect set (bit-for-bit).
   This is the exact INT-Q4 window and MUST have a dedicated test.
2. **R1 invisibility:** while `Δ_B` staged (mid-ConnectBlock), assert
   `LitenyxIsSharedSpent(op)` returns the LIVE value (delta not visible) for a
   staged-but-unpublished outpoint.
3. **R2 publish-once:** successful `ConnectTip` ⇒ live set contains exactly `Δ_B`;
   no double application on reorg re-entry.
4. **Connect/disconnect symmetry:** connect B then disconnect B ⇒ live set restored
   (publish then revert cancels), per `LITENYX_sharedstate.h:19–24`.
5. **Within-block double spend:** two txs in B spending same outpoint ⇒ staged
   reject at `StageSpend`, block invalid, live set untouched.
6. **Batch/IBD:** connect N blocks via the step loop ⇒ N publishes, order preserved,
   final set == fold of all N deltas.

All P4/P5/P6-scoped: the fix is orthogonal to topology/lifecycle/execution hooks and
must not perturb their ConnectBlock ordering
(`litenyx-validation.patch:43–111`).

---

## 5. Invariants preserved / not touched

- SS-INV-1..7: mutation locus stays `RecordSpend`/`RevertSpend`; publish is a batched
  invocation of the SAME writer, so the doctrine's single-writer property holds.
- G-INT-3 / RPC-NOGO single reader: no new reader path; delta is write-only.
- DA-OPEN-1: remains OPEN and untouched — this map is drain-agnostic
  (`effMayRoute = p6.mayRoute` when DrainCommitment absent, G-XCT-5).
- No Phase-2 green tag reopened; no consensus rule changed — only the TIMING of when
  a proven-canonical block's spends become live is corrected.

---

## 6. Remaining implementation choice (single, bounded)

The only open decision is the C++ ownership vehicle for `Δ_B` across the
ConnectBlock→tail span (§3.3): explicit local threaded through the existing hook
context vs. a patched `ConnectTip`-local. Both satisfy R1–R3; selection is an
ergonomics/patch-surface call to be made at implementation time, recorded in the
commit. Nothing else is undecided.
