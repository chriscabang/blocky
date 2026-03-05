/**
 * @file block.h
 * @brief Block data structure and single-block operations.
 */

#ifndef BLOCK_H
#define BLOCK_H

#include "transaction.h"

#include <stdint.h>
#include <time.h>

#define MAX_TRANSACTIONS 10

typedef struct Block {
  /* Header */
  uint32_t      index;                  /* Block number */
  time_t        timestamp;              /* Block creation time */
  unsigned char previous_hash[65];      /* Hash of the previous block */
  unsigned char merkle_root[65];        /* Merkle tree root hash */
  uint32_t      nonce;                  /* Proof of work counter */
  uint8_t       consensus;              /* 0 = PoW, 1 = PoS */
  unsigned char hash[65];              /* Block hash */

  /* Content */
  Transaction transactions[MAX_TRANSACTIONS];
  uint32_t    transaction_count;

  /* Runtime only — NEVER written to disk. Zero this after any fread. */
  struct Block *next;
} Block;

/**
 * @brief Allocate and initialise a new block.
 *
 * Sets index, timestamp (now), and previous_hash. If prev_hash is NULL the
 * genesis sentinel ("0") is used. Does NOT compute the hash — call
 * block_compute_hash() after filling in any additional fields.
 * Caller must free with block_free().
 */
Block *block_create(uint32_t index, const unsigned char *prev_hash);

/**
 * @brief Compute and store the SHA-256 hash of the block.
 * @return EXIT_SUCCESS or EXIT_FAILURE.
 */
int block_compute_hash(Block *block);

/**
 * @brief Verify that the block's stored hash matches a freshly computed hash.
 * @return EXIT_SUCCESS if valid, EXIT_FAILURE if tampered or block is NULL.
 */
int block_verify_hash(const Block *block);

/**
 * @brief Free a heap-allocated block. Safe to call with NULL.
 */
void block_free(Block *block);

#endif /* BLOCK_H */
