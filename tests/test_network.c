/* test_network.c — Unit tests for the network module (network.h / network.c).
 *
 * Test groups
 * ───────────
 *   context/server  — net_context_server() argument validation and lifecycle
 *   context/client  — net_context_client() argument validation and lifecycle
 *   serialize       — net_serialize_block() correctness and bounds checking
 *   broadcast       — net_broadcast_block() argument validation
 *
 * Network note
 * ────────────
 * These are unit tests; no live TLS connections are made.  Context-creation
 * tests that require a real certificate use write_test_cert() to generate an
 * ephemeral RSA-2048 self-signed cert in /tmp, then remove it in teardown.
 * Tests that exercise PQC group enforcement are omitted here because they
 * require the OQS OpenSSL provider to be loaded at runtime (see ADR-014).
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

#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "block.h"
#include "log.h"
#include "network.h"

/* ── Test-cert helpers ────────────────────────────────────────────────── */

typedef struct {
    char cert[64];
    char key[64];
} CertState;

/*
 * Generate an ephemeral RSA-2048 self-signed certificate and write it to
 * cert_path / key_path as PEM files.  Returns 0 on success, -1 on failure.
 */
static int write_test_cert(const char *cert_path, const char *key_path)
{
    EVP_PKEY_CTX *pctx  = NULL;
    EVP_PKEY     *pkey  = NULL;
    X509         *x509  = NULL;
    X509_NAME    *name  = NULL;
    FILE         *fp    = NULL;
    int           ok    = 0;

    pctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, NULL);
    if (!pctx) goto out;
    if (EVP_PKEY_keygen_init(pctx) <= 0) goto out;
    if (EVP_PKEY_CTX_set_rsa_keygen_bits(pctx, 2048) <= 0) goto out;
    if (EVP_PKEY_keygen(pctx, &pkey) <= 0) goto out;

    x509 = X509_new();
    if (!x509) goto out;

    ASN1_INTEGER_set(X509_get_serialNumber(x509), 1);
    X509_gmtime_adj(X509_getm_notBefore(x509), 0);
    X509_gmtime_adj(X509_getm_notAfter(x509), 86400L); /* valid 24 hours */

    if (X509_set_pubkey(x509, pkey) != 1) goto out;

    name = X509_get_subject_name(x509);
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
                               (const unsigned char *)"blocky-test",
                               -1, -1, 0);
    if (X509_set_issuer_name(x509, name) != 1) goto out;
    if (!X509_sign(x509, pkey, EVP_sha256())) goto out;

    fp = fopen(cert_path, "w");
    if (!fp) goto out;
    if (PEM_write_X509(fp, x509) != 1) { fclose(fp); fp = NULL; goto out; }
    fclose(fp); fp = NULL;

    fp = fopen(key_path, "w");
    if (!fp) goto out;
    if (PEM_write_PrivateKey(fp, pkey, NULL, NULL, 0, NULL, NULL) != 1) {
        fclose(fp); fp = NULL; goto out;
    }
    fclose(fp); fp = NULL;

    ok = 1;

out:
    if (fp)   fclose(fp);
    X509_free(x509);
    EVP_PKEY_free(pkey);
    EVP_PKEY_CTX_free(pctx);
    return ok ? 0 : -1;
}

static int setup_certs(void **state)
{
    CertState *s = calloc(1, sizeof(*s));
    if (!s) return -1;
    snprintf(s->cert, sizeof(s->cert), "/tmp/test_net_%d.crt", getpid());
    snprintf(s->key,  sizeof(s->key),  "/tmp/test_net_%d.key", getpid());
    if (write_test_cert(s->cert, s->key) != 0) { free(s); return -1; }
    *state = s;
    return 0;
}

static int teardown_certs(void **state)
{
    CertState *s = *state;
    if (s) {
        unlink(s->cert);
        unlink(s->key);
        free(s);
    }
    *state = NULL;
    return 0;
}

static int setup_none(void **state) { (void)state; return 0; }
static int teardown_none(void **state) { (void)state; return 0; }

/* ── context/server ───────────────────────────────────────────────────── */

/* net_context_server(NULL) must return NULL. */
static void test_server_null_config(void **state)
{
    (void)state;
    assert_null(net_context_server(NULL));
}

/* Missing cert_file must be rejected. */
static void test_server_null_cert(void **state)
{
    (void)state;
    NetConfig cfg = {
        .cert_file = NULL,
        .key_file  = "/tmp/dummy.key",
        .port      = 4433,
    };
    assert_null(net_context_server(&cfg));
}

