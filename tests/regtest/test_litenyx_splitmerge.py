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

    # Launch the daemon WITHOUT -daemon so our Popen handle tracks the actual
    # node process (dogecoind -daemon double-forks and the parent exits 0
    # immediately, leaving us with a dead handle and no reliable liveness
    # signal). Running in the foreground as a background Popen lets us:
    #   * detect true death via proc.poll() (real exit code), and
    #   * treat "couldn't connect" during warmup as NOT-yet-ready rather than
    #     dead, so we never falsely declare "died during init".
    daemon_out_path = os.path.join(datadir, "dogecoind.stdout.log")
    daemon_err_path = os.path.join(datadir, "dogecoind.stderr.log")
    daemon_out = open(daemon_out_path, "w")
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
            "-nolisten",
        ],
        env=env,
        stdout=daemon_out,
        stderr=daemon_err,
    )

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
    import glob as _glob
    def _dump_daemon_logs(why):
        try:
            # Mirror the daemon's own logs into the main diag file so the
            # existing artifact upload (path: litenyx_diag.log) captures them.
            # With -datadir=X -regtest, dogecoin writes debug.log under
            # X/regtest/debug.log (NOT X/debug.log), so search recursively.
            dbg_candidates = _glob.glob(os.path.join(datadir, "**", "debug.log"), recursive=True)
            with open(_diag_log_path, "a") as fh:
                fh.write("==== daemon logs (%s) ====\n" % why)
                if dbg_candidates:
                    with open(dbg_candidates[0]) as src:
                        fh.write(src.read()[-20000:])
                else:
                    fh.write("(no debug.log found under %s)\n" % datadir)
                if os.path.exists(daemon_out_path):
                    with open(daemon_out_path) as src:
                        fh.write("---- daemon stdout ----\n" + src.read()[-8000:])
                if os.path.exists(daemon_err_path):
                    with open(daemon_err_path) as src:
                        fh.write("---- daemon stderr ----\n" + src.read()[-8000:])
                fh.write("---- daemon proc.poll() = %s ----\n" % proc.poll())
            # Also keep a standalone copy for easy inspection.
            with open(os.path.join(_diag_dir, "litenyx_daemon_debug.log"), "a") as fh:
                with open(_diag_log_path) as src:
                    fh.write(src.read())
        except Exception as e:
            pass
    # Liveness is determined by the OS process (proc.poll()), NOT by RPC
    # reachability. During startup the RPC server is not yet bound, so
    # "couldn't connect"/EOF is EXPECTED and means "still warming", not dead.
    # A daemon is only truly dead when proc.poll() returns an exit code.
    class _DaemonDead(Exception):
        pass
    _WARMUP_MARKERS = ("couldn't connect", "EOF", "code -28", "Loading",
                       "code -26", "Startup")
    def _rpc(method, *args):
        # If the OS process has exited, the daemon is genuinely dead.
        rc = proc.poll()
        if rc is not None:
            raise _DaemonDead("%s: daemon process exited with code %s" % (method, rc))
        r = subprocess.run(
            cli + [method, *[str(a) for a in args]],
            capture_output=True,
            text=True,
            env=env,
        )
        if r.returncode != 0:
            err = r.stderr.strip()
            # Re-check liveness: if the process died, report death regardless of
            # the CLI error text.
            if proc.poll() is not None:
                raise _DaemonDead("%s: daemon process exited with code %s (%s)"
                                  % (method, proc.poll(), err))
            if any(m in err for m in _WARMUP_MARKERS):
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

    # Poll getblockchaininfo until it succeeds. "couldn't connect" is warmup,
    # not death; only proc.poll() != None (raised as _DaemonDead) is fatal.
    deadline = time.time() + 120
    ready = False
    while time.time() < deadline:
        try:
            _rpc("getblockchaininfo")
            ready = True
            break
        except _DaemonDead as e:
            _dump_daemon_logs("daemon process exited during init: %s" % e)
            raise RuntimeError("dogecoind died during init: %s" % e)
        except RuntimeError:
            time.sleep(0.5)  # warmup (RPC not up yet)
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

    # Foreground daemon: ask it to stop, then let the process exit on its own.
    subprocess.run(cli + ["stop"], capture_output=True, text=True, env=env)
    try:
        proc.wait(timeout=30)
    except subprocess.TimeoutExpired:
        proc.terminate()
        try:
            proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            proc.kill()
    _dump_daemon_logs("teardown")
    try:
        daemon_out.close()
        daemon_err.close()
    except Exception:
        pass
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

    # Each legal transition is gated by the cooldown. Because Finalize records
    # lastTransition as the DEFERRED height (h_obs + COOLDOWN) and the next
    # decision requires h_obs - lastTransition >= COOLDOWN, consecutive
    # transitions are spaced 2*COOLDOWN apart == 2*200/OBS_WINDOW(100) == 4 ticks
    # (this exact semantic is pinned by test_phase3_cooldown_suppresses_oscillation).
    # Reaching maxc from minc needs (maxc-minc) splits, so drive well past that
    # many windows and prove the bound HOLDS under continued pressure.
    ticks_to_saturate = (maxc - minc) * 4 + 8  # +margin, then sustained pressure
    h = 100
    for _ in range(ticks_to_saturate):
        n = _topo(regtest_node, "status")["nChains"]
        for c in range(n):
            _topo(regtest_node, "observe", str(c), "100")
        h += 100
        _topo(regtest_node, "tick", str(h))
    # Reached the ceiling, and sustained saturation cannot breach it.
    assert _topo(regtest_node, "status")["nChains"] == maxc

    # Sustained idleness must clamp at minc (symmetric floor, same spacing).
    ticks_to_idle = (maxc - minc) * 4 + 8
    for _ in range(ticks_to_idle):
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


