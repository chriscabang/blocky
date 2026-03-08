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

#include "consensus.h"
#include "block.h"
#include "pow.h"
#include "log.h"
#include "validator.h"
#include "vrf.h"
#include "sha256.h"
#include "transaction.h"
#include <oqs/oqs.h>

/*
 * NOTE: verify_consensus uses DIFFICULTY (defined in pow.h) for PoW checks.
 * PoW tests mine at DIFFICULTY to produce a valid hash; expected to complete
 * in well under 1 second (average ~65 000 hashes for DIFFICULTY=4).
 */

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

static int setup_clean(void **state)
{
  (void)state;
  system("rm -rf .chain");
  return 0;
}

static int teardown_clean(void **state)
{
  if (*state) { block_free(*state); *state = NULL; }
  system("rm -rf .chain");
  return 0;
}

/* ── PoS full pipeline state ──────────────────────────────────────────── */

typedef struct {
  uint8_t  pk[MAX_PUBLIC_KEY_LENGTH];
  uint8_t *sk;
  size_t   sk_len;
  Block   *block;
} PosState;

static int setup_pos(void **state)
{
  system("rm -rf .chain");

  OQS_SIG *sig = OQS_SIG_new(OQS_SIG_alg_dilithium_3);
  if (!sig) return -1;

  PosState *ps = calloc(1, sizeof *ps);
  if (!ps) { OQS_SIG_free(sig); return -1; }
  ps->sk_len = sig->length_secret_key;
  ps->sk     = malloc(ps->sk_len);
  if (!ps->sk) { free(ps); OQS_SIG_free(sig); return -1; }
  OQS_SIG_keypair(sig, ps->pk, ps->sk);
  OQS_SIG_free(sig);

  /* Register the validator with sufficient stake. */
  ValidatorRegistry *reg = validator_registry_load();
  if (!reg) { free(ps->sk); free(ps); return -1; }
  Validator v;
  memset(&v, 0, sizeof v);
  strncpy(v.id, "proposer", VALIDATOR_ID_SIZE - 1);
  memcpy(v.public_key, ps->pk, MAX_PUBLIC_KEY_LENGTH);
  v.stake = VALIDATOR_MIN_STAKE;
  validator_register(reg, &v);
  validator_registry_free(reg);

  /* Build a PoS block and sign it. */
  ps->block = block_create(1, NULL);
  if (!ps->block) { free(ps->sk); free(ps); return -1; }
  ps->block->timestamp = 1700000000;
  ps->block->consensus = CONSENSUS_POS;

  /* Derive slot_msg and produce VRF proof.
   * previous_hash for block 1 is "0" (genesis sentinel) → decode as zeros. */
  uint8_t prev_raw[SHA256_DIGEST_LEN] = {0};
  uint8_t slot_msg[VRF_OUTPUT_LEN];
  vrf_slot_message((uint64_t)ps->block->index, prev_raw, slot_msg);

  VRFProof proof;
  vrf_prove("proposer", slot_msg, ps->sk, ps->sk_len, &proof);

  block_sign(ps->block, "proposer", ps->sk, ps->sk_len, &proof);

  *state = ps;
  return 0;
}

static int teardown_pos(void **state)
{
  PosState *ps = *state;
  if (ps) {
    block_free(ps->block);
    free(ps->sk);
    free(ps);
    *state = NULL;
  }
  system("rm -rf .chain");
  return 0;
}

/* ── consensus/verify_consensus ───────────────────────────────────────── */

/* NULL block must return EXIT_FAILURE without crashing. */
static void test_verify_null_block(void **state) {
    (void)state;
    assert_int_equal(verify_consensus(NULL), EXIT_FAILURE);
}

/*
 * A PoW block mined at DIFFICULTY must pass verify_consensus.
 * Confirms the dispatch table routes to verify_pow_rules correctly.
 */
static void test_pow_valid_mined(void **state) {
    Block *b = block_create(1, NULL);
    assert_non_null(b);
    b->timestamp  = 1700000000;
    b->consensus  = CONSENSUS_POW;

    assert_int_equal(mine_block(b, DIFFICULTY), EXIT_SUCCESS);
    assert_int_equal(verify_consensus(b), EXIT_SUCCESS);

    *state = b;
}

/*
 * A PoW block whose hash does NOT satisfy DIFFICULTY (just block_compute_hash,
 * no mining) must fail verify_consensus.
 * Confirms the difficulty gate in verify_pow_rules is enforced.
 */
static void test_pow_hash_not_meeting_difficulty(void **state) {
    Block *b = block_create(1, NULL);
    assert_non_null(b);
    b->timestamp  = 1700000000;
    b->consensus  = CONSENSUS_POW;

    /* Compute a valid hash but without mining — almost certainly no leading zeros. */
    assert_int_equal(block_compute_hash(b), EXIT_SUCCESS);

    /*
     * There is a negligible chance (1 in 16^DIFFICULTY) that the plain hash
     * accidentally meets the difficulty target.  In that case the test would
     * pass incorrectly; we accept this theoretical risk for simplicity.
     */
    assert_int_equal(verify_consensus(b), EXIT_FAILURE);

    *state = b;
}

/*
 * Tampering with block->nonce after mining must cause verify_consensus to fail
 * for a PoW block (the stored hash no longer matches the fields).
 */
