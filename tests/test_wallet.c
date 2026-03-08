/**
 * @file test_wallet.c
 * @brief Unit tests for the wallet module (Dilithium-3 keypair storage).
 *
 * Key generation is handled by the key_gen utility (build/utils/key_gen).
 * These tests use that utility as setup wherever real key material is needed.
 *
 * Groups:
 *   wallet/load_pk   — null args, not found, valid load
 *   wallet/load_sk   — null args, not found, valid load
 *   wallet/exists    — null, not found, found after keygen
 *   wallet/roundtrip — sign_transaction with loaded sk, verify with loaded pk
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

#include "wallet.h"
#include "transaction.h"
#include "log.h"

/* Path to the key_gen utility (built as part of 'make utils'). */
#define KEY_GEN_BIN "./build/utils/key_gen"

/* Generate a keypair via the key_gen utility; used as test setup. */
static void keygen_helper(const char *id)
{
    char cmd[256];
    snprintf(cmd, sizeof cmd, KEY_GEN_BIN " %s 2>/dev/null", id);
    system(cmd);
}

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

/* ── wallet/load_pk ───────────────────────────────────────────────────── */

static void test_load_pk_not_found(void **state)
{
    (void)state;
    uint8_t pk[MAX_PUBLIC_KEY_LENGTH];
    assert_int_equal(wallet_load_pk("nobody", pk, MAX_PUBLIC_KEY_LENGTH),
                     EXIT_FAILURE);
}

static void test_load_pk_null_args(void **state)
{
    (void)state;
    uint8_t pk[MAX_PUBLIC_KEY_LENGTH];
    assert_int_equal(wallet_load_pk(NULL,    pk,  MAX_PUBLIC_KEY_LENGTH), EXIT_FAILURE);
    assert_int_equal(wallet_load_pk("alice", NULL, MAX_PUBLIC_KEY_LENGTH), EXIT_FAILURE);
}

static void test_load_pk_valid(void **state)
{
    (void)state;
    keygen_helper("alice");
    uint8_t pk[MAX_PUBLIC_KEY_LENGTH];
    memset(pk, 0, sizeof pk);
    assert_int_equal(wallet_load_pk("alice", pk, MAX_PUBLIC_KEY_LENGTH),
                     EXIT_SUCCESS);
    /* Public key must be non-zero (an all-zero key would be pathological). */
    int all_zero = 1;
    for (size_t i = 0; i < MAX_PUBLIC_KEY_LENGTH; i++) {
        if (pk[i] != 0) { all_zero = 0; break; }
    }
    assert_false(all_zero);
}

/* ── wallet/load_sk ───────────────────────────────────────────────────── */

static void test_load_sk_not_found(void **state)
{
    (void)state;
    uint8_t sk[WALLET_SK_LEN];
    assert_int_equal(wallet_load_sk("nobody", sk, WALLET_SK_LEN), EXIT_FAILURE);
}

static void test_load_sk_null_args(void **state)
{
    (void)state;
    uint8_t sk[WALLET_SK_LEN];
    assert_int_equal(wallet_load_sk(NULL,    sk,   WALLET_SK_LEN), EXIT_FAILURE);
    assert_int_equal(wallet_load_sk("alice", NULL, WALLET_SK_LEN), EXIT_FAILURE);
}

static void test_load_sk_valid(void **state)
{
    (void)state;
    keygen_helper("alice");
    uint8_t sk[WALLET_SK_LEN];
    memset(sk, 0, sizeof sk);
    assert_int_equal(wallet_load_sk("alice", sk, WALLET_SK_LEN), EXIT_SUCCESS);
    int all_zero = 1;
    for (size_t i = 0; i < WALLET_SK_LEN; i++) {
        if (sk[i] != 0) { all_zero = 0; break; }
    }
    assert_false(all_zero);
    memset(sk, 0, sizeof sk); /* zero key material */
}

/* ── wallet/exists ────────────────────────────────────────────────────── */

static void test_exists_not_found(void **state)
{
    (void)state;
    assert_int_equal(wallet_exists("nobody"), 0);
}

static void test_exists_null(void **state)
{
    (void)state;
    assert_int_equal(wallet_exists(NULL), 0);
}

static void test_exists_after_keygen(void **state)
{
    (void)state;
    assert_int_equal(wallet_exists("alice"), 0);
    keygen_helper("alice");
    assert_int_equal(wallet_exists("alice"), 1);
}

/* ── wallet/roundtrip ─────────────────────────────────────────────────── */

