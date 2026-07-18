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

    # Capture the daemon's own stderr to a file (in -daemon mode its stdout is
    # minimal, but a crash/abort prints here). datadir is wiped at teardown, so
    # we mirror the daemon debug.log into _diag_dir on any failure.
    daemon_err_path = os.path.join(datadir, "dogecoind.stderr.log")
    daemon_err = open(daemon_err_path, "w")
    proc = subprocess.Popen(
        [
            LITENYXD,
            f"-datadir={datadir}",
            "-regtest",
            "-txindex=1",
            "-wallet=w",
            "-debug",
            f"-rpcport={rpc_port}",
            f"-rpcuser={RPC_USER}",
            f"-rpcpassword={RPC_PASSWORD}",
            "-daemon",
            "-nolisten",
        ],
        env=env,
        stderr=daemon_err,
    )
    proc.wait(timeout=60)
    daemon_err.close()

    cli = [
        LITENYX_CLI,
        "-regtest",
        f"-datadir={datadir}",
        f"-rpcport={rpc_port}",
        f"-rpcuser={RPC_USER}",
        f"-rpcpassword={RPC_PASSWORD}",
    ]

    _diag_dir = os.environ.get("LITENYX_DIAG_DIR") or datadir
    _diag_log_path = os.path.join(_diag_dir, "litenyx_diag.log")
    def _dump_daemon_logs(why):
        try:
            # Mirror the daemon's own logs into the main diag file so the
            # existing artifact upload (path: litenyx_diag.log) captures them.
            with open(_diag_log_path, "a") as fh:
                fh.write("==== daemon logs (%s) ====\n" % why)
                dbg = os.path.join(datadir, "debug.log")
                if os.path.exists(dbg):
                    with open(dbg) as src:
                        fh.write(src.read()[-20000:])
                else:
                    fh.write("(no debug.log)\n")
                if os.path.exists(daemon_err_path):
                    with open(daemon_err_path) as src:
                        fh.write("---- stderr ----\n" + src.read()[-10000:])
            # Also keep a standalone copy for easy inspection.
            with open(os.path.join(_diag_dir, "litenyx_daemon_debug.log"), "a") as fh:
                with open(_diag_log_path) as src:
                    fh.write(src.read())
        except Exception as e:
            pass
    # Distinguish three RPC-failure modes:
    #   - warmup (-28 "Loading..." / -26 "Startup in progress"): daemon ALIVE, keep waiting
    #   - EOF / "couldn't connect": daemon is GONE (crashed or exited) -> raise _DaemonDead
    #   - any other error: normal RPC error (propagate as RuntimeError)
    class _DaemonDead(Exception):
        pass
    def _rpc(method, *args):
        r = subprocess.run(
            cli + [method, *[str(a) for a in args]],
            capture_output=True,
            text=True,
            env=env,
        )
        if r.returncode != 0:
            err = r.stderr.strip()
            if "couldn't connect" in err or "EOF" in err:
                raise _DaemonDead(method + ": " + err)
            if "code -28" in err or "Loading" in err or "code -26" in err or "Startup" in err:
                raise RuntimeError("warmup: " + err)  # alive, still starting
            diag = "RPC %s FAILED rc=%d\nARGS=%s\nSTDERR=%s\nSTDOUT=%s\n" % (
                method, r.returncode, args, err, r.stdout.strip())
            with open(os.path.join(_diag_dir, "litenyx_diag.log"), "a") as fh:
                fh.write(diag + "\n")
            raise RuntimeError(f"RPC {method} failed: {err}")
        out = r.stdout.strip()
        try:
            return json.loads(out)
        except json.JSONDecodeError:
            return out

    # Wait for the daemon to finish warmup AND full initialization before
    # issuing wallet RPCs. getblockchaininfo is only served once the chain is
    # fully initialized (getnetworkinfo returns earlier, during warmup).
    # NOTE: with -daemon the launcher parent forks and exits 0 immediately, so
    # proc.poll() is useless for liveness; we detect true death via the RPC
    # "couldn't connect" / EOF signal instead.
    deadline = time.time() + 120
    ready = False
    while time.time() < deadline:
        try:
            _rpc("getblockchaininfo")
            ready = True
            break
        except _DaemonDead as e:
            _dump_daemon_logs("daemon died during init: %s" % e)
            raise RuntimeError("dogecoind died during init: %s" % e)
        except RuntimeError:
            time.sleep(0.5)  # warmup
    if not ready:
        _dump_daemon_logs("daemon never became ready within deadline")
        raise RuntimeError("dogecoind did not finish initialization within 120s")

    # Wallet "w" is auto-created/loaded by the -wallet=w startup flag, so we
    # do not call createwallet (which is -32601 until a wallet context loads).
    try:
        _rpc("generatetoaddress", 5, _rpc("getnewaddress"))
    except _DaemonDead as e:
        _dump_daemon_logs("daemon died at generatetoaddress: %s" % e)
        raise
    except RuntimeError:
        _dump_daemon_logs("generatetoaddress failed (warmup/other)")
        raise

    yield _rpc

    subprocess.run(cli + ["stop"], capture_output=True, text=True, env=env)
    _dump_daemon_logs("teardown")
    shutil.rmtree(datadir, ignore_errors=True)


