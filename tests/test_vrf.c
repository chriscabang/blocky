/**
 * @file test_vrf.c
 * @brief Unit tests for the VRF leader selection module.
 *
 * Groups:
 *   vrf/slot_message   — determinism, distinct-slot output, NULL guard
 *   vrf/selection_hash — determinism, distinct-ID output, NULL guard
 *   vrf/is_elected     — zero-total guard, guaranteed election, zero-stake
 *   vrf/prove          — NULL guards, valid round-trip
 *   vrf/verify         — NULL guard, round-trip, tampered sig,
 *                        wrong validator, unelected validator
 */

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

#include <oqs/oqs.h>
#include <string.h>
#include <stdlib.h>

#include "vrf.h"
#include "log.h"

/* ── shared test fixtures ─────────────────────────────────────────────── */

/* Fixed test prev_hash (all 0x55 pattern). */
static const uint8_t TEST_PREV[SHA256_DIGEST_LEN] = {
  0x55,0x55,0x55,0x55, 0x55,0x55,0x55,0x55,
  0x55,0x55,0x55,0x55, 0x55,0x55,0x55,0x55,
  0x55,0x55,0x55,0x55, 0x55,0x55,0x55,0x55,
  0x55,0x55,0x55,0x55, 0x55,0x55,0x55,0x55,
};

/* State used by vrf/prove and vrf/verify test groups. */
typedef struct {
  uint8_t  pk[MAX_PUBLIC_KEY_LENGTH];
  uint8_t *sk;          /* malloc'd; sig->length_secret_key bytes */
  size_t   sk_len;
  uint8_t  slot_msg[VRF_OUTPUT_LEN];
} KeyState;

static int setup_keys(void **state)
{
  OQS_SIG *sig = OQS_SIG_new(OQS_SIG_alg_dilithium_3);
  if (!sig) return -1;

  KeyState *ks = calloc(1, sizeof *ks);
  if (!ks) { OQS_SIG_free(sig); return -1; }

  ks->sk_len = sig->length_secret_key;
  ks->sk     = malloc(ks->sk_len);
  if (!ks->sk) { free(ks); OQS_SIG_free(sig); return -1; }

  OQS_STATUS rc = OQS_SIG_keypair(sig, ks->pk, ks->sk);
  OQS_SIG_free(sig);
  if (rc != OQS_SUCCESS) { free(ks->sk); free(ks); return -1; }

  vrf_slot_message(1, TEST_PREV, ks->slot_msg);

  *state = ks;
  return 0;
}

static int teardown_keys(void **state)
{
  KeyState *ks = *state;
  if (ks) {
    free(ks->sk);
    free(ks);
    *state = NULL;
  }
  return 0;
}

static int setup_none(void **state) { (void)state; return 0; }
static int teardown_none(void **state) { (void)state; return 0; }

/* ── vrf/slot_message ─────────────────────────────────────────────────── */

/* Same slot and prev_hash produce the same output. */
static void test_slot_msg_deterministic(void **state)
{
  (void)state;
  uint8_t a[VRF_OUTPUT_LEN], b[VRF_OUTPUT_LEN];
  vrf_slot_message(42, TEST_PREV, a);
  vrf_slot_message(42, TEST_PREV, b);
  assert_memory_equal(a, b, VRF_OUTPUT_LEN);
}

/* Different slot numbers must produce different outputs. */
static void test_slot_msg_different_slots(void **state)
{
  (void)state;
  uint8_t a[VRF_OUTPUT_LEN], b[VRF_OUTPUT_LEN];
  vrf_slot_message(1, TEST_PREV, a);
  vrf_slot_message(2, TEST_PREV, b);
  assert_memory_not_equal(a, b, VRF_OUTPUT_LEN);
}

/* NULL prev_hash must not crash (guard test). */
static void test_slot_msg_null_prev_hash(void **state)
{
  (void)state;
  uint8_t out[VRF_OUTPUT_LEN];
  memset(out, 0xAA, VRF_OUTPUT_LEN);
  vrf_slot_message(1, NULL, out); /* must not crash */
  /* out is unchanged (function is a no-op on NULL input) */
  uint8_t expected[VRF_OUTPUT_LEN];
  memset(expected, 0xAA, VRF_OUTPUT_LEN);
  assert_memory_equal(out, expected, VRF_OUTPUT_LEN);
}

/* ── vrf/selection_hash ───────────────────────────────────────────────── */

/* Same inputs produce the same output. */
static void test_selection_hash_deterministic(void **state)
{
  (void)state;
  uint8_t slot_msg[VRF_OUTPUT_LEN];
  vrf_slot_message(7, TEST_PREV, slot_msg);

  uint8_t a[VRF_OUTPUT_LEN], b[VRF_OUTPUT_LEN];
  vrf_selection_hash("alice", slot_msg, a);
  vrf_selection_hash("alice", slot_msg, b);
  assert_memory_equal(a, b, VRF_OUTPUT_LEN);
}

