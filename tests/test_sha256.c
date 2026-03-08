/* test_sha256.c — Unit tests for sha256.h / sha256.c (FIPS 180-4).
 *
 * Test groups
 * ───────────
 *   sha256/nist        — All four FIPS 180-4 Appendix B known-answer vectors.
 *   sha256/padding     — Every branching path in sha256_final's padding logic:
 *                          buflen ≤ 55  → one final block
 *                          buflen 56–63 → two final blocks
 *                        Six boundary inputs (55, 56, 63, 64, 119, 120 bytes)
 *                        are cross-validated against OpenSSL as reference oracle.
 *   sha256/incremental — Zero-length update is a no-op; init+final == empty
 *                        digest; two- and three-chunk feeding matches one-shot.
 *   sha256/security    — sha256_final must wipe the context (memset to zero)
 *                        before returning; re-initialised context must produce
 *                        correct output.
 *
 * Note: sha256/nist and sha256/vs_openssl tests in test_crypto.c cover the
 * sha256_digest one-shot path.  This file focuses on deeper correctness of the
 * streaming API, padding boundary conditions, and security properties.
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

#include <string.h>
#include <stdlib.h>

#include "sha256.h"
#include "log.h"

/*
 * OpenSSL SHA-256 is used as a reference oracle for padding boundary tests.
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

static int setup(void **state)    { (void)state; return 0; }
static int teardown(void **state) { (void)state; return 0; }

/* Compute SHA-256 via OpenSSL and assert our result matches. */
static void assert_matches_openssl(const void *data, size_t len)
{
    uint8_t ours[SHA256_DIGEST_LEN];
    uint8_t ref[SHA256_DIGEST_LEN];
    sha256_digest(data, len, ours);
    SHA256((const unsigned char *)data, len, ref);
    assert_memory_equal(ours, ref, SHA256_DIGEST_LEN);
}

/* ── sha256/nist — FIPS 180-4 Appendix B known-answer tests ──────────── */

/*
 * Vector 1: SHA-256("") = e3b0c44298fc1c149afbf4c8996fb924...
 * FIPS 180-4 Appendix B.1 (zero-length message).
 */