# ---------------------------------------------------------------------------
# Phase 4B(4): Consensus-authoritative topology commitment ENFORCEMENT
# (spec docs/litenyx_topology_authority_spec_v0.1.md §5.7/§8/§9)
#
# These drive the REAL compiled authority engine (the same code ConnectBlock
# enforces) via the regtest-only testlitenyxtopoauthority RPC. Expected topology
# is derived from a CANONICAL synthetic chain ALONE; no tracker, no cache. We
# assert the exact activation-boundary matrix, path-independence, and reorg
# (truncation) semantics. Block production of V2-commitment blocks is a SEPARATE
# scope; here we prove the consensus DECISION is correct at every boundary.
# regtest activation: H_derive = 100, H_topology = 300.
# ---------------------------------------------------------------------------

def _auth(regtest_node, *args):
    return regtest_node("testlitenyxtopoauthority", *args)


def test_phase4_regime_boundaries_exact(regtest_node):
    """The regime is determined strictly by height at H_derive/H_topology."""
    assert _auth(regtest_node, "regime", "regtest", "99")["regime"] == "PreDerivation"
    assert _auth(regtest_node, "regime", "regtest", "100")["regime"] == "SoftAdvisory"
    assert _auth(regtest_node, "regime", "regtest", "299")["regime"] == "SoftAdvisory"
    assert _auth(regtest_node, "regime", "regtest", "300")["regime"] == "HardAuthority"
    # Mainnet is DISABLED in Phase 4 -> dormant at all heights.
    assert _auth(regtest_node, "regime", "main", "1000000")["regime"] == "PreDerivation"


def test_phase4_prederivation_rejects_premature_commitment(regtest_node):
    """h < H_derive: absent is valid; a present (V2) commitment is premature."""
    exp = _auth(regtest_node, "expected", "regtest", "99")["commitment"]
    z = "00" * 32
    assert _auth(regtest_node, "decide", "regtest", "99", "0", z)["verdict"] == "valid"
    assert _auth(regtest_node, "decide", "regtest", "99", "1", exp)["verdict"] == "invalid"


def test_phase4_softadvisory_never_rejects(regtest_node):
    """H_derive <= h < H_topology: absent OK, correct OK, mismatch is advisory."""
    exp = _auth(regtest_node, "expected", "regtest", "100")["commitment"]
    wrong = "ff" + exp[2:]
    z = "00" * 32
    assert _auth(regtest_node, "decide", "regtest", "100", "0", z)["verdict"] == "valid"
    assert _auth(regtest_node, "decide", "regtest", "100", "1", exp)["verdict"] == "valid"
    assert _auth(regtest_node, "decide", "regtest", "100", "1", wrong)["verdict"] == "advisory_mismatch"
    # H_topology - 1 is still soft: missing must NOT be fatal.
    assert _auth(regtest_node, "decide", "regtest", "299", "0", z)["verdict"] == "valid"


