#pragma once
#include <stdint.h>
#include <stddef.h>

/* ChaCha20 stream cipher — RFC 7539
 * Key: 32 bytes, Nonce: 12 bytes, Counter: 32-bit starting value */

typedef struct {
    uint32_t state[16];
} chacha20_ctx_t;

void chacha20_init(chacha20_ctx_t *ctx,
                   const uint8_t key[32],
                   const uint8_t nonce[12],
                   uint32_t counter);

/* Encrypt/decrypt in-place (XOR with keystream) */
void chacha20_xor(chacha20_ctx_t *ctx, uint8_t *buf, size_t len);

/* One-shot: encrypt src → dst, len bytes */
void chacha20_encrypt(const uint8_t key[32],
                      const uint8_t nonce[12],
                      uint32_t counter,
                      const uint8_t *src,
                      uint8_t *dst,
                      size_t len);
