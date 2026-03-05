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

#include "sha256.h"
#include "crypto.h"
#include "block.h"
#include "log.h"

/*
 * OpenSSL SHA-256 is used as a reference oracle to validate our implementation.
 * Deprecation warnings are suppressed — this is test-only usage.
 */
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
#include <openssl/sha.h>
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

/* ── helpers ──────────────────────────────────────────────────────────── */

/* Compute SHA-256 via OpenSSL as the ground-truth reference. */
static void ref_sha256(const void *data, size_t len,
                       uint8_t out[SHA256_DIGEST_LEN]) {
    SHA256((const unsigned char *)data, len, out);
}

/* Assert our sha256_digest matches OpenSSL for the given input. */
static void assert_matches_openssl(const void *data, size_t len) {
    uint8_t ours[SHA256_DIGEST_LEN];
    uint8_t ref[SHA256_DIGEST_LEN];
    sha256_digest(data, len, ours);
    ref_sha256(data, len, ref);
    assert_memory_equal(ours, ref, SHA256_DIGEST_LEN);
}

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

/* ── sha256: NIST test vectors ────────────────────────────────────────── */

/*
 * FIPS 180-4 known answer: SHA-256 of the empty string.
 * This vector is fixed and well-known across all SHA-256 implementations.
 */
static void test_sha256_empty_nist(void **state) {
    (void)state;
    static const uint8_t expected[SHA256_DIGEST_LEN] = {
        0xe3, 0xb0, 0xc4, 0x42, 0x98, 0xfc, 0x1c, 0x14,
        0x9a, 0xfb, 0xf4, 0xc8, 0x99, 0x6f, 0xb9, 0x24,
        0x27, 0xae, 0x41, 0xe4, 0x64, 0x9b, 0x93, 0x4c,
        0xa4, 0x95, 0x99, 0x1b, 0x78, 0x52, 0xb8, 0x55,
    };
    uint8_t digest[SHA256_DIGEST_LEN];
    sha256_digest("", 0, digest);
    assert_memory_equal(digest, expected, SHA256_DIGEST_LEN);
}

/* ── sha256: cross-validation against OpenSSL ────────────────────────── */

/* Short ASCII string */
static void test_sha256_vs_openssl_short(void **state) {
    (void)state;
    assert_matches_openssl("hello", 5);
}

/* Single byte */
static void test_sha256_vs_openssl_single_byte(void **state) {
    (void)state;
    assert_matches_openssl("a", 1);
}

/* Exactly 55 bytes (fits in one block before padding) */
static void test_sha256_vs_openssl_55_bytes(void **state) {
    (void)state;
    static const char data[55] = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    assert_matches_openssl(data, 55);
}

/* Exactly 56 bytes (forces two-block padding path) */
static void test_sha256_vs_openssl_56_bytes(void **state) {
    (void)state;
    static const char data[56] = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    assert_matches_openssl(data, 56);
}

/* Exactly 64 bytes (one full block) */
static void test_sha256_vs_openssl_64_bytes(void **state) {
    (void)state;
    static const char data[64] = {
        'x','x','x','x','x','x','x','x','x','x','x','x','x','x','x','x',
        'x','x','x','x','x','x','x','x','x','x','x','x','x','x','x','x',
        'x','x','x','x','x','x','x','x','x','x','x','x','x','x','x','x',
        'x','x','x','x','x','x','x','x','x','x','x','x','x','x','x','x',
    };
    assert_matches_openssl(data, 64);
}

/* Multi-block: 200 bytes */
static void test_sha256_vs_openssl_multi_block(void **state) {
    (void)state;
    uint8_t data[200];
    for (int i = 0; i < 200; i++) data[i] = (uint8_t)i;
    assert_matches_openssl(data, 200);
}

/* Binary data (all 256 byte values) */
static void test_sha256_vs_openssl_binary(void **state) {
    (void)state;
    uint8_t data[256];
    for (int i = 0; i < 256; i++) data[i] = (uint8_t)i;
    assert_matches_openssl(data, 256);
}

/* ── sha256: incremental vs one-shot ─────────────────────────────────── */

