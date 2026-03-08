/**
 * @file test_key.c
 * @brief Unit tests for the key module (Dilithium-3 key store).
 *
 * Groups:
 *   key/generate  — null/empty id, path-traversal rejection, valid, duplicate,
 *                   two distinct ids
 *   key/load_pk   — null args, not found, valid load
 *   key/load_sk   — null args, not found, valid load
 *   key/exists    — null, empty, not found, found after generate
 *   key/roundtrip — sign_transaction with loaded sk, verify with loaded pk;
 *                   tampered signature rejected
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

#include "key.h"
#include "transaction.h"
#include "log.h"

/* ── setup / teardown ─────────────────────────────────────────────────── */

static int setup(void **state)
{
    (void)state;
    system("rm -rf .chain");
    system("mkdir -p .chain/keys");
    return 0;
}

static int teardown(void **state)
{
    (void)state;
    system("rm -rf .chain");
    return 0;
}

/* ── key/generate ─────────────────────────────────────────────────────── */

static void test_generate_null_id(void **state)
{
    (void)state;
    assert_int_equal(key_generate(NULL), EXIT_FAILURE);
}

static void test_generate_empty_id(void **state)
{
    (void)state;
    assert_int_equal(key_generate(""), EXIT_FAILURE);
}

/* id containing '/' must be rejected to prevent path traversal. */
static void test_generate_path_traversal(void **state)
{
    (void)state;
    assert_int_equal(key_generate("../evil"), EXIT_FAILURE);
    assert_int_equal(key_generate("sub/dir"),  EXIT_FAILURE);
}

static void test_generate_valid(void **state)
{
    (void)state;
    assert_int_equal(key_generate("alice"), EXIT_SUCCESS);
    assert_int_equal(key_exists("alice"), 1);
}

/* Generating the same id twice must fail — no silent overwrite. */
static void test_generate_duplicate(void **state)
{
    (void)state;
    assert_int_equal(key_generate("alice"), EXIT_SUCCESS);
    assert_int_equal(key_generate("alice"), EXIT_FAILURE);
}

/* Two different ids must coexist independently. */
static void test_generate_two_ids(void **state)
{
    (void)state;
    assert_int_equal(key_generate("alice"), EXIT_SUCCESS);
    assert_int_equal(key_generate("bob"),   EXIT_SUCCESS);
    assert_int_equal(key_exists("alice"), 1);
    assert_int_equal(key_exists("bob"),   1);
}

/* ── key/load_pk ──────────────────────────────────────────────────────── */

static void test_load_pk_null_args(void **state)
{
    (void)state;
    uint8_t pk[MAX_PUBLIC_KEY_LENGTH];
    assert_int_equal(key_load_pk(NULL,    pk,  MAX_PUBLIC_KEY_LENGTH), EXIT_FAILURE);
    assert_int_equal(key_load_pk("alice", NULL, MAX_PUBLIC_KEY_LENGTH), EXIT_FAILURE);
}

static void test_load_pk_not_found(void **state)
{
    (void)state;
    uint8_t pk[MAX_PUBLIC_KEY_LENGTH];
    assert_int_equal(key_load_pk("nobody", pk, MAX_PUBLIC_KEY_LENGTH), EXIT_FAILURE);
}

static void test_load_pk_valid(void **state)
{
    (void)state;
    assert_int_equal(key_generate("alice"), EXIT_SUCCESS);
    uint8_t pk[MAX_PUBLIC_KEY_LENGTH];
    memset(pk, 0, sizeof pk);
    assert_int_equal(key_load_pk("alice", pk, MAX_PUBLIC_KEY_LENGTH), EXIT_SUCCESS);
    int all_zero = 1;
    for (size_t i = 0; i < MAX_PUBLIC_KEY_LENGTH; i++) {
        if (pk[i] != 0) { all_zero = 0; break; }
    }
    assert_false(all_zero);
}

/* ── key/load_sk ──────────────────────────────────────────────────────── */

static void test_load_sk_null_args(void **state)
{
    (void)state;
    uint8_t sk[KEY_SK_LEN];
    assert_int_equal(key_load_sk(NULL,    sk,   KEY_SK_LEN), EXIT_FAILURE);
    assert_int_equal(key_load_sk("alice", NULL, KEY_SK_LEN), EXIT_FAILURE);
}

static void test_load_sk_not_found(void **state)
{
    (void)state;
    uint8_t sk[KEY_SK_LEN];
    assert_int_equal(key_load_sk("nobody", sk, KEY_SK_LEN), EXIT_FAILURE);
}

static void test_load_sk_valid(void **state)
{
    (void)state;
    assert_int_equal(key_generate("alice"), EXIT_SUCCESS);
    uint8_t sk[KEY_SK_LEN];
    memset(sk, 0, sizeof sk);
    assert_int_equal(key_load_sk("alice", sk, KEY_SK_LEN), EXIT_SUCCESS);
    int all_zero = 1;
    for (size_t i = 0; i < KEY_SK_LEN; i++) {
        if (sk[i] != 0) { all_zero = 0; break; }
    }
    assert_false(all_zero);
    memset(sk, 0, sizeof sk); /* zero key material */
}

/* ── key/exists ───────────────────────────────────────────────────────── */

static void test_exists_null(void **state)
{
    (void)state;
    assert_int_equal(key_exists(NULL), 0);
}

static void test_exists_empty(void **state)
{
    (void)state;
    assert_int_equal(key_exists(""), 0);
}

static void test_exists_not_found(void **state)
{
    (void)state;
    assert_int_equal(key_exists("nobody"), 0);
}

