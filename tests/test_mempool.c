/**
 * @file test_mempool.c
 * @brief Unit tests for the mempool module (signed transaction queue).
 *
 * Groups:
 *   mempool/add       — null tx, valid add, count after add
 *   mempool/load_all  — null args, empty pool, single tx, multiple txs
 *   mempool/count     — empty, after adds
 *   mempool/purge     — remove mined txs, non-existent (no crash)
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

#include "mempool.h"
#include "transaction.h"
#include "log.h"

/* ── helpers ──────────────────────────────────────────────────────────── */

static Transaction make_tx(const char *from, const char *to,
                            uint64_t tokens, uint64_t nonce)
{
    Transaction tx;
    memset(&tx, 0, sizeof tx);
    strncpy(tx.sender,    from, sizeof(tx.sender)    - 1);
    strncpy(tx.recipient, to,   sizeof(tx.recipient) - 1);
    tx.amount = tokens * MICRO_PER_TOKEN;
    tx.nonce  = nonce;
    return tx;
}

/* ── setup / teardown ─────────────────────────────────────────────────── */

static int setup(void **state)
{
    (void)state;
    system("rm -rf .chain");
    system("mkdir -p " MEMPOOL_DIR);
    return 0;
}

static int teardown(void **state)
{
    (void)state;
    system("rm -rf .chain");
    return 0;
}

/* ── mempool/add ──────────────────────────────────────────────────────── */

static void test_add_null(void **state)
{
    (void)state;
    assert_int_equal(mempool_add(NULL), EXIT_FAILURE);
}

static void test_add_valid(void **state)
{
    (void)state;
    Transaction tx = make_tx("alice", "bob", 10, 1);
    assert_int_equal(mempool_add(&tx), EXIT_SUCCESS);
}

static void test_add_count_increases(void **state)
{
    (void)state;
    assert_int_equal((int)mempool_count(), 0);
    Transaction tx = make_tx("alice", "bob", 10, 1);
    assert_int_equal(mempool_add(&tx), EXIT_SUCCESS);
    assert_int_equal((int)mempool_count(), 1);
}

/* Two transactions with different nonces must produce different filenames. */
static void test_add_two_distinct_txs(void **state)
{
    (void)state;
    Transaction tx1 = make_tx("alice", "bob", 10, 1);
    Transaction tx2 = make_tx("alice", "bob", 10, 2); /* different nonce */
    assert_int_equal(mempool_add(&tx1), EXIT_SUCCESS);
    assert_int_equal(mempool_add(&tx2), EXIT_SUCCESS);
    assert_int_equal((int)mempool_count(), 2);
}

/* Adding the same transaction twice must be idempotent (same filename → overwrite). */
static void test_add_duplicate_idempotent(void **state)
{
    (void)state;
    Transaction tx = make_tx("alice", "bob", 10, 42);
    assert_int_equal(mempool_add(&tx), EXIT_SUCCESS);
    assert_int_equal(mempool_add(&tx), EXIT_SUCCESS); /* overwrites same file */
    assert_int_equal((int)mempool_count(), 1);
}

/* ── mempool/load_all ─────────────────────────────────────────────────── */

static void test_load_all_null_args(void **state)
{
    (void)state;
    Transaction out[8];
    uint32_t count = 0;
    assert_int_equal(mempool_load_all(NULL, 8, &count), EXIT_FAILURE);
    assert_int_equal(mempool_load_all(out,  8, NULL),   EXIT_FAILURE);
}

static void test_load_all_empty(void **state)
{
    (void)state;
    Transaction out[8];
    uint32_t count = 99;
    assert_int_equal(mempool_load_all(out, 8, &count), EXIT_SUCCESS);
    assert_int_equal((int)count, 0);
}

static void test_load_all_single(void **state)
{
    (void)state;
    Transaction tx = make_tx("alice", "bob", 50, 1);
    assert_int_equal(mempool_add(&tx), EXIT_SUCCESS);

    Transaction out[8];
    uint32_t count = 0;
    assert_int_equal(mempool_load_all(out, 8, &count), EXIT_SUCCESS);
    assert_int_equal((int)count, 1);
    assert_string_equal(out[0].sender,    "alice");
    assert_string_equal(out[0].recipient, "bob");
    assert_int_equal((long long)out[0].amount, 50LL * MICRO_PER_TOKEN);
    assert_int_equal((long long)out[0].nonce, 1LL);
}

static void test_load_all_multiple(void **state)
{
    (void)state;
    Transaction txs[3] = {
        make_tx("alice", "bob",  10, 1),
        make_tx("carol", "dave", 20, 2),
        make_tx("eve",   "frank", 5, 3),
    };
    for (int i = 0; i < 3; i++)
        assert_int_equal(mempool_add(&txs[i]), EXIT_SUCCESS);

    Transaction out[8];
    uint32_t count = 0;
    assert_int_equal(mempool_load_all(out, 8, &count), EXIT_SUCCESS);
    assert_int_equal((int)count, 3);
}

