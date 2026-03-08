/* wallet.c — Dilithium-3 keypair generation and loading for user identities. */

#include "wallet.h"
#include "log.h"

#include <oqs/oqs.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef _WIN32
#  include <direct.h>
#  define mkdir(path, mode) _mkdir(path)
#else
#  include <sys/stat.h>
#endif

#define PATH_BUF 512

#if defined(OQS_SIG_dilithium_3_length_secret_key)
_Static_assert(WALLET_SK_LEN >= OQS_SIG_dilithium_3_length_secret_key,
               "WALLET_SK_LEN too small for Dilithium-3 secret key");
#endif

/* ── internal helpers ─────────────────────────────────────────────────── */

static int ensure_dir(void)
{
    if (mkdir(".chain", 0755) == -1 && errno != EEXIST) {
        log_error("wallet: cannot create .chain: %s", strerror(errno));
        return EXIT_FAILURE;
    }
    if (mkdir(WALLET_DIR, 0755) == -1 && errno != EEXIST) {
        log_error("wallet: cannot create %s: %s", WALLET_DIR, strerror(errno));
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

static int write_binary_file(const char *path,
                              const uint8_t *data, size_t len,
                              int restrict_perms)
{
    FILE *f = fopen(path, "wb");
    if (!f) {
        log_error("wallet: cannot open %s for write: %s", path, strerror(errno));
        return EXIT_FAILURE;
    }
    if (restrict_perms)
        fchmod(fileno(f), 0600); /* secret key: owner read/write only */

    size_t n = fwrite(data, 1, len, f);
    fflush(f);
    fsync(fileno(f));
    fclose(f);

    if (n != len) {
        log_error("wallet: short write to %s (%zu/%zu bytes)", path, n, len);
        remove(path);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

/* ── public API ───────────────────────────────────────────────────────── */

int wallet_keygen(const char *id)
{
    if (!id || id[0] == '\0') {
        log_error("wallet_keygen: NULL or empty id");
        return EXIT_FAILURE;
    }

    if (ensure_dir() != EXIT_SUCCESS)
        return EXIT_FAILURE;

    char pk_path[PATH_BUF], sk_path[PATH_BUF];
    snprintf(pk_path, sizeof(pk_path), "%s/%s.pk", WALLET_DIR, id);
    snprintf(sk_path, sizeof(sk_path), "%s/%s.sk", WALLET_DIR, id);

    /* Refuse to overwrite an existing key — explicit is safer. */
    if (access(pk_path, F_OK) == 0) {
        log_warn("wallet_keygen: key '%s' already exists (use a different id)",
                 id);
        return EXIT_FAILURE;
    }

    OQS_SIG *sig = OQS_SIG_new(OQS_SIG_alg_dilithium_3);
    if (!sig) {
        log_error("wallet_keygen: OQS_SIG_new failed");
        return EXIT_FAILURE;
    }

    uint8_t  pk[MAX_PUBLIC_KEY_LENGTH];
    uint8_t *sk = malloc(sig->length_secret_key);
    if (!sk) {
        log_error("wallet_keygen: malloc failed for secret key");
        OQS_SIG_free(sig);
        return EXIT_FAILURE;
    }

    int rc = EXIT_FAILURE;
    if (OQS_SIG_keypair(sig, pk, sk) != OQS_SUCCESS) {
        log_error("wallet_keygen: OQS_SIG_keypair failed");
        goto cleanup;
    }

    if (write_binary_file(pk_path, pk, MAX_PUBLIC_KEY_LENGTH, 0) != EXIT_SUCCESS ||
        write_binary_file(sk_path, sk, sig->length_secret_key, 1) != EXIT_SUCCESS) {
        /* Clean up partial writes. */
        remove(pk_path);
        remove(sk_path);
        goto cleanup;
    }

    log_info("wallet_keygen: created keypair for '%s'", id);
    rc = EXIT_SUCCESS;

cleanup:
    memset(sk, 0, sig->length_secret_key); /* zero key material */
    free(sk);
    OQS_SIG_free(sig);
    return rc;
}

int wallet_load_pk(const char *id, uint8_t *pk_out, size_t pk_len)
{
    if (!id || !pk_out) {
        log_error("wallet_load_pk: NULL argument");
        return EXIT_FAILURE;
    }

    char path[PATH_BUF];
    snprintf(path, sizeof(path), "%s/%s.pk", WALLET_DIR, id);

    FILE *f = fopen(path, "rb");
    if (!f) {
        log_warn("wallet_load_pk: key '%s' not found (%s)", id, path);
        return EXIT_FAILURE;
    }

    size_t n = fread(pk_out, 1, pk_len, f);
    fclose(f);

    if (n != pk_len) {
        log_error("wallet_load_pk: short read for '%s' (%zu/%zu bytes)",
                  id, n, pk_len);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

int wallet_load_sk(const char *id, uint8_t *sk_out, size_t sk_len)
{
    if (!id || !sk_out) {
        log_error("wallet_load_sk: NULL argument");
        return EXIT_FAILURE;
    }

    char path[PATH_BUF];
    snprintf(path, sizeof(path), "%s/%s.sk", WALLET_DIR, id);

    FILE *f = fopen(path, "rb");
    if (!f) {
        log_warn("wallet_load_sk: key '%s' not found (%s)", id, path);
        return EXIT_FAILURE;
    }

    size_t n = fread(sk_out, 1, sk_len, f);
    fclose(f);

    if (n != sk_len) {
        log_error("wallet_load_sk: short read for '%s' (%zu/%zu bytes)",
                  id, n, sk_len);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

int wallet_exists(const char *id)
{
    if (!id || id[0] == '\0') return 0;
    char path[PATH_BUF];
    snprintf(path, sizeof(path), "%s/%s.pk", WALLET_DIR, id);
    return (access(path, F_OK) == 0) ? 1 : 0;
}
