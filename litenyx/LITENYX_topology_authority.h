// Litenyx Phase 4A — Pure consensus-authoritative topology engine (spec
// docs/litenyx_topology_authority_spec_v0.1.md).
//
// This header is the SINGLE SOURCE OF TRUTH for the AUTHORITATIVE topology
// derivation. It is pure, header-only, integer-only, and has ZERO dependencies
// on singletons, mutable trackers, wall-clock, mempool, RPC, or network state.
// It is standalone-unit-testable AND compilable into the daemon.
//
// Boundary (spec §2): the Phase-3 LitenyxTopologyTracker remains OBSERVATIONAL.
// This engine is the consensus path: T_h is a pure function of (T_{h-1}, C_h).
//
// Pipeline (spec §5.5 / §5.5.5), all integer-only, version-pinned to nVersion=1:
//   D_v1(block)              := floor(GetBlockWeight * DEMAND_SCALE / MAX_WEIGHT)   0..DEMAND_SCALE
//   M_c_v1(chain, window)    := floor(sum_{i in W} D_v1(block_i) / W)               0..DEMAND_SCALE (FULL precision)
//   ControllerInput.M_c      := floor(M_c_v1 / 100)                                 0..100 (SINGLE downscale)
//   decision                 := LitenyxTopoDecide(...)   [frozen Phase-3 controller, UNCHANGED]
//   T_h                      := LitenyxTopoApply(T_{h-1}.N, decision)
//
// Path-independence (spec §0.1): derivation depends ONLY on committed history,
// never on block arrival order. IBD, sequential connect, and reorg re-derivation
// MUST all yield byte-identical TopologyStateHash at every height.

#ifndef LITENYX_TOPOLOGY_AUTHORITY_H
#define LITENYX_TOPOLOGY_AUTHORITY_H

#include <cstdint>
#include <cstring>
#include <cstddef>
#include <vector>
#include <utility>
#include <algorithm>

#include "LITENYX_types.h"
#include "LITENYX_topology.h" // frozen controller: LitenyxTopoDecide/Apply/TransitionHeight

// ---- Frozen consensus constants (spec §5.5) --------------------------------
// DEMAND_SCALE: fixed-point demand scale. 0..DEMAND_SCALE == 0.00%..100.00%.
static const int64_t LITENYX_DEMAND_SCALE = 10000;
// Canonical consensus block-weight bound. In the daemon this MUST equal the
// consensus constant MAX_BLOCK_WEIGHT (src/consensus/consensus.h == 4000000),
// which is asserted in LITENYX_validation.cpp when wired. Pinned here so the
// pure engine and its proofs use the identical bound.
static const int64_t LITENYX_MAX_BLOCK_WEIGHT = 4000000;
// Controller-boundary downscale divisor: DEMAND_SCALE / 100 (spec §5.5.5).
static const int64_t LITENYX_CONTROLLER_DOWNSCALE = LITENYX_DEMAND_SCALE / 100;

// TopologyState canonical serialization/authority version (spec §3). Changing
// D_v1 / M_c_v1 / the downscale REQUIRES a new nVersion + activation height.
static const uint32_t LITENYX_TOPOLOGY_STATE_VERSION = 1;

// ---- Activation semantics (spec §8, FROZEN scheme) -------------------------
// Named "never" sentinel. A network with H_derive == DISABLED is dormant: the
// authority engine never derives/enforces. This is NOT a large-but-reachable
// height; callers MUST test == DISABLED, never h >= someHugeNumber.
static const uint32_t LITENYX_TOPO_ACTIVATION_DISABLED = 0xFFFFFFFFu;

// The three activation regimes (spec §8).
enum class LitenyxTopoRegime {
    PreDerivation = 0, // h < H_derive (or disabled): legacy, no derivation
    SoftAdvisory  = 1, // H_derive <= h < H_topology: derive + index, warn-only
    HardAuthority = 2, // h >= H_topology: derive + index, fail-closed
};

// Per-network activation heights (mirrors Consensus::Params in the daemon; kept
// here as a pure struct so the engine + proofs share one definition).
struct LitenyxTopoActivation {
    uint32_t hDerive   = LITENYX_TOPO_ACTIVATION_DISABLED;
    uint32_t hTopology = LITENYX_TOPO_ACTIVATION_DISABLED;

    bool IsDisabled() const { return hDerive == LITENYX_TOPO_ACTIVATION_DISABLED; }

    // Structural validity (spec §8.1). Enforced at params construction.
    bool IsValid() const {
        if (hDerive == LITENYX_TOPO_ACTIVATION_DISABLED)
            return hTopology == LITENYX_TOPO_ACTIVATION_DISABLED; // both-disabled coupling
        if (hDerive == 0) return false;                          // 0 < H_derive
        return hDerive <= hTopology;                             // ordering
    }