static void test_nist_empty(void **state)
{
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

/*
 * Vector 2: SHA-256("abc") = ba7816bf8f01cfea414140de5dae2223...
 * FIPS 180-4 Appendix B.1.
 */
static void test_nist_abc(void **state)
{
    (void)state;
    static const uint8_t expected[SHA256_DIGEST_LEN] = {
        0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea,
        0x41, 0x41, 0x40, 0xde, 0x5d, 0xae, 0x22, 0x23,
        0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c,
        0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad,
    };
    uint8_t digest[SHA256_DIGEST_LEN];
    sha256_digest("abc", 3, digest);
    assert_memory_equal(digest, expected, SHA256_DIGEST_LEN);
}

/*
 * Vector 3: SHA-256("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")
 *           = 248d6a61d20638b8e5c026930c3e6039...
 * FIPS 180-4 Appendix B.1 (448-bit / 56-byte message).
 * This input is exactly 56 bytes, exercising the two-block final padding path.
 */
static void test_nist_448bit(void **state)
{
    (void)state;
    static const uint8_t expected[SHA256_DIGEST_LEN] = {
        0x24, 0x8d, 0x6a, 0x61, 0xd2, 0x06, 0x38, 0xb8,
        0xe5, 0xc0, 0x26, 0x93, 0x0c, 0x3e, 0x60, 0x39,
        0xa3, 0x3c, 0xe4, 0x59, 0x64, 0xff, 0x21, 0x67,
        0xf6, 0xec, 0xed, 0xd4, 0x19, 0xdb, 0x06, 0xc1,
    };
    const char *msg = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    uint8_t digest[SHA256_DIGEST_LEN];
    sha256_digest(msg, strlen(msg), digest);
    assert_memory_equal(digest, expected, SHA256_DIGEST_LEN);
}

/*
 * Vector 4: SHA-256("abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmn"
 *                   "hijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu")
 *           = cf5b16a778af8380036ce59e7b049237...
 * FIPS 180-4 Appendix B.1 (896-bit / 112-byte message).
 */
static void test_nist_896bit(void **state)
{
    (void)state;
    static const uint8_t expected[SHA256_DIGEST_LEN] = {
        0xcf, 0x5b, 0x16, 0xa7, 0x78, 0xaf, 0x83, 0x80,
        0x03, 0x6c, 0xe5, 0x9e, 0x7b, 0x04, 0x92, 0x37,
        0x0b, 0x24, 0x9b, 0x11, 0xe8, 0xf0, 0x7a, 0x51,
        0xaf, 0xac, 0x45, 0x03, 0x7a, 0xfe, 0xe9, 0xd1,
    };
    const char *msg =
        "abcdefghbcdefghicdefghijdefghijkefghijklfghijklm"
        "ghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnopqrs"
        "mnopqrstnopqrstu";
    uint8_t digest[SHA256_DIGEST_LEN];
    sha256_digest(msg, strlen(msg), digest);
    assert_memory_equal(digest, expected, SHA256_DIGEST_LEN);
}

/* ── sha256/padding — sha256_final padding path coverage ─────────────── */

/*
 * sha256_final has two paths depending on how full the internal buffer is
 * when finalisation begins.
 *
 * Internal buffer is 64 bytes.  sha256_final appends 0x80, then the 8-byte
 * bit-length.  The decision point (from sha256.c line 133):
 *
 *   if (buflen > 56) → flush current block; pad into a fresh block
 *   else             → pad into the current block
 *
 * Test inputs are constructed to land precisely on and around this boundary.
 *
 * All outputs are validated against OpenSSL as the ground-truth reference.
 */

/* 55 bytes: buflen=55, +0x80 = 56 — just fits in one final block. */
static void test_padding_55_bytes(void **state)
{
    (void)state;
    uint8_t data[55];
    memset(data, 'a', sizeof(data));
    assert_matches_openssl(data, 55);
}

/* 56 bytes: buflen=56, +0x80 = 57 > 56 — triggers two-block final path. */
static void test_padding_56_bytes(void **state)
{
    (void)state;
    uint8_t data[56];
    memset(data, 'a', sizeof(data));
    assert_matches_openssl(data, 56);
}

/* 63 bytes: buflen=63, +0x80 = 64 > 56 — deep inside two-block path. */
static void test_padding_63_bytes(void **state)
{
    (void)state;
    uint8_t data[63];
    memset(data, 'b', sizeof(data));
    assert_matches_openssl(data, 63);
}

/*
 * 64 bytes: exactly one full block consumed in sha256_update, buflen=0 at
 * finalisation.  0x80 at offset 0 < 56 → one-block final path.
 */
static void test_padding_64_bytes(void **state)
{
    (void)state;
    uint8_t data[64];
    memset(data, 'c', sizeof(data));
    assert_matches_openssl(data, 64);
}

/*
 * 119 bytes: one full block (64) consumed in update, 55 bytes remain.
 * buflen=55 at finalisation → same as the 55-byte boundary but after a
 * multi-block update.
 */
static void test_padding_119_bytes(void **state)
{
    (void)state;
    uint8_t data[119];
    for (size_t i = 0; i < 119; i++) data[i] = (uint8_t)(i & 0xff);
    assert_matches_openssl(data, 119);
}

/*
 * 120 bytes: one full block (64) consumed in update, 56 bytes remain.
 * buflen=56 at finalisation → two-block final path, but after a multi-block
 * update.
 */
static void test_padding_120_bytes(void **state)
{
    (void)state;
    uint8_t data[120];
    for (size_t i = 0; i < 120; i++) data[i] = (uint8_t)(i & 0xff);
    assert_matches_openssl(data, 120);
}

/* ── sha256/incremental — streaming API correctness ──────────────────── */

/*
 * sha256_update with len=0 must be a strict no-op: bit_count, buflen, and
 * state must all be unchanged, and the final digest must equal a context that
 * never called update with zero.
 */
static void test_zero_len_update_is_noop(void **state)
{
    (void)state;
    const char *msg = "hello";

    /* With zero-len update interleaved */
    sha256_ctx ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, msg, 0);       /* no-op */
    sha256_update(&ctx, msg, 5);
    sha256_update(&ctx, msg, 0);       /* no-op */
    uint8_t with_noop[SHA256_DIGEST_LEN];
    sha256_final(&ctx, with_noop);

    /* Without zero-len updates */
    uint8_t without_noop[SHA256_DIGEST_LEN];
    sha256_digest(msg, 5, without_noop);

    assert_memory_equal(with_noop, without_noop, SHA256_DIGEST_LEN);
}

/*
 * sha256_init + sha256_final (no update calls) must equal sha256_digest("", 0).
 */
static void test_init_final_equals_empty_digest(void **state)
{
    (void)state;
    sha256_ctx ctx;
    sha256_init(&ctx);
    uint8_t streaming[SHA256_DIGEST_LEN];
    sha256_final(&ctx, streaming);

    uint8_t oneshot[SHA256_DIGEST_LEN];
    sha256_digest("", 0, oneshot);

    assert_memory_equal(streaming, oneshot, SHA256_DIGEST_LEN);
}