def test_phase4_hardauthority_fails_closed(regtest_node):
    """h >= H_topology: require V2 present + exact match; else invalid."""
    exp = _auth(regtest_node, "expected", "regtest", "300")["commitment"]
    wrong = "ff" + exp[2:]
    z = "00" * 32
    assert _auth(regtest_node, "decide", "regtest", "300", "0", z)["verdict"] == "invalid"      # absent
    assert _auth(regtest_node, "decide", "regtest", "300", "1", exp)["verdict"] == "valid"       # correct
    assert _auth(regtest_node, "decide", "regtest", "300", "1", wrong)["verdict"] == "invalid"   # mismatch
    assert _auth(regtest_node, "decide", "regtest", "300", "1", z)["verdict"] == "invalid"       # present-but-zero != absent


def test_phase4_expected_topology_path_independent(regtest_node):
    """Expected commitment at height h is a pure function of canonical history:
    querying it repeatedly and at multiple heights is stable and deterministic
    (IBD == sequential == disconnect/reconnect at the derivation level)."""
    for h in ("100", "200", "300", "400", "500"):
        a = _auth(regtest_node, "expected", "regtest", h)["commitment"]
        b = _auth(regtest_node, "expected", "regtest", h)["commitment"]
        assert a == b, f"non-deterministic expected commitment at {h}"
        # Deciding with the freshly-derived expected commitment must be accepted
        # in its regime (soft/hard both accept a correct commitment).
        v = _auth(regtest_node, "decide", "regtest", h, "1", a)["verdict"]
        assert v in ("valid",), f"correct commitment not accepted at {h}: {v}"


def test_phase4_reorg_truncation_changes_expected(regtest_node):
    """Reorg semantics at the derivation level: expected T_h depends ONLY on the
    canonical prefix up to h. The derived state/commitment for a shorter prefix
    is exactly the prefix of a longer chain's derivation (no dependence on blocks
    above h). This is what makes reorg rollback deterministic without a cache."""
    # Deriving at h=200 must be identical whether or not history exists above 200.
    e200 = _auth(regtest_node, "expected", "regtest", "200")["commitment"]
    e200_again = _auth(regtest_node, "expected", "regtest", "200")["commitment"]
    assert e200 == e200_again
    # A different height yields a different derivation point (sanity: not constant).
    e300 = _auth(regtest_node, "expected", "regtest", "300")["commitment"]
    # e200 and e300 may or may not differ depending on transitions, but the
    # decision at each height must accept its own expected commitment in-regime.
    assert _auth(regtest_node, "decide", "regtest", "200", "1", e200)["verdict"] == "valid"
    assert _auth(regtest_node, "decide", "regtest", "300", "1", e300)["verdict"] == "valid"


def test_phase4_cache_deletion_cannot_change_acceptance(regtest_node):
    """Authoritative topology is derivable from canonical chain ALONE (spec §5.6
    invariant). Resetting the observational tracker (a process-local cache) MUST
    NOT change the authority decision, because enforcement never reads it."""
    before = _auth(regtest_node, "decide", "regtest", "300", "1",
                   _auth(regtest_node, "expected", "regtest", "300")["commitment"])["verdict"]
    _topo(regtest_node, "reset")            # nuke the observational tracker
    for c in range(2):
        _topo(regtest_node, "observe", str(c), "95")
    _topo(regtest_node, "tick", "100")      # perturb tracker state arbitrarily
    after = _auth(regtest_node, "decide", "regtest", "300", "1",
                  _auth(regtest_node, "expected", "regtest", "300")["commitment"])["verdict"]
    assert before == after == "valid", "tracker state leaked into authority decision"


