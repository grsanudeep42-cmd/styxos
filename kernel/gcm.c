#include "gcm.h"
#include "string.h"
#include "serial.h"

/* ── Galois Field Multiplication in GF(2^128) ─────────────────────────────── */
/* Reduction polynomial: x^128 + x^7 + x^2 + x + 1.
 * GCM uses the little-endian bit-reflected representation.
 */
static void ghash_multiply(uint8_t x[16], const uint8_t y[16]) {
    uint8_t z[16];
    uint8_t v[16];
    memset(z, 0, 16);
    memcpy(v, y, 16);

    for (int i = 0; i < 128; i++) {
        // If the i-th bit of x is 1 (left-to-right ordering)
        if (x[i / 8] & (1 << (7 - (i % 8)))) {
            for (int j = 0; j < 16; j++) {
                z[j] ^= v[j];
            }
        }

        // Shift v by 1 bit to the right
        uint8_t carry = 0;
        for (int j = 0; j < 16; j++) {
            uint8_t next_carry = v[j] & 1;
            v[j] = (v[j] >> 1) | (carry << 7);
            carry = next_carry;
        }
        if (carry) {
            v[0] ^= 0xE1; // GCM polynomial reduction byte
        }
    }
    memcpy(x, z, 16);
}

/* ── GHASH function ───────────────────────────────────────────────────────── */
static void ghash(const uint8_t *h,
                  const uint8_t *aad, size_t aad_len,
                  const uint8_t *ct, size_t ct_len,
                  uint8_t *out_tag) {
    uint8_t x[16];
    memset(x, 0, 16);

    // 1. Process AAD (padded to 16-byte boundary)
    size_t i;
    for (i = 0; i < aad_len; i += 16) {
        size_t chunk_len = (aad_len - i >= 16) ? 16 : (aad_len - i);
        uint8_t block[16];
        memset(block, 0, 16);
        memcpy(block, aad + i, chunk_len);

        for (int j = 0; j < 16; j++) {
            x[j] ^= block[j];
        }
        ghash_multiply(x, h);
    }

    // 2. Process Ciphertext (padded to 16-byte boundary)
    for (i = 0; i < ct_len; i += 16) {
        size_t chunk_len = (ct_len - i >= 16) ? 16 : (ct_len - i);
        uint8_t block[16];
        memset(block, 0, 16);
        memcpy(block, ct + i, chunk_len);

        for (int j = 0; j < 16; j++) {
            x[j] ^= block[j];
        }
        ghash_multiply(x, h);
    }

    // 3. Process length block: (aad_len * 8) || (ct_len * 8) as big-endian 64-bit
    uint64_t aad_bits = (uint64_t)aad_len * 8;
    uint64_t ct_bits = (uint64_t)ct_len * 8;

    uint8_t len_block[16];
    for (int j = 0; j < 8; j++) {
        len_block[j] = (uint8_t)(aad_bits >> (56 - j * 8));
        len_block[j + 8] = (uint8_t)(ct_bits >> (56 - j * 8));
    }

    for (int j = 0; j < 16; j++) {
        x[j] ^= len_block[j];
    }
    ghash_multiply(x, h);

    memcpy(out_tag, x, 16);
}

/* ── Counter Incrementation ───────────────────────────────────────────────── */
static void inc_counter(uint8_t y[16]) {
    for (int i = 15; i >= 12; i--) {
        y[i]++;
        if (y[i] != 0) break;
    }
}

/* ── Public APIs ────────────────────────────────────────────────────────── */

void aes_gcm_init(aes_gcm_ctx_t *ctx, const uint8_t *key) {
    aes256_init_encrypt(&ctx->aes_ctx, key);
    uint8_t zero[16];
    memset(zero, 0, 16);
    aes256_encrypt_block(&ctx->aes_ctx, zero, ctx->h);
}

void aes_gcm_encrypt(const aes_gcm_ctx_t *ctx,
                     const uint8_t *iv,
                     const uint8_t *aad, size_t aad_len,
                     const uint8_t *pt, size_t pt_len,
                     uint8_t *ct, uint8_t *tag) {
    uint8_t y[16];
    memset(y, 0, 16);
    memcpy(y, iv, 12);
    y[15] = 1; // J0 = IV || 0^31 || 1

    uint8_t tag_mask[16];
    aes256_encrypt_block(&ctx->aes_ctx, y, tag_mask);

    size_t i;
    for (i = 0; i < pt_len; i += 16) {
        inc_counter(y);
        uint8_t keystream[16];
        aes256_encrypt_block(&ctx->aes_ctx, y, keystream);

        size_t chunk_len = (pt_len - i >= 16) ? 16 : (pt_len - i);
        for (size_t j = 0; j < chunk_len; j++) {
            ct[i + j] = pt[i + j] ^ keystream[j];
        }
    }

    uint8_t ghash_out[16];
    ghash(ctx->h, aad, aad_len, ct, pt_len, ghash_out);

    for (int j = 0; j < 16; j++) {
        tag[j] = ghash_out[j] ^ tag_mask[j];
    }
}