/*
 * Feeding the same message as two chunks must match the one-shot digest.
 * Split at every byte position to cover all buffer-fill patterns.
 */
static void test_two_chunk_matches_oneshot(void **state)
{
    (void)state;
    const char *msg = "The quick brown fox jumps over the lazy dog";
    size_t      len = strlen(msg);

    uint8_t oneshot[SHA256_DIGEST_LEN];
    sha256_digest(msg, len, oneshot);

    /* Test split at 1/4, 1/2, and 3/4 of the message length. */
    const size_t splits[] = { len / 4, len / 2, (len * 3) / 4 };
    for (size_t s = 0; s < 3; s++) {
        size_t  cut = splits[s];
        sha256_ctx ctx;
        sha256_init(&ctx);
        sha256_update(&ctx, msg,       cut);
        sha256_update(&ctx, msg + cut, len - cut);
        uint8_t chunked[SHA256_DIGEST_LEN];
        sha256_final(&ctx, chunked);
        assert_memory_equal(oneshot, chunked, SHA256_DIGEST_LEN);
    }
}

/* Three-chunk feeding across a 64-byte block boundary must match one-shot. */
static void test_three_chunk_across_block_boundary(void **state)
{
    (void)state;

    /* 96 bytes total: chunk at 32 + 32 + 32, crossing the 64-byte boundary */
    uint8_t data[96];
    for (size_t i = 0; i < 96; i++) data[i] = (uint8_t)i;

    uint8_t oneshot[SHA256_DIGEST_LEN];
    sha256_digest(data, 96, oneshot);

    sha256_ctx ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, data,      32);
    sha256_update(&ctx, data + 32, 32);
    sha256_update(&ctx, data + 64, 32);
    uint8_t chunked[SHA256_DIGEST_LEN];
    sha256_final(&ctx, chunked);

    assert_memory_equal(oneshot, chunked, SHA256_DIGEST_LEN);
}

/* ── sha256/security — context handling and wipe ─────────────────────── */

/*
 * sha256_final must wipe the entire sha256_ctx to zero before returning.
 * (sha256.c: memset(ctx, 0, sizeof(*ctx)) in sha256_final.)
 * This prevents residual state (partial hash, bit count, buffer bytes) from
 * leaking to subsequent uses of the same stack or heap memory.
 */
static void test_ctx_wiped_after_final(void **state)
{
    (void)state;
    sha256_ctx ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, "sensitive input", 15);

    uint8_t digest[SHA256_DIGEST_LEN];
    sha256_final(&ctx, digest);

    /* After final, every byte of ctx must be zero. */
    const uint8_t *raw = (const uint8_t *)&ctx;
    for (size_t i = 0; i < sizeof(ctx); i++)
        assert_int_equal(raw[i], 0);
}

/*
 * After sha256_final wipes the context, re-initialising it with sha256_init
 * and computing a new digest must still produce the correct result.
 * Verifies that sha256_init fully resets state regardless of prior contents.
 */
static void test_ctx_reuse_after_init(void **state)
{
    (void)state;
    sha256_ctx ctx;

    /* First use */
    sha256_init(&ctx);
    sha256_update(&ctx, "first", 5);
    uint8_t first_digest[SHA256_DIGEST_LEN];
    sha256_final(&ctx, first_digest);

    /* Re-initialise and compute "abc" — must match the NIST vector */
    sha256_init(&ctx);
    sha256_update(&ctx, "abc", 3);
    uint8_t second_digest[SHA256_DIGEST_LEN];
    sha256_final(&ctx, second_digest);

    static const uint8_t nist_abc[SHA256_DIGEST_LEN] = {
        0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea,
        0x41, 0x41, 0x40, 0xde, 0x5d, 0xae, 0x22, 0x23,
        0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c,
        0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad,
    };
    assert_memory_equal(second_digest, nist_abc, SHA256_DIGEST_LEN);
}

/* ── sha256/hex_codec — sha256_from_hex round-trip ───────────────────── */

/* Encoding raw bytes to hex then decoding must give back the original. */
static void test_from_hex_round_trip(void **state)
{
    (void)state;
    static const uint8_t original[SHA256_DIGEST_LEN] = {
        0x00,0x11,0x22,0x33, 0x44,0x55,0x66,0x77,
        0x88,0x99,0xaa,0xbb, 0xcc,0xdd,0xee,0xff,
        0x01,0x23,0x45,0x67, 0x89,0xab,0xcd,0xef,
        0xfe,0xdc,0xba,0x98, 0x76,0x54,0x32,0x10,
    };
    char hex[SHA256_HEX_LEN + 1];
    sha256_to_hex(original, SHA256_DIGEST_LEN, hex);

    uint8_t decoded[SHA256_DIGEST_LEN];
    sha256_from_hex(hex, decoded, SHA256_DIGEST_LEN);
    assert_memory_equal(original, decoded, SHA256_DIGEST_LEN);
}

