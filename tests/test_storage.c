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

#include "storage.h"
#include "crypto.h"
#include "log.h"

/* ── helpers ──────────────────────────────────────────────────────────── */

#define CHAIN_LEN 5

typedef struct {
  Block *blocks[CHAIN_LEN];
} ChainState;

static Block *make_block(uint32_t index, const unsigned char *prev_hash) {
  Block *b = calloc(1, sizeof(Block));
  b->index     = index;
  b->timestamp = (time_t)(1700000000 + index);
  b->transaction_count = 0;
  b->next      = NULL;

  if (prev_hash) {
    memcpy(b->previous_hash, prev_hash, HASH_SIZE);
  } else {
    /* genesis: sentinel previous hash */
    b->previous_hash[0] = GENESIS_PREVIOUS_HASH[0];
    b->previous_hash[1] = '\0';
  }

  block_hash(b);
  return b;
}

/* ── setup / teardown ─────────────────────────────────────────────────── */

/* Clean slate — no .chain directory */
static int setup_empty(void **state) {
  (void)state;
  system("rm -rf .chain");
  return 0;
}

/* Genesis block inserted and checked out. state = genesis Block* */
static int setup_genesis(void **state) {
  system("rm -rf .chain");
  Block *genesis = make_block(0, NULL);
  assert_int_equal(storage_insert(genesis), EXIT_SUCCESS);
  assert_int_equal(storage_checkout((char *)genesis->hash), EXIT_SUCCESS);
  *state = genesis;
  return 0;
}

/*
 * Genesis inserted but NOT checked out.
 * Used to test storage_head() on an empty ref.
 * state = genesis Block*
 */
static int setup_inserted_no_checkout(void **state) {
  system("rm -rf .chain");
  Block *genesis = make_block(0, NULL);
  assert_int_equal(storage_insert(genesis), EXIT_SUCCESS);
  *state = genesis;
  return 0;
}