/* Missing key_file must be rejected. */
static void test_server_null_key(void **state)
{
    (void)state;
    NetConfig cfg = {
        .cert_file = "/tmp/dummy.crt",
        .key_file  = NULL,
        .port      = 4433,
    };
    assert_null(net_context_server(&cfg));
}

/* Non-existent cert file must cause context creation to fail. */
static void test_server_missing_cert_file(void **state)
{
    (void)state;
    NetConfig cfg = {
        .cert_file = "/tmp/nonexistent_cert_blocky.pem",
        .key_file  = "/tmp/nonexistent_key_blocky.pem",
        .port      = 4433,
    };
    assert_null(net_context_server(&cfg));
}

/*
 * Valid cert + key with no PQC group: context must be created successfully.
 * pqc_group is NULL so no OQS provider is required.
 */
static void test_server_valid_config(void **state)
{
    CertState *s = *state;
    NetConfig cfg = {
        .cert_file = s->cert,
        .key_file  = s->key,
        .pqc_group = NULL,   /* skip PQC group — no OQS provider needed */
        .port      = 4433,
    };
    NetContext *ctx = net_context_server(&cfg);
    assert_non_null(ctx);
    net_context_free(ctx);
}

/* net_context_free(NULL) must not crash. */
static void test_free_null(void **state)
{
    (void)state;
    net_context_free(NULL); /* must not crash */
}

/* ── context/client ───────────────────────────────────────────────────── */

/* net_context_client(NULL) must return NULL. */
static void test_client_null_config(void **state)
{
    (void)state;
    assert_null(net_context_client(NULL));
}

/*
 * Client with no cert/key and no PQC group must succeed — clients only need
 * a CA bundle (or system store) for peer verification; client certs are
 * optional (mutual TLS only).
 */
static void test_client_no_cert_required(void **state)
{
    (void)state;
    NetConfig cfg = {
        .cert_file = NULL,
        .key_file  = NULL,
        .ca_file   = NULL,   /* use system CA store */
        .pqc_group = NULL,
        .port      = 4433,
    };
    NetContext *ctx = net_context_client(&cfg);
    assert_non_null(ctx);
    net_context_free(ctx);
}

/* Non-existent ca_file must cause context creation to fail. */
static void test_client_missing_ca_file(void **state)
{
    (void)state;
    NetConfig cfg = {
        .cert_file = NULL,
        .key_file  = NULL,
        .ca_file   = "/tmp/nonexistent_ca_blocky.pem",
        .pqc_group = NULL,
        .port      = 4433,
    };
    assert_null(net_context_client(&cfg));
}

/* ── serialize ────────────────────────────────────────────────────────── */

/* NULL block must return -1. */
static void test_serialize_null_block(void **state)
{
    (void)state;
    char buf[NET_BLOCK_BUF_SIZE];
    assert_int_equal(net_serialize_block(NULL, buf, sizeof(buf)), -1);
}

/* NULL buf must return -1. */
static void test_serialize_null_buf(void **state)
{
    (void)state;
    Block *b = block_create(0, NULL);
    assert_non_null(b);
    assert_int_equal(net_serialize_block(b, NULL, NET_BLOCK_BUF_SIZE), -1);
    block_free(b);
}

/* Zero bufsz must return -1. */
static void test_serialize_zero_bufsz(void **state)
{
    (void)state;
    Block *b = block_create(0, NULL);
    assert_non_null(b);
    char buf[1];
    assert_int_equal(net_serialize_block(b, buf, 0), -1);
    block_free(b);
}

/* Buffer that is too small to hold the output must return -1. */
static void test_serialize_buf_too_small(void **state)
{
    (void)state;
    Block *b = block_create(99, NULL);
    assert_non_null(b);
    block_compute_hash(b);
    char buf[10]; /* deliberately too small */
    assert_int_equal(net_serialize_block(b, buf, sizeof(buf)), -1);
    block_free(b);
}

/* Valid block must produce a positive byte count and correct field tags. */
static void test_serialize_basic(void **state)
{
    (void)state;
    Block *b = block_create(42, NULL);
    assert_non_null(b);
    block_compute_hash(b);

    char buf[NET_BLOCK_BUF_SIZE];
    int  n = net_serialize_block(b, buf, sizeof(buf));

    assert_true(n > 0);
    assert_non_null(strstr(buf, "index:42\n"));
    assert_non_null(strstr(buf, "hash:"));
    assert_non_null(strstr(buf, "prev_hash:"));
    assert_non_null(strstr(buf, "tx_count:0\n"));
    assert_non_null(strstr(buf, "consensus:"));

    block_free(b);
}

