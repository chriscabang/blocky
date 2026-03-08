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

/* ── public API ───────────────────────────────────────────────────────── */

void compute_merkle_root(const Block *block, char *merkle_root) {
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
        sha256_update(&ctx, &tx->nonce,    sizeof(tx->nonce));
    }

    uint8_t digest[SHA256_DIGEST_LEN];
    sha256_final(&ctx, digest);
    sha256_to_hex(digest, SHA256_DIGEST_LEN, merkle_root);
}

int block_hash(Block *block) {
    if (!block) {
        log_error("block_hash: NULL block");
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
    sha256_update(&ctx, block->proposer_id,
                  strlen((const char *)block->proposer_id));

    uint8_t digest[SHA256_DIGEST_LEN];
    sha256_final(&ctx, digest);

    /* Write 64-char hex + null into block->hash (HASH_SIZE = 65). */
    sha256_to_hex(digest, SHA256_DIGEST_LEN, (char *)block->hash);

    return EXIT_SUCCESS;
}