int aes_gcm_decrypt(const aes_gcm_ctx_t *ctx,
                    const uint8_t *iv,
                    const uint8_t *aad, size_t aad_len,
                    const uint8_t *ct, size_t ct_len,
                    const uint8_t *tag, uint8_t *pt) {
    uint8_t y[16];
    memset(y, 0, 16);
    memcpy(y, iv, 12);
    y[15] = 1; // J0 = IV || 0^31 || 1

    uint8_t tag_mask[16];
    aes256_encrypt_block(&ctx->aes_ctx, y, tag_mask);

    size_t i;
    for (i = 0; i < ct_len; i += 16) {
        inc_counter(y);
        uint8_t keystream[16];
        aes256_encrypt_block(&ctx->aes_ctx, y, keystream);

        size_t chunk_len = (ct_len - i >= 16) ? 16 : (ct_len - i);
        for (size_t j = 0; j < chunk_len; j++) {
            pt[i + j] = ct[i + j] ^ keystream[j];
        }
    }

    uint8_t ghash_out[16];
    ghash(ctx->h, aad, aad_len, ct, ct_len, ghash_out);

    uint8_t expected_tag[16];
    for (int j = 0; j < 16; j++) {
        expected_tag[j] = ghash_out[j] ^ tag_mask[j];
    }

    if (memcmp(expected_tag, tag, 16) == 0) {
        return 0; // Success
    }
    return -1; // Authentication failure
}

int aes_gcm_self_test(void) {
    // Test Vector 1: Empty inputs
    uint8_t key[32];
    uint8_t iv[12];
    memset(key, 0, 32);
    memset(iv, 0, 12);

    aes_gcm_ctx_t ctx;
    aes_gcm_init(&ctx, key);

    uint8_t tag1[16];
    aes_gcm_encrypt(&ctx, iv, NULL, 0, NULL, 0, NULL, tag1);

    uint8_t expected_tag1[16] = {
        0x53, 0x0f, 0x8a, 0xfb, 0xc7, 0x45, 0x36, 0xb9,
        0xa9, 0x63, 0xb4, 0xf1, 0xc4, 0xcb, 0x73, 0x8b
    };

    if (memcmp(tag1, expected_tag1, 16) != 0) {
        serial_printf("[GCM-TEST] Tag1 mismatch!\n");
        return -1;
    }

    // Test Vector 2: With plaintext and AAD
    const char *pt2 = "hello world";
    const char *aad2 = "aad";
    size_t pt2_len = 11;
    size_t aad2_len = 3;

    uint8_t ct2[16];
    uint8_t tag2[16];
    aes_gcm_encrypt(&ctx, iv, (const uint8_t *)aad2, aad2_len, (const uint8_t *)pt2, pt2_len, ct2, tag2);

    uint8_t expected_ct2[11] = {
        0xa6, 0xc2, 0x2c, 0x51, 0x22, 0x40, 0x1c, 0x01, 0x75, 0x22, 0xa1
    };
    uint8_t expected_tag2[16] = {
        0x71, 0x21, 0xd5, 0x03, 0xce, 0xe8, 0xd2, 0x8d,
        0x75, 0xdf, 0xa3, 0x43, 0x90, 0xd7, 0x5c, 0xf1
    };

    if (memcmp(ct2, expected_ct2, 11) != 0) {
        serial_printf("[GCM-TEST] CT2 mismatch!\n");
        return -1;
    }
    if (memcmp(tag2, expected_tag2, 16) != 0) {
        serial_printf("[GCM-TEST] Tag2 mismatch!\n");
        return -1;
    }

    // Test Decryption
    uint8_t decrypted[16];
    memset(decrypted, 0, 16);
    int rc = aes_gcm_decrypt(&ctx, iv, (const uint8_t *)aad2, aad2_len, ct2, pt2_len, tag2, decrypted);
    if (rc != 0) {
        serial_printf("[GCM-TEST] Decrypt status error: %d\n", rc);
        return -1;
    }
    if (memcmp(decrypted, pt2, pt2_len) != 0) {
        serial_printf("[GCM-TEST] Decrypted plaintext mismatch!\n");
        return -1;
    }

    // Test Tampered Decryption
    tag2[0] ^= 0x01; // Corrupt tag
    rc = aes_gcm_decrypt(&ctx, iv, (const uint8_t *)aad2, aad2_len, ct2, pt2_len, tag2, decrypted);
    if (rc == 0) {
        serial_printf("[GCM-TEST] Decrypt succeeded on corrupted tag!\n");
        return -1;
    }

    return 0; // All passed
}
