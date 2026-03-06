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

#include "pow.h"
#include "block.h"
#include "crypto.h"
#include "sha256.h"
#include "log.h"

/* ── setup / teardown ─────────────────────────────────────────────────── */

static int setup(void **state) {
    (void)state;
    return 0;
}

/*
 * Free any heap-allocated Block stored in *state.
 * Tests that call block_create() must assign the result to *state.
 */
static int teardown(void **state) {
    if (*state) {
        block_free(*state);
        *state = NULL;
    }
    return 0;
}

/* ── pow/mine ─────────────────────────────────────────────────────────── */

/* mine_block(NULL, ...) must return EXIT_FAILURE without crashing. */
static void test_mine_null_block(void **state) {
    (void)state;
    assert_int_equal(mine_block(NULL, 1), EXIT_FAILURE);
}

/* difficulty=0 is invalid and must be rejected immediately. */
static void test_mine_difficulty_zero(void **state) {
    (void)state;
    Block *b = block_create(0, NULL);
    assert_non_null(b);
    assert_int_equal(mine_block(b, 0), EXIT_FAILURE);
    *state = b;
}

/* difficulty > SHA256_HEX_LEN (64) is invalid and must be rejected. */
static void test_mine_difficulty_too_large(void **state) {
    (void)state;
    Block *b = block_create(0, NULL);
    assert_non_null(b);
    assert_int_equal(mine_block(b, SHA256_HEX_LEN + 1), EXIT_FAILURE);
    *state = b;
}

/*
 * With difficulty=1 (~16 hashes on average) mine_block must return
 * EXIT_SUCCESS and set block->hash to a non-empty string.
 */
static void test_mine_difficulty_1_succeeds(void **state) {
    Block *b = block_create(0, NULL);
    assert_non_null(b);
    b->timestamp = 1700000000;  /* pin timestamp for reproducibility */
    assert_int_equal(mine_block(b, 1), EXIT_SUCCESS);
    assert_true(b->hash[0] != '\0');
    *state = b;
}

/*
 * After mining with difficulty=1, block->hash must start with at least
 * one hex '0' character.
 */
static void test_mine_hash_leading_zero(void **state) {
    Block *b = block_create(0, NULL);
    assert_non_null(b);
    b->timestamp = 1700000000;
    assert_int_equal(mine_block(b, 1), EXIT_SUCCESS);
    assert_int_equal(b->hash[0], '0');
    *state = b;
}

/*
 * The hash written by mine_block must be exactly SHA256_HEX_LEN (64)
 * characters long — a full SHA-256 hex output.
 */
static void test_mine_hash_length(void **state) {
    Block *b = block_create(0, NULL);
    assert_non_null(b);
    b->timestamp = 1700000000;
    assert_int_equal(mine_block(b, 1), EXIT_SUCCESS);
    assert_int_equal((int)strlen((char *)b->hash), SHA256_HEX_LEN);
    *state = b;
}

/*
 * After mine_block, block_verify_hash must succeed.
 * This confirms that mine_block sets both block->nonce and block->hash
 * consistently — the stored hash must match the block fields.
 */
static void test_mine_hash_integrity(void **state) {
    Block *b = block_create(0, NULL);
    assert_non_null(b);
    b->timestamp = 1700000000;
    assert_int_equal(mine_block(b, 1), EXIT_SUCCESS);
    assert_int_equal(block_verify_hash(b), EXIT_SUCCESS);
    *state = b;
}

/* ── pow/validate ─────────────────────────────────────────────────────── */

/* validate_block_pow(NULL, ...) must return EXIT_FAILURE without crashing. */
static void test_validate_null_block(void **state) {
    (void)state;
    assert_int_equal(validate_block_pow(NULL, 1), EXIT_FAILURE);
}

/*
 * A freshly mined block must pass validate_block_pow at the same difficulty.
 */
static void test_validate_mined_block(void **state) {
    Block *b = block_create(0, NULL);
    assert_non_null(b);
    b->timestamp = 1700000000;
    assert_int_equal(mine_block(b, 1), EXIT_SUCCESS);
    assert_int_equal(validate_block_pow(b, 1), EXIT_SUCCESS);
    *state = b;
}

/*
 * A block mined at difficulty=1 (one leading hex zero) must fail
 * validate_block_pow at difficulty=2 (two leading hex zeros).
 * The probability that a difficulty=1 solution also satisfies difficulty=2
 * is 1/16 — but this is fine: the test may need to re-mine if unlucky.
 * To make it deterministic, we directly manipulate hash[1] instead.
 */
