/**
 * @file block.c
 * @brief Single-block operations: create, hash, verify, free.
 */

#include "block.h"
#include "crypto.h"
#include "storage.h" /* GENESIS_PREVIOUS_HASH */

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
  return hash(block);
}

int block_verify_hash(const Block *block) {
  if (!block) return EXIT_FAILURE;

  /* Compute hash on a stack copy without touching the original */
  Block copy = *block;
  memset(copy.hash, 0, sizeof(copy.hash));
  copy.next = NULL; /* exclude runtime pointer from hash input */

  if (hash(&copy) != EXIT_SUCCESS) return EXIT_FAILURE;

  return (memcmp(copy.hash, block->hash, HASH_SIZE) == 0)
           ? EXIT_SUCCESS
           : EXIT_FAILURE;
}

void block_free(Block *block) {
  free(block);
}
