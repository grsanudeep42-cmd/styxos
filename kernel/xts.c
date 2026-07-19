#include "xts.h"
#include "string.h"

void aes256_xts_init(aes256_xts_ctx_t *ctx, const uint8_t *key_512) {
    aes256_init_encrypt(&ctx->aes_key1, key_512);      // Key1
    aes256_init_encrypt(&ctx->aes_key2, key_512 + 32); // Key2
}

static void gf_mul_alpha(uint64_t tweak[2]) {
    uint64_t carry = tweak[1] >> 63;
    tweak[1] = (tweak[1] << 1) | (tweak[0] >> 63);
    tweak[0] = (tweak[0] << 1) ^ (carry ? 0x87 : 0);
}

void aes256_xts_encrypt_sector(const aes256_xts_ctx_t *ctx, uint32_t sector_lba, const uint8_t *in, uint8_t *out) {
    uint64_t tweak[2];
    tweak[0] = sector_lba;
    tweak[1] = 0;

    // Encrypt tweak with Key2
    aes256_encrypt_block(&ctx->aes_key2, (const uint8_t *)tweak, (uint8_t *)tweak);

    for (int block = 0; block < 32; block++) {
        uint64_t block_in[2];
        uint64_t block_out[2];

        memcpy(block_in, in + block * 16, 16);

        block_in[0] ^= tweak[0];
        block_in[1] ^= tweak[1];

        aes256_encrypt_block(&ctx->aes_key1, (const uint8_t *)block_in, (uint8_t *)block_out);

        block_out[0] ^= tweak[0];
        block_out[1] ^= tweak[1];

        memcpy(out + block * 16, block_out, 16);

        gf_mul_alpha(tweak);
    }
}

void aes256_xts_decrypt_sector(const aes256_xts_ctx_t *ctx, uint32_t sector_lba, const uint8_t *in, uint8_t *out) {
    uint64_t tweak[2];
    tweak[0] = sector_lba;
    tweak[1] = 0;

    // Encrypt tweak with Key2
    aes256_encrypt_block(&ctx->aes_key2, (const uint8_t *)tweak, (uint8_t *)tweak);

    for (int block = 0; block < 32; block++) {
        uint64_t block_in[2];
        uint64_t block_out[2];

        memcpy(block_in, in + block * 16, 16);

        block_in[0] ^= tweak[0];
        block_in[1] ^= tweak[1];

        aes256_decrypt_block(&ctx->aes_key1, (const uint8_t *)block_in, (uint8_t *)block_out);

        block_out[0] ^= tweak[0];
        block_out[1] ^= tweak[1];

        memcpy(out + block * 16, block_out, 16);

        gf_mul_alpha(tweak);
    }
}
