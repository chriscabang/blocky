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
#include "consensus.h"
#include "crypto.h"
#include "log.h"
#include "vrf.h"
#include "validator.h"
#include <oqs/oqs.h>

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

/* ── block/sign and block/verify_sig ─────────────────────────────────── */

typedef struct {
  uint8_t  pk[MAX_PUBLIC_KEY_LENGTH];
  uint8_t *sk;
  size_t   sk_len;
  Block   *block;
  VRFProof proof;
} SignState;

static int setup_sign(void **state)
{
  OQS_SIG *sig = OQS_SIG_new(OQS_SIG_alg_dilithium_3);
  if (!sig) return -1;

  SignState *ss = calloc(1, sizeof *ss);
  if (!ss) { OQS_SIG_free(sig); return -1; }
  ss->sk_len = sig->length_secret_key;
  ss->sk     = malloc(ss->sk_len);
  if (!ss->sk) { free(ss); OQS_SIG_free(sig); return -1; }
  OQS_SIG_keypair(sig, ss->pk, ss->sk);
  OQS_SIG_free(sig);

  /* Build a block with a computed hash (no proposer_id yet). */
  ss->block = block_create(1, NULL);
  if (!ss->block) { free(ss->sk); free(ss); return -1; }
  ss->block->timestamp = 1700000000;
  ss->block->consensus = CONSENSUS_POS;
  block_compute_hash(ss->block);

  /* Build a VRF proof for the block slot. */
  uint8_t prev[SHA256_DIGEST_LEN] = {0};
  uint8_t slot_msg[VRF_OUTPUT_LEN];
  vrf_slot_message(1, prev, slot_msg);
  vrf_prove("alice", slot_msg, ss->sk, ss->sk_len, &ss->proof);

  *state = ss;
  return 0;
}

static int teardown_sign(void **state)
{
  SignState *ss = *state;
  if (ss) {
    block_free(ss->block);
    free(ss->sk);
    free(ss);
    *state = NULL;
  }
  return 0;
}

/* ── block/sign ───────────────────────────────────────────────────────── */

static void test_sign_null_block(void **state)
{
  SignState *ss = *state;
  assert_int_equal(block_sign(NULL, "alice", ss->sk, ss->sk_len, &ss->proof),
                   EXIT_FAILURE);
}

static void test_sign_null_id(void **state)
{
  SignState *ss = *state;
  assert_int_equal(block_sign(ss->block, NULL, ss->sk, ss->sk_len, &ss->proof),
                   EXIT_FAILURE);
}

static void test_sign_empty_id(void **state)
{
  SignState *ss = *state;
  assert_int_equal(block_sign(ss->block, "", ss->sk, ss->sk_len, &ss->proof),
                   EXIT_FAILURE);
}

static void test_sign_null_sk(void **state)
{
  SignState *ss = *state;
  assert_int_equal(block_sign(ss->block, "alice", NULL, 0, &ss->proof),
                   EXIT_FAILURE);
}

static void test_sign_null_proof(void **state)
{
  SignState *ss = *state;
  assert_int_equal(block_sign(ss->block, "alice", ss->sk, ss->sk_len, NULL),
                   EXIT_FAILURE);
}

/* Valid sign: proposer_id set, sig_len > 0, hash integrity preserved. */
static void test_sign_valid(void **state)
{
  SignState *ss = *state;
  assert_int_equal(block_sign(ss->block, "alice", ss->sk, ss->sk_len, &ss->proof),
                   EXIT_SUCCESS);
  assert_string_equal(ss->block->proposer_id, "alice");
  assert_true(ss->block->proposer_sig_len > 0);
  /* Hash must be valid after block_sign recomputes it. */
  assert_int_equal(block_verify_hash(ss->block), EXIT_SUCCESS);
}