static void test_exists_after_generate(void **state)
{
    (void)state;
    assert_int_equal(key_exists("alice"), 0);
    assert_int_equal(key_generate("alice"), EXIT_SUCCESS);
    assert_int_equal(key_exists("alice"), 1);
}

/* ── key/roundtrip ────────────────────────────────────────────────────── */

/*
 * Full round-trip: generate → load sk → sign_transaction →
 * load pk → verify_transaction.
 */
static void test_roundtrip_sign_verify(void **state)
{
    (void)state;
    assert_int_equal(key_generate("alice"), EXIT_SUCCESS);

    Transaction tx;
    memset(&tx, 0, sizeof tx);
    strncpy(tx.sender,    "alice", sizeof(tx.sender)    - 1);
    strncpy(tx.recipient, "bob",   sizeof(tx.recipient) - 1);
    tx.amount = 42000000ULL; /* 42 tokens */
    tx.nonce  = 1;

    uint8_t sk[KEY_SK_LEN];
    assert_int_equal(key_load_sk("alice", sk, KEY_SK_LEN), EXIT_SUCCESS);
    assert_int_equal(sign_transaction(&tx, sk), EXIT_SUCCESS);
    memset(sk, 0, sizeof sk);

    assert_true(tx.signature_length > 0);

    uint8_t pk[MAX_PUBLIC_KEY_LENGTH];
    assert_int_equal(key_load_pk("alice", pk, MAX_PUBLIC_KEY_LENGTH), EXIT_SUCCESS);
    assert_int_equal(verify_transaction(&tx, pk), EXIT_SUCCESS);
}

/* Tampered signature must fail verification. */
static void test_roundtrip_tampered_sig(void **state)
{
    (void)state;
    assert_int_equal(key_generate("alice"), EXIT_SUCCESS);

    Transaction tx;
    memset(&tx, 0, sizeof tx);
    strncpy(tx.sender,    "alice", sizeof(tx.sender)    - 1);
    strncpy(tx.recipient, "bob",   sizeof(tx.recipient) - 1);
    tx.amount = 1000000ULL;
    tx.nonce  = 1;

    uint8_t sk[KEY_SK_LEN];
    assert_int_equal(key_load_sk("alice", sk, KEY_SK_LEN), EXIT_SUCCESS);
    assert_int_equal(sign_transaction(&tx, sk), EXIT_SUCCESS);
    memset(sk, 0, sizeof sk);

    tx.signature[0] ^= 0xFF; /* tamper */

    uint8_t pk[MAX_PUBLIC_KEY_LENGTH];
    assert_int_equal(key_load_pk("alice", pk, MAX_PUBLIC_KEY_LENGTH), EXIT_SUCCESS);
    assert_int_equal(verify_transaction(&tx, pk), EXIT_FAILURE);
}

/* ── main ─────────────────────────────────────────────────────────────── */

int main(void)
{
    log_set_stream(stderr);

    const struct CMUnitTest generate_tests[] = {
        cmocka_unit_test_setup_teardown(test_generate_null_id,         setup, teardown),
        cmocka_unit_test_setup_teardown(test_generate_empty_id,        setup, teardown),
        cmocka_unit_test_setup_teardown(test_generate_path_traversal,  setup, teardown),
        cmocka_unit_test_setup_teardown(test_generate_valid,           setup, teardown),
        cmocka_unit_test_setup_teardown(test_generate_duplicate,       setup, teardown),
        cmocka_unit_test_setup_teardown(test_generate_two_ids,         setup, teardown),
    };

    const struct CMUnitTest load_pk_tests[] = {
        cmocka_unit_test_setup_teardown(test_load_pk_null_args,  setup, teardown),
        cmocka_unit_test_setup_teardown(test_load_pk_not_found,  setup, teardown),
        cmocka_unit_test_setup_teardown(test_load_pk_valid,      setup, teardown),
    };

    const struct CMUnitTest load_sk_tests[] = {
        cmocka_unit_test_setup_teardown(test_load_sk_null_args,  setup, teardown),
        cmocka_unit_test_setup_teardown(test_load_sk_not_found,  setup, teardown),
        cmocka_unit_test_setup_teardown(test_load_sk_valid,      setup, teardown),
    };

    const struct CMUnitTest exists_tests[] = {
        cmocka_unit_test_setup_teardown(test_exists_null,          setup, teardown),
        cmocka_unit_test_setup_teardown(test_exists_empty,         setup, teardown),
        cmocka_unit_test_setup_teardown(test_exists_not_found,     setup, teardown),
        cmocka_unit_test_setup_teardown(test_exists_after_generate, setup, teardown),
    };

    const struct CMUnitTest roundtrip_tests[] = {
        cmocka_unit_test_setup_teardown(test_roundtrip_sign_verify,  setup, teardown),
        cmocka_unit_test_setup_teardown(test_roundtrip_tampered_sig, setup, teardown),
    };

    int failures = 0;
    failures += cmocka_run_group_tests_name("key/generate",  generate_tests,  NULL, NULL);
    failures += cmocka_run_group_tests_name("key/load_pk",   load_pk_tests,   NULL, NULL);
    failures += cmocka_run_group_tests_name("key/load_sk",   load_sk_tests,   NULL, NULL);
    failures += cmocka_run_group_tests_name("key/exists",    exists_tests,    NULL, NULL);
    failures += cmocka_run_group_tests_name("key/roundtrip", roundtrip_tests, NULL, NULL);
    return failures;
}
