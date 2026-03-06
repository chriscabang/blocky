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

/*
 * liboqs — used here only to generate Dilithium-3 key pairs for testing.
 * The sign_transaction / verify_transaction implementations use the same
 * algorithm internally; we just need real keys to drive them.
 */
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
#include <oqs/oqs.h>
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include "transaction.h"
#include "log.h"

/* ── key fixtures ─────────────────────────────────────────────────────── */

/*
 * One Dilithium-3 key pair (primary) and a second independent pair (other).
 * Generated once in group_setup_keys, shared across all sign/verify tests.
 *
 * Dilithium-3 sizes:
 *   public key  : MAX_PUBLIC_KEY_LENGTH = 1952 bytes
 *   secret key  : 4000 bytes  (OQS_SIG_dilithium_3_length_secret_key)
 */
#define DILITHIUM3_SECRET_KEY_LEN 4000

static uint8_t g_pub  [MAX_PUBLIC_KEY_LENGTH];      /* primary public key  */
static uint8_t g_priv [DILITHIUM3_SECRET_KEY_LEN];  /* primary secret key  */
static uint8_t g_pub2 [MAX_PUBLIC_KEY_LENGTH];      /* secondary public key  */
static uint8_t g_priv2[DILITHIUM3_SECRET_KEY_LEN];  /* secondary secret key  */

static int group_setup_keys(void **state) {
    (void)state;
    OQS_SIG *sig = OQS_SIG_new(OQS_SIG_alg_dilithium_3);
    if (!sig) return -1;

    int rc = OQS_SIG_keypair(sig, g_pub, g_priv);
    if (rc != OQS_SUCCESS) { OQS_SIG_free(sig); return -1; }

    rc = OQS_SIG_keypair(sig, g_pub2, g_priv2);
    OQS_SIG_free(sig);
    return (rc == OQS_SUCCESS) ? 0 : -1;
}

/* ── setup / teardown ─────────────────────────────────────────────────── */

static int setup(void **state) {
    (void)state;
    return 0;
}

static int teardown(void **state) {
    (void)state;
    return 0;
}

/* ── transaction/struct ───────────────────────────────────────────────── */

/*
 * A zero-initialised Transaction has the expected default state.
 * This guards against accidental non-zero padding or field reordering.
 */
static void test_tx_zero_init(void **state) {
    (void)state;
    Transaction tx = {0};
    assert_int_equal(tx.amount, 0);
    assert_int_equal((int)tx.nonce, 0);
    assert_int_equal((int)tx.signature_length, 0);
}

/*
 * sender and recipient fields must accommodate a full Dilithium-3 public key.
 * MAX_PUBLIC_KEY_LENGTH is calibrated to OQS_SIG_dilithium_3_length_public_key.
 */
static void test_tx_field_sizes(void **state) {
    (void)state;
    Transaction tx;
    assert_int_equal((int)sizeof(tx.sender),    MAX_PUBLIC_KEY_LENGTH);
    assert_int_equal((int)sizeof(tx.recipient), MAX_PUBLIC_KEY_LENGTH);
    assert_int_equal((int)sizeof(tx.signature), MAX_SIGNATURE_LENGTH);
}

/*
 * TX_MESSAGE_LEN must equal the two fixed-width key fields plus the two
 * uint64_t scalar fields — this is the canonical signed message layout.
 */
static void test_tx_message_len(void **state) {
    (void)state;
    assert_int_equal((int)TX_MESSAGE_LEN,
                     MAX_PUBLIC_KEY_LENGTH * 2 + (int)sizeof(uint64_t) * 2);
}

/* MICRO_PER_TOKEN is the fixed-point scale factor for token amounts. */
static void test_tx_micro_per_token(void **state) {
    (void)state;
    assert_int_equal((long long)MICRO_PER_TOKEN, 1000000LL);
}

/* ── transaction/sign ─────────────────────────────────────────────────── */

/* Signing a well-formed transaction with a valid key must succeed. */
static void test_sign_success(void **state) {
    (void)state;
    Transaction tx = {0};
    strncpy(tx.sender,    "alice", MAX_PUBLIC_KEY_LENGTH - 1);
    strncpy(tx.recipient, "bob",   MAX_PUBLIC_KEY_LENGTH - 1);
    tx.amount = 42 * MICRO_PER_TOKEN;
    tx.nonce  = 1;

    assert_int_equal(sign_transaction(&tx, g_priv), EXIT_SUCCESS);
}