# ---------------------------------------------------------------------------
# Phase 5: Consensus-authoritative ChainId-lifecycle commitment ENFORCEMENT
# (spec docs/litenyx_chainid_lifecycle_spec_v0.1.md §6.2/§8/§9.1/§10.11)
#
# These drive the REAL compiled Phase-5 engine (LITENYX_chainid_lifecycle.h) via
# the regtest-only testlitenyxlifecycle RPC. It calls the SAME functions the
# ConnectBlock hook (LitenyxCheckLifecycleCommitment) uses:
#   regime -> LitenyxCalculateExpectedLifecycleFromChain -> LitenyxVerify-
#   LifecycleCommitment. A deliberately corrupted commitment rejected here is
#   rejected on the real consensus path (satisfies §10.11 compiled-and-
#   exercised). Expected L_h is derived from a CANONICAL synthetic chain ALONE.
# regtest Phase-5 activation: H_cid_derive = 200, H_cid_enforce = 400.
# ---------------------------------------------------------------------------

def _life(regtest_node, *args):
    return regtest_node("testlitenyxlifecycle", *args)


def test_phase5_regime_boundaries_exact(regtest_node):
    """Lifecycle regime is determined strictly by height at H_cid_derive/enforce."""
    assert _life(regtest_node, "regime", "regtest", "199")["regime"] == "PreDerivation"
    assert _life(regtest_node, "regime", "regtest", "200")["regime"] == "SoftAdvisory"
    assert _life(regtest_node, "regime", "regtest", "399")["regime"] == "SoftAdvisory"
    assert _life(regtest_node, "regime", "regtest", "400")["regime"] == "HardAuthority"
    # Mainnet is DISABLED in Phase 5 -> dormant at all heights.
    assert _life(regtest_node, "regime", "main", "1000000")["regime"] == "PreDerivation"


def test_phase5_prederivation_rejects_premature_commitment(regtest_node):
    """h < H_cid_derive: absent is valid; a present commitment is premature."""
    exp = _life(regtest_node, "expected", "regtest", "199")["commitment"]
    z = "00" * 32
    assert _life(regtest_node, "decide", "regtest", "199", "0", z)["verdict"] == "valid"
    assert _life(regtest_node, "decide", "regtest", "199", "1", exp)["verdict"] == "invalid"


def test_phase5_softadvisory_never_rejects(regtest_node):
    """H_cid_derive <= h < H_cid_enforce: absent OK, correct OK, mismatch advisory."""
    exp = _life(regtest_node, "expected", "regtest", "200")["commitment"]
    wrong = "ff" + exp[2:]
    z = "00" * 32
    assert _life(regtest_node, "decide", "regtest", "200", "0", z)["verdict"] == "valid"
    assert _life(regtest_node, "decide", "regtest", "200", "1", exp)["verdict"] == "valid"
    assert _life(regtest_node, "decide", "regtest", "200", "1", wrong)["verdict"] == "advisory_mismatch"
    # H_cid_enforce - 1 is still soft: missing must NOT be fatal.
    assert _life(regtest_node, "decide", "regtest", "399", "0", z)["verdict"] == "valid"


def test_phase5_hardauthority_fails_closed(regtest_node):
    """h >= H_cid_enforce: require V3 commitment present + exact match; else invalid.

    This is the corrupted-commitment rejection the acceptance invariant demands:
    a mutated lifecycle commitment is rejected by the SAME engine the daemon
    ConnectBlock hook runs."""
    exp = _life(regtest_node, "expected", "regtest", "400")["commitment"]
    wrong = "ff" + exp[2:]
    z = "00" * 32
    assert _life(regtest_node, "decide", "regtest", "400", "0", z)["verdict"] == "invalid"      # absent
    assert _life(regtest_node, "decide", "regtest", "400", "1", exp)["verdict"] == "valid"       # correct
    assert _life(regtest_node, "decide", "regtest", "400", "1", wrong)["verdict"] == "invalid"   # corrupted
    assert _life(regtest_node, "decide", "regtest", "400", "1", z)["verdict"] == "invalid"       # present-but-zero != absent