/* Serialized output must be NUL-terminated. */
static void test_serialize_nul_terminated(void **state)
{
    (void)state;
    Block *b = block_create(1, NULL);
    assert_non_null(b);
    block_compute_hash(b);

    char buf[NET_BLOCK_BUF_SIZE];
    int  n = net_serialize_block(b, buf, sizeof(buf));

    assert_true(n > 0);
    assert_int_equal(buf[n], '\0');

    block_free(b);
}

/* Two different blocks must produce different serialized output. */
static void test_serialize_distinct_blocks(void **state)
{
    (void)state;
    Block *a = block_create(1,  NULL);
    Block *b = block_create(99, NULL);
    assert_non_null(a);
    assert_non_null(b);
    block_compute_hash(a);
    block_compute_hash(b);

    char bufa[NET_BLOCK_BUF_SIZE];
    char bufb[NET_BLOCK_BUF_SIZE];
    assert_true(net_serialize_block(a, bufa, sizeof(bufa)) > 0);
    assert_true(net_serialize_block(b, bufb, sizeof(bufb)) > 0);
    assert_string_not_equal(bufa, bufb);

    block_free(a);
    block_free(b);
}

/* ── deserialize ──────────────────────────────────────────────────────── */

/* NULL buf must return EXIT_FAILURE. */
static void test_deserialize_null_buf(void **state)
{
    (void)state;
    Block b;
    assert_int_equal(net_deserialize_block(NULL, 64, &b), EXIT_FAILURE);
}

/* Zero length must return EXIT_FAILURE. */
static void test_deserialize_zero_len(void **state)
{
    (void)state;
    Block b;
    assert_int_equal(net_deserialize_block("index:0\n", 0, &b), EXIT_FAILURE);
}

/* NULL output block must return EXIT_FAILURE. */
static void test_deserialize_null_out(void **state)
{
    (void)state;
    assert_int_equal(net_deserialize_block("index:0\n", 8, NULL), EXIT_FAILURE);
}

/* Garbage data must fail hash verification and return EXIT_FAILURE. */
static void test_deserialize_garbage(void **state)
{
    (void)state;
    Block b;
    assert_int_equal(
        net_deserialize_block("not:valid\ndata:garbage\n", 23, &b),
        EXIT_FAILURE);
}

/*
 * Serialize a well-formed block then deserialize it back.
 * Key header fields must survive the round-trip and hash must verify.
 */
static void test_deserialize_roundtrip(void **state)
{
    (void)state;
    Block *orig = block_create(7, NULL);
    assert_non_null(orig);
    orig->timestamp = 1700000007;
    orig->nonce     = 42;
    assert_int_equal(block_compute_hash(orig), EXIT_SUCCESS);

    char buf[NET_BLOCK_BUF_SIZE];
    int  n = net_serialize_block(orig, buf, sizeof(buf));
    assert_true(n > 0);

    Block result;
    assert_int_equal(net_deserialize_block(buf, (size_t)n, &result),
                     EXIT_SUCCESS);

    assert_int_equal((int)result.index, (int)orig->index);
    assert_int_equal((long)result.timestamp, (long)orig->timestamp);
    assert_int_equal((int)result.nonce, (int)orig->nonce);
    assert_string_equal((char *)result.hash, (char *)orig->hash);
    assert_string_equal((char *)result.previous_hash,
                        (char *)orig->previous_hash);

    block_free(orig);
}

/* ── broadcast (argument validation only — no live network) ───────────── */

/* NULL context must return -1. */
static void test_broadcast_null_ctx(void **state)
{
    (void)state;
    Block *b = block_create(0, NULL);
    assert_non_null(b);
    assert_int_equal(net_broadcast_block(NULL, b, "127.0.0.1", 4433), -1);
    block_free(b);
}

/* NULL block must return -1. */
static void test_broadcast_null_block(void **state)
{
    CertState *s = *state;
    NetConfig cfg = {
        .cert_file = NULL,
        .key_file  = NULL,
        .pqc_group = NULL,
        .port      = 4433,
    };
    /* Use a client context (no cert needed) just to get a valid NetContext. */
    (void)s;
    NetContext *ctx = net_context_client(&cfg);
    assert_non_null(ctx);
    assert_int_equal(net_broadcast_block(ctx, NULL, "127.0.0.1", 4433), -1);
    net_context_free(ctx);
}

/* NULL peer address must return -1. */
static void test_broadcast_null_addr(void **state)
{
    (void)state;
    NetConfig cfg = {
        .cert_file = NULL,
        .key_file  = NULL,
        .pqc_group = NULL,
        .port      = 4433,
    };
    NetContext *ctx = net_context_client(&cfg);
    assert_non_null(ctx);

    Block *b = block_create(0, NULL);
    assert_non_null(b);
    assert_int_equal(net_broadcast_block(ctx, b, NULL, 4433), -1);

    block_free(b);
    net_context_free(ctx);
}

