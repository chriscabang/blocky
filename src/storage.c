/* storage.c */
// Implements block storage and retrieval functions

#include "storage.h"
/*#include "crypto.h"*/
#include "log.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// :TODO: Implement cross-platform compatibility
#ifdef _WIN32
#include <direct.h>
#define mkdir(path, mode) _mkdir(path)
#else
#include <sys/stat.h>
#endif

// Directory paths for block storage
#define STORAGE_DIR ".chain/"
#define BLOCKS_DIR ".chain/blocks/"
#define HEAD_FILE ".chain/HEAD"
#define REFS_DIR ".chain/refs/"

#define HASH_SIZE 65 // SHA-256 hash size + null terminator

/**
 * init
 * @brief Initialize the storage directory structure.
 */
static void init(void) {
  log_info("Initializing storage directories");

  int status = 0;
  do {
    if ((status = mkdir(STORAGE_DIR, 0755)) == -1)
      break;
    if ((status = mkdir(BLOCKS_DIR, 0755)) == -1)
      break;
    if ((status = mkdir(REFS_DIR, 0755)) == -1)
      break;
  } while (0);

  if (status == -1 && errno != EEXIST) {
    log_error("Error creating storage directories: (%d) %s", errno,
              strerror(errno));
    exit(EXIT_FAILURE);
  }
}

/**
 * update_head
 * @brief Update the HEAD file with the hash of the latest block.
 *
 * @param hash: The hash of the latest block.
 * @return EXIT_SUCCESS if successful, EXIT_FAILURE otherwise.
 */
static int update_head(const char *hash) {
  log_debug("Updating head");
  FILE *file = fopen(HEAD_FILE, "w");
  if (file == NULL) {
    log_error("Error updating latest block reference");
    return EXIT_FAILURE;
  }
  fprintf(file, "%s", hash);
  fclose(file);
  log_debug("Head updated to %s", hash);
  return EXIT_SUCCESS;
}

/**
 * storage_insert
 * @brief Save a block to storage using its hash as an identifier.
 *
 * @param block: The block to save.
 * @return EXIT_SUCCESS if successful, EXIT_FAILURE otherwise.
 */
int storage_insert(const Block *block) {
  init();

  // Check if block is not NULL
  if (block == NULL) {
    log_error("Cannot insert NULL block");
    return EXIT_FAILURE;
  }

  // Check if block hash is not NULL
  const char *hash = (const char *)block->hash;
  if (hash == NULL) {
    log_error("Cannot insert block with NULL hash");
    return EXIT_FAILURE;
  }

  char path[256];
  snprintf(path, sizeof(path), "%s%s", BLOCKS_DIR, hash);

  log_debug("Inserting block %s", path);

  FILE *file = fopen(path, "wb");
  if (file == NULL) {
    log_error("Error saving block");
    return EXIT_FAILURE;
  }

  log_info("Writing block to storage");

  fwrite(block, sizeof(Block), 1, file);
  fclose(file);

  if (update_head(hash)) {
    // Delete the block file if the head update fails
    log_error("Failed to update head block reference");
    remove(path);
    return EXIT_FAILURE;
  }

  log_info("Block %s saved successfully", hash);

  return EXIT_SUCCESS;
}

/**
 * storage_read
 * @brief Reads a block from storage using its hash as an identifier.
 *
 * @param hash: The hash of the block to load.
 * @return The block information, or NULL if the block could not be loaded.
 */
Block *storage_read(const char *hash) {
  char path[256];
  snprintf(path, sizeof(path), "%s%s", BLOCKS_DIR, hash);

  log_info("Reading block %s", path);

  FILE *file = fopen(path, "rb");
  if (file == NULL) {
    log_error("Block not found");
    return NULL;
  }

  Block *block = (Block *)malloc(sizeof(Block));
  fread(block, sizeof(Block), 1, file);
  fclose(file);

  log_info("Block successfully read");

  return block;
}

/**
 * storage_move
 * @brief Move to a block in the blockchain.
 *
 * @param hash: The hash of the block to move to.
 * @return EXIT_SUCCESS if successful, EXIT_FAILURE otherwise.
 */
int storage_move(const char *hash) {
  const char *head_hash = storage_head();

  log_info("Moving block %s to head", hash);

  if (memcmp(hash, head_hash, HASH_SIZE) == 0) {
    return EXIT_SUCCESS;
  }
  return update_head(hash);
}

/**
 * storage_head
 * @brief Retrieves the head block hash of the current blockchain.
 *
 * @return The head block of the current blockchain.
 */
const char *storage_head(void) {
  FILE *file = fopen(HEAD_FILE, "r");
  log_info("Reading head block reference");
  if (file == NULL) {
    log_error("Error reading head block reference");
    return NULL;
  }

  char *hash = (char *)malloc(HASH_SIZE);
  memset(hash, 0, HASH_SIZE);
  fgets(hash, HASH_SIZE, file);
  fclose(file);

  log_debug("Head block hash: %s", hash);

  return hash;
}

/**
 * storage_scan
 * @brief Reads blocks between the offset and offset + count. A 0 offset reads
 * from the head block.
 *
 * @param offset: Block offset to start reading from.
 * @param count: Number of blocks to read, return the actual number read.
 * @return An array of block hashes.
 */
char **storage_scan(unsigned int offset, unsigned int *count) {
  if (!count) {
    return NULL;
  }

  unsigned int max = *count;
  unsigned int actual = 0;
  const char *head_hash = storage_head();
  if (memcmp(head_hash, "0", 1) == 0) {
    return NULL;
  }

  char current_hash[HASH_SIZE];
  memcpy(current_hash, head_hash, HASH_SIZE);
  for (unsigned int i = 0; i < offset; i++) {
    Block *block = storage_read(current_hash);
    if (!block) {
      break;
    }
    if (memcmp(block->previous_hash, "0", 1) == 0) {
      free(block);
      break;
    }
    memcpy(current_hash, block->previous_hash, HASH_SIZE);
    free(block);
  }

  char **scans = malloc(max * sizeof(char *));
  while (actual < max) {
    Block *block = storage_read(current_hash);
    if (!block) {
      break;
    }
    scans[actual] = malloc(HASH_SIZE);
    memcpy(scans[actual], current_hash, HASH_SIZE);
    if (memcmp(block->previous_hash, "0", 1) == 0) {
      free(block);
      break;
    }
    memcpy(current_hash, block->previous_hash, HASH_SIZE);
    free(block);
    actual++;
  }

  *count = actual;
  return scans;
}