/* max parameter limits how many are loaded. */
static void test_load_all_max_limit(void **state)
{
    (void)state;
    for (int i = 0; i < 5; i++) {
        Transaction tx = make_tx("alice", "bob", 1, (uint64_t)(i + 1));
        assert_int_equal(mempool_add(&tx), EXIT_SUCCESS);
    }

    Transaction out[2];
    uint32_t count = 0;
    assert_int_equal(mempool_load_all(out, 2, &count), EXIT_SUCCESS);
    assert_int_equal((int)count, 2);
}

/* ── mempool/count ────────────────────────────────────────────────────── */

static void test_count_empty(void **state)
{
    (void)state;
    assert_int_equal((int)mempool_count(), 0);
}

static void test_count_after_adds(void **state)
{
    (void)state;
    Transaction tx1 = make_tx("a", "b", 1, 1);
    Transaction tx2 = make_tx("c", "d", 2, 2);
    mempool_add(&tx1);
    assert_int_equal((int)mempool_count(), 1);
    mempool_add(&tx2);
    assert_int_equal((int)mempool_count(), 2);
}

/* ── mempool/purge ────────────────────────────────────────────────────── */

static void test_purge_removes_txs(void **state)
{
    (void)state;
    Transaction tx1 = make_tx("alice", "bob", 10, 1);
    Transaction tx2 = make_tx("carol", "dave", 5, 2);
    assert_int_equal(mempool_add(&tx1), EXIT_SUCCESS);
    assert_int_equal(mempool_add(&tx2), EXIT_SUCCESS);
    assert_int_equal((int)mempool_count(), 2);

    Transaction mined[2] = { tx1, tx2 };
    mempool_purge(mined, 2);
    assert_int_equal((int)mempool_count(), 0);
}

/* Purging a tx that was never added must not crash. */
static void test_purge_nonexistent_no_crash(void **state)
{
    (void)state;
    Transaction tx = make_tx("ghost", "nobody", 1, 9999);
    mempool_purge(&tx, 1); /* must not crash */
}

/* Partial purge: remove one, leave one. */
static void test_purge_partial(void **state)
{
    (void)state;
    Transaction tx1 = make_tx("alice", "bob", 10, 1);
    Transaction tx2 = make_tx("carol", "dave", 5, 2);
    assert_int_equal(mempool_add(&tx1), EXIT_SUCCESS);
    assert_int_equal(mempool_add(&tx2), EXIT_SUCCESS);

    mempool_purge(&tx1, 1); /* remove only tx1 */
    assert_int_equal((int)mempool_count(), 1);
}

/* ── main ─────────────────────────────────────────────────────────────── */

int main(void)
{
    log_set_stream(stderr);

    const struct CMUnitTest add_tests[] = {
        cmocka_unit_test_setup_teardown(test_add_null,               setup, teardown),
        cmocka_unit_test_setup_teardown(test_add_valid,              setup, teardown),
        cmocka_unit_test_setup_teardown(test_add_count_increases,    setup, teardown),
        cmocka_unit_test_setup_teardown(test_add_two_distinct_txs,   setup, teardown),
        cmocka_unit_test_setup_teardown(test_add_duplicate_idempotent, setup, teardown),
    };

    const struct CMUnitTest load_tests[] = {
        cmocka_unit_test_setup_teardown(test_load_all_null_args, setup, teardown),
        cmocka_unit_test_setup_teardown(test_load_all_empty,     setup, teardown),
        cmocka_unit_test_setup_teardown(test_load_all_single,    setup, teardown),
        cmocka_unit_test_setup_teardown(test_load_all_multiple,  setup, teardown),
        cmocka_unit_test_setup_teardown(test_load_all_max_limit, setup, teardown),
    };

    const struct CMUnitTest count_tests[] = {
        cmocka_unit_test_setup_teardown(test_count_empty,      setup, teardown),
        cmocka_unit_test_setup_teardown(test_count_after_adds, setup, teardown),
    };

    const struct CMUnitTest purge_tests[] = {
        cmocka_unit_test_setup_teardown(test_purge_removes_txs,        setup, teardown),
        cmocka_unit_test_setup_teardown(test_purge_nonexistent_no_crash, setup, teardown),
        cmocka_unit_test_setup_teardown(test_purge_partial,            setup, teardown),
    };

    int failures = 0;
    failures += cmocka_run_group_tests_name("mempool/add",      add_tests,   NULL, NULL);
    failures += cmocka_run_group_tests_name("mempool/load_all", load_tests,  NULL, NULL);
    failures += cmocka_run_group_tests_name("mempool/count",    count_tests, NULL, NULL);
    failures += cmocka_run_group_tests_name("mempool/purge",    purge_tests, NULL, NULL);
    return failures;
}