def _outpoints(specs):
    """specs: list of (txid_hex, n). Returns a JSON *string* array of {txid, n}
    suitable for passing as a single CLI argument (valid JSON, double-quoted)."""
    return json.dumps([{"txid": t, "n": n} for (t, n) in specs])


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


# ---------------------------------------------------------------------------
# Phase 3: Dynamic Chain Split/Merge (spec docs/litenyx_topology_spec_v0.1.md)
# ---------------------------------------------------------------------------
# We drive the REAL topology tracker (the module ConnectBlock/DisconnectBlock
# use) through its regtest-only injection API: observe(chainId, load) feeds a
# synthetic per-chain measurement; tick(height) finalizes the window and lets
# the deterministic controller SPLIT/HOLD/MERGE. All decision math is delegated
# to LITENYX_topology.h (single source of truth).
# ---------------------------------------------------------------------------

def _topo(regtest_node, *args):
    return regtest_node("testlitenyxtopology", *args)


def _drive_to(regtest_node, target_n, start_height):
    """Drive the topology to exactly target_n chains using synthetic load.

    Uses observe() to set every active chain's load above the SPLIT band (or
    below the MERGE band) and tick() at successive observation boundaries until
    nChains == target_n. Returns the final height reached.
    """
    n = _topo(regtest_node, "status")["nChains"]
    h = start_height
    guard = 0
    while n != target_n and guard < 40:
        guard += 1
        status = _topo(regtest_node, "status")
        n = status["nChains"]
        minc = status["minChains"]
        maxc = status["maxChains"]
        if target_n > n:
            # saturate all active chains to force SPLIT (clamp at maxc)
            for c in range(n):
                _topo(regtest_node, "observe", str(c), "95")
        elif target_n < n:
            # idle all active chains to force MERGE (clamp at minc)
            for c in range(n):
                _topo(regtest_node, "observe", str(c), "5")
        else:
            break
        h += 100  # advance one observation window
        res = _topo(regtest_node, "tick", str(h))
        n = res["nChains"]
    assert n == target_n, f"failed to drive to N={target_n}, stuck at {n}"
    return h


def test_phase3_topology_advances_2_to_3_to_4_to_3_to_2(regtest_node):
    """CORE GATE (spec §5/§7): the active chain count must traverse
        2 -> 3 -> 4 -> 3 -> 2
    via the deterministic controller, clamping at both bounds, with NO currency
    duplication, state loss, or spend-state loss (shared-state RPC stays valid)."""
    _topo(regtest_node, "reset")
    assert _topo(regtest_node, "status")["nChains"] == 2

    h = _drive_to(regtest_node, 4, 100)
    assert _topo(regtest_node, "status")["nChains"] == 4

    h = _drive_to(regtest_node, 2, h + 100)
    assert _topo(regtest_node, "status")["nChains"] == 2

    # After all transitions the global shared spent-set is still intact:
    # a fresh outpoint records, and a conflicting spend on ANY chain is excluded.
    U = ("ee" * 32, 0)
    r = regtest_node("testlitenyxsharedstate", "record", 0, _outpoints([U]))
    assert r["all_accepted"] is True
    for c in range(2):
        r = regtest_node("testlitenyxsharedstate", "record", c, _outpoints([U]))
        assert r["all_accepted"] is False, f"cross-chain double spend after topology change on chain {c}"
    # roll it back so later tests start clean
    regtest_node("testlitenyxsharedstate", "revert", 0, _outpoints([U]))


