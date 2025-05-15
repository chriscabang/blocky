#include <stdarg.h>
#include <stddef.h>

#if defined (__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#endif
#include <setjmp.h>
#include <cmocka.h>
#if defined (__clang__)
#pragma clang diagnostic pop
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "blockchain.h"
#include "crypto.h"
#include "storage.h"

#include "log.h"

/**
 * Setup function - runs before each test.
 */
static int setup(void **state) {
  Block *genesis = malloc(sizeof(Block));
  memset(genesis, 0, sizeof(Block));
  genesis->index = 1;
  memset(genesis->transactions, '0', sizeof(genesis->transactions));
  genesis->timestamp = 1700000000;
  genesis->transaction_count = 0;

  hash(genesis);

  *state = genesis;

  storage_insert(genesis);

  return 0;
}

/**
 * Teardown function - runs after each test.
 */
static int teardown(void **state) {
  Block *genesis = *state;
  free(genesis);

  // Recusively remove the blocks directory
  char command[256];
  snprintf(command, sizeof(command), "rm -rf %s", ".chain");
  system(command);

  return 0;
}

/**
 * Test saving and loading a block.
 */
static void save_and_load_block(void **state) {
  Block *block = *state;

  assert_int_equal(storage_insert(block), EXIT_SUCCESS);

  Block *loaded_block = storage_read(storage_head());

  assert_non_null(loaded_block);
  assert_int_equal(loaded_block->index, block->index);
  assert_string_equal((char *)loaded_block->previous_hash,
                      (char *)block->previous_hash);
  assert_string_equal((char *)loaded_block->hash, (char *)block->hash);
  assert_int_equal(loaded_block->timestamp, block->timestamp);

  free(loaded_block);
}

/**
 * Test updating and retrieving the latest block hash.
 */
static void update_and_get_head(void **state) {
  (void) state; // Suppress unused variable warning

  // Get the current head hash
  const char *head_hash = storage_head();
  assert_non_null(head_hash);

  // Create a new block with the specified hash
  Block *new_block = malloc(sizeof(Block));
  memset(new_block, 0, sizeof(Block));
  new_block->index = 2;
  memset(new_block->transactions, '0', sizeof(new_block->transactions));
  new_block->timestamp = 1800000000;
  new_block->transaction_count = 0;

  assert_int_equal(hash(new_block), EXIT_SUCCESS);
  assert_int_equal(storage_insert(new_block), EXIT_SUCCESS);

  const char *current_head = storage_head();
  assert_non_null(current_head);
  assert_string_not_equal(current_head, head_hash);

  free(new_block);
}

/**
 * Test switching to a block state.
 */
static void switch_block_state(void **state) {
  (void) state; // Suppress unused variable warning

  const char *head_hash = storage_head();

  // Create a new block with the specified hash
  Block *new_block = malloc(sizeof(Block));
  memset(new_block, 0, sizeof(Block));
  new_block->index = 2;
  memset(new_block->transactions, '0', sizeof(new_block->transactions));
  new_block->timestamp = 1800000000;
  new_block->transaction_count = 0;

  assert_int_equal(hash(new_block), EXIT_SUCCESS);
  assert_int_equal(storage_insert(new_block), EXIT_SUCCESS);

  const char *latest_head = storage_head();

  assert_string_equal(latest_head, (char*) new_block->hash);
  assert_int_equal(storage_move(head_hash), EXIT_SUCCESS);
  assert_string_equal(storage_head(), head_hash);

  free(new_block);
}

/**
 * Test scanning blocks.
 */
static void scan_blocks(void **state) {
  (void) state; // Suppress unused variable warning

  // Create multiple blocks, do a loop to insert them
  for (int i = 2; i < 5; i++) {
    Block *block = malloc(sizeof(Block));
    memset(block, 0, sizeof(Block));
    block->index = i;
    memset(block->transactions, '0', sizeof(block->transactions));
    block->timestamp = 1800000000 + i;
    block->transaction_count = i*i;

    assert_int_equal(hash(block), EXIT_SUCCESS);
    assert_int_equal(storage_insert(block), EXIT_SUCCESS);

    free(block);
  }

  // Scan the blocks, store the returned block hashes so we can check them
  unsigned int count = 3;
  char **blocks = storage_scan(0, &count);

  // Block count should be 3 or more
  assert_int_not_equal(count, 0);


  // Dealocate the blocks
  for (unsigned int i = 0; i < count; i++) {
    free(blocks[i]);
  }
}

/**
 * Main function to run all tests.
 */
int main(void) {
  log_set_stream(stderr);
  const struct CMUnitTest tests[] = {
      cmocka_unit_test_setup(save_and_load_block, setup),
      cmocka_unit_test(update_and_get_head),
      cmocka_unit_test(switch_block_state),
      cmocka_unit_test_teardown(scan_blocks, teardown),
  };

  return cmocka_run_group_tests(tests, NULL, NULL);
}
