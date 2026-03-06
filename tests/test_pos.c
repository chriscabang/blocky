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

#include "pos.h"
#include "block.h"
#include "log.h"

/*
 * NOTE: validate_block_pos returns 1 on success and 0 on failure — opposite
 * of the EXIT_SUCCESS / EXIT_FAILURE convention used elsewhere in the codebase.
 * Tests assert the actual return values (1/0) to document current behaviour.
 *
 * NOTE: select_validator calls exit(1) when validator_count == 0.
 * That case cannot be tested with CMocka (it terminates the process).
 * All select tests ensure at least one validator is present before calling.
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

/* ── pos/init ─────────────────────────────────────────────────────────── */

/* A freshly initialised PoSSystem has zero registered validators. */
static void test_init_zeros_count(void **state) {
    (void)state;
    PoSSystem pos;
    init_pos(&pos);
    assert_int_equal(pos.validator_count, 0);
}

/* ── pos/stake ────────────────────────────────────────────────────────── */

/* Staking for a new validator ID increases validator_count by one. */
static void test_stake_adds_new_validator(void **state) {
    (void)state;
    PoSSystem pos;
    init_pos(&pos);
    stake_coins(&pos, 1, 1000);
    assert_int_equal(pos.validator_count, 1);
    assert_int_equal(pos.validators[0].id, 1);
    assert_int_equal((long long)pos.validators[0].stake, 1000LL);
}

/* Staking twice for the same ID accumulates stake (no duplicate entry). */
static void test_stake_accumulates_same_id(void **state) {
    (void)state;
    PoSSystem pos;
    init_pos(&pos);
    stake_coins(&pos, 5, 300);
    stake_coins(&pos, 5, 200);
    assert_int_equal(pos.validator_count, 1);
    assert_int_equal((long long)pos.validators[0].stake, 500LL);
}

/* Multiple distinct IDs are each tracked as separate validators. */
static void test_stake_multiple_validators(void **state) {
    (void)state;
    PoSSystem pos;
    init_pos(&pos);
    stake_coins(&pos, 10, 100);
    stake_coins(&pos, 20, 200);
    stake_coins(&pos, 30, 300);
    assert_int_equal(pos.validator_count, 3);
}

/* Adding exactly MAX_VALIDATORS validators must all fit. */
static void test_stake_at_capacity(void **state) {
    (void)state;
    PoSSystem pos;
    init_pos(&pos);
    for (int i = 0; i < MAX_VALIDATORS; i++)
        stake_coins(&pos, i, 100);
    assert_int_equal(pos.validator_count, MAX_VALIDATORS);
}

/*
 * Attempting to add a validator beyond MAX_VALIDATORS must be silently
 * rejected — validator_count must stay at MAX_VALIDATORS.
 */
static void test_stake_beyond_capacity(void **state) {
    (void)state;
    PoSSystem pos;
    init_pos(&pos);
    for (int i = 0; i < MAX_VALIDATORS; i++)
        stake_coins(&pos, i, 100);
    stake_coins(&pos, MAX_VALIDATORS, 100);  /* one too many */
    assert_int_equal(pos.validator_count, MAX_VALIDATORS);
}

/* ── pos/select ───────────────────────────────────────────────────────── */

/*
 * With a single validator, select_validator must always return that validator
 * regardless of the rand() seed — the math guarantees it:
 *   rand_val = rand() % stake  → always in [0, stake-1]
 *   running_total after first validator = stake  → rand_val < stake always true
 */
static void test_select_single_always_chosen(void **state) {
    (void)state;
    PoSSystem pos;
    init_pos(&pos);
    stake_coins(&pos, 42, 1000000);

    /* Call multiple times to confirm determinism regardless of rand state. */
    for (int i = 0; i < 5; i++) {
        Validator v = select_validator(&pos);
        assert_int_equal(v.id, 42);
    }
}

/* The returned validator's ID must match one that was registered. */
static void test_select_correct_id(void **state) {
    (void)state;
    PoSSystem pos;
    init_pos(&pos);
    stake_coins(&pos, 7, 500);

    Validator v = select_validator(&pos);
    assert_int_equal(v.id, 7);
}