    LitenyxTopoRegime RegimeAt(uint32_t h) const {
        if (IsDisabled() || h < hDerive)   return LitenyxTopoRegime::PreDerivation;
        if (h < hTopology)                 return LitenyxTopoRegime::SoftAdvisory;
        return LitenyxTopoRegime::HardAuthority;
    }
};

// Concrete per-network activations (spec §8.2, FROZEN for Phase 4).
inline LitenyxTopoActivation LitenyxTopoActivationRegtest() { return LitenyxTopoActivation{100, 300}; }
inline LitenyxTopoActivation LitenyxTopoActivationTestnet() { return LitenyxTopoActivation{500, 1500}; }
inline LitenyxTopoActivation LitenyxTopoActivationMainnet() {
    return LitenyxTopoActivation{LITENYX_TOPO_ACTIVATION_DISABLED,
                                 LITENYX_TOPO_ACTIVATION_DISABLED};
}

// ---- D_v1: per-block demand (stateless, spec §5.5) -------------------------
// blockWeight MUST be the canonical GetBlockWeight(block). Integer-only; the
// intermediate product fits int64 (4e6 * 1e4 = 4e10 << 2^63). A defensive clamp
// tolerates future constant changes (spec §5.5.1 item 3).
inline int32_t LitenyxDemandV1(int64_t blockWeight)
{
    if (blockWeight < 0) blockWeight = 0;
    int64_t d = (blockWeight * LITENYX_DEMAND_SCALE) / LITENYX_MAX_BLOCK_WEIGHT;
    if (d < 0) d = 0;
    if (d > LITENYX_DEMAND_SCALE) d = LITENYX_DEMAND_SCALE;
    return (int32_t)d;
}

// ---- M_c_v1: per-chain window aggregation (FULL 0..DEMAND_SCALE precision) --
// samples = the D_v1 values for one chain over its committed window W (|W| >= 1).
// Integer floor mean; NO per-sample downscaling (spec §5.5.5).
inline int32_t LitenyxMcV1(const std::vector<int32_t>& samples)
{
    if (samples.empty()) return 0;
    int64_t sum = 0;
    for (int32_t s : samples) sum += (int64_t)s; // sum <= W*DEMAND_SCALE << 2^63
    return (int32_t)(sum / (int64_t)samples.size());
}

// ---- Controller-boundary downscale: single floor(/100) (spec §5.5.5) -------
// M_c_v1 in 0..DEMAND_SCALE -> controller M_c in 0..100. This is the ONLY place
// the domain crosses from fixed-point to the frozen controller's 0..100 scale.
inline int32_t LitenyxMcToControllerInput(int32_t mcV1)
{
    if (mcV1 < 0) mcV1 = 0;
    int32_t r = (int32_t)((int64_t)mcV1 / LITENYX_CONTROLLER_DOWNSCALE);
    if (r > 100) r = 100;
    return r;
}

// A committed per-block sample for one height: the per-chain block weights of
// the blocks that extended each active chain at this height. weightByChain[c]
// is the canonical GetBlockWeight for chain c (0 if that chain had no block).
struct LitenyxCommittedSample {
    uint32_t height = 0;
    std::vector<int64_t> weightByChain; // index == chainId; size == active N
};

// ---- TopologyState: the consensus-authoritative topology commitment (spec §3)
// Minimal authoritative fields. nN is the active chain count; nLastTransition is
// the canonical-history-derived height of the last applied transition (0 if
// none). nHeight is the height this state is valid AT. All integer; canonically
// serialized for hashing (spec §3).
struct LitenyxTopologyState {
    uint32_t nVersion = LITENYX_TOPOLOGY_STATE_VERSION;
    uint32_t nHeight = 0;
    uint8_t  nN = LITENYX_MIN_CHAINS;
    uint32_t nLastTransition = 0;

    static LitenyxTopologyState Genesis() {
        LitenyxTopologyState s;
        s.nVersion = LITENYX_TOPOLOGY_STATE_VERSION;
        s.nHeight = 0;
        s.nN = LITENYX_MIN_CHAINS;
        s.nLastTransition = 0;
        return s;
    }

    bool operator==(const LitenyxTopologyState& o) const {
        return nVersion == o.nVersion && nHeight == o.nHeight &&
               nN == o.nN && nLastTransition == o.nLastTransition;
    }
    bool operator!=(const LitenyxTopologyState& o) const { return !(*this == o); }
};

