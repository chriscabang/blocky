/* pow.c — Proof of Work: mine and validate blocks. */
#include "pow.h"
#include "sha256.h"
#include "block.h"
#include "log.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* ── helpers ──────────────────────────────────────────────────────────── */

/* Encode `len` raw bytes as lowercase hex into out[len*2 + 1]. */
static void bytes_to_hex(const uint8_t *bytes, size_t len, char *out) {
    static const char HEX[] = "0123456789abcdef";
    for (size_t i = 0; i < len; i++) {
        out[i * 2]     = HEX[(bytes[i] >> 4) & 0xf];
        out[i * 2 + 1] = HEX[ bytes[i]       & 0xf];
    }
    out[len * 2] = '\0';
}

/*
 * Check whether the first `difficulty` hex characters of the raw digest
 * represent zero, without converting to hex string.
 *
 * Each hex character maps to 4 bits:
 *   even difficulty  → check difficulty/2 full zero bytes
 *   odd  difficulty  → also check the high nibble of the next byte
 *
 * Examples:
 *   difficulty=1 → (digest[0] & 0xf0) == 0
 *   difficulty=2 → digest[0] == 0x00
 *   difficulty=3 → digest[0] == 0x00 && (digest[1] & 0xf0) == 0
 *   difficulty=4 → digest[0] == 0x00 && digest[1] == 0x00
 */
static int meets_difficulty_raw(const uint8_t *digest, uint32_t difficulty) {
    for (uint32_t i = 0; i < difficulty / 2; i++)
        if (digest[i] != 0) return 0;
    if (difficulty & 1)
        if ((digest[difficulty / 2] & 0xf0) != 0) return 0;
    return 1;
}

/* ── public API ───────────────────────────────────────────────────────── */

int mine_block(Block *block, uint32_t difficulty) {
    if (!block) {
        log_error("mine_block: NULL block");
        return EXIT_FAILURE;
    }
    if (difficulty == 0 || difficulty > SHA256_HEX_LEN) {
        log_error("mine_block: invalid difficulty %u (must be 1..%d)",
                  difficulty, SHA256_HEX_LEN);
        return EXIT_FAILURE;
    }

    /*
     * SHA-256 midstate optimisation.
     *
     * The field order fed into hash() in crypto.c is:
     *   index → timestamp → previous_hash → merkle_root → nonce → consensus
     *
     * The first four fields are constant for the lifetime of the mining loop.
     * Pre-compute a SHA-256 context through them; copy it for each nonce
     * candidate and feed only nonce + consensus before finalising.
     * Saves approximately 2 out of 3 compression rounds per iteration.
     */
    sha256_ctx base;
    sha256_init(&base);
    sha256_update(&base, &block->index,     sizeof(block->index));
    sha256_update(&base, &block->timestamp, sizeof(block->timestamp));
    sha256_update(&base, block->previous_hash,
                  strlen((const char *)block->previous_hash));
    sha256_update(&base, block->merkle_root,
                  strlen((const char *)block->merkle_root));

    log_info("mine_block: starting index=%u difficulty=%u",
             block->index, difficulty);

    for (uint64_t n = 0; n <= (uint64_t)UINT32_MAX; n++) {
        block->nonce = (uint32_t)n;

        sha256_ctx ctx = base;           /* copy pre-computed state  */
        sha256_update(&ctx, &block->nonce,     sizeof(block->nonce));
        sha256_update(&ctx, &block->consensus, sizeof(block->consensus));

        uint8_t digest[SHA256_DIGEST_LEN];
        sha256_final(&ctx, digest);      /* wipes ctx (copy), not base */

        if (meets_difficulty_raw(digest, difficulty)) {
            bytes_to_hex(digest, SHA256_DIGEST_LEN, (char *)block->hash);
            log_info("mine_block: solved nonce=%u hash=%.16s...",
                     block->nonce, (char *)block->hash);
            return EXIT_SUCCESS;
        }
    }

    log_error("mine_block: nonce exhausted (index=%u difficulty=%u)",
              block->index, difficulty);
    return EXIT_FAILURE;
}

int validate_block_pow(const Block *block, uint32_t difficulty) {
    if (!block) {
        log_error("validate_block_pow: NULL block");
        return EXIT_FAILURE;
    }

    /* 1. Difficulty: stored hash must have `difficulty` leading '0' chars. */
    for (uint32_t i = 0; i < difficulty; i++) {
        if (block->hash[i] != '0') {
            log_error("validate_block_pow: block %u fails difficulty %u "
                      "(hash=%.16s...)",
                      block->index, difficulty, (const char *)block->hash);
            return EXIT_FAILURE;
        }
    }

    /* 2. Integrity: stored hash must match a freshly computed hash. */
    if (block_verify_hash(block) != EXIT_SUCCESS) {
        log_error("validate_block_pow: integrity check failed for block %u",
                  block->index);
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
