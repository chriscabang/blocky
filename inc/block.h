/**
 * @file block.h
 * @brief Block data structure and single-block operations.
 */

#ifndef BLOCK_H
#define BLOCK_H

#include "transaction.h"
#include "vrf.h"

#include <stdint.h>
#include <time.h>

#define MAX_TRANSACTIONS 10

#define PROPOSER_ID_SIZE 65  /* matches VALIDATOR_ID_SIZE in validator.h */

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

  /* Proposer attestation (PoS only; zeroed for PoW blocks) */
  char    proposer_id[PROPOSER_ID_SIZE];       /* validator ID; '\0' for PoW */
  VRFProof vrf_proof;                           /* VRF election proof */
  uint8_t  proposer_sig[MAX_SIGNATURE_LENGTH];  /* Dilithium-3 sig over block hash */
  size_t   proposer_sig_len;                    /* 0 if unsigned */

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

/**
 * @brief Sign a PoS block with the proposer's Dilithium-3 private key.
 *
 * Sets block->proposer_id, copies @vrf_proof into block->vrf_proof,
 * recomputes the block hash (which now includes proposer_id), then
 * signs the 32-byte raw hash with Dilithium-3.
 *
 * Must be called after all other block fields are set (transactions,
 * merkle_root, etc.) but before the block is broadcast or stored.
 *
 * @param block        Block to sign (modified in place)
 * @param proposer_id  NUL-terminated validator ID string
 * @param private_key  Dilithium-3 private key bytes
 * @param sk_len       length of @private_key (used for logging)
 * @param vrf_proof    VRF election proof from vrf_prove()
 * @return EXIT_SUCCESS or EXIT_FAILURE
 */
int block_sign(Block *block, const char *proposer_id,
               const uint8_t *private_key, size_t sk_len,
               const VRFProof *vrf_proof);

/**
 * @brief Verify a block's Dilithium-3 proposer signature.
 *
 * Decodes block->hash from hex to 32 raw bytes and verifies
 * block->proposer_sig using the provided public key.
 *
 * @param block       Block to verify (read-only)
 * @param public_key  Dilithium-3 public key bytes
 * @param pk_len      length of @public_key (used for logging)
 * @return EXIT_SUCCESS if signature is valid, EXIT_FAILURE otherwise
 */
int block_verify_sig(const Block *block,
                     const uint8_t *public_key, size_t pk_len);

#endif /* BLOCK_H */
