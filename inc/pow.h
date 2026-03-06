/* pow.h — Proof of Work: mine and validate blocks. */
#ifndef POW_H
#define POW_H

#include "block.h"
#include <stdint.h>

/* Default difficulty: number of leading hex zero characters required. */
#define DIFFICULTY 4

/*
 * Mine a block by incrementing nonce until block->hash has `difficulty`
 * leading hex zero characters.
 *
 * Uses a SHA-256 midstate optimisation: all header fields except nonce are
 * hashed once before the loop; only nonce + consensus are re-hashed each
 * iteration (saves ~2/3 of SHA-256 compression work per candidate nonce).
 *
 * Returns EXIT_SUCCESS when a solution is found.
 * Returns EXIT_FAILURE on NULL block, invalid difficulty, or nonce exhaustion
 * (all 2^32 nonces tried without a solution).
 */
int mine_block(Block *block, uint32_t difficulty);

/*
 * Validate that block->hash satisfies `difficulty` leading hex zero characters
 * AND matches the block's fields (via block_verify_hash).
 * Const-correct — does not modify the block.
 *
 * Returns EXIT_SUCCESS if both checks pass, EXIT_FAILURE otherwise.
 */
int validate_block_pow(const Block *block, uint32_t difficulty);

#endif /* POW_H */
