/* key.c — Dilithium-3 key store: generate, load, and check existence.
 *
 * Owns .chain/keys/ — the single source of truth for identity keypairs.
 * key_generate() creates the keypair; key_load_pk/sk() read it back for
 * signing (cmd_send) and verification (cmd_mine).
 */

#include "key.h"
#include "log.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/stat.h>

#include <oqs/oqs.h>

#define PATH_BUF 512

/* ── internal helpers ─────────────────────────────────────────────────── */

static int ensure_dirs(void)
{
    if (mkdir(".chain", 0755) == -1 && errno != EEXIST) {
        log_error("key: cannot create .chain: %s", strerror(errno));
        return EXIT_FAILURE;
    }
    if (mkdir(KEYS_DIR, 0755) == -1 && errno != EEXIST) {
        log_error("key: cannot create %s: %s", KEYS_DIR, strerror(errno));
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

static int write_key_file(const char *path, const uint8_t *data, size_t len,
                           mode_t mode)
{
    if (access(path, F_OK) == 0) {
        log_error("key: file already exists: %s", path);
        return EXIT_FAILURE;
    }
    FILE *f = fopen(path, "wb");
    if (!f) {
        log_error("key: cannot open %s: %s", path, strerror(errno));
        return EXIT_FAILURE;
    }
    size_t written = fwrite(data, 1, len, f);
    fclose(f);
    if (written != len) {
        log_error("key: short write to %s", path);
        return EXIT_FAILURE;
    }
    if (chmod(path, mode) != 0)
        log_warn("key: chmod %o %s: %s", (unsigned)mode, path, strerror(errno));
    return EXIT_SUCCESS;
}

/* ── public API ───────────────────────────────────────────────────────── */

int key_generate(const char *id)
{
    if (!id || id[0] == '\0') {
        log_error("key_generate: id must not be empty");
        return EXIT_FAILURE;
    }
    if (strchr(id, '/') != NULL) {
        log_error("key_generate: id must not contain '/'");
        return EXIT_FAILURE;
    }

    if (ensure_dirs() != EXIT_SUCCESS)
        return EXIT_FAILURE;

    char pk_path[PATH_BUF], sk_path[PATH_BUF];
    snprintf(pk_path, sizeof(pk_path), "%s/%s.pk", KEYS_DIR, id);
    snprintf(sk_path, sizeof(sk_path), "%s/%s.sk", KEYS_DIR, id);

    OQS_SIG *sig = OQS_SIG_new(OQS_SIG_alg_dilithium_3);
    if (!sig) {
        log_error("key_generate: OQS_SIG_new failed");
        return EXIT_FAILURE;
    }

    uint8_t *pub  = malloc(sig->length_public_key);
    uint8_t *priv = malloc(sig->length_secret_key);
    if (!pub || !priv) {
        log_error("key_generate: out of memory");
        free(pub); free(priv);
        OQS_SIG_free(sig);
        return EXIT_FAILURE;
    }

    int rc = EXIT_SUCCESS;
    if (OQS_SIG_keypair(sig, pub, priv) != OQS_SUCCESS) {
        log_error("key_generate: OQS_SIG_keypair failed");
        rc = EXIT_FAILURE;
    } else if (write_key_file(pk_path, pub,  sig->length_public_key, 0644) != EXIT_SUCCESS ||
               write_key_file(sk_path, priv, sig->length_secret_key, 0600) != EXIT_SUCCESS) {
        rc = EXIT_FAILURE;
    }

    OQS_MEM_cleanse(priv, sig->length_secret_key);
    free(pub);
    free(priv);
    OQS_SIG_free(sig);
    return rc;
}

int key_load_pk(const char *id, uint8_t *pk_out, size_t pk_len)
{
    if (!id || !pk_out) {
        log_error("key_load_pk: NULL argument");
        return EXIT_FAILURE;
    }

    char path[PATH_BUF];
    snprintf(path, sizeof(path), "%s/%s.pk", KEYS_DIR, id);

    FILE *f = fopen(path, "rb");
    if (!f) {
        log_warn("key_load_pk: key '%s' not found (%s)", id, path);
        return EXIT_FAILURE;
    }

    size_t n = fread(pk_out, 1, pk_len, f);
    fclose(f);

    if (n != pk_len) {
        log_error("key_load_pk: short read for '%s' (%zu/%zu bytes)",
                  id, n, pk_len);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

int key_load_sk(const char *id, uint8_t *sk_out, size_t sk_len)
{
    if (!id || !sk_out) {
        log_error("key_load_sk: NULL argument");
        return EXIT_FAILURE;
    }

    char path[PATH_BUF];
    snprintf(path, sizeof(path), "%s/%s.sk", KEYS_DIR, id);

    FILE *f = fopen(path, "rb");
    if (!f) {
        log_warn("key_load_sk: key '%s' not found (%s)", id, path);
        return EXIT_FAILURE;
    }

    size_t n = fread(sk_out, 1, sk_len, f);
    fclose(f);

    if (n != sk_len) {
        log_error("key_load_sk: short read for '%s' (%zu/%zu bytes)",
                  id, n, sk_len);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

int key_exists(const char *id)
{
    if (!id || id[0] == '\0') return 0;
    char path[PATH_BUF];
    snprintf(path, sizeof(path), "%s/%s.pk", KEYS_DIR, id);
    return (access(path, F_OK) == 0) ? 1 : 0;
}