// ---- Canonical serialization (spec §3) -------------------------------------
// Fixed little-endian field order, no padding, deterministic across platforms.
// This exact byte layout is what TopologyStateHash commits to.
inline void LitenyxSerializeTopologyState(
    const LitenyxTopologyState& s, unsigned char out[13])
{
    auto put32 = [](unsigned char* p, uint32_t v) {
        p[0] = (unsigned char)(v & 0xFF);
        p[1] = (unsigned char)((v >> 8) & 0xFF);
        p[2] = (unsigned char)((v >> 16) & 0xFF);
        p[3] = (unsigned char)((v >> 24) & 0xFF);
    };
    put32(out + 0, s.nVersion);
    put32(out + 4, s.nHeight);
    out[8] = s.nN;
    put32(out + 9, s.nLastTransition);
}

// ---- Self-contained double-SHA256 -------------------------------------------
// Deliberately self-contained (works identically in standalone tests AND the
// daemon) so TopologyStateHash is byte-identical across every build/platform.
// This is exactly what path-independence (spec §0.1) requires. It is NOT wired
// into ConnectBlock in Phase 4A.
namespace litenyx_detail {

struct Sha256Ctx {
    uint32_t s[8];
    uint64_t len;
    unsigned char buf[64];
    uint32_t idx;
};

inline uint32_t rotr(uint32_t x, uint32_t n) { return (x >> n) | (x << (32 - n)); }

inline void sha256_init(Sha256Ctx& c) {
    static const uint32_t iv[8] = {
        0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
        0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
    std::memcpy(c.s, iv, sizeof(iv));
    c.len = 0; c.idx = 0;
}

inline void sha256_block(Sha256Ctx& c, const unsigned char* p) {
    static const uint32_t k[64] = {
        0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
        0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
        0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
        0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
        0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
        0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
        0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
        0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u};
    uint32_t w[64];
    for (int i = 0; i < 16; ++i)
        w[i] = ((uint32_t)p[i*4] << 24) | ((uint32_t)p[i*4+1] << 16) |
               ((uint32_t)p[i*4+2] << 8) | (uint32_t)p[i*4+3];
    for (int i = 16; i < 64; ++i) {
        uint32_t s0 = rotr(w[i-15],7) ^ rotr(w[i-15],18) ^ (w[i-15] >> 3);
        uint32_t s1 = rotr(w[i-2],17) ^ rotr(w[i-2],19) ^ (w[i-2] >> 10);
        w[i] = w[i-16] + s0 + w[i-7] + s1;
    }
    uint32_t a=c.s[0],b=c.s[1],cc=c.s[2],d=c.s[3],e=c.s[4],f=c.s[5],g=c.s[6],h=c.s[7];
    for (int i = 0; i < 64; ++i) {
        uint32_t S1 = rotr(e,6) ^ rotr(e,11) ^ rotr(e,25);
        uint32_t ch = (e & f) ^ (~e & g);
        uint32_t t1 = h + S1 + ch + k[i] + w[i];
        uint32_t S0 = rotr(a,2) ^ rotr(a,13) ^ rotr(a,22);
        uint32_t maj = (a & b) ^ (a & cc) ^ (b & cc);
        uint32_t t2 = S0 + maj;
        h=g; g=f; f=e; e=d+t1; d=cc; cc=b; b=a; a=t1+t2;
    }
    c.s[0]+=a; c.s[1]+=b; c.s[2]+=cc; c.s[3]+=d; c.s[4]+=e; c.s[5]+=f; c.s[6]+=g; c.s[7]+=h;
}

inline void sha256_update(Sha256Ctx& c, const unsigned char* p, size_t n) {
    c.len += (uint64_t)n * 8;
    while (n) {
        size_t take = 64 - c.idx;
        if (take > n) take = n;
        std::memcpy(c.buf + c.idx, p, take);
        c.idx += (uint32_t)take; p += take; n -= take;
        if (c.idx == 64) { sha256_block(c, c.buf); c.idx = 0; }
    }
}

inline void sha256_final(Sha256Ctx& c, unsigned char out[32]) {
    uint64_t bits = c.len;
    unsigned char pad = 0x80;
    sha256_update(c, &pad, 1);
    unsigned char zero = 0;
    while (c.idx != 56) sha256_update(c, &zero, 1);
    unsigned char lenbe[8];
    for (int i = 0; i < 8; ++i) lenbe[i] = (unsigned char)((bits >> (56 - i*8)) & 0xFF);
    sha256_update(c, lenbe, 8);
    for (int i = 0; i < 8; ++i) {
        out[i*4]   = (unsigned char)((c.s[i] >> 24) & 0xFF);
        out[i*4+1] = (unsigned char)((c.s[i] >> 16) & 0xFF);
        out[i*4+2] = (unsigned char)((c.s[i] >> 8) & 0xFF);
        out[i*4+3] = (unsigned char)(c.s[i] & 0xFF);
    }
}

inline void double_sha256(const unsigned char* p, size_t n, unsigned char out[32]) {
    unsigned char once[32];
    Sha256Ctx c;
    sha256_init(c); sha256_update(c, p, n); sha256_final(c, once);
    sha256_init(c); sha256_update(c, once, 32); sha256_final(c, out);
}

} // namespace litenyx_detail

// TopologyStateHash: double-SHA256 of the canonical serialization (spec §3).
// Returns 32 bytes. Deterministic and platform-independent.
inline void LitenyxTopologyStateHash(
    const LitenyxTopologyState& s, unsigned char out[32])
{
    unsigned char ser[13];
    LitenyxSerializeTopologyState(s, ser);
    litenyx_detail::double_sha256(ser, sizeof(ser), out);
}

// ---- Pure authoritative transition: F(T_{h-1}, C_h) (spec §4/§5) -----------
// Given the previous authoritative state and the committed sample at an
// observation boundary height, derive the next state. This is evaluated ONLY at
// observation boundaries (height % OBS_WINDOW == 0); at non-boundary heights the
// state is carried forward with nHeight advanced. sampleByChain holds, per active
// chain, the FULL-precision M_c_v1 already aggregated over that chain's window.
//
// This is a PURE function: no globals, no order dependence. Callers that replay
// history in any order (IBD, sequential, reorg) get identical results.
inline LitenyxTopologyState LitenyxDeriveTopologyAtBoundary(
    const LitenyxTopologyState& prev,
    uint32_t hObs,
    const std::vector<int32_t>& mcV1ByChain)
{
    LitenyxTopologyState next = prev;
    next.nHeight = hObs;

    // Build the frozen controller's observation vector via the SINGLE downscale.
    std::vector<LitenyxChainObservation> obs;
    obs.reserve(mcV1ByChain.size());
    for (int32_t mcV1 : mcV1ByChain)
        obs.push_back(LitenyxChainObservation{ LitenyxMcToControllerInput(mcV1) });

    LitenyxTopoDecision d = LitenyxTopoDecide(obs, prev.nN, hObs, prev.nLastTransition);
    if (d != LitenyxTopoDecision::HOLD) {
        uint8_t newN = LitenyxTopoApply(prev.nN, d);
        if (newN != prev.nN) {
            next.nN = newN;
            next.nLastTransition = LitenyxTopoTransitionHeight(hObs);
        }
    }
    return next;
}

// ---- Full derivation from committed history (spec §5.1) ---------------------
// A committed history for the authority engine: for each observation-boundary
// height, the per-chain M_c_v1 (already aggregated over the window). This models
// exactly what a node reconstructs from canonical block data alone. Derivation
// walks boundaries in ascending height order and applies F; the RESULT depends
// only on the set of (height -> M_c_v1) pairs, not on arrival order.
struct LitenyxCommittedHistory {
    // boundaryHeight -> per-chain M_c_v1 (index == chainId).
    std::vector<std::pair<uint32_t, std::vector<int32_t>>> boundaries;

