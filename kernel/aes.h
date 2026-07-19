#pragma once

#include <stdint.h>
#include <stddef.h>

typedef struct {
    uint32_t round_keys[60]; // 15 round keys * 4 words
} aes256_ctx_t;

void aes256_init_encrypt(aes256_ctx_t *ctx, const uint8_t *key);
void aes256_init_decrypt(aes256_ctx_t *ctx, const uint8_t *key);
void aes256_encrypt_block(const aes256_ctx_t *ctx, const uint8_t *in, uint8_t *out);
void aes256_decrypt_block(const aes256_ctx_t *ctx, const uint8_t *in, uint8_t *out);
