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
    pos_init(&pos);
    assert_int_equal(pos.validator_count, 0);
}

/* ── pos/stake ────────────────────────────────────────────────────────── */

/* Staking for a new validator ID increases validator_count by one. */
static void test_stake_adds_new_validator(void **state) {
    (void)state;
    PoSSystem pos;
    pos_init(&pos);
    pos_stake(&pos, 1, 1000);
    assert_int_equal(pos.validator_count, 1);
    assert_int_equal((int)pos.validators[0].id, 1);
    assert_int_equal((long long)pos.validators[0].stake, 1000LL);
}

/* Staking twice for the same ID accumulates stake (no duplicate entry). */
static void test_stake_accumulates_same_id(void **state) {
    (void)state;
    PoSSystem pos;
    pos_init(&pos);
    pos_stake(&pos, 5, 300);
    pos_stake(&pos, 5, 200);
    assert_int_equal(pos.validator_count, 1);
    assert_int_equal((long long)pos.validators[0].stake, 500LL);
}

/* Multiple distinct IDs are each tracked as separate validators. */
static void test_stake_multiple_validators(void **state) {
    (void)state;
    PoSSystem pos;
    pos_init(&pos);
    pos_stake(&pos, 10, 100);
    pos_stake(&pos, 20, 200);
    pos_stake(&pos, 30, 300);
    assert_int_equal(pos.validator_count, 3);
}

/* Adding exactly MAX_VALIDATORS validators must all fit. */
static void test_stake_at_capacity(void **state) {
    (void)state;
    PoSSystem pos;
    pos_init(&pos);
    for (int i = 0; i < MAX_VALIDATORS; i++)
        pos_stake(&pos, (uint32_t)i, 100);
    assert_int_equal(pos.validator_count, MAX_VALIDATORS);
}

/*
 * Attempting to add a validator beyond MAX_VALIDATORS must be silently
 * rejected — validator_count must stay at MAX_VALIDATORS.
 */
static void test_stake_beyond_capacity(void **state) {
    (void)state;
    PoSSystem pos;
    pos_init(&pos);
    for (int i = 0; i < MAX_VALIDATORS; i++)
        pos_stake(&pos, (uint32_t)i, 100);
    pos_stake(&pos, MAX_VALIDATORS, 100);  /* one too many */
    assert_int_equal(pos.validator_count, MAX_VALIDATORS);
}

/* ── pos/select ───────────────────────────────────────────────────────── */

/* Selecting from an empty pool must return EXIT_FAILURE without crashing. */
static void test_select_empty_pool_fails(void **state) {
    (void)state;
    PoSSystem pos;
    pos_init(&pos);
    PosEntry v;
    assert_int_equal(pos_select_validator(&pos, &v), EXIT_FAILURE);
}

/*
 * With a single validator, pos_select_validator must always return that
 * validator regardless of the rand() seed — the math guarantees it:
 *   target = rand() % stake  → always in [0, stake-1]
 *   running_total after first validator = stake  → target < stake always true
 */
static void test_select_single_always_chosen(void **state) {
    (void)state;
    PoSSystem pos;
    pos_init(&pos);
    pos_stake(&pos, 42, 1000000);

    /* Call multiple times to confirm determinism regardless of rand state. */
    for (int i = 0; i < 5; i++) {
        PosEntry v;
        assert_int_equal(pos_select_validator(&pos, &v), EXIT_SUCCESS);
        assert_int_equal((int)v.id, 42);
    }
}

/* The returned validator's ID must match one that was registered. */
static void test_select_correct_id(void **state) {
    (void)state;
    PoSSystem pos;
    pos_init(&pos);
    pos_stake(&pos, 7, 500);

    PosEntry v;
    assert_int_equal(pos_select_validator(&pos, &v), EXIT_SUCCESS);
    assert_int_equal((int)v.id, 7);
}

/*
 * With two validators, pos_select_validator must return one of them.
 * We do not constrain which one since rand() may vary;
 * the test simply confirms the returned ID is a registered validator.
 */
static void test_select_from_multiple(void **state) {
    (void)state;
    PoSSystem pos;
    pos_init(&pos);
    pos_stake(&pos, 100, 500);
    pos_stake(&pos, 200, 500);

    PosEntry v;
    assert_int_equal(pos_select_validator(&pos, &v), EXIT_SUCCESS);
    assert_true(v.id == 100 || v.id == 200);
}

/* ── pos/validate ─────────────────────────────────────────────────────── */

/*
 * pos_validate_block returns EXIT_SUCCESS for blocks where index % 10 == 0.
 * This models the every-tenth-block PoS placeholder (ADR-003 pending).
 */
static void test_validate_pos_divisible_index(void **state) {
    Block *b = block_create(0, NULL);
    assert_non_null(b);
    b->index = 10;

    PosEntry v = {.id = 1, .stake = 1000};
    assert_int_equal(pos_validate_block(b, &v), EXIT_SUCCESS);

    *state = b;
}

/* pos_validate_block returns EXIT_FAILURE for blocks where index % 10 != 0. */
static void test_validate_pos_non_divisible_index(void **state) {
    Block *b = block_create(0, NULL);
    assert_non_null(b);
    b->index = 7;

    PosEntry v = {.id = 1, .stake = 1000};
    assert_int_equal(pos_validate_block(b, &v), EXIT_FAILURE);

    *state = b;
}

/* pos_validate_block at index 0 (genesis) must also return EXIT_SUCCESS. */
static void test_validate_pos_genesis_index(void **state) {
    Block *b = block_create(0, NULL);
    assert_non_null(b);
    /* index is already 0 from block_create */

    PosEntry v = {.id = 2, .stake = 500};
    assert_int_equal(pos_validate_block(b, &v), EXIT_SUCCESS);

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
        cmocka_unit_test_setup_teardown(test_select_empty_pool_fails,     setup, teardown),
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
