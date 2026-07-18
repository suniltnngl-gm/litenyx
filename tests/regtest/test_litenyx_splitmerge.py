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
import subprocess
import tempfile
from pathlib import Path

import pytest

LITENYXD = os.environ.get("LITENYXD_BIN") or shutil.which("dogecoind") or "dogecoind"
LITENYX_CLI = (
    os.environ.get("LITENYX_CLI_BIN") or shutil.which("dogecoin-cli") or "dogecoin-cli"
)

RPC_USER = "Litenyx"
RPC_PASSWORD = "litenyxtest"


def _have_binaries() -> bool:
    return shutil.which(LITENYXD) is not None and shutil.which(LITENYX_CLI) is not None


@pytest.fixture(scope="module")
def regtest_node():
    """Start a fresh regtest dogecoind, mine a few blocks, then tear down."""
    if not _have_binaries():
        pytest.skip(f"dogecoind/dogecoin-cli not found ({LITENYXD}, {LITENYX_CLI})")

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

    _rpc("createwallet", "w")
    _rpc("generatetoaddress", 5, _rpc("getnewaddress"))

    yield _rpc

    subprocess.run(cli + ["stop"], capture_output=True, text=True, env=env)
    shutil.rmtree(datadir, ignore_errors=True)


def test_node_reachable_and_mining(regtest_node):
    """Phase 1 gate (skeleton): node mines and reports a positive height."""
    height = regtest_node("getblockcount")
    assert isinstance(height, int)
    assert height >= 5
