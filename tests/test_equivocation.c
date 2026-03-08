/**
 * @file test_equivocation.c
 * @brief Unit tests for the equivocation guard module.
 *
 * Groups:
 *   equivocation/check  — null/empty proposer_id, no record, same slot,
 *                         different slot
 *   equivocation/record — null/empty proposer_id, creates record, overwrites,
 *                         multiple proposers independent
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "equivocation.h"
#include "log.h"

/* ── setup / teardown ─────────────────────────────────────────────────── */

static int setup(void **state)
{
    (void)state;
    system("rm -rf .chain");
    system("mkdir -p .chain/slots");
    return 0;
}

static int teardown(void **state)
{
    (void)state;
    system("rm -rf .chain");
    return 0;
}

/* ── equivocation/check ───────────────────────────────────────────────── */

static void test_check_null_proposer(void **state)
{
    (void)state;
    assert_int_equal(equivocation_check(NULL, 0), EXIT_FAILURE);
}

static void test_check_empty_proposer(void **state)
{
    (void)state;
    assert_int_equal(equivocation_check("", 0), EXIT_FAILURE);
}

/* No record on disk yet — check must succeed (allow the proposal). */
static void test_check_no_record(void **state)
{
    (void)state;
    assert_int_equal(equivocation_check("alice", 5), EXIT_SUCCESS);
}

/* Record slot 5, then check slot 5 — must be rejected as equivocation. */
static void test_check_same_slot_rejected(void **state)
{
    (void)state;
    assert_int_equal(equivocation_record("alice", 5), EXIT_SUCCESS);
    assert_int_equal(equivocation_check("alice", 5), EXIT_FAILURE);
}

/* Record slot 5, then check slot 6 — different slot, must be allowed. */
static void test_check_different_slot_allowed(void **state)
{
    (void)state;
    assert_int_equal(equivocation_record("alice", 5), EXIT_SUCCESS);
    assert_int_equal(equivocation_check("alice", 6), EXIT_SUCCESS);
}

/* ── equivocation/record ──────────────────────────────────────────────── */

static void test_record_null_proposer(void **state)
{
    (void)state;
    assert_int_equal(equivocation_record(NULL, 0), EXIT_FAILURE);
}

static void test_record_empty_proposer(void **state)
{
    (void)state;
    assert_int_equal(equivocation_record("", 0), EXIT_FAILURE);
}

/* Recording creates a file that makes the next check for the same slot fail. */
static void test_record_creates_slot_file(void **state)
{
    (void)state;
    assert_int_equal(equivocation_check("alice", 3), EXIT_SUCCESS);
    assert_int_equal(equivocation_record("alice", 3), EXIT_SUCCESS);
    assert_int_equal(equivocation_check("alice", 3), EXIT_FAILURE);
}

/* Overwrite: record slot 3, then record slot 4 — slot 3 should be allowed. */
static void test_record_overwrites_previous(void **state)
{
    (void)state;
    assert_int_equal(equivocation_record("alice", 3), EXIT_SUCCESS);
    assert_int_equal(equivocation_record("alice", 4), EXIT_SUCCESS);
    /* Slot 3 is no longer the recorded slot */
    assert_int_equal(equivocation_check("alice", 3), EXIT_SUCCESS);
    /* Slot 4 is now the recorded slot — must be rejected */
    assert_int_equal(equivocation_check("alice", 4), EXIT_FAILURE);
}

/* Records for different proposers are independent. */
static void test_record_multiple_proposers_independent(void **state)
{
    (void)state;
    assert_int_equal(equivocation_record("alice", 5), EXIT_SUCCESS);
    assert_int_equal(equivocation_record("bob",   5), EXIT_SUCCESS);

    assert_int_equal(equivocation_check("alice", 5), EXIT_FAILURE);
    assert_int_equal(equivocation_check("bob",   5), EXIT_FAILURE);

    /* Each proposer's record is independent */
    assert_int_equal(equivocation_check("alice", 6), EXIT_SUCCESS);
    assert_int_equal(equivocation_check("bob",   6), EXIT_SUCCESS);
}

/* ── main ─────────────────────────────────────────────────────────────── */

int main(void)
{
    log_set_stream(stderr);

    const struct CMUnitTest check_tests[] = {
        cmocka_unit_test_setup_teardown(test_check_null_proposer,      setup, teardown),
        cmocka_unit_test_setup_teardown(test_check_empty_proposer,     setup, teardown),
        cmocka_unit_test_setup_teardown(test_check_no_record,          setup, teardown),
        cmocka_unit_test_setup_teardown(test_check_same_slot_rejected, setup, teardown),
        cmocka_unit_test_setup_teardown(test_check_different_slot_allowed, setup, teardown),
    };

    const struct CMUnitTest record_tests[] = {
        cmocka_unit_test_setup_teardown(test_record_null_proposer,              setup, teardown),
        cmocka_unit_test_setup_teardown(test_record_empty_proposer,             setup, teardown),
        cmocka_unit_test_setup_teardown(test_record_creates_slot_file,          setup, teardown),
        cmocka_unit_test_setup_teardown(test_record_overwrites_previous,        setup, teardown),
        cmocka_unit_test_setup_teardown(test_record_multiple_proposers_independent, setup, teardown),
    };

    int failures = 0;
    failures += cmocka_run_group_tests_name("equivocation/check",  check_tests,  NULL, NULL);
    failures += cmocka_run_group_tests_name("equivocation/record", record_tests, NULL, NULL);
    return failures;
}