/* After a successful sign, signature_length must be > 0. */
static void test_sign_fills_signature_length(void **state) {
    (void)state;
    Transaction tx = {0};
    strncpy(tx.sender,    "alice", MAX_PUBLIC_KEY_LENGTH - 1);
    strncpy(tx.recipient, "bob",   MAX_PUBLIC_KEY_LENGTH - 1);
    tx.amount = 10 * MICRO_PER_TOKEN;
    tx.nonce  = 1;

    assert_int_equal(sign_transaction(&tx, g_priv), EXIT_SUCCESS);
    assert_true(tx.signature_length > 0);
}

/*
 * Dilithium-3 always produces a fixed-size signature.
 * signature_length must equal MAX_SIGNATURE_LENGTH (3293).
 */
static void test_sign_exact_length(void **state) {
    (void)state;
    Transaction tx = {0};
    strncpy(tx.sender,    "alice", MAX_PUBLIC_KEY_LENGTH - 1);
    strncpy(tx.recipient, "bob",   MAX_PUBLIC_KEY_LENGTH - 1);
    tx.amount = 5 * MICRO_PER_TOKEN;
    tx.nonce  = 2;

    assert_int_equal(sign_transaction(&tx, g_priv), EXIT_SUCCESS);
    assert_int_equal((int)tx.signature_length, MAX_SIGNATURE_LENGTH);
}

/* The signature bytes must not all be zero after signing. */
static void test_sign_nonzero_signature(void **state) {
    (void)state;
    Transaction tx = {0};
    strncpy(tx.sender,    "alice", MAX_PUBLIC_KEY_LENGTH - 1);
    strncpy(tx.recipient, "bob",   MAX_PUBLIC_KEY_LENGTH - 1);
    tx.amount = 1 * MICRO_PER_TOKEN;
    tx.nonce  = 1;

    assert_int_equal(sign_transaction(&tx, g_priv), EXIT_SUCCESS);

    int nonzero = 0;
    for (int i = 0; i < MAX_SIGNATURE_LENGTH; i++)
        if (tx.signature[i] != 0) { nonzero = 1; break; }
    assert_true(nonzero);
}

/* ── transaction/verify ───────────────────────────────────────────────── */

/* sign then verify with the matching public key must succeed. */
static void test_verify_after_sign(void **state) {
    (void)state;
    Transaction tx = {0};
    strncpy(tx.sender,    "alice", MAX_PUBLIC_KEY_LENGTH - 1);
    strncpy(tx.recipient, "bob",   MAX_PUBLIC_KEY_LENGTH - 1);
    tx.amount = 100 * MICRO_PER_TOKEN;
    tx.nonce  = 1;

    assert_int_equal(sign_transaction(&tx, g_priv), EXIT_SUCCESS);
    assert_int_equal(verify_transaction(&tx, g_pub), EXIT_SUCCESS);
}

/*
 * Modifying amount after signing must invalidate the signature.
 * Amount is included in the signed message (ADR-009).
 */
static void test_verify_tampered_amount(void **state) {
    (void)state;
    Transaction tx = {0};
    strncpy(tx.sender,    "alice", MAX_PUBLIC_KEY_LENGTH - 1);
    strncpy(tx.recipient, "bob",   MAX_PUBLIC_KEY_LENGTH - 1);
    tx.amount = 100 * MICRO_PER_TOKEN;
    tx.nonce  = 1;

    assert_int_equal(sign_transaction(&tx, g_priv), EXIT_SUCCESS);
    tx.amount = 200 * MICRO_PER_TOKEN;  /* tamper */
    assert_int_equal(verify_transaction(&tx, g_pub), EXIT_FAILURE);
}

/*
 * Modifying sender after signing must invalidate the signature.
 * Sender is included in the signed message using its full fixed-width field.
 */
static void test_verify_tampered_sender(void **state) {
    (void)state;
    Transaction tx = {0};
    strncpy(tx.sender,    "alice", MAX_PUBLIC_KEY_LENGTH - 1);
    strncpy(tx.recipient, "bob",   MAX_PUBLIC_KEY_LENGTH - 1);
    tx.amount = 50 * MICRO_PER_TOKEN;
    tx.nonce  = 1;

    assert_int_equal(sign_transaction(&tx, g_priv), EXIT_SUCCESS);
    tx.sender[0] = 'A';  /* tamper: 'a' → 'A' */
    assert_int_equal(verify_transaction(&tx, g_pub), EXIT_FAILURE);
}

/* Modifying recipient after signing must invalidate the signature. */
static void test_verify_tampered_recipient(void **state) {
    (void)state;
    Transaction tx = {0};
    strncpy(tx.sender,    "alice", MAX_PUBLIC_KEY_LENGTH - 1);
    strncpy(tx.recipient, "bob",   MAX_PUBLIC_KEY_LENGTH - 1);
    tx.amount = 50 * MICRO_PER_TOKEN;
    tx.nonce  = 1;

    assert_int_equal(sign_transaction(&tx, g_priv), EXIT_SUCCESS);
    tx.recipient[0] = 'B';  /* tamper */
    assert_int_equal(verify_transaction(&tx, g_pub), EXIT_FAILURE);
}

