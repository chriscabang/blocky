/* consensus.h — consensus routing: PoW and PoS block validation. */
#ifndef CONSENSUS_H
#define CONSENSUS_H

#include "block.h"

/*
 * Consensus type constants stored in Block.consensus.
 * 0 = Proof of Work  (block hash must meet a difficulty target)
 * 1 = Proof of Stake (block hash integrity + proposer stake/VRF — see ADR-003)
 */
#define CONSENSUS_POW 0
#define CONSENSUS_POS 1

/*
 * Verify that `block` satisfies its declared consensus rules.
 *
 * Dispatches to verify_pow_rules or verify_pos_rules based on
 * block->consensus. Unknown consensus values return EXIT_FAILURE.
 *
 * Returns EXIT_SUCCESS if all rules pass, EXIT_FAILURE otherwise.
 * Safe to call with NULL (returns EXIT_FAILURE without crashing).
 */
int verify_consensus(const Block *block);

/*
 * Verify the proposer's Dilithium-3 block signature.
 *
 * Stub: returns EXIT_FAILURE until the validator key registry is in place
 * (ADR-003). Wired here to prevent unsigned blocks from passing consensus
 * validation before signing is fully implemented.
 */
int verify_block_signature(const Block *block);

#endif /* CONSENSUS_H */
