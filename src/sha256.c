/**
 * @file sha256.c
 * @brief SHA-256 per FIPS 180-4. No external dependencies.
 *
 * Reference: NIST FIPS 180-4 (2015), sections 4–6.
 * Test vectors: NIST FIPS 180-4 Appendix B, cross-validated against OpenSSL
 * in tests/test_crypto.c.
 */

#include "sha256.h"
#include <stdio.h>
#include <string.h>

/* ── FIPS 180-4 § 4.1.2: bit functions and σ / Σ ─────────────────────── */

#define ROR32(x, n)   (((x) >> (n)) | ((x) << (32 - (n))))

#define CH(x, y, z)   (((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x, y, z)  (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define EP0(x)        (ROR32(x,  2) ^ ROR32(x, 13) ^ ROR32(x, 22))
#define EP1(x)        (ROR32(x,  6) ^ ROR32(x, 11) ^ ROR32(x, 25))
#define SIG0(x)       (ROR32(x,  7) ^ ROR32(x, 18) ^ ((x) >>  3))
#define SIG1(x)       (ROR32(x, 17) ^ ROR32(x, 19) ^ ((x) >> 10))

/* ── FIPS 180-4 § 4.2.2: SHA-256 round constants ─────────────────────── */

static const uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
    0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
    0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

/* ── FIPS 180-4 § 5.3.3: SHA-256 initial hash value ─────────────────── */

static const uint32_t H0[8] = {
    0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
    0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19,
};

/* ── helpers: big-endian byte ↔ uint32_t ─────────────────────────────── */

static uint32_t be32_get(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16)
         | ((uint32_t)p[2] <<  8) |  (uint32_t)p[3];
}

static void be32_put(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >>  8);
    p[3] = (uint8_t) v;
}

/* ── FIPS 180-4 § 6.2.2: process one 512-bit (64-byte) block ─────────── */

static void sha256_block(sha256_ctx *ctx, const uint8_t blk[64]) {
    uint32_t W[64];
    uint32_t a, b, c, d, e, f, g, h;
    uint32_t T1, T2;

    /* Prepare message schedule W (FIPS 180-4 step 1) */
    for (int i =  0; i < 16; i++) W[i] = be32_get(blk + i * 4);
    for (int i = 16; i < 64; i++)
        W[i] = SIG1(W[i-2]) + W[i-7] + SIG0(W[i-15]) + W[i-16];

    /* Initialise working variables (step 2) */
    a = ctx->state[0]; b = ctx->state[1];
    c = ctx->state[2]; d = ctx->state[3];
    e = ctx->state[4]; f = ctx->state[5];
    g = ctx->state[6]; h = ctx->state[7];

    /* 64 compression rounds (step 3) */
    for (int i = 0; i < 64; i++) {
        T1 = h + EP1(e) + CH(e, f, g) + K[i] + W[i];
        T2 = EP0(a) + MAJ(a, b, c);
        h = g; g = f; f = e; e = d + T1;
        d = c; c = b; b = a; a = T1 + T2;
    }

    /* Compute intermediate hash value (step 4) */
    ctx->state[0] += a; ctx->state[1] += b;
    ctx->state[2] += c; ctx->state[3] += d;
    ctx->state[4] += e; ctx->state[5] += f;
    ctx->state[6] += g; ctx->state[7] += h;
}

/* ── public API ───────────────────────────────────────────────────────── */

void sha256_init(sha256_ctx *ctx) {
    memcpy(ctx->state, H0, sizeof(H0));
    ctx->bit_count = 0;
    ctx->buflen    = 0;
}

void sha256_update(sha256_ctx *ctx, const void *data, size_t len) {
    const uint8_t *p = (const uint8_t *)data;

    ctx->bit_count += (uint64_t)len * 8;

    while (len > 0) {
        size_t space = 64 - ctx->buflen;
        size_t take  = (len < space) ? len : space;

        memcpy(ctx->buf + ctx->buflen, p, take);
        ctx->buflen += take;
        p   += take;
        len -= take;

        if (ctx->buflen == 64) {
            sha256_block(ctx, ctx->buf);
            ctx->buflen = 0;
        }
    }
}

void sha256_final(sha256_ctx *ctx, uint8_t digest[SHA256_DIGEST_LEN]) {
    /* Append the mandatory '1' bit as a full 0x80 byte */
    ctx->buf[ctx->buflen++] = 0x80;

    /* If there is no room for the 8-byte length field, flush this block */
    if (ctx->buflen > 56) {
        memset(ctx->buf + ctx->buflen, 0, 64 - ctx->buflen);
        sha256_block(ctx, ctx->buf);
        ctx->buflen = 0;
    }

    /* Zero-pad to byte offset 56 */
    memset(ctx->buf + ctx->buflen, 0, 56 - ctx->buflen);

    /* Append 64-bit big-endian message length in bits (FIPS 180-4 § 5.1.1) */
    be32_put(ctx->buf + 56, (uint32_t)(ctx->bit_count >> 32));
    be32_put(ctx->buf + 60, (uint32_t)(ctx->bit_count & 0xffffffffu));

    sha256_block(ctx, ctx->buf);

    /* Produce the 256-bit digest as 8 big-endian 32-bit words */
    for (int i = 0; i < 8; i++)
        be32_put(digest + i * 4, ctx->state[i]);

    /* Wipe sensitive state */
    memset(ctx, 0, sizeof(*ctx));
}

void sha256_to_hex(const uint8_t *bytes, size_t len, char *out) {
    static const char HEX[] = "0123456789abcdef";
    for (size_t i = 0; i < len; i++) {
        out[i * 2]     = HEX[(bytes[i] >> 4) & 0xf];
        out[i * 2 + 1] = HEX[ bytes[i]       & 0xf];
    }
    out[len * 2] = '\0';
}

void sha256_digest(const void *data, size_t len,
                   uint8_t out[SHA256_DIGEST_LEN]) {
    sha256_ctx ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, data, len);
    sha256_final(&ctx, out);
}

void sha256_from_hex(const char *hex, uint8_t *out, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        unsigned int b = 0;
        /* sscanf with %02x reads exactly 2 hex chars */
        (void)sscanf(hex + i * 2, "%02x", &b);
        out[i] = (uint8_t)b;
    }
}
