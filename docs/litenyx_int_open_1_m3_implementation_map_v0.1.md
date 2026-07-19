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

### 3.7 Makefile pin (INT-Q5) — DONE

Pinned. `deploy/Makefile` `clone-dogecoin` now checks out the exact commit
`e0a1c157791544e818c901bd9341896965afbf9d` (Dogecoin Core **v1.14.9**) via
`DOGECOIN_PIN` (overridable). Verified this track:

- fresh clone + `git fetch --depth 1 origin <pin>` + checkout ⇒
  `HEAD = e0a1c157791544e818c901bd9341896965afbf9d` (exact match);
- patch `git apply --check` against the pinned tree ⇒ 5/5 hunks apply
  (`src/validation.cpp` `ConnectTip` 2313–2361 structure intact);
- 6/6 delta KATs (`KERRNYX_DOGE_SUPPORT`) re-run against the pinned tree's
  `uint256.cpp` / `utilstrencodings.cpp` ⇒ pass.

**M3 verified integration substrate (record):**
\[
\boxed{\text{Dogecoin Core v1.14.9, commit } e0a1c157791544e818c901bd9341896965afbf9d}
\]
This is the tree G1–G3 daemon integration MUST build against, so the
`ConnectTip` failure boundary under test equals the one M3 was designed against
(INT-Q4). An unpinned `master` would NOT be an authoritative substrate.

---

## 4. Test obligations — status

> **Scope boundary (verified this track):**
> \[
> \boxed{\text{M3 class-level KATs} \neq \text{daemon integration proof}}
> \]
> The class-level KAT (below) exercises the REAL `LitenyxCandidateSpendDelta`,
> `LitenyxSpendPublishScope`, and `LitenyxSharedSpendSet` (no model, no mock) and
> proves staging invisibility, default discard, explicit publication, idempotent
> one-shot publication, within-candidate conflict handling, and connect/disconnect
> symmetry. It does NOT execute the daemon `ConnectTip` control flow, so the
> critical obligation
> \[
> ConnectTip(B)=Failure \Rightarrow SSS_{after}=SSS_{before}
> \]
> under a REAL failure AFTER a successful `ConnectBlock` (the INT-Q4 window) still
> requires daemon-level integration coverage. See §4.2.

### 4.1 Class-level KAT — DONE (`cpp_reference/test/test_litenyx_shared_delta.cpp`)

Linked against the real `LITENYX_sharedstate.cpp` plus the minimum real dogecoin
support TUs (`uint256.cpp`, `utilstrencodings.cpp` — `base_blob::SetHex`,
`HexDigit`); Boost.Test header-only. Makefile rule added at `deploy/Makefile`
(`KERRNYX_DELTA_SRC`/`KERRNYX_DOGE_SUPPORT`). Result: **6/6 pass**.

1. **R1 invisibility** — staged-but-unpublished spend NOT visible to
   `LitenyxIsSharedSpent` / live `IsSpent`.
2. **R3 / SS-INV-4 (core INT-Q4 window)** — attempt that stages then exits WITHOUT
   `PublishActive` leaves the live set bit-for-bit unchanged (including a
   pre-existing live spend that must survive).
3. **R2 publish-once** — explicit `PublishActive` applies the delta exactly once;
   a second `PublishActive` is a no-op (no double-apply on reorg re-entry).
4. **Connect/disconnect symmetry** — publish then `RevertSpend` restores the live
   set (frozen canonical inverse unchanged).
5. **Within-attempt + live-set double spend** — same outpoint staged twice, and a
   live-set conflict, are both rejected; nothing extra staged.
6. **Batch/IBD fold** — N sequential publishes ⇒ final set == fold of all N, order
   preserved; an unstaged outpoint stays unspent.

### 4.2 Daemon integration coverage — STILL OWED (not run in this track)

Required before INT-OPEN-1 is classified `RESOLVED / VERIFIED`:

- **G1 (the load-bearing one):** drive `ConnectTip(B)` to a real failure AFTER
  `ConnectBlock` succeeds — specifically `FlushStateToDisk` failing at
  `ConnectTip:2348` — and assert the live SSS equals the pre-connect state. The
  class KAT proves the mechanism; the daemon test proves the verified attach points
  behave in the actual control flow.
- **G2:** P4/P5/P6 rejection mid-`ConnectBlock` must also leave the live set
  unchanged (the scope destructor discards `Δ_B`); cover at least one topology /
  lifecycle / execution rejection.
- **G3:** a normal multi-block `ActivateBestChainStep` run publishes exactly one
  delta per connected block and is reversible on a following reorg
  (`DisconnectTip` → `LitenyxDisconnectSharedState`).

These need a built `dogecoind` (production-build target) and the regtest harness;
out of scope for the standalone KAT. INT-Q5 (pin the Makefile clone to v1.14.9)
landed first (commit 5586b07) so the daemon built is the structurally-verified one.

