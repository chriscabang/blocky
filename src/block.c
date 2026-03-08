/**
 * @file block.c
 * @brief Single-block operations: create, hash, verify, free.
 */

#include "block.h"
#include "crypto.h"
#include "log.h"
#include "sha256.h"
#include "storage.h" /* GENESIS_PREVIOUS_HASH */

#include <oqs/oqs.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

Block *block_create(uint32_t index, const unsigned char *prev_hash) {
  Block *b = calloc(1, sizeof(Block));
  if (!b) return NULL;

  b->index     = index;
  b->timestamp = time(NULL);
  b->next      = NULL;

  if (prev_hash) {
    memcpy(b->previous_hash, prev_hash, HASH_SIZE);
  } else {
    /* Genesis sentinel: previous_hash = "0\0" */
    b->previous_hash[0] = GENESIS_PREVIOUS_HASH[0];
    b->previous_hash[1] = '\0';
  }

  return b;
}

int block_compute_hash(Block *block) {
  return block_hash(block);
}

int block_verify_hash(const Block *block) {
  if (!block) return EXIT_FAILURE;

  /* Compute hash on a stack copy without touching the original */
  Block copy = *block;
  memset(copy.hash, 0, sizeof(copy.hash));
  copy.next = NULL; /* exclude runtime pointer from hash input */

  if (block_hash(&copy) != EXIT_SUCCESS) return EXIT_FAILURE;

  return (memcmp(copy.hash, block->hash, HASH_SIZE) == 0)
           ? EXIT_SUCCESS
           : EXIT_FAILURE;
}

void block_free(Block *block) {
  free(block);
}

int block_sign(Block *block, const char *proposer_id,
               const uint8_t *private_key, size_t sk_len,
               const VRFProof *vrf_proof)
{
  if (!block || !proposer_id || !private_key || !vrf_proof) {
    log_error("block_sign: NULL argument");
    return EXIT_FAILURE;
  }
  if (proposer_id[0] == '\0') {
    log_error("block_sign: empty proposer_id");
    return EXIT_FAILURE;
  }

  /* Set proposer identity and VRF proof (proposer_id is part of the hash). */
  strncpy(block->proposer_id, proposer_id, PROPOSER_ID_SIZE - 1);
  block->proposer_id[PROPOSER_ID_SIZE - 1] = '\0';
  block->vrf_proof = *vrf_proof;

  /* Recompute hash — now covers proposer_id. */
  if (block_compute_hash(block) != EXIT_SUCCESS) {
    log_error("block_sign: block_compute_hash failed");
    return EXIT_FAILURE;
  }

  /* Decode hex hash to 32 raw bytes for signing. */
  uint8_t raw_hash[SHA256_DIGEST_LEN];
  sha256_from_hex((const char *)block->hash, raw_hash, SHA256_DIGEST_LEN);

  /* Sign raw hash with Dilithium-3. */
  OQS_SIG *sig = OQS_SIG_new(OQS_SIG_alg_dilithium_3);
  if (!sig) {
    log_error("block_sign: OQS_SIG_new failed");
    return EXIT_FAILURE;
  }

  block->proposer_sig_len = MAX_SIGNATURE_LENGTH;
  OQS_STATUS rc = OQS_SIG_sign(sig,
                                block->proposer_sig, &block->proposer_sig_len,
                                raw_hash, SHA256_DIGEST_LEN,
                                private_key);
  OQS_SIG_free(sig);

  if (rc != OQS_SUCCESS) {
    log_error("block_sign: OQS_SIG_sign failed (sk_len=%zu)", sk_len);
    block->proposer_sig_len = 0;
    return EXIT_FAILURE;
  }

  log_info("block_sign: block %u signed by '%s'", block->index, proposer_id);
  return EXIT_SUCCESS;
}

int block_verify_sig(const Block *block,
                     const uint8_t *public_key, size_t pk_len)
{
  if (!block || !public_key) {
    log_error("block_verify_sig: NULL argument");
    return EXIT_FAILURE;
  }
  if (block->proposer_sig_len == 0) {
    log_warn("block_verify_sig: block %u has no signature", block->index);
    return EXIT_FAILURE;
  }

  uint8_t raw_hash[SHA256_DIGEST_LEN];
  sha256_from_hex((const char *)block->hash, raw_hash, SHA256_DIGEST_LEN);

  OQS_SIG *sig = OQS_SIG_new(OQS_SIG_alg_dilithium_3);
  if (!sig) {
    log_error("block_verify_sig: OQS_SIG_new failed");
    return EXIT_FAILURE;
  }

  OQS_STATUS rc = OQS_SIG_verify(sig,
                                  raw_hash,            SHA256_DIGEST_LEN,
                                  block->proposer_sig, block->proposer_sig_len,
                                  public_key);
  OQS_SIG_free(sig);

  if (rc != OQS_SUCCESS) {
    log_warn("block_verify_sig: invalid signature on block %u (pk_len=%zu)",
             block->index, pk_len);
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
