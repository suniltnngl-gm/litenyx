# Litenyx Technical White Paper — v0.2

*Status: LOCKED (constitutional). Changes require explicit re-freeze.*

## 1. Why this system exists

Prior experiments (KerrNyx, Veltrix, WaveCore) explored multi-chain topologies
for a single shared currency. The lessons that are now locked:

1. **One currency, one global monetary state.** Multiple chains must share a
   single UTXO universe. There is no per-chain ledger and no bridge. A coin is
   spendable on any chain; spending it on one chain must make it unspendable on
   all others.
2. **Chains are acceptance parameters, not ledgers.** Chains differ only in
   fee rate, block size, and block time. They are lanes, not separate economies.
3. **Topology is endogenous and target-regulated.** Chain count changes
   (split/merge) are driven by observed endogenous signals (per-chain interval
   ratio), not by external hash observation or by wish.
4. **Consensus core is sacred.** Experimental economic and wallet mechanisms
   live behind a strict `ConsensusCore != RuntimePolicy != WalletPolicy`
   boundary so they can never break basic validation.
5. **Gradual introduction.** Experimental mechanisms are introduced one phase
   at a time with activation gates, never all at once.

Litenyx applies these principles on top of a **Dogecoin** baseline (Scrypt PoW +
Dogecoin-style AuxPoW), chosen for its proven AuxPoW and UTXO machinery.

## 2. The shared-state invariant

The central engineering problem of a shared-state multi-chain is double spend
across chains. The invariant we must hold:

```
Spend(U, Chain_A)  =>  NOT Spend(U, Chain_B)      for the same spendable state U
```

This is solved by a **single global spent-state** keyed on the outpoint, not on
the chain. A transaction input spends an outpoint; once spent, that outpoint is
globally spent regardless of which chain confirmed it. Chains are therefore
*validation contexts* that all write to one shared spent-set.

## 3. Why fixed chains first

Dynamic split/merge (Phase 3) assumes the shared-state machinery already works
for a fixed N. If two fixed chains cannot guarantee the invariant under
adversarial reorgs and double spends, adding a moving N only multiplies the
failure modes. Therefore Phase 2 (fixed N=2) is the substrate and the gating
milestone before any topology change is allowed.

### 3.1 Phase 2 status — GREEN (frozen)

Phase 2 is formally accepted and frozen at tag **`phase2-green`** (commit
`f5ff75b`, CI run `29638614462`). The fixed two-chain shared-state core proves
`Spend(U, A) => NOT Spend(U, B)` with reorg-safe rollback, validated by the
mandatory CI regtest gate (4/4 tests) plus C++ unit tests. The Phase-2
acceptance gate is closed.

### 3.2 Phase 3 — Dynamic Chain Split/Merge (OPEN)

With the substrate proven, Phase 3 adds a **deterministic, target-regulated
topology controller** that varies the active chain count `N_h` between the
locked bounds `LITENYX_MIN_CHAINS = 2` and a fixed `LITENYX_MAX_CHAINS`. It
introduces no other FUTURE controller. Full behavioral definition:
`docs/litenyx_topology_spec_v0.1.md`.

The Phase-3 acceptance invariant extends Phase 2:

```
forall N_h,   Spend(U, C_i) => NOT Spend(U, C_j),   i != j
```

and additionally requires that any `N -> N±1` transition does NOT cause
currency duplication, state loss, or spend-state loss. The locked pattern
continues: lock principle -> specify -> standalone proof -> daemon integration
-> mandatory adversarial regtest -> GREEN tag.

## 4. Why controllers come later

Block size (Phase 4) and block reward (Phase 5) are actuators on capacity and
positive supply. Negative supply (Phase 6) and dynamic wallet count / negative
position (Phase 7) are even more experimental and are first run as
*shadow accounting* (calculated but not enforced) so their mathematics can be
observed without risking real balances. Only after each is understood and
manipulation-resistant do they approach enforcement.

## 5. Stability, not just correctness

The end goal (Phase 8) is not that each controller works in isolation, but that
the coupled control system is stable: capacity, security, fee pressure, supply,
and wallet population must not produce unintended positive feedback when their
thresholds cross simultaneously.

## 6. Deployment discipline

Progression is Regtest -> Private Devnet -> Persistent Devnet -> Public Testnet
-> Adversarial Testnet -> Mainnet, with an explicit activation gate between each
stage. AuxPoW and basic multi-chain operation may activate before the more
experimental monetary controllers.
