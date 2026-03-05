#include <stdarg.h>
#include <stddef.h>

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#endif
#include <setjmp.h>
#include <cmocka.h>
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "blockchain.h"
#include "storage.h"
#include "crypto.h"
#include "log.h"

/* ── helpers ──────────────────────────────────────────────────────────── */

static Block *make_block(uint32_t index, const unsigned char *prev_hash) {
  Block *b = calloc(1, sizeof(Block));
  b->index     = index;
  b->timestamp = (time_t)(1700000000 + index);
  b->next      = NULL;

  if (prev_hash) {
    memcpy(b->previous_hash, prev_hash, HASH_SIZE);
  } else {
    b->previous_hash[0] = '0';
    b->previous_hash[1] = '\0';
  }

  hash(b);
  return b;
}

/* ── setup / teardown ─────────────────────────────────────────────────── */

/* Empty chain — no blocks on disk */
static int setup_empty(void **state) {
  (void)state;
  system("rm -rf .chain");
  return 0;
}

/* One genesis block inserted and checked out */
static int setup_genesis(void **state) {
  system("rm -rf .chain");
  Block *genesis = make_block(0, NULL);
  assert_int_equal(storage_insert(genesis), EXIT_SUCCESS);
  assert_int_equal(storage_checkout((char *)genesis->hash), EXIT_SUCCESS);
  *state = genesis;
  return 0;
}

static int teardown(void **state) {
  if (*state) {
    free(*state);
    *state = NULL;
  }
  system("rm -rf .chain");
  return 0;
}

/* ── load() ───────────────────────────────────────────────────────────── */

/*
 * load() on an empty chain should synthesize a genesis block in memory.
 * (blockchain.c creates one when storage_head returns no block.)
 */
static void test_load_empty_chain(void **state) {
  (void)state;
  assert_int_equal(load(), EXIT_SUCCESS);
  unload();
}

/*
 * load() on a chain with an existing block should read that block from
 * storage and set it as the chain head.
 */
static void test_load_existing_chain(void **state) {
  Block *genesis = *state;

  assert_int_equal(load(), EXIT_SUCCESS);

  /* Verify by reading HEAD from storage independently */
  char head[HASH_SIZE];
  assert_int_equal(storage_head(head, sizeof(head)), EXIT_SUCCESS);
  assert_string_equal(head, (char *)genesis->hash);

  unload();
}

/*
 * load() called twice must fail on the second call (chain already loaded).
 */
static void test_load_twice(void **state) {
  (void)state;
  assert_int_equal(load(), EXIT_SUCCESS);
  assert_int_equal(load(), EXIT_FAILURE);
  unload();
}

/* ── validate() ───────────────────────────────────────────────────────── */

static void test_validate_null_block(void **state) {
  (void)state;
  assert_int_equal(validate(NULL), EXIT_FAILURE);
}

/* ── main ─────────────────────────────────────────────────────────────── */

int main(void) {
  log_set_stream(stderr);

  const struct CMUnitTest load_tests[] = {
    cmocka_unit_test_setup_teardown(test_load_empty_chain,    setup_empty,   teardown),
    cmocka_unit_test_setup_teardown(test_load_existing_chain, setup_genesis, teardown),
    cmocka_unit_test_setup_teardown(test_load_twice,          setup_empty,   teardown),
  };

  const struct CMUnitTest validate_tests[] = {
    cmocka_unit_test_setup_teardown(test_validate_null_block, setup_empty, teardown),
  };

  int failures = 0;
  failures += cmocka_run_group_tests_name("load",     load_tests,     NULL, NULL);
  failures += cmocka_run_group_tests_name("validate", validate_tests, NULL, NULL);
  return failures;
}
