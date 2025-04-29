#ifndef CRYPTO_H
#define CRYPTO_H

#include "blockchain.h"

#define HASH_SIZE 65 // SHA-256 hash size + null terminator

/**
 * hash
 * @brief Compute the SHA-256 hash of a block and the merkle
 * root of the block's transactions.
 *
 * @param block: The block to hash. Returns the hash in the block.
 * @return EXIT_SUCCESS on success, EXIT_FAILURE on failure.
 */
int hash(Block* block);

/**
 * sign
 * @brief Sign a block using a private key.
 *
 * @param block: The block to sign.
 * @param private_key: The private key to sign the block with.
 * @param signature: The signature to return.
 */
int sign(Block* block, const char* private_key, char* signature);

/**
 * verify
 * @brief Verify a blocks signature using a public key.
 *
 * @param block: The block to verify.
 * @param public_key: The public key to verify the block with.
 */
int verify(const Block* block, const char* public_key);

/* Computes the Merkle root for all transactions in a block.
 * For simplicity, this implementation concatenates the sender strings
 * of all transactions and computes a SHA-256 hash over that data.
 */
/*void compute_merkle_root(Block *block, char *merkle_root);*/

/* Computes the block hash over its header fields:
 *  - index
 *  - timestamp
 *  - previous_hash
 *  - nonce
 *  - merkle_root
 */
/*void compute_block_hash(Block *block, char *hash);*/

#endif // CRYPTO_H
