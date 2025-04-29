#ifndef STORAGE_H
#define STORAGE_H

/**
 * @file storage.h
 * @brief Implements block storage and retrieval functions
 *
 * The storage module provides functions to save and load blocks from disk.
 * The blocks are stored in the `chain/blocks/` directory with the block hash as
 * the filename. The `HEAD` file contains the hash of the latest block in the
 * blockchain.
 * The storage module also provides functions to update the reference to the
 * latest block in the blockchain.
 * The storage module also provides functions to mark a block as an orphan.
 * The storage module is used by the consensus module to store and retrieve
 * blocks from disk.
 * The storage module is used by the network module to send and receive blocks
 * over the network.
 * The storage module is used by the main module to create a blockchain with a
 * genesis block and add new blocks with transactions.
 * The storage module is used by the main module to save the blockchain to disk
 * and load it back into memory.
 * The storage module is used by the main module to destroy the blockchain.
 * The storage module is used by the main module to create a block from a
 * transaction.
 *
 * chain/
 * ├── blocks/           <-- Stores blocks by their hash
 * │   ├── a1b2c3d4...   <-- Block file (hashed contents)
 * │   ├── e5f6g7h8...
 * │   └── ...
 * ├── refs/             <-- Stores references to blocks
 * │   ├── main          <-- Reference to the main blockchain
 * │   ├── orphan        <-- Reference to orphaned blocks
 * │   └── ...
 * ├── heads/             <-- Stores references to blocks
 * │   ├── main          <-- Reference to the main blockchain
 * │   ├── orphan        <-- Reference to orphaned blocks
 * │   └── ...
 * └── HEAD              <-- Symbolic link to the current head block
 *
 */


#include "blockchain.h"

/**
 * storage_insert
 * @brief Inserts a block to storage using its hash as an identifier.
 *
 * @param block: The block to save.
 * @return EXIT_SUCCESS if successful, EXIT_FAILURE otherwise.
 */
int          storage_insert(const Block* block);

/**
 * storage_read
 * @brief Reads a block from storage using its hash as an identifier.
 *
 * @param hash: The hash of the block to load.
 * @return The block information, or NULL if the block could not be loaded.
 */
Block*       storage_read(const char* hash);

/**
 * storage_move
 * @brief Moves to a block in the blockchain.
 *
 * @param hash: The hash of the block to move to.
 * @return EXIT_SUCCESS if successful, EXIT_FAILURE otherwise.
 */
int          storage_move(const char* hash);

/**
 * storage_head
 * @brief Retrieves the head block hash of the current blockchain.
 *
 * @return The head block of the current blockchain.
 */
const char*  storage_head(void);

/**
 * storage_scan
 * @brief Reads blocks between the offset and offset + count. A 0 offset reads
 * from the head block.
 *
 * @param offset: Block offset to start reading from.
 * @param count: Number of blocks to read, return the actual number read.
 * @return Array of block hashes, or NULL if no blocks are found.
 */
char**       storage_scan(unsigned int offset, unsigned int* count);

#endif//STORAGE_H