static void test_sha256_incremental_matches_oneshot(void **state) {
    (void)state;
    const char *msg = "The quick brown fox jumps over the lazy dog";
    size_t      len = strlen(msg);

    /* One-shot */
    uint8_t oneshot[SHA256_DIGEST_LEN];
    sha256_digest(msg, len, oneshot);

    /* Byte-by-byte incremental */
    sha256_ctx ctx;
    sha256_init(&ctx);
    for (size_t i = 0; i < len; i++)
        sha256_update(&ctx, msg + i, 1);
    uint8_t incremental[SHA256_DIGEST_LEN];
    sha256_final(&ctx, incremental);

    assert_memory_equal(oneshot, incremental, SHA256_DIGEST_LEN);
}

/* ── sha256: basic properties ────────────────────────────────────────── */

static void test_sha256_deterministic(void **state) {
    (void)state;
    uint8_t a[SHA256_DIGEST_LEN], b[SHA256_DIGEST_LEN];
    sha256_digest("same input", 10, a);
    sha256_digest("same input", 10, b);
    assert_memory_equal(a, b, SHA256_DIGEST_LEN);
}

static void test_sha256_different_inputs_differ(void **state) {
    (void)state;
    uint8_t a[SHA256_DIGEST_LEN], b[SHA256_DIGEST_LEN];
    sha256_digest("input A", 7, a);
    sha256_digest("input B", 7, b);
    assert_memory_not_equal(a, b, SHA256_DIGEST_LEN);
}

static void test_sha256_output_length(void **state) {
    (void)state;
    uint8_t digest[SHA256_DIGEST_LEN];
    sha256_digest("test", 4, digest);
    /* Verify all 32 bytes are set (spot-check: at least one is non-zero) */
    int nonzero = 0;
    for (int i = 0; i < SHA256_DIGEST_LEN; i++)
        if (digest[i] != 0) { nonzero = 1; break; }
    assert_true(nonzero);
}

/* ── block hash: regression and correctness ──────────────────────────── */

/*
 * Regression: old code did memcpy(block->hash, hex, SHA256_DIGEST_LENGTH=32),
 * truncating the output to 32 hex chars instead of 64.
 * This test documents and enforces the correct full-length output.
 */
static void test_block_hash_full_64_chars(void **state) {
    Block *b = block_create(0, NULL);
    assert_non_null(b);
    b->timestamp = 1700000000;
    assert_int_equal(block_compute_hash(b), EXIT_SUCCESS);
    assert_int_equal((int)strlen((char *)b->hash), SHA256_HEX_LEN);
    *state = b;
}

/* Changing the nonce must change the hash. */
static void test_block_hash_changes_on_nonce(void **state) {
    Block *a = block_create(0, NULL);
    Block *b = block_create(0, NULL);
    assert_non_null(a);
    assert_non_null(b);
    a->timestamp = b->timestamp = 1700000000;
    a->nonce = 0;
    b->nonce = 1;
    block_compute_hash(a);
    block_compute_hash(b);
    assert_string_not_equal((char *)a->hash, (char *)b->hash);
    block_free(a);
    *state = b;
}

/*
 * Regression: the old code omitted the `consensus` field from the hash input.
 * A block with consensus=0 and consensus=1 must now produce different hashes.
 */
static void test_block_hash_includes_consensus(void **state) {
    Block *a = block_create(0, NULL);
    Block *b = block_create(0, NULL);
    assert_non_null(a);
    assert_non_null(b);
    a->timestamp = b->timestamp = 1700000000;
    a->consensus = 0;
    b->consensus = 1;
    block_compute_hash(a);
    block_compute_hash(b);
    assert_string_not_equal((char *)a->hash, (char *)b->hash);
    block_free(a);
    *state = b;
}

/* hash() on NULL block must return FAILURE without crashing. */
static void test_block_hash_null_block(void **state) {
    (void)state;
    assert_int_equal(hash(NULL), EXIT_FAILURE);
}

/* ── compute_merkle_root ─────────────────────────────────────────────── */

static void test_merkle_empty_transactions(void **state) {
    (void)state;
    Block b = {0};
    char root[HASH_SIZE];
    compute_merkle_root(&b, root);
    assert_string_equal(root, "0");
}

static void test_merkle_null_args(void **state) {
    (void)state;
    char root[HASH_SIZE];
    compute_merkle_root(NULL, root); /* must not crash */
    Block b = {0};
    compute_merkle_root(&b, NULL);  /* must not crash */
}

