/* crypto.c */
#include "crypto.h"
#include "log.h"

#include <openssl/sha.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// Compute Merkle root for all transactions in a block
// This simple implementation concatenates the sender strings of all transactions
// :TODO: Implement a proper Merkle tree
void compute_merkle_root(Block *block, char *merkle_root) {
  if (!block || !merkle_root) {
    puts("Invalid block or merkle root");
    return;
  }

  if (block->transaction_count == 0) {
    memcpy(merkle_root, "0", 1);
    return;
  }

  char concatenated[4096] = {0};
  for (uint32_t i = 0; i < block->transaction_count; i++) {
    /*strncat(concatenated, block->transactions[i].sender, sizeof(concatenated) - strlen(concatenated) - 1);*/
    memcpy(concatenated + strlen(concatenated), block->transactions[i].sender, strlen(block->transactions[i].sender));
  }

  unsigned char hash[SHA256_DIGEST_LENGTH];
  SHA256_CTX sha256;
  SHA256_Init(&sha256);
  SHA256_Update(&sha256, concatenated, strlen(concatenated));
  SHA256_Final(hash, &sha256);

  for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
    sprintf(merkle_root + (i * 2), "%02x", hash[i]);
  }
  merkle_root[SHA256_DIGEST_LENGTH * 2] = '\0';
}

/**
 * hash
 * @brief Computes the SHA-256 hash of a block.
 *
 * @param block: The block to hash.
 * @return EXIT_SUCCESS if successful, EXIT_FAILURE otherwise.
 */
int hash(Block *block) {

  log_info("Hashing block %u", block->index);

  if (block == NULL) {
    log_error("Invalid block or output hash");
    return EXIT_FAILURE;
  }

  SHA256_CTX sha256;
  SHA256_Init(&sha256);

  // Combine block data to hash
  SHA256_Update(&sha256, &block->index, sizeof(block->index));
  SHA256_Update(&sha256, &block->timestamp, sizeof(block->timestamp));
  SHA256_Update(&sha256, block->previous_hash,
                strlen((const char *)block->previous_hash));
  SHA256_Update(&sha256, block->merkle_root, strlen((const char *)block->merkle_root));
  SHA256_Update(&sha256, &block->nonce, sizeof(block->nonce));

  unsigned char block_hash[SHA256_DIGEST_LENGTH];
  SHA256_Final(block_hash, &sha256);

  memcpy(block->hash, block_hash, SHA256_DIGEST_LENGTH);
  
  return EXIT_SUCCESS;
}