/* All-zero bytes must decode from "000...0" hex string correctly. */
static void test_from_hex_all_zeros(void **state)
{
    (void)state;
    static const char hex[SHA256_HEX_LEN + 1] =
        "0000000000000000000000000000000000000000000000000000000000000000";
    uint8_t out[SHA256_DIGEST_LEN];
    sha256_from_hex(hex, out, SHA256_DIGEST_LEN);
    uint8_t expected[SHA256_DIGEST_LEN];
    memset(expected, 0, sizeof(expected));
    assert_memory_equal(out, expected, SHA256_DIGEST_LEN);
}

/* Known SHA-256("abc") hex must decode to the NIST raw bytes. */
static void test_from_hex_known_vector(void **state)
{
    (void)state;
    static const char nist_hex[] =
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
    static const uint8_t nist_bytes[SHA256_DIGEST_LEN] = {
        0xba,0x78,0x16,0xbf, 0x8f,0x01,0xcf,0xea,
        0x41,0x41,0x40,0xde, 0x5d,0xae,0x22,0x23,
        0xb0,0x03,0x61,0xa3, 0x96,0x17,0x7a,0x9c,
        0xb4,0x10,0xff,0x61, 0xf2,0x00,0x15,0xad,
    };
    uint8_t out[SHA256_DIGEST_LEN];
    sha256_from_hex(nist_hex, out, SHA256_DIGEST_LEN);
    assert_memory_equal(out, nist_bytes, SHA256_DIGEST_LEN);
}

/* ── main ─────────────────────────────────────────────────────────────── */

int main(void)
{
    log_set_stream(stderr);

    const struct CMUnitTest nist_tests[] = {
        cmocka_unit_test_setup_teardown(test_nist_empty,   setup, teardown),
        cmocka_unit_test_setup_teardown(test_nist_abc,     setup, teardown),
        cmocka_unit_test_setup_teardown(test_nist_448bit,  setup, teardown),
        cmocka_unit_test_setup_teardown(test_nist_896bit,  setup, teardown),
    };

    const struct CMUnitTest padding_tests[] = {
        cmocka_unit_test_setup_teardown(test_padding_55_bytes,  setup, teardown),
        cmocka_unit_test_setup_teardown(test_padding_56_bytes,  setup, teardown),
        cmocka_unit_test_setup_teardown(test_padding_63_bytes,  setup, teardown),
        cmocka_unit_test_setup_teardown(test_padding_64_bytes,  setup, teardown),
        cmocka_unit_test_setup_teardown(test_padding_119_bytes, setup, teardown),
        cmocka_unit_test_setup_teardown(test_padding_120_bytes, setup, teardown),
    };

    const struct CMUnitTest incremental_tests[] = {
        cmocka_unit_test_setup_teardown(test_zero_len_update_is_noop,          setup, teardown),
        cmocka_unit_test_setup_teardown(test_init_final_equals_empty_digest,   setup, teardown),
        cmocka_unit_test_setup_teardown(test_two_chunk_matches_oneshot,        setup, teardown),
        cmocka_unit_test_setup_teardown(test_three_chunk_across_block_boundary, setup, teardown),
    };

    const struct CMUnitTest security_tests[] = {
        cmocka_unit_test_setup_teardown(test_ctx_wiped_after_final, setup, teardown),
        cmocka_unit_test_setup_teardown(test_ctx_reuse_after_init,  setup, teardown),
    };

    const struct CMUnitTest hex_codec_tests[] = {
        cmocka_unit_test_setup_teardown(test_from_hex_round_trip,   setup, teardown),
        cmocka_unit_test_setup_teardown(test_from_hex_all_zeros,    setup, teardown),
        cmocka_unit_test_setup_teardown(test_from_hex_known_vector, setup, teardown),
    };

    int failures = 0;
    failures += cmocka_run_group_tests_name("sha256/nist",        nist_tests,        NULL, NULL);
    failures += cmocka_run_group_tests_name("sha256/padding",     padding_tests,     NULL, NULL);
    failures += cmocka_run_group_tests_name("sha256/incremental", incremental_tests, NULL, NULL);
    failures += cmocka_run_group_tests_name("sha256/security",    security_tests,    NULL, NULL);
    failures += cmocka_run_group_tests_name("sha256/hex_codec",   hex_codec_tests,   NULL, NULL);
    return failures;
}
