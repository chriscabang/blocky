#include "blockchain.h"
#include "consensus.h"
#include "storage.h"
#include "crypto.h"
#include "log.h"

#include <stdlib.h>
#include <string.h>

Block *chain = NULL;

/** 
 * load
 * @brief Load the blockchain from storage.
 *
 * @return EXIT_SUCCESS successful, EXIT_FAILURE otherwise.
 */
int load(void) {
  if (chain != NULL) {
    log_debug("Blockchain already loaded");
    return EXIT_FAILURE;
  }

  log_info("Loading blockchain from storage");

  chain = storage_read(storage_head());
  if (chain == NULL) {
    // Initialize the blockchain with a genesis block
    chain = (Block*) malloc(sizeof(Block));
    if (chain == NULL) {
      log_error("Failed to allocate memory for blockchain");
      return EXIT_FAILURE;
    }

    chain->index = 0;

    // :TODO: Apply memory manglement
    const char* timestamp = "1994-12-22 00:00:00";
    struct tm tm;
    strptime(timestamp, "%Y-%m-%d %H:%M:%S", &tm);
    chain->timestamp = mktime(&tm);

    memcpy(chain->previous_hash, "0", 1);
    memcpy(chain->merkle_root, "fist-of-legend", 7);

    chain->transaction_count = 0;
    chain->consensus = 0; // PoW
                          // :TODO: Implement PoS
    chain->nonce = 0;
    chain->next = NULL;

    hash(chain);
  }
  
  return EXIT_SUCCESS;
}

/**
 * validate
 * @brief Validates a block by checking:
 * - Correct previous hash
 * - Recomputed block hash (includes merkle root)
 * - Transaction validity
 * - Consensus algorithm (PoW or PoS)
 * - Branch-base signature verification
 *
 * @param block: The block to validate.
 * @return EXIT_SUCCESS if successful, EXIT_FAILURE otherwise.
 */
int validate(Block* block) {
  log_info("Validating block %u", block->index);

  // :TOOD: Cleanup, too much if statements. Use do-while loop

  if (block == NULL) {
    log_error("Block cannot be NULL");
    return EXIT_FAILURE;
  }
  
  // Check if the previous hash is valid
  Block* previous_block = storage_read((char*) block->previous_hash);
  if (previous_block == NULL) {
    log_error("Invalid previous block hash");
    return EXIT_FAILURE;
  } else {
    // :TODO: Implement own memory management
    // Check if the previous block hash matches
    if (memcmp(block->previous_hash, previous_block->hash, HASH_SIZE) != 0) {
      log_error("Mismatch between previous block hash and current block");
      free(previous_block);
      return EXIT_FAILURE;
    }

    //check if previous block next hash matches the current block hash
    if (memcmp(previous_block->next->hash, block->hash, HASH_SIZE) != 0) {
      log_error("Previous block next hash does not match current block hash");
      free(previous_block);
      return EXIT_FAILURE;
    }
    free(previous_block);
  }

  // Recompute the block hash
  unsigned char block_hash[HASH_SIZE] = {0};
  memcpy(block_hash, block->hash, HASH_SIZE);
  if (hash(block) != EXIT_SUCCESS) {
    log_error("Failed to compute block hash");
    return EXIT_FAILURE;
  }
  if (memcmp(block->hash, block_hash, HASH_SIZE) != 0) {
    log_error("Block hash does not match computed hash");
    return EXIT_FAILURE;
  }

  // Check transaction validity
  // Transaction cannot be NULL or empty
  for (uint32_t i = 0; i < block->transaction_count; i++) {
    if (block->transactions[i].amount <= 0) {
      log_error("Invalid transaction amount");
      return EXIT_FAILURE;
    }
  }

  // Check consensus algorithm
  if (block->consensus != 0 && block->consensus != 1) {
    log_error("Invalid consensus algorithm");
    return EXIT_FAILURE;
  }
  // :TODO: Validate Consensus 

  // Check branch-base signature verification
  /*if (verify_signature(block) != EXIT_SUCCESS) {*/
  /*  log_error("Invalid block signature");*/
  /*  return EXIT_FAILURE;*/
  /*}*/
  // :TODO: Validate signature

  return EXIT_SUCCESS;
}

