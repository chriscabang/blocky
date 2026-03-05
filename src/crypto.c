/**
 * @file crypto.c
 * @brief Block hashing and Merkle root using built-in SHA-256.
 *
 * No OpenSSL SHA functions are used here. All hashing goes through
 * sha256.h / sha256.c (FIPS 180-4, self-contained).
 *
 * OpenSSL (-lcrypto) remains linked for TLS only.
 */

#include "crypto.h"
#include "sha256.h"
#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── internal helpers ─────────────────────────────────────────────────── */

/* Encode raw bytes as lowercase hex, writing out[len*2] + null terminator. */
static void to_hex(const uint8_t *bytes, size_t len, char *out) {
    static const char HEX[] = "0123456789abcdef";
    for (size_t i = 0; i < len; i++) {
        out[i * 2]     = HEX[(bytes[i] >> 4) & 0xf];
        out[i * 2 + 1] = HEX[ bytes[i]       & 0xf];
    }
    out[len * 2] = '\0';
}

/* ── public API ───────────────────────────────────────────────────────── */

void compute_merkle_root(Block *block, char *merkle_root) {
    if (!block || !merkle_root) {
        log_error("compute_merkle_root: NULL argument");
        return;
    }

    if (block->transaction_count == 0) {
        merkle_root[0] = '0';
        merkle_root[1] = '\0';
        return;
    }

    /* Hash all transactions incrementally: sender + recipient + raw amount
     * bytes. Using sha256_update avoids an intermediate accumulation buffer
     * and includes all fields that make a transaction unique. */
    sha256_ctx ctx;
    sha256_init(&ctx);

    for (uint32_t i = 0; i < block->transaction_count; i++) {
        const Transaction *tx = &block->transactions[i];
        sha256_update(&ctx, tx->sender,    strlen(tx->sender));
        sha256_update(&ctx, tx->recipient, strlen(tx->recipient));
        sha256_update(&ctx, &tx->amount,   sizeof(tx->amount));
    }

    uint8_t digest[SHA256_DIGEST_LEN];
    sha256_final(&ctx, digest);
    to_hex(digest, SHA256_DIGEST_LEN, merkle_root);
}

int hash(Block *block) {
    if (!block) {
        log_error("hash: NULL block");
        return EXIT_FAILURE;
    }

    log_info("Hashing block %u", block->index);

    sha256_ctx ctx;
    sha256_init(&ctx);

    /* Feed every field that defines the block header.
     * Order matches the original crypto.c to preserve hash compatibility,
     * with `consensus` added (it was previously omitted — a security gap). */
    sha256_update(&ctx, &block->index,       sizeof(block->index));
    sha256_update(&ctx, &block->timestamp,   sizeof(block->timestamp));
    sha256_update(&ctx, block->previous_hash,
                  strlen((const char *)block->previous_hash));
    sha256_update(&ctx, block->merkle_root,
                  strlen((const char *)block->merkle_root));
    sha256_update(&ctx, &block->nonce,       sizeof(block->nonce));
    sha256_update(&ctx, &block->consensus,   sizeof(block->consensus));

    uint8_t digest[SHA256_DIGEST_LEN];
    sha256_final(&ctx, digest);

    /* Write 64-char hex + null into block->hash (HASH_SIZE = 65). */
    to_hex(digest, SHA256_DIGEST_LEN, (char *)block->hash);

    return EXIT_SUCCESS;
}

int sign(Block *block, const char *private_key, char *signature) {
    (void)block;
    (void)private_key;
    (void)signature;
    log_error("sign: not implemented — pending Dilithium integration (ADR-003)");
    return EXIT_FAILURE;
}

int verify(const Block *block, const char *public_key) {
    (void)block;
    (void)public_key;
    log_error("verify: not implemented — pending Dilithium integration (ADR-003)");
    return EXIT_FAILURE;
}
