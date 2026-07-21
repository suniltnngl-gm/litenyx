# Litenyx Build/Plan Agent — Authoritative Engineering White Paper

**Version**: 0.1  
**Status**: LIVING (updated with each engineering gate)  
**Purpose**: Primary context source for a Build/Plan Agent — not merely descriptive, but prescriptive: tells the agent what is true, what is intended, what is proven, what remains open, what has been superseded, and how it is permitted to reason before proposing or modifying code.  
**Classification**: Engineering authority document. Not a user-facing white paper. Not a marketing document.

---

## Table of Contents

1. [Agent Reasoning Constitution](#1-agent-reasoning-constitution)
2. [Engineering Authority and Maturity Model](#2-engineering-authority-and-maturity-model)
3. [Project Scope: Litenyx → Liten Universe](#3-project-scope-litenyx--liten-universe)
4. [Protocol Architecture](#4-protocol-architecture)
5. [Engineering Environment Doctrine](#5-engineering-environment-doctrine)
6. [Frozen Implementation and Recovery Anchors](#6-frozen-implementation-and-recovery-anchors)
7. [Active Engineering Boundaries](#7-active-engineering-boundaries)
8. [Superseded Mechanisms](#8-superseded-mechanisms)
9. [Agent Decision Records](#9-agent-decision-records)
10. [Evidence Ledger](#10-evidence-ledger)

---

## 1. Agent Reasoning Constitution

### 1.1 Fundamental Rules

These rules are NOT advisory. They are binding constraints on how the Agent reasons about the Litenyx codebase.

**R1 — Never infer authority from recency.**  
A change that was made recently is not more correct than an older one. Freshness is not an authority signal.

**R2 — Never treat LOCKED as IMPLEMENTED.**  
A locked design decision means the architecture is decided. It does not mean the code exists.

**R3 — Never treat IMPLEMENTED as VERIFIED.**  
Code that compiles and runs has not necessarily been proven correct. Verification requires explicit evidence: tests, proofs, or invariant checks.

**R4 — Never treat VERIFIED as PROMOTED.**  
Green tests produce evidence. They do not independently authorize a maturity transition. Promotion requires explicit gate review.

**R5 — Never treat OPEN as available protocol capability.**  
An OPEN engineering gate means the mechanism is not yet implemented, not yet verified, or not yet integrated. It must NOT be assumed present or relied upon.

**R6 — Never revive SUPERSEDED mechanisms.**  
A mechanism explicitly marked as superseded must not be resurrected to solve a current problem unless the design decision is formally reopened.

**R7 — Never modify a frozen recovery anchor to accommodate a later architecture.**  
The Dogecoin pin (INT-Q5) and the Phase-2→6 consensus hooks are frozen. They must not be adjusted to make later phases easier.

**R8 — Before adding consensus complexity, identify the invariant that requires it.**  
Every consensus modification must be justified by an explicit protocol invariant, not by convenience or symmetry.

**R9 — Prefer the smallest deterministic mechanism sufficient to enforce the invariant.**  
Minimal mechanism is a design requirement, not a stylistic preference.

**R10 — Separate defect classes before proposing repairs.**  
A failure must be classified into exactly one category before protocol code is modified (see §1.2).

**R11 — A green test suite produces evidence, not authority.**  
A passing CI run demonstrates that the code behaves as written under the tested conditions. It does not independently authorize a maturity transition.

### 1.2 Failure Classification Tree

Before changing protocol code, classify the failure into exactly one category:

```
Failure →
    Protocol / consensus
    | Production integration (correctness under real conditions)
    | Build / compiler
    | Dependency / ABI
    | Patch / source-tree state
    | Shell / command semantics
    | CI / harness
    | Host / container environment
    | Path / filesystem
    | Encoding / BOM / line endings
    | Parsing / quoting
    | Environment (mixed toolchain)
```

**Rule**: If the failure can be reproduced in a category other than "Protocol / consensus," do NOT modify protocol code. Fix the environment, harness, or integration path instead.

### 1.3 Engineering Priority

```
Agent correctness
    > authority preservation
        > implementation guidance
            > architectural explanation
                > historical detail
```

The document is written so the Agent can make sharp engineering decisions without accidentally resurrecting obsolete designs or promoting unverified mechanisms.

### 1.4 Engineering Method

```
Concept
    → Minimal Mechanism
        → Invariant
            → Deterministic Validation
                → Evidence
```

Every mechanism must follow this path. Skipping steps is not permitted.

### 1.5 Evidence Doctrine

- A passing unit test is evidence of local correctness.
- A passing integration test (daemon + RPC + real modules) is evidence of integration correctness.
- A passing gate (G1 ∧ G2 ∧ G3 ∧ KAT ∧ SKIP=0 ∧ FAIL=0) is evidence of gate closure.
- Evidence is timestamped, attributed, and filed in the Evidence Ledger (§10).
- Evidence does not independently authorize promotion. Promotion requires explicit review against the Maturity Model (§2).

---

## 2. Engineering Authority and Maturity Model

### 2.1 Authority States

Every engineering gate, mechanism, and decision record has exactly one authority state:

| State | Meaning |
|---|---|
| **OPEN** | Not yet implemented; design may be locked or still under discussion. |
| **IMPLEMENTATION-DONE** | Code exists and compiles. No verification evidence required. |
| **KAT-VERIFIED** | Key Algorithmic Tests pass against isolated C++ modules. |
| **DAEMON-VERIFIED** | Integration tests (daemon + RPC + real global state) pass. |
| **GATE-CLOSED** | All gate criteria satisfied, regression tests pass, evidence captured. |
| **FROZEN** | Locked and immutable for the current engineering phase. May be superseded in a later phase. |

### 2.2 Gate Promotion Rules

A gate advances through states in strict order. Skipping is not permitted.

```
OPEN
    → IMPLEMENTATION-DONE (code merged)
        → KAT-VERIFIED (KAT suite 6/6 PASS)
            → DAEMON-VERIFIED (G1 ∧ G2 ∧ G3 ∧ SKIP=0 ∧ FAIL=0)
                → GATE-CLOSED (regression verified, evidence captured)
```

**Critical distinction**: KAT-VERIFIED does NOT imply DAEMON-VERIFIED. DAEMON-VERIFIED does NOT imply GATE-CLOSED.

### 2.3 Authority Classes

The engineering qualification progression in §2.2 is an **engineering gate model**. It is distinct from the higher-level authority classes that apply to any mechanism, design decision, or concept regardless of its engineering stage:

```
LOCKED ≠ FROZEN ≠ VERIFIED ≠ OPEN
```

| Class | Meaning | Applies to |
|---|---|---|
| **LOCKED** | Design direction is decided. Architecture is settled. | Architectural decisions, monetary model, terminology |
| **FROZEN** | Implementation is locked and must NOT be modified for current phase. | Consensus hooks, CI workflow, Dogecoin pin |
| **VERIFIED** | Evidence exists confirming correctness. | KAT tests, patch applicability, gate criteria |
| **OPEN** | Not yet implemented, or implementation not yet verified. | Engineering gates, cryptographic machinery, future phases |

**Rule**: The Agent must never confuse these classes. A mechanism may be LOCKED (architecture decided) but OPEN (not implemented). A mechanism may be FROZEN (must not be modified) but not yet VERIFIED (lack of evidence is not authority to change it).

Example: The four-component inter-planet transaction model is LOCKED (architecture direction decided) but OPEN (implementation not yet started). The Phase-2→6 consensus hooks are FROZEN (immutable for current phase) and VERIFIED (patch applicability and KAT evidence exist).

### 2.4 Status Notation

A gate's current state is written as:

```
GATE-NAME = STATUS / SUB-STATUS / NOTES
```

Example:
```
INT-OPEN-1 = OPEN / KAT-VERIFIED / NOT DAEMON-VERIFIED
```

---

## 3. Project Scope: Litenyx → Liten Universe

### 3.1 The Complete Scope

**Litenyx** is the experimental engineering foundation of the **Liten Universe**. It begins from the locked Dogecoin Scrypt/AuxPoW blockchain foundation and introduces only the deliberately isolated Litenyx customization surfaces required to prove the architecture before the real planetary lineage begins.

### 3.2 Terminology

| Term | Meaning |
|---|---|
| **Liten Universe** | Complete multi-planet protocol architecture. |
| **Planet** | Independent Liten blockchain domain (Liten0, Liten1, ...). |
| **Lane** | Internal dynamic topology unit of a planet. |
| **Ecosystem** | Broader descriptive term for miners, wallets, nodes, markets, software, and participants. |

### 3.3 Litenyx → Liten0 → Liten(x)

```
    Litenyx
        ↓  Experimental engineering / proof foundation
    Liten0
        ↓  First intended real Planet
    Liten1
        ↓
    Liten2
        ↓
      ...
    Liten(x)
```

**Litenyx proves the architecture.** Liten0 begins the intended real lineage. Liten(x) extends that lineage into a finite multi-generation Universe.

### 3.4 Architecture Hierarchy

```
Liten Universe
    ⊃ Planets (Liten0, Liten1, ...)
        ⊃ Dynamic Lanes
            ⊃ Transactions
```

Lanes belong to the same planet and are governed by the native topology state machine. A lane is NOT an independent blockchain.

```
Lane creation = internal topology event.
Planet creation = new independent blockchain domain.
```

### 3.5 Litenyx Boundary

Litenyx is explicitly LIMITED to proving the currently authorized mechanisms. It does NOT introduce generalized frameworks "because they might be useful later."

The Litenyx customization boundary is intentionally narrow:

```
Dogecoin Foundation
    + Dynamic Lane Topology
    + Geographic Reward
    + Chain/Lane-Specific Minimum Balance
```

Minimum balance is chain-specific transaction/request spam protection, NOT a global supply controller.

### 3.6 Liten0 Boundary

Liten0 introduces the generalized **Min/Max/Step bounded-adaptation doctrine**:

```
X_min ≤ X ≤ X_max                        (bounds the admissible state)
|ΔX| ≤ X_step                            (bounds the admissible transition)
```

Every Liten0 adaptive mechanism is evaluated against:

> Does this mechanism require a minimum safe state, maximum safe state, and maximum deterministic adjustment step?

If yes, Min/Max/Step is applied. If not, the mechanism remains simpler.

**Liten0 prospective doctrine, NOT Litenyx constraint.**

| Scope | Min/Max/Step |
|---|---|
| **Litenyx** | NO generalized retrofit |
| **Liten0** | LOCKED prospective engineering doctrine |
| **Liten(x>0)** | Inherits/refines subject to evidence |

### 3.7 Planetary Lifecycle

Target parameters (LOCKED, not yet implemented):

| Parameter | Value |
|---|---|
| T_creation | ~2.5 years |
| T_life | ~25 years |
| N_planets | ~10 |
| T_halving | ~5 years |

A planet's reward epochs:

```
R0 → R0/2 → R0/4 → R0/8 → R0/16
```

Implementation must use deterministic block-height parameters, NOT wall-clock years.

### 3.8 Inter-Planet Migration

Forward-only:

```
L(i) → L(j)  ⇔  j > i
```

Backward protocol migration is prohibited.

Existing migrating supply is reduced according to planetary distance:

```
ρ(i, j) = 10% × (j - i)
```

Example:
```
L0 → L1 = 10% removal
L0 → L2 = 20% removal
```

Two separate monetary controls:

```
Halving              = reduces future issuance
Inter-Planet Removal = removes existing migrating supply
```

### 3.9 Inter-Planet Transaction Model

Current minimal conceptual decomposition (LOCKED direction, implementation OPEN):

```
Inter-Planet TX = Fee + Minimum Balance + Transfer + Inter-Planet Burn
```

Each component has exactly one responsibility:

| Component | Purpose |
|---|---|
| Fee (F) | Transaction/miner fee |
| Minimum Balance (M) | Chain-specific spam protection |
| Transfer (T) | Destination value |
| Inter-Planet Burn (B) | Planetary supply removal |

**Rule**: Do NOT merge these meanings. Each has a separate purpose.

The cryptographic inter-planet machinery (source consumption/destruction proofs, SPV/state proofs, replay protection, destination activation, final settlement) is **OPEN** until implemented and verified.

---

## 4. Protocol Architecture

### 4.1 Dogecoin Foundation

The base blockchain is Dogecoin Core v1.14.9, commit `e0a1c157791544e818c901bd9341896965afbf9d` (INT-Q5 hard pin).

This is the **frozen recovery anchor**. It is Scrypt proof-of-work with AuxPoW (auxiliary proof-of-work). NO changes to the base consensus rules (Scrypt, AuxPoW, block reward schedule, transaction format) are permitted within Litenyx.

### 4.2 Phase-2→6 Hooks (FROZEN)

These are implemented in the `litenyx-validation.patch`, `litenyx-block-aux.patch`, and `litenyx-identity.patch` files. They introduce:

- **Phase 2** (LitenyxSharedState): Global cross-chain spent-set enforcement. Prevents double-spends across topology lanes.
- **Phase 3** (LitenyxTopologyTracker): Observational-only topology bookkeeping. Records canonical chain data for transition decisions.
- **Phase 4B(4)** (LitenyxTopologyAuthority): Consensus-authoritative topology commitment. PreDerivation/SoftAdvisory/HardAuthority regimes.
- **Phase 5** (LitenyxChainIdLifecycle): ChainId lifecycle commitment. Enforces lane binding/rebinding rules.
- **Phase 6** (LitenyxExecutionAuthority): Execution authority resolution. Determines whether a given lane may produce blocks.

These hooks are FROZEN. They must NOT be modified to accommodate later engineering boundaries.

### 4.3 Dynamic Lane Topology

Internal to each planet. Governed by the topology state machine (LITENYX_topology.h). Lanes can SPLIT, HOLD, or MERGE based on observation-window boundaries. The decision math is deterministic and consensus-authoritative in HardAuthority regime.

### 4.4 Geographic Reward and Lane-Count Model

The relationship between geographic validation and lane count is LOCKED as an architectural direction. Implementation is OPEN.

### 4.5 Chain-Specific Minimum Balance

Settled meaning: transaction/request spam protection, specific to each chain/lane. NOT a global supply controller.

---

## 5. Engineering Environment Doctrine

### 5.1 The Development Matrix

Litenyx is developed in a heterogeneous toolchain environment. The active environment must be established before any failure diagnosis or command execution.

```
Engineering Environment = OS + Shell + Filesystem/Path + Encoding
                          + Line Endings + BOM + Parser + Toolchain
                          + Dependencies + Execution Host
```

The practical development matrix:

| Component | Variants |
|---|---|
| OS | Windows 11, Linux (Ubuntu 20.04 container in GHA) |
| Shell | PowerShell 5/7, CMD, Git Bash, MSYS2, sh (dash in containers) |
| Filesystem | Windows (C:\), Linux (/), MSYS2 (/c/...) |
| Path semantics | `C:\...`, `C:/...`, `/c/...`, case sensitivity |
| Line endings | CRLF (Windows), LF (Linux/container) |
| Encoding | UTF-8, UTF-8 with BOM |
| Parser contexts | unified-diff parser, YAML parser, shell parser, C/C++ preprocessor, JSON-RPC, PowerShell |
| Toolchain | g++-9, g++-10, clang (host); g++-10 (container) |
| Dependencies | Boost 1.71 (ubuntu:20.04), pytest |
| Host | Local Windows, GitHub Actions (ubuntu-22.04 + ubuntu:20.04 container), Codespaces, Replit |

### 5.2 Preflight Checklist

Before diagnosing a failure or emitting environment-sensitive commands, establish:

1. **Execution context**: Windows 11, Linux/container, Codespaces, Replit, GitHub Actions.
2. **Shell**: PowerShell 5.1, PowerShell 7, CMD, Git Bash, MSYS2, Bash/sh/dash.
3. **Path semantics**: absolute/relative roots; current working directory; nested paths (`litenyx\litenyx`); case sensitivity; quoting rules.
4. **Text representation**: UTF-8/ASCII; UTF-8 BOM present/absent; LF/CRLF; Git `core.autocrlf` / `.gitattributes`.
5. **Parsing semantics**: which parser will consume the output — unified-diff, YAML, shell, C/C++ preprocessor, JSON-RPC, PowerShell.
6. **Toolchain**: compiler and version, C++ standard, Boost version, configure-detected macros (`HAVE_CONFIG_H`, `HAVE_DECL_BSWAP_*`).
7. **Transformation history**: which prerequisite patches are applied; whether the target file is pristine, patched, generated, or manually edited; the pinned commit.

### 5.3 Failure Classification

```
Failure →
    Protocol / consensus
    | Production integration
    | Build / compiler / dependency
    | Patch / source-tree state
    | Shell / command semantics
    | CI / harness
    | Host / container environment
    | Path / filesystem
    | Encoding / BOM
    | Line endings
    | Parsing / quoting
    | Environment (mixed toolchain)
```

**Rule**: Before repairing a file that appears corrupt, determine whether the bytes are corrupt, the parser rejects the representation, the path resolves to the wrong object, or the file is merely incompatible with the current transformation state. Never rewrite protocol artifacts until those cases are distinguished.

### 5.7 Concrete Case Study: The RPC Patch Corruption Episode

The following episode from the INT-OPEN-1 gate demonstrates why the Engineering Environment Doctrine matters.

**Background**: The `testlitenyxforceflushfail` RPC was identified as missing from the command registration table and its handler body. A fix was applied by editing the unified-diff patch file (`litenyx-rpc.patch`) using a text editor.

**First CI result (d12ad8f)**: The CI log showed:
```
FATAL: litenyx-rpc.patch does not apply and is not already applied
make: *** [Makefile:114: inject-hooks] Error 1
```

The Agent's first instinct might be: "G1 still failing, RPC fix didn't work, need a different approach to register the RPC." This would be WRONG.

**Correct diagnosis**:
1. The three prerequisite patches (identity, block-aux, validation) applied cleanly.
2. The RPC patch failed at `git apply --check` with `corrupt patch at line 577`.
3. The patch file had 576 lines — `git apply` was reporting a structural parse failure, not a context mismatch.
4. `--reject` could not help because the unified-diff parser could not interpret the patch structure at all.

**Root cause**: The manual text edit inserted lines into the middle of a hunk but produced an invalid unified-diff structure. Git's `git apply` is strict about unified-diff format. The editor's in-place insertions did not preserve the required diff syntax invariants (hunk header line counts, proper context separators, correct trailing whitespace semantics).

**Resolution**: The patch was regenerated by:
1. Checking out the correct pinned Dogecoin tree (`e0a1c157791544e818c901bd9341896965afbf9d`).
2. Applying the prerequisite patches (identity, block-aux, validation).
3. Making the two intended edits directly to the C++ source file (`src/rpc/mining.cpp`).
4. Running `git diff -- src/rpc/mining.cpp > litenyx-rpc.patch`.
5. Validating with `git apply --check litenyx-rpc.patch` against a clean revert.
6. Replacing the corrupted patch file with the Git-generated diff.

**Classification**: This was a **patch-artifact/environment defect**, NOT a protocol defect. The RPC implementation was semantically correct; the delivery mechanism (the patch file) was structurally invalid.

**Lesson for the Agent**:
- "Corrupt patch" is NOT equivalent to "bad RPC implementation."
- Before modifying protocol code, establish whether the failure is in the protocol, the integration harness, the CI environment, or the patch/storage artifact.
- Regenerate patches with `git diff`, not by editing unified-diff files directly.
- `git apply --check` is the authoritative validity test for a patch file. If it fails with "corrupt patch," the patch is malformed. If it fails with "patch does not apply," the context does not match the target tree.
- The correct fix for a corrupt patch is NEVER to manually adjust hunk headers. The correct fix is to regenerate from a known-good tree state.

### 5.4 Environment-Specific Patterns

**CRLF / LF**: A patch can be semantically correct but fail because of CRLF vs LF. Git on Windows may auto-convert. Use `git add --renormalize .` to fix. Verify with `file` or `od -c`. Store patches with LF in the repository.

**UTF-8 BOM**: PowerShell 5 may emit UTF-8 with BOM by default. This breaks `git apply` on Linux. Use `-Encoding UTF8` (no BOM) in PowerShell 7, or `[System.Text.UTF8Encoding]::new($false)`.

**`sh -e`**: The ubuntu:20.04 container uses `dash` as `/bin/sh` with `-e`. In `sh -e`, a bare non-zero command terminates the shell before subsequent `rc=$?` and `cat` execute. Wrap in `if` blocks:

```sh
rc=0
if make target > /tmp/log 2>&1; then :; else rc=$?; fi
cat /tmp/log
exit $rc
```

**`tee` and pipes**: `make ... | tee ...` propagates tee's exit code, not make's. Capture rc before tee.

**Boost 1.71 + C++20**: Boost 1.71 on ubuntu:20.04 has wchar_t stream deletion incompatibility with -std=c++20. Use -std=c++17 for KAT compilation.

**`HAVE_CONFIG_H`**: The Dogecoin configure step defines macros like `HAVE_DECL_BSWAP_64=1`. The KAT compilation does not run configure, so these must be provided explicitly: `-DHAVE_CONFIG_H -Isrc/config`.

**Patch line count / hunk headers**: Manual edits to unified-diff patches can corrupt hunk headers. Always regenerate patches with `git diff` rather than editing patch files directly.

### 5.5 Identifying Shell Environment in CI

The ubuntu:20.04 container runs `sh` (dash). `bash` features (`set -o pipefail`, `[[ ]]`, arrays, process substitution) are NOT available. Check with:

```sh
# In the Makefile / CI script:
SHELL_CHECK=$(readlink /proc/$$/exe 2>/dev/null || echo "unknown")
# Or just assume dash-safe syntax in container steps.
```

### 5.6 Consensus Evidence Should Be Qualified in Linux CI

While Windows/MSYS2/Codespaces/Replit are valuable development and diagnostic environments, **reproducible Linux CI (GitHub Actions, ubuntu:20.04 container)** is the qualification environment for consensus/build evidence.

---

## 6. Frozen Implementation and Recovery Anchors

### 6.1 INT-Q5: Dogecoin Pin

- **Pin**: `e0a1c157791544e818c901bd9341896965afbf9d` (Dogecoin Core v1.14.9)
- **Rationale**: The M3 attach points (ConnectTip failure boundary) were verified at this commit. Advancing would invalidate the INT-Q4 boundary analysis.
- **Status**: HARD PIN. Do NOT change without explicit INT-Q5 review.

### 6.2 Phase-2→6 Litenyx Customization (FROZEN)

The consensus hooks in `litenyx-validation.patch`, `litenyx-block-aux.patch`, `litenyx-identity.patch`, and `litenyx-rpc.patch` are FROZEN for the current engineering phase. They implement:
- Global shared spent set (Phase 2)
- Topology observation (Phase 3)
- Topology authority commitment (Phase 4B(4))
- ChainId lifecycle commitment (Phase 5)
- Execution authority (Phase 6)
- Regtest RPCs for KAT harness (Phases 2-6)

These patches must NOT be modified to accommodate later phase work unless the modification is itself a frozen-boundary issue.

### 6.3 CI Workflow

The CI workflow at `.github/workflows/m3-integration.yml` is frozen for the INT-OPEN-1 gate.

---

## 7. Active Engineering Boundaries

### 7.1 INT-OPEN-1: M3 Daemon Integration Gate

**State**: OPEN / KAT-VERIFIED / NOT DAEMON-VERIFIED

**Gate criteria**:
- Pin `e0a1c157791544e818c901bd9341896965afbf9d`
- Apply 5 patches (identity, block-aux, validation, rpc, makefile)
- DEBUG build (`--enable-debug`, NO `-DNDEBUG`)
- M3 class KATs 6/6 PASS
- G1 (force-flush-fail RPC → daemon response)
- G2 (exactly-once visibility → invariant check)
- G3 (failure isolation → live set unchanged)
- SKIP = 0, FAIL = 0
- Fail-closed harness (non-zero exit propagates to workflow)

**Current diagnosis**:
```
G1 root cause identified
    → missing RPC registration + handler body
    → candidate fix in commit d12ad8f (RPC + handler)
    → patch corruption in d12ad8f fixed in commit e419671 (git-regenerated patch)
    → CI verification pending
```

### 7.2 H1-SHADOW-ENG

**State**: CLOSED (NON-AUTHORIZED). Shadow engineering is not authorized for the current phase.

### 7.3 MON-SPEC-1 (Monetary Spec)

**State**: PROPOSAL-ONLY. W_t^virtual denominator is OPEN/BLOCKING.

### 7.4 DA-OPEN-1 (Drain Authority Trigger)

**State**: OPEN. Phase-7 is blocked pending INT-OPEN-1 closure.

---

## 8. Superseded Mechanisms

### 8.1 S1 Superseded Cooldown / Drop

The original S1 cooldown-drop mechanism is superseded. See `docs/litenyx_s1_superseded_cooldown_drop_conclusion.md` for analysis.

**Rule**: Do NOT revive S1 cooldown-drop to solve lane-transition or topology problems. If a current problem appears to require cooldown-drop semantics, identify the invariant first, then design the minimal mechanism from that invariant.

### 8.2 Clang-12 Workaround for wchar_t

The earlier clang-12 workaround for Boost.Test wchar_t stream deletion is superseded by the simpler and more correct `-std=c++17` fix. The clang-12 workaround must NOT be reintroduced.

---

## 9. Agent Decision Records

Every major mechanism or engineering gate has a Decision Record. The template is:

```
### [MECHANISM-NAME]

| Field | Value |
|---|---|
| **Status** | OPEN / IMPLEMENTATION-DONE / KAT-VERIFIED / DAEMON-VERIFIED / GATE-CLOSED / FROZEN / SUPERSEDED |
| **Authority** | Phase / engineering gate reference |
| **Invariant** | The protocol invariant this mechanism enforces |
| **Implementation** | Files and patches |
| **KAT Evidence** | Test file, pass count, date |
| **Daemon Evidence** | CI run, G1/G2/G3 results, date |
| **Open Risks** | Identified but unresolved risks |
```

### 9.1 INT-OPEN-1 Decision Record

| Field | Value |
|---|---|
| **Status** | OPEN / KAT-VERIFIED / NOT DAEMON-VERIFIED |
| **Authority** | INT-OPEN-1 gate contract |
| **Invariant** | M3 atomicity: a post-ConnectBlock failure MUST leave the live shared spent set unchanged (R3/SS-INV-4) |
| **Implementation** | `litenyx-validation.patch` (ConnectTip hook + arm), `litenyx-rpc.patch` (RPC) |
| **KAT Evidence** | 6/6 PASS (shared_state, spent_set, reorg, topology, lifecycle, execution) |
| **Daemon Evidence** | Pending — CI verification required |
| **G1 root cause** | RPC registration + handler were missing; fixed in d12ad8f; patch regenerated in e419671 |
| **Open Risks** | G2 may fail independently once G1 passes |

### 9.2 MON-SPEC-1 Decision Record

| Field | Value |
|---|---|
| **Status** | PROPOSAL-ONLY |
| **Authority** | MON-SPEC-1 proposal |
| **Invariant** | Not yet defined |
| **Implementation** | Not yet started |
| **Evidence** | Not yet applicable |
| **Open Risks** | W_t^virtual denominator undefined (BLOCKING) |

### 9.3 DA-OPEN-1 Decision Record

| Field | Value |
|---|---|
| **Status** | OPEN |
| **Authority** | DA-OPEN-1 |
| **Invariant** | Not yet defined |
| **Implementation** | Not yet started |
| **Evidence** | Not yet applicable |
| **Open Risks** | Blocked pending INT-OPEN-1 closure |

---

## 10. Evidence Ledger

### 10.1 KAT Evidence

| Test | Count | Status | Date | File |
|---|---|---|---|---|
| M3 shared_state KAT | 1/1 | PASS | 2026-07-XX | `cpp_reference/test/test_litenyx_shared_delta.cpp` |
| M3 spent_set KAT | 1/1 | PASS | 2026-07-XX | `cpp_reference/test/test_litenyx_shared_delta.cpp` |
| M3 reorg KAT | 1/1 | PASS | 2026-07-XX | `cpp_reference/test/test_litenyx_shared_delta.cpp` |
| M3 topology KAT | 1/1 | PASS | 2026-07-XX | `cpp_reference/test/test_litenyx_shared_delta.cpp` |
| M3 lifecycle KAT | 1/1 | PASS | 2026-07-XX | `cpp_reference/test/test_litenyx_shared_delta.cpp` |
| M3 execution KAT | 1/1 | PASS | 2026-07-XX | `cpp_reference/test/test_litenyx_shared_delta.cpp` |
| **Total M3 KAT** | **6/6** | **PASS** | **2026-07-XX** | — |

### 10.2 CI Run History

| Run ID | Commit | Conclusion | KAT | G1 | G2 | G3 | Notes |
|---|---|---|---|---|---|---|---|
| 29826623955 | d12ad8f | failure | ? | ? | ? | ? | Harness `sh -e` interrupted (no output captured) |
| 29824565104 | 7ecc52d | failure | 6/6 PASS | FAIL (Method not found -32601) | FAIL (assertion, cascade) | PASS | Harness correctly propagated rc=1 |
| 29827595524 | 08b60b9 | failure | ? | ? | ? | ? | Patch corruption: "corrupt patch at line 577" |
| — | e419671 | pending | — | — | — | — | Git-regenerated patch |

### 10.3 Patch Verification

| Patch | Status | Method |
|---|---|---|
| `litenyx-identity.patch` | VERIFIED | `git apply --check` |
| `litenyx-block-aux.patch` | VERIFIED | `git apply --check` |
| `litenyx-validation.patch` | VERIFIED | `git apply --check` |
| `litenyx-rpc.patch` (e419671) | VERIFIED | `git apply --check` against pinned/prerequisite-patched tree |
| `litenyx-makefile.patch` | VERIFIED | `git apply --check` |
