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
static void test_save_and_load_block(void **state) {
  Block *test_block = *state;

  assert_non_null(storage_insert(test_block));

  Block *loaded_block = storage_read(storage_head());
  assert_non_null(loaded_block);
  assert_int_equal(loaded_block->index, test_block->index);
  assert_string_equal((char *)loaded_block->previous_hash,
                      (char *)test_block->previous_hash);
  assert_string_equal((char *)loaded_block->hash, (char *)test_block->hash);
  assert_int_equal(loaded_block->timestamp, test_block->timestamp);

  free(loaded_block);
}

/**
 * Test updating and retrieving the latest block hash.
 */
static void test_update_and_get_head(void **state) {
  (void)state; // Suppress unused variable warning

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
static void test_switch_block_state(void **state) {
  (void)state; // Suppress unused variable warning

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

  assert_int_equal(storage_move(head_hash), EXIT_SUCCESS);
  assert_string_equal(storage_head(), head_hash);
  assert_int_equal(storage_move(latest_head), EXIT_SUCCESS);
  assert_string_not_equal(storage_head(), head_hash);
  assert_string_equal(storage_head(), latest_head);

  free(new_block);
}

/**
 * Main function to run all tests.
 */
int main(void) {
  const struct CMUnitTest tests[] = {
      cmocka_unit_test_setup_teardown(test_save_and_load_block, setup,
                                      teardown),
      cmocka_unit_test(test_update_and_get_head),
      cmocka_unit_test_teardown(test_switch_block_state, teardown),
  };

  return cmocka_run_group_tests(tests, NULL, NULL);
}
