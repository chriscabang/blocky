/* wallet.c — Dilithium-3 keypair loading for user identities.
 *
 * Key generation is out of scope here — use build/utils/key_gen.
 * This module only abstracts the .chain/keys/ path convention so that
 * cmd_send and cmd_mine can locate keys without duplicating path logic.
 */

#include "wallet.h"
#include "log.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define PATH_BUF 512

/* ── public API ───────────────────────────────────────────────────────── */

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
