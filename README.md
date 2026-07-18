# Litenyx

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

| Track | Phase | Scope | Status |
|-------|-------|-------|--------|
| I | 0 | Protocol constitution (white paper + consensus spec) | in progress |
| I | 1 | Clean Dogecoin fork (identity, magic, ports, genesis, AuxPoW) | planned |
| II | 2 | Fixed two-chain shared-state consensus | next milestone |
| II | 3 | Dynamic split/merge (N_t -> N_t+1) | future |
| III | 4 | Dynamic block size | future |
| III | 5 | Dynamic block reward | future |
| IV | 6 | Negative supply shadow accounting | future |
| IV | 7 | Dynamic wallet count / negative position | future |
| IV | 8 | Unified controller simulation | future |
| IV | 9 | Public network progression | future |

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
consensus validation. Every feature carries one of four statuses:
`LOCKED`, `EXPERIMENTAL`, `OPEN`, `FUTURE`.

## Build & test

See `deploy/Makefile`. CI runs build + `make cpp-test` + regtest on every push.

## Immediate next milestone

Fixed Multi-Chain Shared-State Consensus (Phase 2 substrate): prove that a
wallet can spend the same globally-valid currency through either of two fixed
parallel chains while guaranteeing

```
Spend(U, Chain_A) => NOT Spend(U, Chain_B)
```

for the same spendable state U. See `docs/litenyx_consensus_spec_v0.1.md`.