static void test_pow_tampered_nonce(void **state) {
    Block *b = block_create(1, NULL);
    assert_non_null(b);
    b->timestamp  = 1700000000;
    b->consensus  = CONSENSUS_POW;

    assert_int_equal(mine_block(b, DIFFICULTY), EXIT_SUCCESS);
    b->nonce++;  /* tamper: hash is now stale */
    assert_int_equal(verify_consensus(b), EXIT_FAILURE);

    *state = b;
}

/* A PoS block with no proposer_id must fail — PoS requires a proposer. */
static void test_pos_no_proposer_fails(void **state) {
    Block *b = block_create(1, NULL);
    assert_non_null(b);
    b->timestamp  = 1700000000;
    b->consensus  = CONSENSUS_POS;
    assert_int_equal(block_compute_hash(b), EXIT_SUCCESS);
    /* proposer_id is empty ('\0') — must fail */
    assert_int_equal(verify_consensus(b), EXIT_FAILURE);
    *state = b;
}

/*
 * A PoS block with a tampered hash must fail verify_consensus.
 * Confirms that hash integrity is enforced even without a difficulty target.
 */
static void test_pos_tampered_hash(void **state) {
    Block *b = block_create(1, NULL);
    assert_non_null(b);
    b->timestamp  = 1700000000;
    b->consensus  = CONSENSUS_POS;

    assert_int_equal(block_compute_hash(b), EXIT_SUCCESS);
    b->hash[0] ^= 0xFF;  /* corrupt stored hash */
    assert_int_equal(verify_consensus(b), EXIT_FAILURE);

    *state = b;
}

/*
 * An unknown consensus type (not CONSENSUS_POW or CONSENSUS_POS) must
 * return EXIT_FAILURE — the dispatch table has no entry for it.
 */
static void test_unknown_consensus_type(void **state) {
    Block *b = block_create(1, NULL);
    assert_non_null(b);
    b->consensus = 2;  /* undefined type */
    assert_int_equal(verify_consensus(b), EXIT_FAILURE);
    *state = b;
}

/* ── consensus/pos_full ───────────────────────────────────────────────── */

/* A fully signed PoS block with registered validator must pass. */
static void test_pos_valid_signed(void **state)
{
  PosState *ps = *state;
  assert_int_equal(verify_consensus(ps->block), EXIT_SUCCESS);
}

/* Tampering with the block signature must fail signature check. */
static void test_pos_tampered_sig(void **state)
{
  PosState *ps = *state;
  ps->block->proposer_sig[0] ^= 0xFF;
  assert_int_equal(verify_consensus(ps->block), EXIT_FAILURE);
}

/* An unregistered proposer must fail at registry lookup. */
static void test_pos_unregistered_proposer(void **state)
{
  Block *b = block_create(1, NULL);
  assert_non_null(b);
  b->timestamp  = 1700000000;
  b->consensus  = CONSENSUS_POS;
  /* Set a proposer_id, but don't register it in the empty registry. */
  strncpy(b->proposer_id, "ghost", PROPOSER_ID_SIZE - 1);
  block_compute_hash(b);
  assert_int_equal(verify_consensus(b), EXIT_FAILURE);
  *state = b;
}

/* ── consensus/verify_block_signature ────────────────────────────────── */

/* verify_block_signature with no proposer must fail. */
static void test_sig_no_proposer_fails(void **state)
{
  (void)state;
  Block *b = block_create(0, NULL);
  assert_non_null(b);
  /* proposer_id is empty */
  assert_int_equal(verify_block_signature(b), EXIT_FAILURE);
  block_free(b);
}

/* verify_block_signature on a validly signed block must succeed. */
static void test_sig_valid(void **state)
{
  PosState *ps = *state;
  assert_int_equal(verify_block_signature(ps->block), EXIT_SUCCESS);
}

/* ── main ─────────────────────────────────────────────────────────────── */

int main(void) {
    log_set_stream(stderr);

    const struct CMUnitTest verify_tests[] = {
        cmocka_unit_test_setup_teardown(test_verify_null_block,               setup,       teardown),
        cmocka_unit_test_setup_teardown(test_pow_valid_mined,                 setup,       teardown),
        cmocka_unit_test_setup_teardown(test_pow_hash_not_meeting_difficulty, setup,       teardown),
        cmocka_unit_test_setup_teardown(test_pow_tampered_nonce,              setup,       teardown),
        cmocka_unit_test_setup_teardown(test_pos_no_proposer_fails,           setup_clean, teardown_clean),
        cmocka_unit_test_setup_teardown(test_pos_tampered_hash,               setup,       teardown),
        cmocka_unit_test_setup_teardown(test_unknown_consensus_type,          setup,       teardown),
    };

    const struct CMUnitTest pos_full_tests[] = {
        cmocka_unit_test_setup_teardown(test_pos_valid_signed,          setup_pos,   teardown_pos),
        cmocka_unit_test_setup_teardown(test_pos_tampered_sig,          setup_pos,   teardown_pos),
        cmocka_unit_test_setup_teardown(test_pos_unregistered_proposer, setup_clean, teardown_clean),
    };

    const struct CMUnitTest sig_tests[] = {
        cmocka_unit_test_setup_teardown(test_sig_no_proposer_fails, setup,     teardown),
        cmocka_unit_test_setup_teardown(test_sig_valid,             setup_pos, teardown_pos),
    };

    int failures = 0;
    failures += cmocka_run_group_tests_name("consensus/verify_consensus",       verify_tests,   NULL, NULL);
    failures += cmocka_run_group_tests_name("consensus/pos_full",               pos_full_tests, NULL, NULL);
    failures += cmocka_run_group_tests_name("consensus/verify_block_signature", sig_tests,      NULL, NULL);
    return failures;
}
