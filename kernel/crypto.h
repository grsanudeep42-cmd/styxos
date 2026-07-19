#pragma once

#include <stddef.h>
#include <stdint.h>

#define SHA512_DIGEST_SIZE 64
#define SHA512_BLOCK_SIZE  128

typedef struct {
    uint64_t state[8];
    uint64_t bitlen;
    uint8_t  buffer[128];
} sha512_ctx_t;

void sha512_init(sha512_ctx_t *ctx);
void sha512_update(sha512_ctx_t *ctx, const uint8_t *data, size_t len);
void sha512_final(sha512_ctx_t *ctx, uint8_t *digest);
void sha512(const uint8_t *data, size_t len, uint8_t *digest);

void hmac_sha512(const uint8_t *key, size_t key_len, const uint8_t *data, size_t data_len, uint8_t *digest);

void hkdf_sha512_extract(const uint8_t *salt, size_t salt_len, const uint8_t *ikm, size_t ikm_len, uint8_t *prk);
void hkdf_sha512_expand(const uint8_t *prk, const uint8_t *info, size_t info_len, uint8_t *okm, size_t okm_len);