/* Invalid IP address string must return -1. */
static void test_broadcast_invalid_addr(void **state)
{
    (void)state;
    NetConfig cfg = {
        .cert_file = NULL,
        .key_file  = NULL,
        .pqc_group = NULL,
        .port      = 4433,
    };
    NetContext *ctx = net_context_client(&cfg);
    assert_non_null(ctx);

    Block *b = block_create(0, NULL);
    assert_non_null(b);
    assert_int_equal(net_broadcast_block(ctx, b, "not-an-ip", 4433), -1);

    block_free(b);
    net_context_free(ctx);
}

/* ── main ─────────────────────────────────────────────────────────────── */

int main(void)
{
    log_set_stream(stderr);

    const struct CMUnitTest server_tests[] = {
        cmocka_unit_test_setup_teardown(test_server_null_config,
                                        setup_none,  teardown_none),
        cmocka_unit_test_setup_teardown(test_server_null_cert,
                                        setup_none,  teardown_none),
        cmocka_unit_test_setup_teardown(test_server_null_key,
                                        setup_none,  teardown_none),
        cmocka_unit_test_setup_teardown(test_server_missing_cert_file,
                                        setup_none,  teardown_none),
        cmocka_unit_test_setup_teardown(test_server_valid_config,
                                        setup_certs, teardown_certs),
        cmocka_unit_test_setup_teardown(test_free_null,
                                        setup_none,  teardown_none),
    };

    const struct CMUnitTest client_tests[] = {
        cmocka_unit_test_setup_teardown(test_client_null_config,
                                        setup_none, teardown_none),
        cmocka_unit_test_setup_teardown(test_client_no_cert_required,
                                        setup_none, teardown_none),
        cmocka_unit_test_setup_teardown(test_client_missing_ca_file,
                                        setup_none, teardown_none),
    };

    const struct CMUnitTest serialize_tests[] = {
        cmocka_unit_test_setup_teardown(test_serialize_null_block,
                                        setup_none, teardown_none),
        cmocka_unit_test_setup_teardown(test_serialize_null_buf,
                                        setup_none, teardown_none),
        cmocka_unit_test_setup_teardown(test_serialize_zero_bufsz,
                                        setup_none, teardown_none),
        cmocka_unit_test_setup_teardown(test_serialize_buf_too_small,
                                        setup_none, teardown_none),
        cmocka_unit_test_setup_teardown(test_serialize_basic,
                                        setup_none, teardown_none),
        cmocka_unit_test_setup_teardown(test_serialize_nul_terminated,
                                        setup_none, teardown_none),
        cmocka_unit_test_setup_teardown(test_serialize_distinct_blocks,
                                        setup_none, teardown_none),
    };

    const struct CMUnitTest deserialize_tests[] = {
        cmocka_unit_test_setup_teardown(test_deserialize_null_buf,
                                        setup_none, teardown_none),
        cmocka_unit_test_setup_teardown(test_deserialize_zero_len,
                                        setup_none, teardown_none),
        cmocka_unit_test_setup_teardown(test_deserialize_null_out,
                                        setup_none, teardown_none),
        cmocka_unit_test_setup_teardown(test_deserialize_garbage,
                                        setup_none, teardown_none),
        cmocka_unit_test_setup_teardown(test_deserialize_roundtrip,
                                        setup_none, teardown_none),
    };

    const struct CMUnitTest broadcast_tests[] = {
        cmocka_unit_test_setup_teardown(test_broadcast_null_ctx,
                                        setup_none,  teardown_none),
        cmocka_unit_test_setup_teardown(test_broadcast_null_block,
                                        setup_certs, teardown_certs),
        cmocka_unit_test_setup_teardown(test_broadcast_null_addr,
                                        setup_none,  teardown_none),
        cmocka_unit_test_setup_teardown(test_broadcast_invalid_addr,
                                        setup_none,  teardown_none),
    };

    int failures = 0;
    failures += cmocka_run_group_tests_name("network/context/server",
                                            server_tests,      NULL, NULL);
    failures += cmocka_run_group_tests_name("network/context/client",
                                            client_tests,      NULL, NULL);
    failures += cmocka_run_group_tests_name("network/serialize",
                                            serialize_tests,   NULL, NULL);
    failures += cmocka_run_group_tests_name("network/deserialize",
                                            deserialize_tests, NULL, NULL);
    failures += cmocka_run_group_tests_name("network/broadcast",
                                            broadcast_tests,   NULL, NULL);
    return failures;
}