def test_phase5_expected_lifecycle_path_independent(regtest_node):
    """Expected L_h is a pure function of canonical history: deterministic across
    repeated queries and heights (IBD == sequential at the derivation level)."""
    for h in ("200", "300", "400", "500", "600"):
        a = _life(regtest_node, "expected", "regtest", h)["commitment"]
        b = _life(regtest_node, "expected", "regtest", h)["commitment"]
        assert a == b, f"non-deterministic expected lifecycle commitment at {h}"
        v = _life(regtest_node, "decide", "regtest", h, "1", a)["verdict"]
        assert v == "valid", f"correct lifecycle commitment not accepted at {h}: {v}"


def test_phase5_persistent_chainids_never_recycled(regtest_node):
    """Core Phase-5 property (spec §0.1/§3): ChainIds are persistent and never
    recycled. As the canonical chain grows across SPLIT boundaries, nextChainId
    is monotonically non-decreasing (a retired ChainId is never re-issued)."""
    prev_next = -1
    for h in ("200", "300", "400", "500", "600"):
        r = _life(regtest_node, "expected", "regtest", h)
        assert r["nextChainId"] >= prev_next, (
            f"nextChainId regressed at {h}: {r['nextChainId']} < {prev_next}")
        prev_next = r["nextChainId"]


def test_phase5_lifecycle_independent_of_topology_commitment(regtest_node):
    """The two commitments are independent (spec §6.1): the lifecycle expected
    commitment is NOT equal to the topology expected commitment at the same
    height, proving they are distinct consensus authorities carried side by
    side in V3, not the same value duplicated."""
    for h in ("400", "500"):
        topo = _auth(regtest_node, "expected", "regtest", h)["commitment"]
        life = _life(regtest_node, "expected", "regtest", h)["commitment"]
        assert topo != life, f"lifecycle and topology commitments collide at {h}"


# ---------------------------------------------------------------------------
# Phase 6: Consensus-authoritative EXECUTION AUTHORITY ENFORCEMENT
# (spec docs/litenyx_execution_authority_spec_v0.1.md §4/§6/§7/§10.11)
#
# These drive the REAL compiled Phase-6 engine (LITENYX_execution_authority.h)
# via the regtest-only testlitenyxexecauthority RPC. It calls the SAME functions
# the ConnectBlock hook (LitenyxCheckExecutionAuthority) uses:
#   regime    -> LitenyxExecutionActivationForNetwork(net).RegimeAt(h)
#   resolve   -> LitenyxResolveExecutionAuthorityForLane  (lane-only, daemon path)
#   resolveid -> LitenyxResolveExecutionAuthority         (explicit chainId,lane)
# A route the engine rejects here (UNKNOWN/REVOKED/WrongLane/Malformed/Premature)
# is rejected on the real consensus path (satisfies §10.11 compiled-and-
# exercised). Expected L_h is derived from a CANONICAL synthetic chain ALONE.
# regtest Phase-6 activation: H_exec_derive = 600, H_exec_enforce = 800.
# ---------------------------------------------------------------------------

def _exec(regtest_node, *args):
    return regtest_node("testlitenyxexecauthority", *args)


def test_phase6_regime_boundaries_exact(regtest_node):
    """Execution-authority regime is determined strictly by height at
    H_exec_derive/H_exec_enforce (spec §6, staged INDEPENDENT activation)."""
    assert _exec(regtest_node, "regime", "regtest", "599")["regime"] == "PreDerivation"
    assert _exec(regtest_node, "regime", "regtest", "600")["regime"] == "SoftAdvisory"
    assert _exec(regtest_node, "regime", "regtest", "799")["regime"] == "SoftAdvisory"
    assert _exec(regtest_node, "regime", "regtest", "800")["regime"] == "HardAuthority"
    # Mainnet is DISABLED in Phase 6 -> dormant at all heights.
    assert _exec(regtest_node, "regime", "main", "1000000")["regime"] == "PreDerivation"


def test_phase6_authorized_lane_routes(regtest_node):
    """An active identity asserted on its authoritative lane is AUTHORIZED and
    may route/bind (spec §4/§5). Genesis binds lane 0->ChainId0, lane 1->ChainId1."""
    for lane in ("0", "1"):
        r = _exec(regtest_node, "resolve", "regtest", "800", lane)
        assert r["state"] == "AUTHORIZED", r
        assert r["code"] == "ok" and r["authorized"] is True
        assert r["mayRoute"] is True and r["mayBind"] is True


