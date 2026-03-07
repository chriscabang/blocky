/**
 * @file sha256.h
 * @brief SHA-256 per FIPS 180-4. Self-contained; no external dependencies.
 *
 * Verified against NIST test vectors in tests/test_crypto.c.
 */

#ifndef SHA256_H
#define SHA256_H

#include <stddef.h>
#include <stdint.h>

#define SHA256_DIGEST_LEN  32   /* raw bytes */
#define SHA256_HEX_LEN     64   /* hex characters (no null terminator) */

typedef struct {
    uint32_t state[8];   /* running hash state */
    uint64_t bit_count;  /* total bits processed so far */
    uint8_t  buf[64];    /* partial block buffer */
    size_t   buflen;     /* bytes currently in buf */
} sha256_ctx;

/** Initialise a fresh SHA-256 context. */
void sha256_init  (sha256_ctx *ctx);

/** Feed data into the hash. May be called multiple times. */
void sha256_update(sha256_ctx *ctx, const void *data, size_t len);

/** Finalise and write the 32-byte digest. Wipes the context. */
void sha256_final (sha256_ctx *ctx, uint8_t digest[SHA256_DIGEST_LEN]);

/** One-shot: compute digest of data[0..len-1]. */
void sha256_digest(const void *data, size_t len,
                   uint8_t out[SHA256_DIGEST_LEN]);

/**
 * Encode raw bytes as a lowercase hex string.
 * out must be at least len*2+1 bytes.  Writes a NUL terminator.
 *
 * Shared utility — avoids duplicating the same function in every module
 * that formats SHA-256 output (crypto.c, pow.c).
 */
void sha256_to_hex(const uint8_t *bytes, size_t len, char *out);

#endif /* SHA256_H */
