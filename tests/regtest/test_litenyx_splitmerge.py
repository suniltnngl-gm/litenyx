"""Litenyx regtest harness skeleton (Phase 1 gate).

Drives the forked dogecoind via RPC to prove the single-chain baseline:
mine, transact, reorganize, sync from genesis, validate AuxPoW.

NOTE: This is a scaffold for Phase 1. The Phase-2 two-chain shared-state
matrix will be added once the fork identity (Phase 1) and the aux extension
(Phase 2) are wired into the daemon. Until then this file is a placeholder
that asserts the node is reachable and reports its status, so CI has a
runnable regtest target.

The Phase-2 acceptance matrix (spec §6) will exercise:
  - shared spent-set forbids cross-chain double spend,
  - anchor commits to same-chain parent,
  - reorg does not resurrect globally-spent outpoints.
"""

from __future__ import annotations

import json
import os
import random
import shutil
import time
import subprocess
import tempfile
from pathlib import Path

import pytest

# Phase-2 gate is MANDATORY. The built binaries must be provided explicitly via
# LITENYXD_BIN / LITENYX_CLI_BIN (set by CI from the Dogecoin build dir). We no
# longer silently fall back to a PATH lookup that may point at a stock dogecoind
# lacking the Litenyx shared-state RPC; if the expected binary is absent the
# harness fails loudly rather than skipping the acceptance gate.
def _resolve(bin_env, name):
    if bin_env:
        if not os.path.exists(bin_env):
            raise RuntimeError(
                f"Litenyx binary not found at explicit path {bin_env!r} "
                f"(set LITENYXD_BIN / LITENYX_CLI_BIN to the built dogecoind/dogecoin-cli)"
            )
        return bin_env
    # Last-resort PATH lookup (local dev only); still must exist.
    found = shutil.which(name)
    if not found:
        raise RuntimeError(
            f"Litenyx binary {name!r} not found on PATH and no explicit "
            f"LITENYXD_BIN / LITENYX_CLI_BIN provided. Phase-2 gate is mandatory."
        )
    return found

LITENYXD = _resolve(os.environ.get("LITENYXD_BIN"), "dogecoind")
LITENYX_CLI = _resolve(os.environ.get("LITENYX_CLI_BIN"), "dogecoin-cli")

RPC_USER = "Litenyx"
RPC_PASSWORD = "litenyxtest"


@pytest.fixture(scope="module")
def regtest_node():
    """Start a fresh regtest dogecoind, mine a few blocks, then tear down."""
    # _resolve already guarantees the binaries exist; this is a hard no-skip gate.

    datadir = tempfile.mkdtemp(prefix="Litenyx-regtest-")
    rpc_port = random.randint(20000, 40000)
    env = dict(os.environ)

    proc = subprocess.Popen(
        [
            LITENYXD,
            f"-datadir={datadir}",
            "-regtest",
            "-txindex=1",
            f"-rpcport={rpc_port}",
            f"-rpcuser={RPC_USER}",
            f"-rpcpassword={RPC_PASSWORD}",
            "-daemon",
            "-nolisten",
        ],
        env=env,
    )
    proc.wait(timeout=60)

    cli = [
        LITENYX_CLI,
        "-regtest",
        f"-datadir={datadir}",
        f"-rpcport={rpc_port}",
        f"-rpcuser={RPC_USER}",
        f"-rpcpassword={RPC_PASSWORD}",
    ]

    def _rpc(method, *args):
        r = subprocess.run(
            cli + [method, *[str(a) for a in args]],
            capture_output=True,
            text=True,
            env=env,
        )
        if r.returncode != 0:
            raise RuntimeError(f"RPC {method} failed: {r.stderr.strip()}")
        out = r.stdout.strip()
        try:
            return json.loads(out)
        except json.JSONDecodeError:
            return out

    # Wait for the daemon to finish warmup AND full initialization before
    # issuing wallet RPCs. getblockchaininfo is only served once the chain is
    # fully initialized (getnetworkinfo returns earlier, during warmup).
    deadline = time.time() + 90
    ready = False
    while time.time() < deadline:
        try:
            _rpc("getblockchaininfo")
            ready = True
            break
        except RuntimeError:
            time.sleep(0.5)
    if not ready:
        raise RuntimeError("dogecoind did not finish initialization within 90s")

    # createwallet may still race initialization; retry briefly.
    cw_deadline = time.time() + 30
    while time.time() < cw_deadline:
        try:
            _rpc("createwallet", "w")
            break
        except RuntimeError as e:
            if "Method not found" in str(e) or "warmup" in str(e).lower():
                time.sleep(0.5)
                continue
            raise
    _rpc("generatetoaddress", 5, _rpc("getnewaddress"))

    yield _rpc

    subprocess.run(cli + ["stop"], capture_output=True, text=True, env=env)
    shutil.rmtree(datadir, ignore_errors=True)


