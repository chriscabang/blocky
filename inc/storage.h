#ifndef STORAGE_H
#define STORAGE_H

/**
 * @file storage.h
 * @brief Block object store and ref management.
 *
 * Mirrors git's repository layout:
 *
 *   .chain/
 *   ├── blocks/              ← object store: one binary file per block, named by hash
 *   │   ├── a1b2c3d4...
 *   │   └── ...
 *   ├── refs/
 *   │   └── heads/
 *   │       └── main        ← tip hash of the main branch
 *   └── HEAD                ← "ref: refs/heads/main"  (symbolic ref)
 *
 * HEAD resolution: HEAD → refs/heads/main → block hash
 *
 * storage_insert() writes a block to the object store only.
 * storage_checkout() advances the current branch tip (like git checkout).
 * These are intentionally separate operations.
 */

#include "block.h"

#ifndef HASH_SIZE
#define HASH_SIZE 65            /* SHA-256 hex string + null terminator */
#endif

#define GENESIS_PREVIOUS_HASH "0"   /* Sentinel: genesis block has no parent */

/**
 * @brief Insert a block into the object store.
 *
 * Does NOT update HEAD or any ref. Call storage_checkout() separately
 * to advance the chain tip. Idempotent: re-inserting an existing block
 * returns EXIT_SUCCESS without modifying anything.
 *
 * @param block  Block to store. Must be non-NULL with a non-empty hash.
 * @return EXIT_SUCCESS or EXIT_FAILURE.
 */
int    storage_insert(const Block *block);

/**
 * @brief Read a block from the object store by its hash.
 *
 * Caller owns the returned Block and must free() it.
 *
 * @param hash  Hex hash string identifying the block.
 * @return Heap-allocated Block, or NULL on error.
 */
Block *storage_read(const char *hash);

/**
 * @brief Check whether a block with the given hash exists on disk.
 *
 * Does not allocate memory or open the block file.
 *
 * @param hash  Hex hash string to check.
 * @return 1 if the block file exists, 0 otherwise.
 */
int    storage_exists(const char *hash);

/**
 * @brief Resolve HEAD and fill buf with the current chain tip hash.
 *
 * Follows the symbolic ref chain: HEAD → refs/heads/main → hash.
 * Returns EXIT_FAILURE if the chain is empty (no checkout has been done yet).
 *
 * @param buf   Caller-provided buffer to receive the null-terminated hash.
 * @param size  Size of buf in bytes. Must be >= HASH_SIZE.
 * @return EXIT_SUCCESS or EXIT_FAILURE.
 */
int    storage_head(char *buf, size_t size);

/**
 * @brief Advance the current branch tip to the given block hash.
 *
 * Updates the ref pointed to by HEAD (e.g. refs/heads/main).
 * Verifies the block exists before updating. No-op if hash already
 * matches the current tip.
 *
 * @param hash  Block hash to set as the new chain tip.
 * @return EXIT_SUCCESS or EXIT_FAILURE.
 */
int    storage_checkout(const char *hash);

/**
 * @brief Walk the chain backwards from HEAD, collecting block hashes.
 *
 * Skips the first 'offset' blocks, then collects up to *count hashes.
 * Stops early at genesis or if a block cannot be read.
 * Updates *count to the actual number of hashes returned.
 *
 * Caller must free each entry and then the outer array:
 *   for (i = 0; i < count; i++) free(hashes[i]);
 *   free(hashes);
 *
 * @param offset  Blocks to skip from HEAD before collecting.
 * @param count   In: max hashes to return. Out: actual number returned.
 * @return Heap-allocated array of hash strings, or NULL if chain is empty
 *         or an error occurs.
 */
char **storage_scan(unsigned int offset, unsigned int *count);

#endif /* STORAGE_H */