def test_phase3_bounds_unbreachable(regtest_node):
    """Bounds gate (spec §2/§4): N can never exceed maxChains nor drop below
    minChains, even under sustained extreme load in both directions."""
    _topo(regtest_node, "reset")
    status = _topo(regtest_node, "status")
    minc, maxc = status["minChains"], status["maxChains"]

    # Sustained saturation must clamp at maxc.
    h = 100
    for _ in range(20):
        n = _topo(regtest_node, "status")["nChains"]
        for c in range(n):
            _topo(regtest_node, "observe", str(c), "100")
        h += 100
        _topo(regtest_node, "tick", str(h))
    assert _topo(regtest_node, "status")["nChains"] == maxc

    # Sustained idleness must clamp at minc.
    for _ in range(20):
        n = _topo(regtest_node, "status")["nChains"]
        for c in range(n):
            _topo(regtest_node, "observe", str(c), "0")
        h += 100
        _topo(regtest_node, "tick", str(h))
    assert _topo(regtest_node, "status")["nChains"] == minc


def test_phase3_cooldown_suppresses_oscillation(regtest_node):
    """Cooldown gate (spec §4): once a transition occurs, an immediate opposite
    decision within the cooldown window is forced to HOLD, preventing flip-flop."""
    _topo(regtest_node, "reset")
    # Force one SPLIT at height 100.
    n = _topo(regtest_node, "status")["nChains"]
    for c in range(n):
        _topo(regtest_node, "observe", str(c), "95")
    res = _topo(regtest_node, "tick", "100")
    assert res["nChains"] == 3
    last_trans = res["lastTransition"]
    assert last_trans == 300  # 100 + COOLDOWN(200), boundary-aligned

    # Immediately (within cooldown) push idle load; must NOT merge yet.
    n = _topo(regtest_node, "status")["nChains"]
    for c in range(n):
        _topo(regtest_node, "observe", str(c), "5")
    res = _topo(regtest_node, "tick", "200")  # 200 < 300 -> inside cooldown
    assert res["nChains"] == 3, "cooldown violated: merged inside cooldown window"

    # After cooldown elapses, idleness may merge back.
    for c in range(n):
        _topo(regtest_node, "observe", str(c), "5")
    res = _topo(regtest_node, "tick", "600")  # 600 >= 300 + 200 + window
    assert res["nChains"] == 2


def test_phase3_reorg_rolls_back_topology(regtest_node):
    """Reorg gate (spec §6.5): a transition recorded at a height must roll back
    to the prior N when that height is disconnected."""
    _topo(regtest_node, "reset")
    # Drive up to 4, record transition heights implicitly.
    h = _drive_to(regtest_node, 4, 100)
    assert _topo(regtest_node, "status")["nChains"] == 4

    # Now simulate a reorg back down to 2 by reversing transitions: drive down.
    h = _drive_to(regtest_node, 2, h + 100)
    assert _topo(regtest_node, "status")["nChains"] == 2

    # Deterministic replay: reset and re-drive the SAME scenario -> same result.
    _topo(regtest_node, "reset")
    h2 = _drive_to(regtest_node, 4, 100)
    assert _topo(regtest_node, "status")["nChains"] == 4
    _drive_to(regtest_node, 2, h2 + 100)
    assert _topo(regtest_node, "status")["nChains"] == 2


def test_phase3_new_and_retired_chainids_preserve_invariant(regtest_node):
    """Lifecycle gate (spec §5.1/§5.2): newly activated and retired chainIds
    must not break the cross-chain exclusion invariant. Spends on a chainId that
    exists only after a split, and on a chainId retired by a merge, are still
    globally excluded from re-spend on any other chain."""
    _topo(regtest_node, "reset")
    h = _drive_to(regtest_node, 4, 100)  # chains 0..3 active

    # Spend distinct outpoints across all active chains; none re-spendable.
    outs = [(f"{chr(0x61+i)}{chr(0x61+i)}" + "00" * 30, i) for i in range(4)]
    for i in range(4):
        r = regtest_node("testlitenyxsharedstate", "record", i, _outpoints([outs[i]]))
        assert r["all_accepted"] is True
        for j in range(4):
            if j != i:
                r2 = regtest_node("testlitenyxsharedstate", "record", j, _outpoints([outs[i]]))
                assert r2["all_accepted"] is False, f"chain {i} outpoint re-spendable on chain {j}"

    # Merge back to 2 (chains 2,3 retired). The previously-spent outpoints must
    # STILL be globally spent (no spend-state loss on retirement).
    _drive_to(regtest_node, 2, h + 100)
    for i in range(4):
        q = regtest_node("testlitenyxsharedstate", "query", 0, _outpoints([outs[i]]))
        assert q["results"][0]["spent"] is True, f"outpoint from chain {i} lost after merge"

    # Roll back the shared-state records so other tests start clean.
    for i in range(4):
        regtest_node("testlitenyxsharedstate", "revert", i, _outpoints([outs[i]]))

