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

/*
 * A PoS block with a correctly computed hash must pass verify_consensus.
 * PoS only requires hash integrity (block_verify_hash) — no mining needed.
 */
static void test_pos_valid_hash(void **state) {
    Block *b = block_create(1, NULL);
    assert_non_null(b);
    b->timestamp  = 1700000000;
    b->consensus  = CONSENSUS_POS;

    assert_int_equal(block_compute_hash(b), EXIT_SUCCESS);
    assert_int_equal(verify_consensus(b), EXIT_SUCCESS);

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

/* ── consensus/verify_block_signature ────────────────────────────────── */

/*
 * verify_block_signature is a stub (ADR-003 pending).
 * It must always return EXIT_FAILURE to prevent unsigned blocks from passing.
 */
static void test_sig_stub_always_fails(void **state) {
    Block *b = block_create(0, NULL);
    assert_non_null(b);
    assert_int_equal(verify_block_signature(b), EXIT_FAILURE);
    *state = b;
}

/* ── main ─────────────────────────────────────────────────────────────── */

int main(void) {
    log_set_stream(stderr);

    const struct CMUnitTest verify_tests[] = {
        cmocka_unit_test_setup_teardown(test_verify_null_block,                setup, teardown),
        cmocka_unit_test_setup_teardown(test_pow_valid_mined,                  setup, teardown),
        cmocka_unit_test_setup_teardown(test_pow_hash_not_meeting_difficulty,  setup, teardown),
        cmocka_unit_test_setup_teardown(test_pow_tampered_nonce,               setup, teardown),
        cmocka_unit_test_setup_teardown(test_pos_valid_hash,                   setup, teardown),
        cmocka_unit_test_setup_teardown(test_pos_tampered_hash,                setup, teardown),
        cmocka_unit_test_setup_teardown(test_unknown_consensus_type,           setup, teardown),
    };

    const struct CMUnitTest sig_tests[] = {
        cmocka_unit_test_setup_teardown(test_sig_stub_always_fails, setup, teardown),
    };

    int failures = 0;
    failures += cmocka_run_group_tests_name("consensus/verify_consensus",      verify_tests, NULL, NULL);
    failures += cmocka_run_group_tests_name("consensus/verify_block_signature", sig_tests,   NULL, NULL);
    return failures;
}