    void add(uint32_t h, const std::vector<int32_t>& mcV1ByChain) {
        boundaries.push_back({h, mcV1ByChain});
    }
};

// CalculateExpectedTopology: derive the authoritative state AT tipHeight from a
// genesis state and committed history. Boundaries are processed in ascending
// height order (canonical), so any permutation of the input yields the same
// result after the internal sort — proving path-independence at the engine level.
inline LitenyxTopologyState LitenyxCalculateExpectedTopology(
    LitenyxTopologyState state,
    LitenyxCommittedHistory history,
    uint32_t tipHeight)
{
    // Canonical ordering: sort boundaries by height. This makes derivation a pure
    // function of the SET of committed boundaries, independent of insertion order.
    std::sort(history.boundaries.begin(), history.boundaries.end(),
              [](const std::pair<uint32_t, std::vector<int32_t>>& a,
                 const std::pair<uint32_t, std::vector<int32_t>>& b) {
                  return a.first < b.first;
              });

    for (const auto& kv : history.boundaries) {
        if (kv.first > tipHeight) break;
        state = LitenyxDeriveTopologyAtBoundary(state, kv.first, kv.second);
    }
    state.nHeight = tipHeight;
    return state;
}

#endif // LITENYX_TOPOLOGY_AUTHORITY_H