### 4.3 Daemon integration scaffolding — AUTHORED, NOT YET EXECUTED

G1 fault-injection mechanism, the arming RPC, and the G1–G3 harness are written
and patch-verified, but NOT run in this environment (no daemon build possible
here — see blocker below).

- **G1 hook:** `ConnectTip` tail (post-`FlushStateToDisk`, post-`UpdateTip`, before
  `PublishActive`) consults a debug-only `std::atomic<bool> g_litenyxForceFlushFail`
  (file-local in `src/validation.cpp`, gated `#ifndef NDEBUG`). When armed it
  `return false` WITHOUT publishing — exactly the INT-Q4 window. Symbol armed via
  `LitenyxArmForceFlushFail(bool)`.
- **Arming RPC:** `testlitenyxforceflushfail "arm"` in `src/rpc/mining.cpp`
  (registered in `commands[]`; harness skips if absent → signals non-debug build).
- **Harness:** `tests/regtest/test_litenyx_m3_integration.py` — `test_g1_*` (forced
  failure ⇒ SSS unchanged), `test_g2_*` (success ⇒ staged delta visible once),
  `test_g3_*` (connect then disconnect/revert ⇒ SSS restored; reconnect converges).
  Mirrors the Phase-1/2 scaffold's mandatory binary gate; collection cleanly gates
  on a present debug daemon.

**Environment blocker (this local MSYS2/UCRT64 install):** no `make`/`autoconf`/
`libtool`, no `pacman`, and no BerkeleyDB/libevent/OpenSSL dev headers — a
`production-build` of `dogecoind` cannot be produced here. Therefore G1–G3 are
AUTHORED and READY-TO-RUN on CI (which builds the pinned debug daemon) but are
**not yet executed**. The classification INT-OPEN-1 remains
`IMPLEMENTATION-DONE / KAT-VERIFIED` and must NOT advance to `RESOLVED / VERIFIED`
until G1–G3 pass against the built daemon.

All P4/P5/P6-scoped: the fix is orthogonal to topology/lifecycle/execution hooks and
must not perturb their ConnectBlock ordering
(`litenyx-validation.patch:43–111`).

### 4.4 CI gate: `make m3-integration` (fail-closed)

Added in commit after 441a3ec. Enforces the evidence hierarchy and fails the
build at EVERY stage:

    Pinned v1.14.9  ->  Patch Application  ->  Debug Daemon Build
                 ->  M3 Delta KATs  ->  G1-G3 Regtest  ->  Verification Verdict

- `[1/5]` Pin: asserts `git -C $(DOGECOIN_DIR) rev-parse HEAD == $(DOGECOIN_PIN)`
  (e0a1c157791544e818c901bd9341896965afbf9d); mismatch => FATAL.
- `[2/5]` Patch application proven by `inject-hooks` (litenyx sources present).
- `[3/5]` Requires the DAEMON to exist at `$(DOGECOIN_DIR)/src/dogecoind`.
- `[4/5]` Builds + runs the 6 real-class delta KATs (`KERRNYX_DELTA_BIN`); any
  failure => gate fails.
- `[5/5]` Runs ONLY `tests/regtest/test_litenyx_m3_integration.py`. A NON-ZERO
  pytest exit => FATAL. A SKIPPED G1-G3 (detected via `grep skipped` on the
  `-rA` report) => FATAL — a skipped test is explicitly NOT success.

`production-build-debug` (dependency) configures with `--enable-debug` (so `-DNDEBUG`
is absent) and REFUSES if the generated Makefile defines `NDEBUG`. This is the
structurally-enforced guarantee that the fault-injection path is only ever present
in debug builds.

#### Production / release exclusion (structurally enforced, not described-only)

The `testlitenyxforceflushfail` RPC and the `g_litenyxForceFlushFail` hook are
wrapped in `#ifndef NDEBUG` in BOTH the `commands[]` registration, the forward
declaration, and the handler body (rpc patch), and in the `ConnectTip` tail (validation
patch). Under `-DNDEBUG` (release / `--disable-debug`) the RPC is NOT compiled into
the binary and the forced-fail branch is removed — so the fault-injection surface
cannot appear in a production build regardless of configuration mistake. The CI gate
additionally refuses to run G1–G3 unless the debug build excluded `NDEBUG`.

#### Promotion rule (explicit)

    G1 ∧ G2 ∧ G3 = PASS   =>  INT-OPEN-1 = RESOLVED / DAEMON-VERIFIED
    NOT EXECUTED ∨ SKIPPED ∨ FAIL  =>  INT-OPEN-1 remains IMPLEMENTATION-DONE / KAT-VERIFIED

The classification advances ONLY when `make m3-integration` exits 0 with zero
skipped tests.


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
