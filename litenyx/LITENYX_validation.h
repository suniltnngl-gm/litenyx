#ifndef LITENYX_VALIDATION_H
#define LITENYX_VALIDATION_H

#include "LITENYX_auxpow.h"
#include "LITENYX_identity.h"

#include <uint256.h>

#include <string>

class CBlock;
class CBlockIndex;
class CValidationState;
namespace Consensus { struct Params; }

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

// Phase 4B(4): consensus-critical topology-commitment enforcement (spec §5.7/§9).
//
// Thin GLUE over the pure engine. Given the block being connected at height
// (pindexPrev->nHeight + 1), it:
//   1. selects the frozen per-network LitenyxTopoActivation and derives the
//      REGIME from the height;
//   2. reconstructs the expected authoritative topology from CANONICAL CHAIN
//      HISTORY ALONE (walking pindexPrev), never from LitenyxTopologyTracker or
//      any persisted topology cache;
//   3. calls the pure LitenyxVerifyTopologyCommitment against block.nyx_aux;
//   4. maps the verdict to a consensus result:
//        Valid            -> return true
//        Invalid          -> state.Invalid(...) + return false (fail-closed)
//        AdvisoryMismatch -> return true (soft regime: reportable, NOT invalid).
//
// MUST be called OUTSIDE any try/catch (consensus-critical). PreDerivation /
// disabled networks preserve legacy behavior. `netId` is
// CChainParams::NetworkIDString(); `consensus` is the active consensus params
// (used only to read block data). No process-local topology state is created,
// so nothing needs undoing on DisconnectBlock for consensus correctness.
bool LitenyxCheckTopologyCommitment(const CBlock& block,
                                    const CBlockIndex* pindexPrev,
                                    const std::string& netId,
                                    const Consensus::Params& consensus,
                                    CValidationState& state);

// Phase 5: consensus-critical ChainId-lifecycle-commitment enforcement (spec
// §6.2 / §9). Thin GLUE over the pure lifecycle engine, layered STRICTLY ABOVE
// the frozen Phase-4 topology authority. It MUST be called AFTER
// LitenyxCheckTopologyCommitment has already returned true (§6.2 ordering), so a
// block can never route around a failed/absent topology commitment by omitting
// the Phase-5 field. Given the block being connected at height
// (pindexPrev->nHeight + 1), it:
//   1. selects the frozen per-network Phase-5 activation and derives the regime;
//   2. reconstructs the expected L_h from CANONICAL CHAIN HISTORY ALONE (the
//      SAME reconstruction Phase 4 uses), folding G over the topology boundaries,
//      never from a tracker/cache;
//   3. calls the pure LitenyxVerifyLifecycleCommitment against block.nyx_aux;
//   4. maps the verdict: Valid -> true; Invalid -> state.Invalid(...) + false
//      (fail-closed); AdvisoryMismatch -> true (soft regime, reportable).
//
// MUST be called OUTSIDE any try/catch (consensus-critical). PreDerivation /
// disabled networks preserve legacy behavior. Re-derivation from the canonical
// prefix means DisconnectBlock needs NO lifecycle undo (spec §7).
bool LitenyxCheckLifecycleCommitment(const CBlock& block,
                                     const CBlockIndex* pindexPrev,
                                     const std::string& netId,
                                     const Consensus::Params& consensus,
                                     CValidationState& state);

#endif // LITENYX_VALIDATION_H