/* Different validator IDs must produce different hashes for the same slot. */
static void test_selection_hash_different_ids(void **state)
{
  (void)state;
  uint8_t slot_msg[VRF_OUTPUT_LEN];
  vrf_slot_message(7, TEST_PREV, slot_msg);

  uint8_t a[VRF_OUTPUT_LEN], b[VRF_OUTPUT_LEN];
  vrf_selection_hash("alice", slot_msg, a);
  vrf_selection_hash("bob",   slot_msg, b);
  assert_memory_not_equal(a, b, VRF_OUTPUT_LEN);
}

/* NULL validator_id must not crash (guard test). */
static void test_selection_hash_null_id(void **state)
{
  (void)state;
  uint8_t slot_msg[VRF_OUTPUT_LEN];
  vrf_slot_message(1, TEST_PREV, slot_msg);

  uint8_t out[VRF_OUTPUT_LEN];
  memset(out, 0xBB, VRF_OUTPUT_LEN);
  vrf_selection_hash(NULL, slot_msg, out); /* must not crash */
  uint8_t expected[VRF_OUTPUT_LEN];
  memset(expected, 0xBB, VRF_OUTPUT_LEN);
  assert_memory_equal(out, expected, VRF_OUTPUT_LEN);
}

/* ── vrf/is_elected ───────────────────────────────────────────────────── */

/* total_stake == 0 must return 0 (no division by zero). */
static void test_is_elected_zero_total(void **state)
{
  (void)state;
  uint8_t hash[VRF_OUTPUT_LEN] = {0xFF};
  assert_int_equal(vrf_is_elected(hash, 1000, 0), 0);
}

/*
 * validator_stake == total_stake guarantees election:
 *   (h64 % V) is in [0, V-1]  →  always < V  →  always elected.
 */
static void test_is_elected_guaranteed(void **state)
{
  (void)state;
  uint8_t hash[VRF_OUTPUT_LEN] = {0};
  assert_int_equal(vrf_is_elected(hash, 1000, 1000), 1);
}

/* validator_stake == 0 means never elected (nothing is < 0). */
static void test_is_elected_zero_stake(void **state)
{
  (void)state;
  uint8_t hash[VRF_OUTPUT_LEN] = {0};
  assert_int_equal(vrf_is_elected(hash, 0, 1000), 0);
}

/* ── vrf/prove ────────────────────────────────────────────────────────── */

static void test_prove_null_id(void **state)
{
  KeyState *ks = *state;
  VRFProof proof;
  assert_int_equal(vrf_prove(NULL, ks->slot_msg, ks->sk, ks->sk_len, &proof),
                   EXIT_FAILURE);
}

static void test_prove_null_slot_msg(void **state)
{
  KeyState *ks = *state;
  VRFProof proof;
  assert_int_equal(vrf_prove("alice", NULL, ks->sk, ks->sk_len, &proof),
                   EXIT_FAILURE);
}

/* Valid key pair and message must produce a non-zero-length signature. */
static void test_prove_valid(void **state)
{
  KeyState *ks = *state;
  VRFProof proof;
  memset(&proof, 0, sizeof proof);
  assert_int_equal(vrf_prove("alice", ks->slot_msg, ks->sk, ks->sk_len, &proof),
                   EXIT_SUCCESS);
  assert_true(proof.sig_len > 0);
  assert_true(proof.sig_len <= MAX_SIGNATURE_LENGTH);
}

/* ── vrf/verify ───────────────────────────────────────────────────────── */

static void test_verify_null_args(void **state)
{
  KeyState *ks = *state;
  VRFProof proof;
  memset(&proof, 0, sizeof proof);
  /* NULL validator_id */
  assert_int_equal(vrf_verify(NULL, ks->slot_msg, &proof,
                               ks->pk, MAX_PUBLIC_KEY_LENGTH, 1, 1),
                   EXIT_FAILURE);
  /* NULL slot_msg */
  assert_int_equal(vrf_verify("alice", NULL, &proof,
                               ks->pk, MAX_PUBLIC_KEY_LENGTH, 1, 1),
                   EXIT_FAILURE);
  /* NULL proof */
  assert_int_equal(vrf_verify("alice", ks->slot_msg, NULL,
                               ks->pk, MAX_PUBLIC_KEY_LENGTH, 1, 1),
                   EXIT_FAILURE);
  /* NULL public_key */
  assert_int_equal(vrf_verify("alice", ks->slot_msg, &proof,
                               NULL, MAX_PUBLIC_KEY_LENGTH, 1, 1),
                   EXIT_FAILURE);
}

/*
 * Full round-trip: prove → verify.
 * Use total_stake=1, validator_stake=1 to guarantee election
 * regardless of the selection_hash value.
 */
