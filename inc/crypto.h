/**
 * @file crypto.h
 * @brief Block hashing and Merkle root.
 *
 * Hashing uses the built-in SHA-256 implementation (sha256.h/.c).
 * OpenSSL is NOT used here. Block signing and signature verification
 * are in block.h / block.c (Dilithium-3 via liboqs, ADR-003).
 */

#ifndef CRYPTO_H
#define CRYPTO_H

#include "block.h"

#ifndef HASH_SIZE
#define HASH_SIZE 65  /* SHA-256 hex string (64 chars) + null terminator */
#endif

/**
 * @brief Compute the SHA-256 hash of a block and store it in block->hash.
 *
 * Feeds the following fields into SHA-256 in order:
 *   index, timestamp, previous_hash, merkle_root, nonce, consensus, proposer_id
 *
 * Does NOT read block->hash as input (avoids circular dependency).
 * Does NOT compute the Merkle root — call compute_merkle_root() first
 * if the block has transactions.
 *
 * @param block  Block to hash. Must be non-NULL.
 * @return EXIT_SUCCESS or EXIT_FAILURE.
 */
int block_hash(Block *block);

/**
 * @brief Compute the Merkle root of a block's transactions.
 *
 * Feeds sender + recipient + amount of every transaction into SHA-256
 * in order. Writes a 64-char hex string into merkle_root.
 * Sets merkle_root to "0" if transaction_count == 0.
 *
 * @param block        Block whose transactions to summarise (read-only).
 * @param merkle_root  Caller-provided buffer of at least HASH_SIZE bytes.
 */
void compute_merkle_root(const Block *block, char *merkle_root);

#endif /* CRYPTO_H */