/*
 * Modifying the nonce after signing must invalidate the signature.
 * This confirms the nonce is included in the signed message (replay protection).
 */
static void test_verify_tampered_nonce(void **state) {
    (void)state;
    Transaction tx = {0};
    strncpy(tx.sender,    "alice", MAX_PUBLIC_KEY_LENGTH - 1);
    strncpy(tx.recipient, "bob",   MAX_PUBLIC_KEY_LENGTH - 1);
    tx.amount = 50 * MICRO_PER_TOKEN;
    tx.nonce  = 1;

    assert_int_equal(sign_transaction(&tx, g_priv), EXIT_SUCCESS);
    tx.nonce = 2;  /* replay with different sequence number */
    assert_int_equal(verify_transaction(&tx, g_pub), EXIT_FAILURE);
}

/*
 * Verifying with a different (unrelated) public key must fail.
 * Guards against accepting signatures from unknown parties.
 */
static void test_verify_wrong_key(void **state) {
    (void)state;
    Transaction tx = {0};
    strncpy(tx.sender,    "alice", MAX_PUBLIC_KEY_LENGTH - 1);
    strncpy(tx.recipient, "bob",   MAX_PUBLIC_KEY_LENGTH - 1);
    tx.amount = 50 * MICRO_PER_TOKEN;
    tx.nonce  = 1;

    assert_int_equal(sign_transaction(&tx, g_priv), EXIT_SUCCESS);
    /* verify with the second (independent) public key — must fail */
    assert_int_equal(verify_transaction(&tx, g_pub2), EXIT_FAILURE);
}

/*
 * Setting signature_length to 0 before verify must return EXIT_FAILURE.
 * OQS must not accept a zero-length signature as valid.
 */
static void test_verify_zero_sig_length(void **state) {
    (void)state;
    Transaction tx = {0};
    strncpy(tx.sender,    "alice", MAX_PUBLIC_KEY_LENGTH - 1);
    strncpy(tx.recipient, "bob",   MAX_PUBLIC_KEY_LENGTH - 1);
    tx.amount = 50 * MICRO_PER_TOKEN;
    tx.nonce  = 1;

    assert_int_equal(sign_transaction(&tx, g_priv), EXIT_SUCCESS);
    tx.signature_length = 0;  /* corrupt length */
    assert_int_equal(verify_transaction(&tx, g_pub), EXIT_FAILURE);
}

/* ── main ─────────────────────────────────────────────────────────────── */

int main(void) {
    log_set_stream(stderr);

    const struct CMUnitTest struct_tests[] = {
        cmocka_unit_test_setup_teardown(test_tx_zero_init,       setup, teardown),
        cmocka_unit_test_setup_teardown(test_tx_field_sizes,     setup, teardown),
        cmocka_unit_test_setup_teardown(test_tx_message_len,     setup, teardown),
        cmocka_unit_test_setup_teardown(test_tx_micro_per_token, setup, teardown),
    };

    const struct CMUnitTest sign_tests[] = {
        cmocka_unit_test_setup_teardown(test_sign_success,              setup, teardown),
        cmocka_unit_test_setup_teardown(test_sign_fills_signature_length, setup, teardown),
        cmocka_unit_test_setup_teardown(test_sign_exact_length,         setup, teardown),
        cmocka_unit_test_setup_teardown(test_sign_nonzero_signature,    setup, teardown),
    };

    const struct CMUnitTest verify_tests[] = {
        cmocka_unit_test_setup_teardown(test_verify_after_sign,       setup, teardown),
        cmocka_unit_test_setup_teardown(test_verify_tampered_amount,  setup, teardown),
        cmocka_unit_test_setup_teardown(test_verify_tampered_sender,  setup, teardown),
        cmocka_unit_test_setup_teardown(test_verify_tampered_recipient, setup, teardown),
        cmocka_unit_test_setup_teardown(test_verify_tampered_nonce,   setup, teardown),
        cmocka_unit_test_setup_teardown(test_verify_wrong_key,        setup, teardown),
        cmocka_unit_test_setup_teardown(test_verify_zero_sig_length,  setup, teardown),
    };

    int failures = 0;
    failures += cmocka_run_group_tests_name("transaction/struct", struct_tests, NULL,              NULL);
    failures += cmocka_run_group_tests_name("transaction/sign",   sign_tests,   group_setup_keys, NULL);
    failures += cmocka_run_group_tests_name("transaction/verify", verify_tests, group_setup_keys, NULL);
    return failures;
}
