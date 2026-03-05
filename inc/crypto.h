/**
 * @file crypto.h
 * @brief Block hashing, Merkle root, and signature stubs.
 *
 * Hashing uses the built-in SHA-256 implementation (sha256.h/.c).
 * OpenSSL is NOT used here. Signing stubs are pending Dilithium
 * integration via liboqs (see ADR-003).
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
 *   index, timestamp, previous_hash, merkle_root, nonce, consensus
 *
 * Does NOT read block->hash as input (avoids circular dependency).
 * Does NOT compute the Merkle root — call compute_merkle_root() first
 * if the block has transactions.
 *
 * @param block  Block to hash. Must be non-NULL.
 * @return EXIT_SUCCESS or EXIT_FAILURE.
 */
int hash(Block *block);

/**
 * @brief Compute the Merkle root of a block's transactions.
 *
 * Feeds sender + recipient + amount of every transaction into SHA-256
 * in order. Writes a 64-char hex string into merkle_root.
 * Sets merkle_root to "0" if transaction_count == 0.
 *
 * @param block        Block whose transactions to summarise.
 * @param merkle_root  Caller-provided buffer of at least HASH_SIZE bytes.
 */
void compute_merkle_root(Block *block, char *merkle_root);

/**
 * @brief Sign a block with a Dilithium private key (stub).
 *
 * Not yet implemented — pending liboqs Dilithium integration (ADR-003).
 *
 * @return EXIT_FAILURE always.
 */
int sign(Block *block, const char *private_key, char *signature);

/**
 * @brief Verify a block's Dilithium signature (stub).
 *
 * Not yet implemented — pending liboqs Dilithium integration (ADR-003).
 *
 * @return EXIT_FAILURE always.
 */
int verify(const Block *block, const char *public_key);

#endif /* CRYPTO_H */
