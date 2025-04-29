/**
 * # blockchain.h
 *
 * **Description**
 * Defines the `Block` data structure.
 *
 * **Author:** Chris Cabang <chriscabang@outlook.com>
 * **Date:** Feb 6 18:54:49 2025
 * **License:** :TODO:
 *
 * Copyright (c) 2025 Chris Cabang
 *
 * This software is provided 'as-is', without any express or implied
 * warranty. In no event will the authors be held liable for any damages
 * arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 *    claim that you wrote the original software. If you use this software
 *    in a product, an acknowledgment in the product documentation would be
 *    appreciated but is not required.
 * 2. Altered source versions must be plainly marked as such, and must not be
 *    misrepresented as being the original software.
 * 3. This notice may not be removed or altered from any source distribution.
 */

#ifndef BLOCKCHAIN_H
#define BLOCKCHAIN_H

#include "transaction.h"

#include <time.h>

#define MAX_TRANSACTIONS 10

typedef struct Block {
  /*Header*/
  uint32_t index;                  // Block number
  time_t timestamp;                // Block creation time
  unsigned char previous_hash[65]; // Hash of the previous block
  unsigned char merkle_root[65];   // Merkle tree root hash
  uint32_t nonce;                  // Proof of work counter
  uint8_t consensus;               // Consensus algorithm (0 for PoW, 1 for PoS)
  unsigned char hash[65];          // Block hash

  /*Content*/
  Transaction transactions[MAX_TRANSACTIONS]; // List of transactions
  uint32_t transaction_count;                 // Number of transactions

  struct Block *next; // Pointer to the next block
} Block;

/**
 * @brief Load the blockchain from storage.
 *
 * @return EXIT_SUCCESS if successful, EXIT_FAILURE otherwise.
 */
int  load(void);

/**
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
int  validate(Block* block);

/**
 * @brief Adds a single validated block to the blockchain.
 * layer. This remains in local storage until the block is proposed.
 *
 * @param block: The block to add.
 * @return EXIT_SUCCESS if successful, EXIT_FAILURE otherwise.
 */
int  add(Block* block);

/**
 * @brief Propose a new block to the blockchain network.
 *
 * @param block: The block to propose.
 * @return EXIT_SUCCESS if successful, EXIT_FAILURE otherwise.
 */
int  propose(Block* block);

/**
 * @brief Show information about the current blockchain.
 *
 * @return EXIT_SUCCESS if successful, EXIT_FAILURE otherwise.
 */
int  info(void);

/**
 * @brief Show a block in the blockchain.
 *
 * @param hash: The hash of the block to show.
 * @return EXIT_SUCCESS if successful, EXIT_FAILURE otherwise.
 */
int  show(const char* hash);

/**
 * @brief List blocks in the blockchain.
 *
 * @param blocks_per_page: The number of blocks to list per page, returns the
 * actual number of blocks listed.
 */
void list(int* blocks_per_page);

/**
 * @brief Unload the blockchain from memory.
 */
void unload(void);

#endif // BLOCKCHAIN_H
