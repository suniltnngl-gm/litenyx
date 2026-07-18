#ifndef LITENYX_VALIDATION_H
#define LITENYX_VALIDATION_H

#include "LITENYX_auxpow.h"
#include "LITENYX_identity.h"

#include <uint256.h>

class CBlock;
class CBlockIndex;
class CValidationState;

// Validate a block's Litenyx aux header against the current tip. Enforces:
//   - aux magic present,
//   - chainId within [0, LITENYX_MAX_CHAINS),
//   - anchor commits to the parent tip on the SAME chain,
//   - reserved == 0.
// Returns true if the aux header is acceptable (or if shared-state is not yet
// active at this height, in which case aux is ignored). On rejection, sets
// `state` and returns false.
bool LitenyxCheckAuxHeader(const CBlock& block, const CBlockIndex* pindexPrev,
                           CValidationState& state);

// Record every input of every non-coinbase transaction in `block` into the
// global shared spent-set under `block.nyx_aux.chainId`. Returns false (and sets
// `state`) if any input is already globally spent (cross-chain double spend).
bool LitenyxConnectSharedState(const CBlock& block, CValidationState& state);

// Revert every input of every non-coinbase transaction in `block` from the
// global shared spent-set. Called from DisconnectBlock so reorgs roll back
// exactly the global state this block introduced.
void LitenyxDisconnectSharedState(const CBlock& block);

#endif // LITENYX_VALIDATION_H