/*
 * Full round-trip: keygen → load sk → sign_transaction → load pk →
 * verify_transaction.  This confirms that the persisted keypair is
 * internally consistent and usable for the full transaction flow.
 */
static void test_roundtrip_sign_verify(void **state)
{
    (void)state;
    keygen_helper("alice");

    Transaction tx;
    memset(&tx, 0, sizeof tx);
    strncpy(tx.sender,    "alice", sizeof(tx.sender)    - 1);
    strncpy(tx.recipient, "bob",   sizeof(tx.recipient) - 1);
    tx.amount = 42000000ULL; /* 42 tokens */
    tx.nonce  = 1;

    /* Sign with the persisted secret key */
    uint8_t sk[WALLET_SK_LEN];
    assert_int_equal(wallet_load_sk("alice", sk, WALLET_SK_LEN), EXIT_SUCCESS);
    assert_int_equal(sign_transaction(&tx, sk), EXIT_SUCCESS);
    memset(sk, 0, sizeof sk);

    assert_true(tx.signature_length > 0);

    /* Verify with the persisted public key */
    uint8_t pk[MAX_PUBLIC_KEY_LENGTH];
    assert_int_equal(wallet_load_pk("alice", pk, MAX_PUBLIC_KEY_LENGTH), EXIT_SUCCESS);
    assert_int_equal(verify_transaction(&tx, pk), EXIT_SUCCESS);
}

/* Tampered signature must fail verification. */
static void test_roundtrip_tampered_sig(void **state)
{
    (void)state;
    keygen_helper("alice");

    Transaction tx;
    memset(&tx, 0, sizeof tx);
    strncpy(tx.sender,    "alice", sizeof(tx.sender)    - 1);
    strncpy(tx.recipient, "bob",   sizeof(tx.recipient) - 1);
    tx.amount = 1000000ULL;
    tx.nonce  = 1;

    uint8_t sk[WALLET_SK_LEN];
    assert_int_equal(wallet_load_sk("alice", sk, WALLET_SK_LEN), EXIT_SUCCESS);
    assert_int_equal(sign_transaction(&tx, sk), EXIT_SUCCESS);
    memset(sk, 0, sizeof sk);

    tx.signature[0] ^= 0xFF; /* tamper */

    uint8_t pk[MAX_PUBLIC_KEY_LENGTH];
    assert_int_equal(wallet_load_pk("alice", pk, MAX_PUBLIC_KEY_LENGTH), EXIT_SUCCESS);
    assert_int_equal(verify_transaction(&tx, pk), EXIT_FAILURE);
}

/* ── main ─────────────────────────────────────────────────────────────── */

int main(void)
{
    log_set_stream(stderr);

    const struct CMUnitTest load_pk_tests[] = {
        cmocka_unit_test_setup_teardown(test_load_pk_not_found, setup, teardown),
        cmocka_unit_test_setup_teardown(test_load_pk_null_args, setup, teardown),
        cmocka_unit_test_setup_teardown(test_load_pk_valid,     setup, teardown),
    };

    const struct CMUnitTest load_sk_tests[] = {
        cmocka_unit_test_setup_teardown(test_load_sk_not_found, setup, teardown),
        cmocka_unit_test_setup_teardown(test_load_sk_null_args, setup, teardown),
        cmocka_unit_test_setup_teardown(test_load_sk_valid,     setup, teardown),
    };

    const struct CMUnitTest exists_tests[] = {
        cmocka_unit_test_setup_teardown(test_exists_not_found,    setup, teardown),
        cmocka_unit_test_setup_teardown(test_exists_null,         setup, teardown),
        cmocka_unit_test_setup_teardown(test_exists_after_keygen, setup, teardown),
    };

    const struct CMUnitTest roundtrip_tests[] = {
        cmocka_unit_test_setup_teardown(test_roundtrip_sign_verify,  setup, teardown),
        cmocka_unit_test_setup_teardown(test_roundtrip_tampered_sig, setup, teardown),
    };

    int failures = 0;
    failures += cmocka_run_group_tests_name("wallet/load_pk",   load_pk_tests,   NULL, NULL);
    failures += cmocka_run_group_tests_name("wallet/load_sk",   load_sk_tests,   NULL, NULL);
    failures += cmocka_run_group_tests_name("wallet/exists",    exists_tests,    NULL, NULL);
    failures += cmocka_run_group_tests_name("wallet/roundtrip", roundtrip_tests, NULL, NULL);
    return failures;
}
