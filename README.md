# Litenyx

> **Current-authority notice (2026-07-24):** this repository contains historical constitutional/experimental material. Before treating an older `LOCKED`, `FROZEN`, `MANDATORY`, or `CONSTITUTIONAL` statement as current architecture, read `docs/CURRENT_AUTHORITY.md`. Historical labels do not independently establish current authority; relaxability does not imply removal.

A clean-slate fork derived from **Dogecoin**, applying the protocol principles
locked across prior experiments (KerrNyx, Veltrix, WaveCore). Litenyx is
structured as a gradual, phased fork of Dogecoin rather than a from-scratch
protocol, so that experimental mechanisms are introduced one at a time with
explicit activation gates.

## Guiding principle (locked)

```
1 Blockchain + 1 Currency + 1 Global Monetary State + N Parallel Chains
```

A transaction valid on one chain is valid on all chains. Chains differ only in
acceptance parameters (fee rate, block size, block time), never in the shared
UTXO universe or currency. This is a **shared-state multi-chain**, not a bridge
and not per-chain ledgers.

## Status

> The phase table below is historical planning provenance. In particular, dynamic split/merge, negative-supply, dynamic-wallet-count, and coupled-controller entries are **not** current authority merely because they appear here. See `docs/CURRENT_AUTHORITY.md`.

| Track | Phase | Scope | Status |
|-------|-------|-------|--------|
| I | 0 | Protocol constitution (white paper + consensus spec) | in progress |
| I | 1 | Clean Dogecoin fork (identity, magic, ports, genesis, AuxPoW) | planned |
| II | 2 | Fixed two-chain shared-state consensus | next milestone |
| II | 3 | Dynamic split/merge (N_t -> N_t+1) | historical planning |
| III | 4 | Dynamic block size | historical planning |
| III | 5 | Dynamic block reward | historical planning |
| IV | 6 | Negative supply shadow accounting | superseded historical planning |
| IV | 7 | Dynamic wallet count / negative position | superseded historical planning |
| IV | 8 | Unified controller simulation | historical research |
| IV | 9 | Public network progression | historical planning |

## Repository layout

```
docs/                 Phase-0 constitution: white paper + consensus spec
litenyx/              Consensus headers (header-only integer math where possible)
cpp_reference/        Standalone C++ unit tests (no daemon link)
deploy/               Fork-Makefile: clone Dogecoin, inject hooks, build, test
tests/regtest/        Python regtest harness driving the daemon RPC
.github/workflows/    CI (build + cpp-test + regtest)
```

## Consensus boundary (locked)

```
ConsensusCore  !=  RuntimePolicy  !=  WalletPolicy
```

Experimental economic / wallet mechanisms must never contaminate basic
consensus validation. Historical feature-status labels must be interpreted
through the current-authority notice rather than by label alone.

## Build & test

See `deploy/Makefile`. CI runs build + `make cpp-test` + regtest on every push.

## Historical milestone context

Fixed Multi-Chain Shared-State Consensus (Phase 2 substrate) originally aimed to prove that a
wallet can spend the same globally-valid currency through either of two fixed
parallel chains while guaranteeing

```
Spend(U, Chain_A) => NOT Spend(U, Chain_B)
```

for the same spendable state U. See `docs/litenyx_consensus_spec_v0.1.md` for the historical specification context and `docs/CURRENT_AUTHORITY.md` before interpreting it as present authority.