static void test_verify_round_trip(void **state)
{
  KeyState *ks = *state;
  VRFProof proof;
  assert_int_equal(vrf_prove("alice", ks->slot_msg, ks->sk, ks->sk_len, &proof),
                   EXIT_SUCCESS);
  assert_int_equal(vrf_verify("alice", ks->slot_msg, &proof,
                               ks->pk, MAX_PUBLIC_KEY_LENGTH, 1, 1),
                   EXIT_SUCCESS);
}

/* Flipping a byte in the signature must invalidate the proof. */
static void test_verify_tampered_sig(void **state)
{
  KeyState *ks = *state;
  VRFProof proof;
  assert_int_equal(vrf_prove("alice", ks->slot_msg, ks->sk, ks->sk_len, &proof),
                   EXIT_SUCCESS);
  proof.sig[0] ^= 0xFF;
  assert_int_equal(vrf_verify("alice", ks->slot_msg, &proof,
                               ks->pk, MAX_PUBLIC_KEY_LENGTH, 1, 1),
                   EXIT_FAILURE);
}

/*
 * Using a different validator_id during verification must fail at
 * step 2 (selection_hash mismatch), even with a valid signature.
 */
static void test_verify_wrong_validator(void **state)
{
  KeyState *ks = *state;
  VRFProof proof;
  assert_int_equal(vrf_prove("alice", ks->slot_msg, ks->sk, ks->sk_len, &proof),
                   EXIT_SUCCESS);
  assert_int_equal(vrf_verify("bob", ks->slot_msg, &proof,
                               ks->pk, MAX_PUBLIC_KEY_LENGTH, 1, 1),
                   EXIT_FAILURE);
}

/* A valid proof fails if the validator has zero stake (not elected). */
static void test_verify_not_elected(void **state)
{
  KeyState *ks = *state;
  VRFProof proof;
  assert_int_equal(vrf_prove("alice", ks->slot_msg, ks->sk, ks->sk_len, &proof),
                   EXIT_SUCCESS);
  /* stake=0 → never elected */
  assert_int_equal(vrf_verify("alice", ks->slot_msg, &proof,
                               ks->pk, MAX_PUBLIC_KEY_LENGTH, 0, 1),
                   EXIT_FAILURE);
}

/* ── main ─────────────────────────────────────────────────────────────── */

int main(void)
{
  log_set_stream(stderr);

  const struct CMUnitTest slot_msg_tests[] = {
    cmocka_unit_test_setup_teardown(test_slot_msg_deterministic,  setup_none, teardown_none),
    cmocka_unit_test_setup_teardown(test_slot_msg_different_slots, setup_none, teardown_none),
    cmocka_unit_test_setup_teardown(test_slot_msg_null_prev_hash,  setup_none, teardown_none),
  };

  const struct CMUnitTest sel_hash_tests[] = {
    cmocka_unit_test_setup_teardown(test_selection_hash_deterministic, setup_none, teardown_none),
    cmocka_unit_test_setup_teardown(test_selection_hash_different_ids, setup_none, teardown_none),
    cmocka_unit_test_setup_teardown(test_selection_hash_null_id,       setup_none, teardown_none),
  };

  const struct CMUnitTest elected_tests[] = {
    cmocka_unit_test_setup_teardown(test_is_elected_zero_total,  setup_none, teardown_none),
    cmocka_unit_test_setup_teardown(test_is_elected_guaranteed,  setup_none, teardown_none),
    cmocka_unit_test_setup_teardown(test_is_elected_zero_stake,  setup_none, teardown_none),
  };

  const struct CMUnitTest prove_tests[] = {
    cmocka_unit_test_setup_teardown(test_prove_null_id,      setup_keys, teardown_keys),
    cmocka_unit_test_setup_teardown(test_prove_null_slot_msg, setup_keys, teardown_keys),
    cmocka_unit_test_setup_teardown(test_prove_valid,         setup_keys, teardown_keys),
  };

  const struct CMUnitTest verify_tests[] = {
    cmocka_unit_test_setup_teardown(test_verify_null_args,       setup_keys, teardown_keys),
    cmocka_unit_test_setup_teardown(test_verify_round_trip,      setup_keys, teardown_keys),
    cmocka_unit_test_setup_teardown(test_verify_tampered_sig,    setup_keys, teardown_keys),
    cmocka_unit_test_setup_teardown(test_verify_wrong_validator, setup_keys, teardown_keys),
    cmocka_unit_test_setup_teardown(test_verify_not_elected,     setup_keys, teardown_keys),
  };

  int failures = 0;
  failures += cmocka_run_group_tests_name("vrf/slot_message",   slot_msg_tests,  NULL, NULL);
  failures += cmocka_run_group_tests_name("vrf/selection_hash", sel_hash_tests,  NULL, NULL);
  failures += cmocka_run_group_tests_name("vrf/is_elected",     elected_tests,   NULL, NULL);
  failures += cmocka_run_group_tests_name("vrf/prove",          prove_tests,     NULL, NULL);
  failures += cmocka_run_group_tests_name("vrf/verify",         verify_tests,    NULL, NULL);
  return failures;
}