static void test_merkle_with_transactions(void **state) {
    (void)state;
    Block b = {0};
    b.transaction_count = 1;
    strncpy(b.transactions[0].sender,    "alice", 5);
    strncpy(b.transactions[0].recipient, "bob",   3);
    b.transactions[0].amount = 42.0;

    char root[HASH_SIZE];
    compute_merkle_root(&b, root);

    /* Root must be a 64-char hex string (full SHA-256) */
    assert_int_equal((int)strlen(root), SHA256_HEX_LEN);
    /* Must not be the empty-tx sentinel */
    assert_string_not_equal(root, "0");
}

static void test_merkle_different_amounts_differ(void **state) {
    (void)state;
    Block a = {0}, b = {0};
    a.transaction_count = b.transaction_count = 1;
    strncpy(a.transactions[0].sender,    "alice", 5);
    strncpy(a.transactions[0].recipient, "bob",   3);
    strncpy(b.transactions[0].sender,    "alice", 5);
    strncpy(b.transactions[0].recipient, "bob",   3);
    a.transactions[0].amount = 10.0;
    b.transactions[0].amount = 20.0;

    char root_a[HASH_SIZE], root_b[HASH_SIZE];
    compute_merkle_root(&a, root_a);
    compute_merkle_root(&b, root_b);

    /* Old code only hashed sender — these would have been equal. */
    assert_string_not_equal(root_a, root_b);
}

/* ── main ─────────────────────────────────────────────────────────────── */

int main(void) {
    log_set_stream(stderr);

    const struct CMUnitTest nist_tests[] = {
        cmocka_unit_test_setup_teardown(test_sha256_empty_nist, setup, teardown),
    };

    const struct CMUnitTest openssl_tests[] = {
        cmocka_unit_test_setup_teardown(test_sha256_vs_openssl_short,       setup, teardown),
        cmocka_unit_test_setup_teardown(test_sha256_vs_openssl_single_byte, setup, teardown),
        cmocka_unit_test_setup_teardown(test_sha256_vs_openssl_55_bytes,    setup, teardown),
        cmocka_unit_test_setup_teardown(test_sha256_vs_openssl_56_bytes,    setup, teardown),
        cmocka_unit_test_setup_teardown(test_sha256_vs_openssl_64_bytes,    setup, teardown),
        cmocka_unit_test_setup_teardown(test_sha256_vs_openssl_multi_block, setup, teardown),
        cmocka_unit_test_setup_teardown(test_sha256_vs_openssl_binary,      setup, teardown),
    };

    const struct CMUnitTest incremental_tests[] = {
        cmocka_unit_test_setup_teardown(test_sha256_incremental_matches_oneshot, setup, teardown),
        cmocka_unit_test_setup_teardown(test_sha256_deterministic,               setup, teardown),
        cmocka_unit_test_setup_teardown(test_sha256_different_inputs_differ,     setup, teardown),
        cmocka_unit_test_setup_teardown(test_sha256_output_length,               setup, teardown),
    };

    const struct CMUnitTest block_tests[] = {
        cmocka_unit_test_setup_teardown(test_block_hash_full_64_chars,      setup, teardown),
        cmocka_unit_test_setup_teardown(test_block_hash_changes_on_nonce,   setup, teardown),
        cmocka_unit_test_setup_teardown(test_block_hash_includes_consensus, setup, teardown),
        cmocka_unit_test_setup_teardown(test_block_hash_null_block,         setup, teardown),
    };

    const struct CMUnitTest merkle_tests[] = {
        cmocka_unit_test_setup_teardown(test_merkle_empty_transactions,     setup, teardown),
        cmocka_unit_test_setup_teardown(test_merkle_null_args,              setup, teardown),
        cmocka_unit_test_setup_teardown(test_merkle_with_transactions,      setup, teardown),
        cmocka_unit_test_setup_teardown(test_merkle_different_amounts_differ, setup, teardown),
    };

    int failures = 0;
    failures += cmocka_run_group_tests_name("sha256/nist",        nist_tests,        NULL, NULL);
    failures += cmocka_run_group_tests_name("sha256/vs_openssl",  openssl_tests,     NULL, NULL);
    failures += cmocka_run_group_tests_name("sha256/incremental", incremental_tests, NULL, NULL);
    failures += cmocka_run_group_tests_name("block_hash",         block_tests,       NULL, NULL);
    failures += cmocka_run_group_tests_name("merkle",             merkle_tests,      NULL, NULL);
    return failures;
}