def _outpoints(specs):
    """specs: list of (txid_hex, n). Returns a JSON array of {txid, n}."""
    return [{"txid": t, "n": n} for (t, n) in specs]


def test_phase1_node_reachable_and_mining(regtest_node):
    """Phase 1 gate: node mines and reports a positive height."""
    height = regtest_node("getblockcount")
    assert isinstance(height, int)
    assert height >= 5


def test_phase2_cross_chain_double_spend_excluded(regtest_node):
    """CORE GATE (spec §3):

        Spend(U, Chain_A) Accepted
        => Spend(U, Chain_B) Rejected

    We drive the SAME global shared spent-set that ConnectBlock uses, via the
    regtest-only RPC. Spending outpoint U on chain 0 must make the conflicting
    spend on chain 1 rejected.
    """
    U = ("aa" * 32, 0)
    # Spend U on Chain_A (chainId 0).
    res = regtest_node("testlitenyxsharedstate", "record", 0, _outpoints([U]))
    print("DIAG record chain0 U:", res)
    assert res["all_accepted"] is True, f"first spend rejected: {res}"

    # Conflicting spend of the SAME U on Chain_B (chainId 1) must be rejected.
    res = regtest_node("testlitenyxsharedstate", "record", 1, _outpoints([U]))
    assert res["all_accepted"] is False, f"cross-chain double spend NOT excluded: {res}"

    # Query confirms U is globally spent (regardless of lane).
    q = regtest_node("testlitenyxsharedstate", "query", 0, _outpoints([U]))
    assert q["results"][0]["spent"] is True


def test_phase2_distinct_outpoints_independent(regtest_node):
    """Different outpoints spend independently on either chain."""
    A = ("bb" * 32, 0)
    B = ("cc" * 32, 0)
    regtest_node("testlitenyxsharedstate", "record", 0, _outpoints([A]))
    regtest_node("testlitenyxsharedstate", "record", 1, _outpoints([B]))
    q = regtest_node("testlitenyxsharedstate", "query", 0, _outpoints([A, B]))
    assert q["results"][0]["spent"] is True
    assert q["results"][1]["spent"] is True


def test_phase2_reorg_rolls_back_shared_state(regtest_node):
    """REORG GATE (canonical-history aware, not an irreversible flag):

        Reorg(A) rolls back the global spend state correctly,
        => a NEW canonical spend of U on Chain_B may become valid again.
    """
    U = ("dd" * 32, 0)
    # Spend U on chain 0.
    regtest_node("testlitenyxsharedstate", "record", 0, _outpoints([U]))
    q = regtest_node("testlitenyxsharedstate", "query", 0, _outpoints([U]))
    assert q["results"][0]["spent"] is True

    # Simulate a reorg that disconnects the block that spent U: revert it.
    regtest_node("testlitenyxsharedstate", "revert", 0, _outpoints([U]))
    q = regtest_node("testlitenyxsharedstate", "query", 0, _outpoints([U]))
    assert q["results"][0]["spent"] is False, "reorg did not roll back global spend state"

    # After rollback, U may be spent again — now canonically on Chain_B.
    res = regtest_node("testlitenyxsharedstate", "record", 1, _outpoints([U]))
    assert res["all_accepted"] is True, f"post-reorg spend rejected: {res}"