/*
 * With two validators, select_validator must return one of them.
 * We do not constrain which one since rand() is non-deterministically seeded;
 * the test simply confirms the returned ID is a registered validator.
 */
static void test_select_from_multiple(void **state) {
    (void)state;
    PoSSystem pos;
    init_pos(&pos);
    stake_coins(&pos, 100, 500);
    stake_coins(&pos, 200, 500);

    Validator v = select_validator(&pos);
    assert_true(v.id == 100 || v.id == 200);
}

/* ── pos/validate ─────────────────────────────────────────────────────── */

/*
 * validate_block_pos returns 1 for blocks where index % 10 == 0.
 * This models every-tenth-block PoS validation.
 *
 * Known issue: the return value of 1 (not EXIT_SUCCESS=0) is inconsistent
 * with the rest of the codebase; this test documents the current behaviour.
 */
static void test_validate_pos_divisible_index(void **state) {
    Block *b = block_create(0, NULL);
    assert_non_null(b);
    b->index = 10;                        /* divisible by 10 → PoS validates */

    Validator v = {.id = 1, .stake = 1000};
    assert_int_equal(validate_block_pos(b, v), 1);

    *state = b;
}

/*
 * validate_block_pos returns 0 for blocks where index % 10 != 0.
 */
static void test_validate_pos_non_divisible_index(void **state) {
    Block *b = block_create(0, NULL);
    assert_non_null(b);
    b->index = 7;                         /* not divisible by 10 → 0 */

    Validator v = {.id = 1, .stake = 1000};
    assert_int_equal(validate_block_pos(b, v), 0);

    *state = b;
}

/* validate_block_pos at index 0 (genesis) must also return 1. */
static void test_validate_pos_genesis_index(void **state) {
    Block *b = block_create(0, NULL);
    assert_non_null(b);
    /* index is already 0 from block_create */

    Validator v = {.id = 2, .stake = 500};
    assert_int_equal(validate_block_pos(b, v), 1);

    *state = b;
}

/* ── main ─────────────────────────────────────────────────────────────── */

int main(void) {
    log_set_stream(stderr);

    const struct CMUnitTest init_tests[] = {
        cmocka_unit_test_setup_teardown(test_init_zeros_count, setup, teardown),
    };

    const struct CMUnitTest stake_tests[] = {
        cmocka_unit_test_setup_teardown(test_stake_adds_new_validator,  setup, teardown),
        cmocka_unit_test_setup_teardown(test_stake_accumulates_same_id, setup, teardown),
        cmocka_unit_test_setup_teardown(test_stake_multiple_validators, setup, teardown),
        cmocka_unit_test_setup_teardown(test_stake_at_capacity,         setup, teardown),
        cmocka_unit_test_setup_teardown(test_stake_beyond_capacity,     setup, teardown),
    };

    const struct CMUnitTest select_tests[] = {
        cmocka_unit_test_setup_teardown(test_select_single_always_chosen, setup, teardown),
        cmocka_unit_test_setup_teardown(test_select_correct_id,           setup, teardown),
        cmocka_unit_test_setup_teardown(test_select_from_multiple,        setup, teardown),
    };

    const struct CMUnitTest validate_tests[] = {
        cmocka_unit_test_setup_teardown(test_validate_pos_divisible_index,     setup, teardown),
        cmocka_unit_test_setup_teardown(test_validate_pos_non_divisible_index, setup, teardown),
        cmocka_unit_test_setup_teardown(test_validate_pos_genesis_index,       setup, teardown),
    };

    int failures = 0;
    failures += cmocka_run_group_tests_name("pos/init",     init_tests,     NULL, NULL);
    failures += cmocka_run_group_tests_name("pos/stake",    stake_tests,    NULL, NULL);
    failures += cmocka_run_group_tests_name("pos/select",   select_tests,   NULL, NULL);
    failures += cmocka_run_group_tests_name("pos/validate", validate_tests, NULL, NULL);
    return failures;
}