static void test_validate_higher_difficulty_fails(void **state) {
    Block *b = block_create(0, NULL);
    assert_non_null(b);
    b->timestamp = 1700000000;
    assert_int_equal(mine_block(b, 1), EXIT_SUCCESS);

    if (b->hash[1] == '0') {
        /* The mined hash accidentally satisfies difficulty=2 — tweak it so
         * it no longer does, while keeping the test logically sound. */
        b->hash[1] = '1';
    }
    /* Now hash[0]=='0' but hash[1]!='0' — must fail difficulty=2. */
    assert_int_equal(validate_block_pow(b, 2), EXIT_FAILURE);
    *state = b;
}

/*
 * Changing block->nonce after mining must cause validate_block_pow to fail.
 * The stored hash was computed for the original nonce; block_verify_hash
 * will detect the mismatch.
 */
static void test_validate_tampered_nonce(void **state) {
    Block *b = block_create(0, NULL);
    assert_non_null(b);
    b->timestamp = 1700000000;
    assert_int_equal(mine_block(b, 1), EXIT_SUCCESS);
    b->nonce++;  /* tamper: stored hash no longer matches fields */
    assert_int_equal(validate_block_pow(b, 1), EXIT_FAILURE);
    *state = b;
}

/*
 * Corrupting a non-leading byte of block->hash must cause validate_block_pow
 * to fail at the integrity check (block_verify_hash detects the mismatch).
 * We tamper position 4 (past any leading zeros for difficulty ≤ 3) to avoid
 * accidentally also failing the difficulty check in the wrong place.
 */
static void test_validate_tampered_hash(void **state) {
    Block *b = block_create(0, NULL);
    assert_non_null(b);
    b->timestamp = 1700000000;
    assert_int_equal(mine_block(b, 1), EXIT_SUCCESS);
    /* Flip a character well past the leading '0' — integrity check fails. */
    b->hash[4] = (b->hash[4] == '0') ? '1' : '0';
    assert_int_equal(validate_block_pow(b, 1), EXIT_FAILURE);
    *state = b;
}

/*
 * validate_block_pow must not modify block->hash.
 * Const-correctness test: save the hash before validation and compare after.
 */
static void test_validate_does_not_mutate(void **state) {
    Block *b = block_create(0, NULL);
    assert_non_null(b);
    b->timestamp = 1700000000;
    assert_int_equal(mine_block(b, 1), EXIT_SUCCESS);

    unsigned char saved[HASH_SIZE];
    memcpy(saved, b->hash, HASH_SIZE);

    validate_block_pow(b, 1);  /* call regardless of result */

    assert_memory_equal(b->hash, saved, HASH_SIZE);
    *state = b;
}

/* ── main ─────────────────────────────────────────────────────────────── */

int main(void) {
    log_set_stream(stderr);

    const struct CMUnitTest mine_tests[] = {
        cmocka_unit_test_setup_teardown(test_mine_null_block,          setup, teardown),
        cmocka_unit_test_setup_teardown(test_mine_difficulty_zero,     setup, teardown),
        cmocka_unit_test_setup_teardown(test_mine_difficulty_too_large, setup, teardown),
        cmocka_unit_test_setup_teardown(test_mine_difficulty_1_succeeds, setup, teardown),
        cmocka_unit_test_setup_teardown(test_mine_hash_leading_zero,   setup, teardown),
        cmocka_unit_test_setup_teardown(test_mine_hash_length,         setup, teardown),
        cmocka_unit_test_setup_teardown(test_mine_hash_integrity,      setup, teardown),
    };

    const struct CMUnitTest validate_tests[] = {
        cmocka_unit_test_setup_teardown(test_validate_null_block,            setup, teardown),
        cmocka_unit_test_setup_teardown(test_validate_mined_block,           setup, teardown),
        cmocka_unit_test_setup_teardown(test_validate_higher_difficulty_fails, setup, teardown),
        cmocka_unit_test_setup_teardown(test_validate_tampered_nonce,        setup, teardown),
        cmocka_unit_test_setup_teardown(test_validate_tampered_hash,         setup, teardown),
        cmocka_unit_test_setup_teardown(test_validate_does_not_mutate,       setup, teardown),
    };

    int failures = 0;
    failures += cmocka_run_group_tests_name("pow/mine",     mine_tests,     NULL, NULL);
    failures += cmocka_run_group_tests_name("pow/validate", validate_tests, NULL, NULL);
    return failures;
}
