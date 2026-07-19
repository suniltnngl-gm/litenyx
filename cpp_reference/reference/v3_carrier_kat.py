#!/usr/bin/env python3
# Independent reference for the Litenyx Phase-5 V3 aux-carrier KAT.
#
# Spec: docs/litenyx_chainid_lifecycle_spec_v0.1.md section 6.1 (V3 carrier).
# This script is DELIBERATELY independent of the C++ implementation: it derives
# the frozen V3 serialization and its digest from first principles, so the C++
# serializer must be proven to MATCH this reference (never the reverse).
#
# Frozen rulings encoded here:
#   * V3 = exact 88-byte V2 body prefix || lifecycleCommitment[32] = 120 bytes.
#   * topologyCommitment := TopologyStateHash(T_h)   (raw, no domain).
#   * lifecycleCommitment := LifecycleStateHash(L_h) (raw, no domain).
#   * All integers little-endian; uint256 fields are raw hash bytes in the
#     forward (data[0..31]) order the hashers emit (verified against Phase-4).
#
# Run:  python3 v3_carrier_kat.py

import hashlib
import struct

MIN_CHAINS = 2
TOPOLOGY_STATE_VERSION = 1
LIFECYCLE_STATE_VERSION = 1
AUX_MAGIC_V1 = 0x4C595858  # "LYXX"
AUX_MAGIC_V2 = 0x4C595932  # "LYY2"
AUX_MAGIC_V3 = 0x4C595933  # "LYY3"  (proposed; frozen in section 6.1)


def sha256d(b: bytes) -> bytes:
    return hashlib.sha256(hashlib.sha256(b).digest()).digest()


# ---- Phase-4 topology state hash (13-byte canonical serialization) ----------
# Layout (little-endian), independently mirrored from LITENYX_topology_authority.h
# (LitenyxSerializeTopologyState):
#   uint32 nVersion | uint32 nHeight | uint8 nN | uint32 nLastTransition == 13
# Genesis T_0: nHeight = 0, nN = MIN_CHAINS, nLastTransition = 0.
def topology_genesis_serialize() -> bytes:
    ser = struct.pack("<I", TOPOLOGY_STATE_VERSION)   # 4  nVersion
    ser += struct.pack("<I", 0)                       # 4  nHeight
    ser += struct.pack("<B", MIN_CHAINS)              # 1  nN
    ser += struct.pack("<I", 0)                       # 4  nLastTransition
    assert len(ser) == 13, len(ser)
    return ser


def topology_commitment_genesis() -> bytes:
    return sha256d(topology_genesis_serialize())


# ---- Phase-5 lifecycle state hash (21-byte canonical serialization) ---------
# Layout (little-endian), independently mirrored from LITENYX_chainid_lifecycle.h
# (LitenyxSerializeLifecycleState):
#   uint16 nVersion | uint32 nextChainId | uint8 bindingCount |
#   bindingCount * (uint8 laneId | uint32 chainId) | uint32 lastLifecycleHeight
# Genesis L_0: nextChainId = MIN_CHAINS, bindings = {(0,0),(1,1)},
# lastLifecycleHeight = 0  ->  2+4+1 + 2*(1+4) + 4 = 21 bytes.
def lifecycle_genesis_serialize() -> bytes:
    bindings = [(i, i) for i in range(MIN_CHAINS)]
    last_lifecycle_height = 0
    ser = struct.pack("<H", LIFECYCLE_STATE_VERSION)  # 2
    ser += struct.pack("<I", MIN_CHAINS)              # 4  nextChainId
    ser += struct.pack("<B", len(bindings))           # 1  bindingCount (uint8)
    for lane, cid in bindings:                         # 5 * 2 = 10
        ser += struct.pack("<B", lane)                # 1  laneId
        ser += struct.pack("<I", cid)                # 4  chainId
    ser += struct.pack("<I", last_lifecycle_height)  # 4  lastLifecycleHeight
    assert len(ser) == 21, len(ser)
    return ser


def lifecycle_commitment_genesis() -> bytes:
    return sha256d(lifecycle_genesis_serialize())


# ---- V2 body (88 bytes) and V3 (120 bytes) ---------------------------------
# Canonical KAT header context (matches the Phase-4 framing test): chainId=1,
# all other legacy fields zero. magic selects the version boundary.
def aux_v2_body(topology_commitment: bytes) -> bytes:
    body = b""
    body += struct.pack("<I", AUX_MAGIC_V3)  # magic (V3 discriminator)
    body += struct.pack("<I", 1)             # chainId
    body += struct.pack("<I", 0)             # eventHeight
    body += b"\x00" * 32                       # auxAnchor
    body += struct.pack("<Q", 0)             # splitVector
    body += struct.pack("<I", 0)             # reserved
    body += topology_commitment              # 32  (byte-identical V2 field)
    assert len(body) == 88, len(body)
    return body


def aux_v3(topology_commitment: bytes, lifecycle_commitment: bytes) -> bytes:
    v3 = aux_v2_body(topology_commitment) + lifecycle_commitment
    assert len(v3) == 120, len(v3)
    return v3


def main():
    tc = topology_commitment_genesis()
    lc = lifecycle_commitment_genesis()
    v3 = aux_v3(tc, lc)
    print("topologyCommitment(T0) =", tc.hex())
    print("lifecycleCommitment(L0) =", lc.hex())
    print("V3 length =", len(v3))
    print("V3 hex =", v3.hex())
    print("SHA256d(V3) =", sha256d(v3).hex())


if __name__ == "__main__":
    main()
