/* mempool.c — Local pending-transaction queue (signed Transaction objects). */

#include "mempool.h"
#include "sha256.h"
#include "log.h"

#include <dirent.h>
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

/* ── internal helpers ─────────────────────────────────────────────────── */

static int ensure_dir(void)
{
    if (mkdir(".chain", 0755) == -1 && errno != EEXIST) {
        log_error("mempool: cannot create .chain: %s", strerror(errno));
        return EXIT_FAILURE;
    }
    if (mkdir(MEMPOOL_DIR, 0755) == -1 && errno != EEXIST) {
        log_error("mempool: cannot create %s: %s", MEMPOOL_DIR, strerror(errno));
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

/*
 * Compute a hex identifier for a transaction from its canonical message bytes:
 *   sender[MAX_PUBLIC_KEY_LENGTH] || recipient[MAX_PUBLIC_KEY_LENGTH]
 *   || amount(uint64_t) || nonce(uint64_t)
 *
 * This mirrors build_message() in transaction.c to produce a stable,
 * collision-resistant filename without depending on the full struct layout.
 * hex_out must be at least SHA256_HEX_LEN + 1 bytes.
 */
static void tx_filename(const Transaction *tx, char hex_out[SHA256_HEX_LEN + 1])
{
    sha256_ctx ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, (const uint8_t *)tx->sender,    MAX_PUBLIC_KEY_LENGTH);
    sha256_update(&ctx, (const uint8_t *)tx->recipient, MAX_PUBLIC_KEY_LENGTH);
    sha256_update(&ctx, &tx->amount,   sizeof(tx->amount));
    sha256_update(&ctx, &tx->nonce,    sizeof(tx->nonce));
    uint8_t digest[SHA256_DIGEST_LEN];
    sha256_final(&ctx, digest);
    sha256_to_hex(digest, SHA256_DIGEST_LEN, hex_out);
}

/* ── public API ───────────────────────────────────────────────────────── */

int mempool_add(const Transaction *tx)
{
    if (!tx) {
        log_error("mempool_add: NULL transaction");
        return EXIT_FAILURE;
    }

    if (ensure_dir() != EXIT_SUCCESS)
        return EXIT_FAILURE;

    char hex[SHA256_HEX_LEN + 1];
    tx_filename(tx, hex);

    char path[PATH_BUF];
    snprintf(path, sizeof(path), "%s/%s", MEMPOOL_DIR, hex);

    FILE *f = fopen(path, "wb");
    if (!f) {
        log_error("mempool_add: cannot write %s: %s", path, strerror(errno));
        return EXIT_FAILURE;
    }

    size_t n = fwrite(tx, sizeof(Transaction), 1, f);
    fflush(f);
    fsync(fileno(f));
    fclose(f);

    if (n != 1) {
        log_error("mempool_add: short write to %s", path);
        remove(path);
        return EXIT_FAILURE;
    }

    log_info("mempool_add: queued tx %s->%s %.6f",
             tx->sender, tx->recipient,
             (double)tx->amount / 1000000.0);
    return EXIT_SUCCESS;
}

int mempool_load_all(Transaction *out, uint32_t max, uint32_t *count_out)
{
    if (!out || !count_out) {
        log_error("mempool_load_all: NULL argument");
        return EXIT_FAILURE;
    }
    *count_out = 0;

    DIR *d = opendir(MEMPOOL_DIR);
    if (!d) {
        log_debug("mempool_load_all: %s not found; empty mempool", MEMPOOL_DIR);
        return EXIT_SUCCESS;
    }

    struct dirent *e;
    while (*count_out < max && (e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') continue;

        char path[PATH_BUF];
        snprintf(path, sizeof(path), "%s/%s", MEMPOOL_DIR, e->d_name);

        FILE *f = fopen(path, "rb");
        if (!f) continue;

        size_t n = fread(&out[*count_out], sizeof(Transaction), 1, f);
        fclose(f);

        if (n == 1)
            (*count_out)++;
        else
            log_warn("mempool_load_all: skipping corrupt entry '%s'", e->d_name);
    }
    closedir(d);

    log_info("mempool_load_all: loaded %u pending transaction(s)", *count_out);
    return EXIT_SUCCESS;
}

void mempool_purge(const Transaction *txns, uint32_t count)
{
    char hex[SHA256_HEX_LEN + 1];
    char path[PATH_BUF];

    for (uint32_t i = 0; i < count; i++) {
        tx_filename(&txns[i], hex);
        snprintf(path, sizeof(path), "%s/%s", MEMPOOL_DIR, hex);
        if (remove(path) == 0)
            log_debug("mempool_purge: removed %s", hex);
        else
            log_warn("mempool_purge: could not remove %s: %s", hex, strerror(errno));
    }
}

uint32_t mempool_count(void)
{
    DIR *d = opendir(MEMPOOL_DIR);
    if (!d) return 0;

    uint32_t n = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] != '.') n++;
    }
    closedir(d);
    return n;
}