/* block_sign must include proposer_id in the hash — different IDs → different hashes. */
static void test_sign_id_changes_hash(void **state)
{
  SignState *ss = *state;

  /* Sign as "alice" */
  assert_int_equal(block_sign(ss->block, "alice", ss->sk, ss->sk_len, &ss->proof),
                   EXIT_SUCCESS);
  unsigned char hash_alice[HASH_SIZE];
  memcpy(hash_alice, ss->block->hash, HASH_SIZE);

  /* Re-sign the block as "bob" — hash must differ. */
  assert_int_equal(block_sign(ss->block, "bob", ss->sk, ss->sk_len, &ss->proof),
                   EXIT_SUCCESS);
  assert_memory_not_equal(hash_alice, ss->block->hash, HASH_SIZE);
}

/* ── block/verify_sig ─────────────────────────────────────────────────── */

static void test_verify_sig_null_block(void **state)
{
  SignState *ss = *state;
  assert_int_equal(block_verify_sig(NULL, ss->pk, MAX_PUBLIC_KEY_LENGTH),
                   EXIT_FAILURE);
}

static void test_verify_sig_unsigned(void **state)
{
  SignState *ss = *state;
  /* Block has not been signed: proposer_sig_len == 0. */
  assert_int_equal(block_verify_sig(ss->block, ss->pk, MAX_PUBLIC_KEY_LENGTH),
                   EXIT_FAILURE);
}

static void test_verify_sig_valid(void **state)
{
  SignState *ss = *state;
  assert_int_equal(block_sign(ss->block, "alice", ss->sk, ss->sk_len, &ss->proof),
                   EXIT_SUCCESS);
  assert_int_equal(block_verify_sig(ss->block, ss->pk, MAX_PUBLIC_KEY_LENGTH),
                   EXIT_SUCCESS);
}

static void test_verify_sig_tampered_hash(void **state)
{
  SignState *ss = *state;
  assert_int_equal(block_sign(ss->block, "alice", ss->sk, ss->sk_len, &ss->proof),
                   EXIT_SUCCESS);
  /* Tamper with nonce — the stored hash is now stale (covers different data). */
  ss->block->nonce++;
  assert_int_equal(block_compute_hash(ss->block), EXIT_SUCCESS);
  /* Verify sig: sig was over old hash, now hash changed → fail. */
  assert_int_equal(block_verify_sig(ss->block, ss->pk, MAX_PUBLIC_KEY_LENGTH),
                   EXIT_FAILURE);
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

  const struct CMUnitTest sign_tests[] = {
    cmocka_unit_test_setup_teardown(test_sign_null_block,      setup_sign, teardown_sign),
    cmocka_unit_test_setup_teardown(test_sign_null_id,         setup_sign, teardown_sign),
    cmocka_unit_test_setup_teardown(test_sign_empty_id,        setup_sign, teardown_sign),
    cmocka_unit_test_setup_teardown(test_sign_null_sk,         setup_sign, teardown_sign),
    cmocka_unit_test_setup_teardown(test_sign_null_proof,      setup_sign, teardown_sign),
    cmocka_unit_test_setup_teardown(test_sign_valid,           setup_sign, teardown_sign),
    cmocka_unit_test_setup_teardown(test_sign_id_changes_hash, setup_sign, teardown_sign),
  };

  const struct CMUnitTest verify_sig_tests[] = {
    cmocka_unit_test_setup_teardown(test_verify_sig_null_block,    setup_sign, teardown_sign),
    cmocka_unit_test_setup_teardown(test_verify_sig_unsigned,      setup_sign, teardown_sign),
    cmocka_unit_test_setup_teardown(test_verify_sig_valid,         setup_sign, teardown_sign),
    cmocka_unit_test_setup_teardown(test_verify_sig_tampered_hash, setup_sign, teardown_sign),
  };

  int failures = 0;
  failures += cmocka_run_group_tests_name("create",           create_tests,     NULL, NULL);
  failures += cmocka_run_group_tests_name("compute",          compute_tests,    NULL, NULL);
  failures += cmocka_run_group_tests_name("verify",           verify_tests,     NULL, NULL);
  failures += cmocka_run_group_tests_name("free",             free_tests,       NULL, NULL);
  failures += cmocka_run_group_tests_name("block/sign",       sign_tests,       NULL, NULL);
  failures += cmocka_run_group_tests_name("block/verify_sig", verify_sig_tests, NULL, NULL);
  return failures;
}
