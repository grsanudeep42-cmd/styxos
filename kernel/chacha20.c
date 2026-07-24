/*
 * chacha20.c — ChaCha20 stream cipher, RFC 7539 compliant
 * 20 rounds, 64-byte block, 96-bit nonce, 32-bit counter.
 */
#include "chacha20.h"
#include "string.h"
#include <stdint.h>

#define ROTL32(v, n) (((v) << (n)) | ((v) >> (32 - (n))))

#define QR(a, b, c, d) do {   \
    (a) += (b); (d) ^= (a); (d) = ROTL32((d), 16); \
    (c) += (d); (b) ^= (c); (b) = ROTL32((b), 12); \
    (a) += (b); (d) ^= (a); (d) = ROTL32((d),  8); \
    (c) += (d); (b) ^= (c); (b) = ROTL32((b),  7); \
} while (0)

static inline uint32_t load32_le(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void chacha20_block(const uint32_t in[16], uint8_t out[64]) {
    uint32_t x[16];
    for (int i = 0; i < 16; i++) x[i] = in[i];

    for (int i = 0; i < 10; i++) {
        /* Column rounds */
        QR(x[0], x[4], x[ 8], x[12]);
        QR(x[1], x[5], x[ 9], x[13]);
        QR(x[2], x[6], x[10], x[14]);
        QR(x[3], x[7], x[11], x[15]);
        /* Diagonal rounds */
        QR(x[0], x[5], x[10], x[15]);
        QR(x[1], x[6], x[11], x[12]);
        QR(x[2], x[7], x[ 8], x[13]);
        QR(x[3], x[4], x[ 9], x[14]);
    }

    for (int i = 0; i < 16; i++) {
        uint32_t v = x[i] + in[i];
        out[i * 4 + 0] = (uint8_t)(v);
        out[i * 4 + 1] = (uint8_t)(v >> 8);
        out[i * 4 + 2] = (uint8_t)(v >> 16);
        out[i * 4 + 3] = (uint8_t)(v >> 24);
    }
}

void chacha20_init(chacha20_ctx_t *ctx,
                   const uint8_t key[32],
                   const uint8_t nonce[12],
                   uint32_t counter) {
    /* RFC 7539 constant "expand 32-byte k" */
    ctx->state[0]  = 0x61707865U;
    ctx->state[1]  = 0x3320646eU;
    ctx->state[2]  = 0x79622d32U;
    ctx->state[3]  = 0x6b206574U;
    /* Key (8 words) */
    for (int i = 0; i < 8; i++)
        ctx->state[4 + i] = load32_le(key + i * 4);
    /* Counter */
    ctx->state[12] = counter;
    /* Nonce (3 words) */
    ctx->state[13] = load32_le(nonce + 0);
    ctx->state[14] = load32_le(nonce + 4);
    ctx->state[15] = load32_le(nonce + 8);
}

void chacha20_xor(chacha20_ctx_t *ctx, uint8_t *buf, size_t len) {
    uint8_t keystream[64];
    size_t i = 0;
    while (i < len) {
        chacha20_block(ctx->state, keystream);
        ctx->state[12]++;   /* Increment block counter */
        size_t chunk = len - i;
        if (chunk > 64) chunk = 64;
        for (size_t j = 0; j < chunk; j++)
            buf[i + j] ^= keystream[j];
        i += chunk;
    }
    /* Zero keystream to not leave key material on stack */
    memset(keystream, 0, 64);
}

void chacha20_encrypt(const uint8_t key[32],
                      const uint8_t nonce[12],
                      uint32_t counter,
                      const uint8_t *src,
                      uint8_t *dst,
                      size_t len) {
    chacha20_ctx_t ctx;
    chacha20_init(&ctx, key, nonce, counter);
    if (src != dst) memcpy(dst, src, len);
    chacha20_xor(&ctx, dst, len);
}
