#pragma once

#include "aes.h"
#include <stdint.h>
#include <stddef.h>

typedef struct {
    aes256_ctx_t aes_key1; // Data cipher
    aes256_ctx_t aes_key2; // Tweak cipher
} aes256_xts_ctx_t;

void aes256_xts_init(aes256_xts_ctx_t *ctx, const uint8_t *key_512);
void aes256_xts_encrypt_sector(const aes256_xts_ctx_t *ctx, uint32_t sector_lba, const uint8_t *in, uint8_t *out);
void aes256_xts_decrypt_sector(const aes256_xts_ctx_t *ctx, uint32_t sector_lba, const uint8_t *in, uint8_t *out);