/* 5-block chain, HEAD at blocks[4]. state = ChainState* */
static int setup_chain(void **state) {
  system("rm -rf .chain");
  ChainState *cs = calloc(1, sizeof(ChainState));

  cs->blocks[0] = make_block(0, NULL); /* genesis */
  assert_int_equal(storage_insert(cs->blocks[0]), EXIT_SUCCESS);
  assert_int_equal(storage_checkout((char *)cs->blocks[0]->hash), EXIT_SUCCESS);

  for (int i = 1; i < CHAIN_LEN; i++) {
    cs->blocks[i] = make_block(i, cs->blocks[i - 1]->hash);
    assert_int_equal(storage_insert(cs->blocks[i]), EXIT_SUCCESS);
    assert_int_equal(storage_checkout((char *)cs->blocks[i]->hash), EXIT_SUCCESS);
  }

  *state = cs;
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

static int teardown_chain(void **state) {
  ChainState *cs = *state;
  if (cs) {
    for (int i = 0; i < CHAIN_LEN; i++) free(cs->blocks[i]);
    free(cs);
    *state = NULL;
  }
  system("rm -rf .chain");
  return 0;
}

/* ── storage_insert ───────────────────────────────────────────────────── */

static void test_insert_null_block(void **state) {
  (void)state;
  assert_int_equal(storage_insert(NULL), EXIT_FAILURE);
}

static void test_insert_empty_hash(void **state) {
  (void)state;
  Block b;
  memset(&b, 0, sizeof(b)); /* hash[0] == '\0' */
  assert_int_equal(storage_insert(&b), EXIT_FAILURE);
}

static void test_insert_valid_block(void **state) {
  (void)state;
  Block *b = make_block(0, NULL);
  assert_int_equal(storage_insert(b), EXIT_SUCCESS);
  assert_int_equal(storage_exists((char *)b->hash), 1);
  free(b);
}

static void test_insert_duplicate(void **state) {
  Block *genesis = *state;
  /* genesis is already inserted by setup — re-inserting must succeed */
  assert_int_equal(storage_insert(genesis), EXIT_SUCCESS);
  /* block file still exists and is intact */
  assert_int_equal(storage_exists((char *)genesis->hash), 1);
}

/* ── storage_read ─────────────────────────────────────────────────────── */

static void test_read_valid_block(void **state) {
  Block *genesis = *state;
  Block *loaded  = storage_read((char *)genesis->hash);

  assert_non_null(loaded);
  assert_int_equal(loaded->index, genesis->index);
  assert_int_equal(loaded->timestamp, genesis->timestamp);
  assert_string_equal((char *)loaded->hash, (char *)genesis->hash);
  assert_string_equal((char *)loaded->previous_hash,
                      (char *)genesis->previous_hash);
  free(loaded);
}

static void test_read_null_hash(void **state) {
  (void)state;
  assert_null(storage_read(NULL));
}

static void test_read_empty_hash(void **state) {
  (void)state;
  assert_null(storage_read(""));
}

static void test_read_nonexistent_hash(void **state) {
  (void)state;
  assert_null(storage_read("0000000000000000000000000000000000000000"
                           "000000000000000000000000dead"));
}

/* ── storage_exists ───────────────────────────────────────────────────── */

static void test_exists_after_insert(void **state) {
  Block *genesis = *state;
  assert_int_equal(storage_exists((char *)genesis->hash), 1);
}

static void test_exists_nonexistent(void **state) {
  (void)state;
  assert_int_equal(storage_exists("deadbeefdeadbeefdeadbeefdeadbeef"
                                  "deadbeefdeadbeefdeadbeefdeadbeef0"), 0);
}

static void test_exists_null_hash(void **state) {
  (void)state;
  assert_int_equal(storage_exists(NULL), 0);
}

/* ── storage_head ─────────────────────────────────────────────────────── */

static void test_head_before_checkout(void **state) {
  /* Block inserted (init ran, HEAD exists) but checkout never called */
  Block *genesis = *state;
  (void)genesis;
  char buf[HASH_SIZE];
  assert_int_equal(storage_head(buf, sizeof(buf)), EXIT_FAILURE);
}

static void test_head_after_checkout(void **state) {
  Block *genesis = *state;
  char buf[HASH_SIZE];
  assert_int_equal(storage_head(buf, sizeof(buf)), EXIT_SUCCESS);
  assert_string_equal(buf, (char *)genesis->hash);
}

static void test_head_invalid_buffer(void **state) {
  (void)state;
  char tiny[4];
  assert_int_equal(storage_head(tiny, sizeof(tiny)), EXIT_FAILURE);
  assert_int_equal(storage_head(NULL, HASH_SIZE), EXIT_FAILURE);
}

/* ── storage_checkout ─────────────────────────────────────────────────── */

static void test_checkout_null_hash(void **state) {
  (void)state;
  assert_int_equal(storage_checkout(NULL), EXIT_FAILURE);
}

static void test_checkout_empty_hash(void **state) {
  (void)state;
  assert_int_equal(storage_checkout(""), EXIT_FAILURE);
}

static void test_checkout_nonexistent_hash(void **state) {
  (void)state;
  assert_int_equal(
    storage_checkout("ffffffffffffffffffffffffffffffffffffffffffffffff"
                     "ffffffffffffffff0"),
    EXIT_FAILURE);
}

static void test_checkout_valid_hash(void **state) {
  ChainState *cs = *state;
  /* Move HEAD back to genesis */
  assert_int_equal(
    storage_checkout((char *)cs->blocks[0]->hash), EXIT_SUCCESS);

  char buf[HASH_SIZE];
  assert_int_equal(storage_head(buf, sizeof(buf)), EXIT_SUCCESS);
  assert_string_equal(buf, (char *)cs->blocks[0]->hash);
}

static void test_checkout_already_at_head(void **state) {
  ChainState *cs = *state;
  /* HEAD is already at blocks[4]; checking out again is a no-op */
  const char *tip = (char *)cs->blocks[CHAIN_LEN - 1]->hash;
  assert_int_equal(storage_checkout(tip), EXIT_SUCCESS);

  char buf[HASH_SIZE];
  assert_int_equal(storage_head(buf, sizeof(buf)), EXIT_SUCCESS);
  assert_string_equal(buf, tip);
}

/* ── storage_scan ─────────────────────────────────────────────────────── */

static void test_scan_null_count(void **state) {
  (void)state;
  assert_null(storage_scan(0, NULL));
}

static void test_scan_empty_chain(void **state) {
  (void)state;
  /* No HEAD set — scan returns NULL */
  unsigned int count = 10;
  char **result = storage_scan(0, &count);
  assert_null(result);
  assert_int_equal(count, 0);
}

static void test_scan_single_block(void **state) {
  Block *genesis = *state;

  unsigned int count = 5;
  char **result = storage_scan(0, &count);

  assert_non_null(result);
  assert_int_equal(count, 1);
  assert_string_equal(result[0], (char *)genesis->hash);

  free(result[0]);
  free(result);
}

static void test_scan_multiple_blocks(void **state) {
  ChainState *cs = *state;

  unsigned int count = CHAIN_LEN;
  char **result = storage_scan(0, &count);

  assert_non_null(result);
  assert_int_equal(count, CHAIN_LEN);

  /* Scan walks newest → oldest: blocks[4] first, blocks[0] last */
  for (int i = 0; i < CHAIN_LEN; i++) {
    int expected_idx = CHAIN_LEN - 1 - i;
    assert_string_equal(result[i], (char *)cs->blocks[expected_idx]->hash);
  }

  for (unsigned int i = 0; i < count; i++) free(result[i]);
  free(result);
}

static void test_scan_count_capped(void **state) {
  (void)state;
  /* Request fewer blocks than the chain length */
  unsigned int count = 3;
  char **result = storage_scan(0, &count);

  assert_non_null(result);
  assert_int_equal(count, 3);

  for (unsigned int i = 0; i < count; i++) free(result[i]);
  free(result);
}

static void test_scan_with_offset(void **state) {
  ChainState *cs = *state;

  /* Skip 2 blocks from HEAD: result starts at blocks[2] */
  unsigned int count = CHAIN_LEN;
  char **result = storage_scan(2, &count);

  assert_non_null(result);
  assert_int_equal(count, CHAIN_LEN - 2);
  assert_string_equal(result[0], (char *)cs->blocks[CHAIN_LEN - 3]->hash);

  for (unsigned int i = 0; i < count; i++) free(result[i]);
  free(result);
}

static void test_scan_offset_past_genesis(void **state) {
  (void)state;
  /* Offset larger than chain length — nothing to return */
  unsigned int count = 5;
  char **result = storage_scan(CHAIN_LEN + 10, &count);
  assert_null(result);
}

/* ── main ─────────────────────────────────────────────────────────────── */

int main(void) {
  log_set_stream(stderr);

  const struct CMUnitTest insert_tests[] = {
    cmocka_unit_test_setup_teardown(test_insert_null_block,  setup_empty,   teardown),
    cmocka_unit_test_setup_teardown(test_insert_empty_hash,  setup_empty,   teardown),
    cmocka_unit_test_setup_teardown(test_insert_valid_block, setup_empty,   teardown),
    cmocka_unit_test_setup_teardown(test_insert_duplicate,   setup_genesis, teardown),
  };

  const struct CMUnitTest read_tests[] = {
    cmocka_unit_test_setup_teardown(test_read_valid_block,       setup_genesis, teardown),
    cmocka_unit_test_setup_teardown(test_read_null_hash,         setup_empty,   teardown),
    cmocka_unit_test_setup_teardown(test_read_empty_hash,        setup_empty,   teardown),
    cmocka_unit_test_setup_teardown(test_read_nonexistent_hash,  setup_empty,   teardown),
  };

  const struct CMUnitTest exists_tests[] = {
    cmocka_unit_test_setup_teardown(test_exists_after_insert, setup_genesis, teardown),
    cmocka_unit_test_setup_teardown(test_exists_nonexistent,  setup_genesis, teardown),
    cmocka_unit_test_setup_teardown(test_exists_null_hash,    setup_empty,   teardown),
  };

  const struct CMUnitTest head_tests[] = {
    cmocka_unit_test_setup_teardown(test_head_before_checkout, setup_inserted_no_checkout, teardown),
    cmocka_unit_test_setup_teardown(test_head_after_checkout,  setup_genesis,              teardown),
    cmocka_unit_test_setup_teardown(test_head_invalid_buffer,  setup_genesis,              teardown),
  };

  const struct CMUnitTest checkout_tests[] = {
    cmocka_unit_test_setup_teardown(test_checkout_null_hash,         setup_empty,  teardown),
    cmocka_unit_test_setup_teardown(test_checkout_empty_hash,        setup_empty,  teardown),
    cmocka_unit_test_setup_teardown(test_checkout_nonexistent_hash,  setup_empty,  teardown),
    cmocka_unit_test_setup_teardown(test_checkout_valid_hash,        setup_chain,  teardown_chain),
    cmocka_unit_test_setup_teardown(test_checkout_already_at_head,   setup_chain,  teardown_chain),
  };

  const struct CMUnitTest scan_tests[] = {
    cmocka_unit_test_setup_teardown(test_scan_null_count,          setup_empty,   teardown),
    cmocka_unit_test_setup_teardown(test_scan_empty_chain,         setup_empty,   teardown),
    cmocka_unit_test_setup_teardown(test_scan_single_block,        setup_genesis, teardown),
    cmocka_unit_test_setup_teardown(test_scan_multiple_blocks,     setup_chain,   teardown_chain),
    cmocka_unit_test_setup_teardown(test_scan_count_capped,        setup_chain,   teardown_chain),
    cmocka_unit_test_setup_teardown(test_scan_with_offset,         setup_chain,   teardown_chain),
    cmocka_unit_test_setup_teardown(test_scan_offset_past_genesis, setup_chain,   teardown_chain),
  };

  int failures = 0;
  failures += cmocka_run_group_tests_name("insert",   insert_tests,   NULL, NULL);
  failures += cmocka_run_group_tests_name("read",     read_tests,     NULL, NULL);
  failures += cmocka_run_group_tests_name("exists",   exists_tests,   NULL, NULL);
  failures += cmocka_run_group_tests_name("head",     head_tests,     NULL, NULL);
  failures += cmocka_run_group_tests_name("checkout", checkout_tests, NULL, NULL);
  failures += cmocka_run_group_tests_name("scan",     scan_tests,     NULL, NULL);
  return failures;
}
