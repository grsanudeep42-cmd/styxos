#pragma once

#include <stdint.h>
#include <stddef.h>
#include "aes.h"

typedef struct {
    aes256_ctx_t aes_ctx;
    uint8_t      h[16]; // Hash subkey
} aes_gcm_ctx_t;

void aes_gcm_init(aes_gcm_ctx_t *ctx, const uint8_t *key);

void aes_gcm_encrypt(const aes_gcm_ctx_t *ctx,
                     const uint8_t *iv,
                     const uint8_t *aad, size_t aad_len,
                     const uint8_t *pt, size_t pt_len,
                     uint8_t *ct, uint8_t *tag);

int aes_gcm_decrypt(const aes_gcm_ctx_t *ctx,
                    const uint8_t *iv,
                    const uint8_t *aad, size_t aad_len,
                    const uint8_t *ct, size_t ct_len,
                    const uint8_t *tag, uint8_t *pt);

int aes_gcm_self_test(void);