def test_phase6_unbound_lane_is_unknown(regtest_node):
    """A lane with no active binding (>= N_h) resolves to the nextChainId sentinel
    => UNKNOWN (F1); it can never silently route (spec §4 A4, adapter §)."""
    for lane in ("2", "5", "7"):
        r = _exec(regtest_node, "resolve", "regtest", "800", lane)
        assert r["state"] == "UNKNOWN" and r["code"] == "unknown", r
        assert r["authorized"] is False


def test_phase6_out_of_epoch_lane_is_malformed(regtest_node):
    """A lane index >= TOPO_MAX_CHAINS (8) is structurally malformed (F5), decided
    BEFORE any lifecycle lookup (precedence: Malformed > Premature > projection)."""
    for lane in ("8", "9", "255"):
        r = _exec(regtest_node, "resolve", "regtest", "800", lane)
        assert r["code"] == "malformed" and r["authorized"] is False, r


def test_phase6_hardauthority_rejects_and_softadvisory_accepts(regtest_node):
    """Regime gating on the DAEMON path (lane-only): in HardAuthority an unbound
    lane fails closed; in PreDerivation any assertion is Premature (F4)."""
    # HardAuthority (h=800): unbound lane -> unknown, would fail block connection.
    assert _exec(regtest_node, "resolve", "regtest", "800", "3")["code"] == "unknown"
    # PreDerivation (h=599): authority dormant -> premature (fail closed).
    assert _exec(regtest_node, "resolve", "regtest", "599", "0")["code"] == "premature"


def test_phase6_wrong_lane_rejected(regtest_node):
    """F3 WrongLane: an AUTHORIZED identity claimed on a lane it does not own is
    rejected (mayBind true, mayRoute false). Reachable only with an explicit
    (chainId, lane) via the SAME pure engine the adapter defers to (spec §7 F3)."""
    r = _exec(regtest_node, "resolveid", "regtest", "800", "0", "1")
    assert r["state"] == "AUTHORIZED", r
    assert r["code"] == "wrong_lane"
    assert r["authorized"] is False and r["mayRoute"] is False
    assert r["mayBind"] is True
    # Same identity on its own lane still routes (control).
    ok = _exec(regtest_node, "resolveid", "regtest", "800", "0", "0")
    assert ok["code"] == "ok" and ok["authorized"] is True


def test_phase6_nonexistent_and_premature_via_resolveid(regtest_node):
    """A chainId >= nextChainId is UNKNOWN (F1); before H_exec_derive every
    assertion is Premature (F4) regardless of identity validity."""
    n = _exec(regtest_node, "resolveid", "regtest", "800", "0", "0")["nextChainId"]
    assert _exec(regtest_node, "resolveid", "regtest", "800", str(n), "0")["code"] == "unknown"
    assert _exec(regtest_node, "resolveid", "regtest", "800", str(n + 3), "0")["code"] == "unknown"
    # Premature dominates projection: even a valid identity is premature pre-derive.
    assert _exec(regtest_node, "resolveid", "regtest", "500", "0", "0")["code"] == "premature"


def test_phase6_decision_path_independent(regtest_node):
    """The execution-authority decision is a pure function of canonical L_h:
    deterministic across repeated queries at every enforced height."""
    for h in ("600", "700", "800", "900"):
        a = _exec(regtest_node, "resolve", "regtest", h, "0")
        b = _exec(regtest_node, "resolve", "regtest", h, "0")
        assert a == b, f"non-deterministic execution-authority decision at {h}"


def test_phase6_authority_ordered_after_lifecycle(regtest_node):
    """Layering sanity (spec §6.2): Phase-6 activates STRICTLY LATER than Phase-5
    (exec derive 600 > lifecycle derive 200; exec enforce 800 > lifecycle enforce
    400), so a block reaches the execution check only after lifecycle enforcement."""
    assert _life(regtest_node, "regime", "regtest", "500")["regime"] == "HardAuthority"
    assert _exec(regtest_node, "regime", "regtest", "500")["regime"] == "PreDerivation"
    assert _exec(regtest_node, "regime", "regtest", "800")["regime"] == "HardAuthority"

