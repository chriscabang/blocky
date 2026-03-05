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

#include "block.h"
#include "crypto.h"
#include "log.h"

/* ── setup / teardown ─────────────────────────────────────────────────── */

static int setup(void **state) {
  (void)state;
  return 0;
}

static int teardown(void **state) {
  if (*state) {
    block_free(*state);
    *state = NULL;
  }
  return 0;
}

/* ── block_create ─────────────────────────────────────────────────────── */

static void test_create_genesis(void **state) {
  Block *b = block_create(0, NULL);
  assert_non_null(b);
  assert_int_equal(b->index, 0);
  assert_int_equal(b->previous_hash[0], '0');
  assert_int_equal(b->previous_hash[1], '\0');
  assert_null(b->next);
  *state = b;
}

static void test_create_non_genesis(void **state) {
  /* Build a genesis first to get a valid prev hash */
  Block *genesis = block_create(0, NULL);
  assert_non_null(genesis);
  assert_int_equal(block_compute_hash(genesis), EXIT_SUCCESS);

  Block *b = block_create(1, genesis->hash);
  assert_non_null(b);
  assert_int_equal(b->index, 1);
  assert_memory_equal(b->previous_hash, genesis->hash, HASH_SIZE);
  assert_null(b->next);

  block_free(genesis);
  *state = b;
}

static void test_create_sets_timestamp(void **state) {
  Block *b = block_create(0, NULL);
  assert_non_null(b);
  assert_true(b->timestamp > 0);
  *state = b;
}

static void test_create_next_is_null(void **state) {
  Block *b = block_create(0, NULL);
  assert_non_null(b);
  assert_null(b->next);
  *state = b;
}

/* ── block_compute_hash ───────────────────────────────────────────────── */

static void test_compute_hash_fills_hash(void **state) {
  Block *b = block_create(0, NULL);
  assert_non_null(b);
  assert_int_equal(block_compute_hash(b), EXIT_SUCCESS);
  assert_true(b->hash[0] != '\0');
  *state = b;
}

static void test_compute_hash_deterministic(void **state) {
  Block *b = block_create(0, NULL);
  assert_non_null(b);

  /* Pin the timestamp so the hash is reproducible */
  b->timestamp = 1700000000;

  assert_int_equal(block_compute_hash(b), EXIT_SUCCESS);

  unsigned char first[HASH_SIZE];
  memcpy(first, b->hash, HASH_SIZE);

  /* Reset hash field and recompute */
  memset(b->hash, 0, HASH_SIZE);
  assert_int_equal(block_compute_hash(b), EXIT_SUCCESS);
  assert_memory_equal(first, b->hash, HASH_SIZE);

  *state = b;
}

/* ── block_verify_hash ────────────────────────────────────────────────── */

static void test_verify_null_block(void **state) {
  (void)state;
  assert_int_equal(block_verify_hash(NULL), EXIT_FAILURE);
}

static void test_verify_valid_block(void **state) {
  Block *b = block_create(0, NULL);
  assert_non_null(b);
  b->timestamp = 1700000000;
  assert_int_equal(block_compute_hash(b), EXIT_SUCCESS);
  assert_int_equal(block_verify_hash(b), EXIT_SUCCESS);
  *state = b;
}

static void test_verify_tampered_hash(void **state) {
  Block *b = block_create(0, NULL);
  assert_non_null(b);
  b->timestamp = 1700000000;
  assert_int_equal(block_compute_hash(b), EXIT_SUCCESS);

  /* Corrupt the stored hash */
  b->hash[0] ^= 0xFF;
  assert_int_equal(block_verify_hash(b), EXIT_FAILURE);

  *state = b;
}

static void test_verify_tampered_field(void **state) {
  Block *b = block_create(0, NULL);
  assert_non_null(b);
  b->timestamp = 1700000000;
  assert_int_equal(block_compute_hash(b), EXIT_SUCCESS);

  /* Tamper with a field that hash() covers */
  b->nonce = 999999;
  assert_int_equal(block_verify_hash(b), EXIT_FAILURE);

  *state = b;
}

/* ── block_free ───────────────────────────────────────────────────────── */

static void test_free_null(void **state) {
  (void)state;
  block_free(NULL); /* must not crash */
}

static void test_free_valid(void **state) {
  (void)state;
  Block *b = block_create(0, NULL);
  assert_non_null(b);
  block_free(b); /* must not crash or leak */
}

/* ── main ─────────────────────────────────────────────────────────────── */

int main(void) {
  log_set_stream(stderr);

  const struct CMUnitTest create_tests[] = {
    cmocka_unit_test_setup_teardown(test_create_genesis,       setup, teardown),
    cmocka_unit_test_setup_teardown(test_create_non_genesis,   setup, teardown),
    cmocka_unit_test_setup_teardown(test_create_sets_timestamp, setup, teardown),
    cmocka_unit_test_setup_teardown(test_create_next_is_null,  setup, teardown),
  };

  const struct CMUnitTest compute_tests[] = {
    cmocka_unit_test_setup_teardown(test_compute_hash_fills_hash,    setup, teardown),
    cmocka_unit_test_setup_teardown(test_compute_hash_deterministic, setup, teardown),
  };

  const struct CMUnitTest verify_tests[] = {
    cmocka_unit_test_setup_teardown(test_verify_null_block,    setup, teardown),
    cmocka_unit_test_setup_teardown(test_verify_valid_block,   setup, teardown),
    cmocka_unit_test_setup_teardown(test_verify_tampered_hash, setup, teardown),
    cmocka_unit_test_setup_teardown(test_verify_tampered_field, setup, teardown),
  };

  const struct CMUnitTest free_tests[] = {
    cmocka_unit_test_setup_teardown(test_free_null,  setup, teardown),
    cmocka_unit_test_setup_teardown(test_free_valid, setup, teardown),
  };

  int failures = 0;
  failures += cmocka_run_group_tests_name("create",  create_tests,  NULL, NULL);
  failures += cmocka_run_group_tests_name("compute", compute_tests, NULL, NULL);
  failures += cmocka_run_group_tests_name("verify",  verify_tests,  NULL, NULL);
  failures += cmocka_run_group_tests_name("free",    free_tests,    NULL, NULL);
  return failures;
}
