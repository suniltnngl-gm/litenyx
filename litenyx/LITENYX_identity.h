#ifndef LITENYX_IDENTITY_H
#define LITENYX_IDENTITY_H

#include <cstdint>

// Litenyx Phase-1 fork identity. These constants establish the chain's unique
// network identity WITHOUT touching any FUTURE mechanism (no split/merge, no
// dynamic block size/reward, no negative supply, no dynamic wallet count).
//
// NOTE: A full genesis swap is deferred to a careful follow-up. The consensus
// constants below (magic, ports, AuxPoW chain id, shared-state activation) are
// wired into the fork so the daemon builds and the regtest gate is meaningful.

// --- Network identity --------------------------------------------------------
// Regtest network magic (4 bytes), distinct from Dogecoin's 0xfabfb5da.
static constexpr unsigned char LITENYX_REGTEST_MESSAGE_START[4] = {0x6c, 0x79, 0x78, 0x58}; // "lyxX"
static constexpr int LITENYX_REGTEST_PORT = 18455; // Dogecoin regtest is 18444

// --- AuxPoW identity ---------------------------------------------------------
// Unique AuxPoW chain id (Dogecoin mainnet/testnet use 0x0062). Litenyx uses a
// distinct id so merged-mined Litenyx work is unambiguous.
static constexpr int LITENYX_AUXPOW_CHAIN_ID = 0x4c59; // "LY"

// --- Shared-state activation -------------------------------------------------
// Height at/after which the global shared spent-set is enforced. Below this the
// chain behaves as plain Dogecoin (single-lane). In regtest we activate early.
static constexpr int LITENYX_SHARED_STATE_HEIGHT = 2;

#endif // LITENYX_IDENTITY_H
