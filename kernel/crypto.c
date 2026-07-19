#include "crypto.h"
#include "string.h"
#include "aes.h"
#include "xts.h"
#include "serial.h"

/* ── SHA-512 Constants ───────────────────────────────────────────────────── */
static const uint64_t k[80] = {
    0x428a2f98d728ae22ULL, 0x7137449123ef65cdULL, 0xb5c0fbcfec4d3b2fULL, 0xe9b5dba58189dbbcULL,
    0x3956c25bf348b538ULL, 0x59f111f1b605d019ULL, 0x923f82a4af194f9bULL, 0xab1c5ed5da6d8118ULL,
    0xd807aa98a3030242ULL, 0x12835b0145706fbeULL, 0x243185be4ee4b28cULL, 0x550c7dc3d5ffb4e2ULL,
    0x72be5d74f27b896fULL, 0x80deb1fe3b1696b1ULL, 0x9bdc06a725c71235ULL, 0xc19bf174cf692694ULL,
    0xe49b69c19ef14ad2ULL, 0xefbe4786384f25e3ULL, 0x0fc19dc68b8cd5b5ULL, 0x240ca1cc77ac9c65ULL,
    0x2de92c6f592b0275ULL, 0x4a7484aa6ea6e483ULL, 0x5cb0a9dcbd41fbd4ULL, 0x76f988da831153b5ULL,
    0x983e5152ee66dfabULL, 0xa831c66d2db43210ULL, 0xb00327c898fb213fULL, 0xbf597fc7beef0ee4ULL,
    0xc6e00bf33da88fc2ULL, 0xd5a79147930aa725ULL, 0x06ca6351e003826fULL, 0x142929670a0e6e70ULL,
    0x27b70a8546d22ffcULL, 0x2e1b21385c26c926ULL, 0x4d2c6dfc5ac42aedULL, 0x53380d139d95b3dfULL,
    0x650a73548baf63deULL, 0x766a0abb3c77b2a8ULL, 0x81c2c92e47edaee6ULL, 0x92722c851482353bULL,
    0xa2bfe8a14cf10364ULL, 0xa81a664bbc423001ULL, 0xc24b8b70d0f89791ULL, 0xc76c51a30654be30ULL,
    0xd192e819d6ef5218ULL, 0xd69906245565a910ULL, 0xf40e35855771202aULL, 0x106aa07032bbd1b8ULL,
    0x19a4c116b8d2d0c8ULL, 0x1e376c085141ab53ULL, 0x2748774cdf8eeb99ULL, 0x34b0bcb5e19b48a8ULL,
    0x391c0cb3c5c95a63ULL, 0x4ed8aa4ae3418acbULL, 0x5b9cca4f7763e373ULL, 0x682e6ff3d6b2b8a3ULL,
    0x748f82ee5defb2fcULL, 0x78a5636f43172f60ULL, 0x84c87814a1f0ab72ULL, 0x8cc702081a6439ecULL,
    0x90befffa23631e28ULL, 0xa4506cebde82bde9ULL, 0xbef9a3f7b2c67915ULL, 0xc67178f2e372532bULL,
    0xca273eceea26619cULL, 0xd186b8c721c0c207ULL, 0xeada7dd6cde0eb1eULL, 0xf57d4f7fee6ed178ULL,
    0x06f067aa72176fbaULL, 0x0a637dc5a2c898a6ULL, 0x113f9804bef90daeULL, 0x1b710b35131c471bULL,
    0x28db77f523047d84ULL, 0x32caab7b40c72493ULL, 0x3c9ebe0a15c9bebcULL, 0x431d67c49c100d4cULL,
    0x4cc5d4becb3e42b6ULL, 0x597f299cfc657e2aULL, 0x5fcb6fab3ad6faecULL, 0x6c44198c4a475817ULL
};

/* ── SHA-512 Helper Macros ────────────────────────────────────────────────── */
#define ROTR(x, n) (((x) >> (n)) | ((x) << (64 - (n))))
#define Ch(x, y, z) (((x) & (y)) ^ (~(x) & (z)))
#define Maj(x, y, z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))

#define Sigma0(x) (ROTR(x, 28) ^ ROTR(x, 34) ^ ROTR(x, 39))
#define Sigma1(x) (ROTR(x, 14) ^ ROTR(x, 18) ^ ROTR(x, 41))
#define sigma0(x) (ROTR(x, 1) ^ ROTR(x, 8) ^ ((x) >> 7))
#define sigma1(x) (ROTR(x, 19) ^ ROTR(x, 61) ^ ((x) >> 6))

/* ── sha512_transform ────────────────────────────────────────────────────── */
static void sha512_transform(sha512_ctx_t *ctx, const uint8_t *data) {
    uint64_t a, b, c, d, e, f, g, h, t1, t2;
    uint64_t m[80];
    int i, j;

    for (i = 0, j = 0; i < 16; i++, j += 8) {
        m[i] = ((uint64_t)data[j] << 56) |
               ((uint64_t)data[j + 1] << 48) |
               ((uint64_t)data[j + 2] << 40) |
               ((uint64_t)data[j + 3] << 32) |
               ((uint64_t)data[j + 4] << 24) |
               ((uint64_t)data[j + 5] << 16) |
               ((uint64_t)data[j + 6] << 8) |
               ((uint64_t)data[j + 7]);
    }

    for (; i < 80; i++) {
        m[i] = sigma1(m[i - 2]) + m[i - 7] + sigma0(m[i - 15]) + m[i - 16];
    }

    a = ctx->state[0];
    b = ctx->state[1];
    c = ctx->state[2];
    d = ctx->state[3];
    e = ctx->state[4];
    f = ctx->state[5];
    g = ctx->state[6];
    h = ctx->state[7];

    for (i = 0; i < 80; i++) {
        t1 = h + Sigma1(e) + Ch(e, f, g) + k[i] + m[i];
        t2 = Sigma0(a) + Maj(a, b, c);
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
    ctx->state[5] += f;
    ctx->state[6] += g;
    ctx->state[7] += h;
}

/* ── sha512_init ─────────────────────────────────────────────────────────── */
void sha512_init(sha512_ctx_t *ctx) {
    ctx->state[0] = 0x6a09e667f3bcc908ULL;
    ctx->state[1] = 0xbb67ae8584caa73bULL;
    ctx->state[2] = 0x3c6ef372fe94f82bULL;
    ctx->state[3] = 0xa54ff53a5f1d36f1ULL;
    ctx->state[4] = 0x510e527fade682d1ULL;
    ctx->state[5] = 0x9b05688c2b3e6c1fULL;
    ctx->state[6] = 0x1f83d9abfb41bd6bULL;
    ctx->state[7] = 0x5be0cd19137e2179ULL;
    ctx->bitlen = 0;
}

/* ── sha512_update ───────────────────────────────────────────────────────── */
void sha512_update(sha512_ctx_t *ctx, const uint8_t *data, size_t len) {
    size_t i;
    uint32_t active = (uint32_t)((ctx->bitlen >> 3) & 127);
    for (i = 0; i < len; i++) {
        ctx->buffer[active++] = data[i];
        ctx->bitlen += 8;
        if (active == 128) {
            sha512_transform(ctx, ctx->buffer);
            active = 0;
        }
    }
}

/* ── sha512_final ────────────────────────────────────────────────────────── */
void sha512_final(sha512_ctx_t *ctx, uint8_t *digest) {
    uint32_t active = (uint32_t)((ctx->bitlen >> 3) & 127);

    ctx->buffer[active++] = 0x80;

    if (active > 112) {
        while (active < 128) {
            ctx->buffer[active++] = 0x00;
        }
        sha512_transform(ctx, ctx->buffer);
        active = 0;
    }

    while (active < 112) {
        ctx->buffer[active++] = 0x00;
    }

    for (int i = 0; i < 8; i++) {
        ctx->buffer[112 + i] = 0x00;
    }
    uint64_t bits = ctx->bitlen;
    ctx->buffer[120] = (uint8_t)(bits >> 56);
    ctx->buffer[121] = (uint8_t)(bits >> 48);
    ctx->buffer[122] = (uint8_t)(bits >> 40);
    ctx->buffer[123] = (uint8_t)(bits >> 32);
    ctx->buffer[124] = (uint8_t)(bits >> 24);
    ctx->buffer[125] = (uint8_t)(bits >> 16);
    ctx->buffer[126] = (uint8_t)(bits >> 8);
    ctx->buffer[127] = (uint8_t)(bits);

    sha512_transform(ctx, ctx->buffer);

    for (int i = 0; i < 8; i++) {
        digest[i * 8]     = (uint8_t)(ctx->state[i] >> 56);
        digest[i * 8 + 1] = (uint8_t)(ctx->state[i] >> 48);
        digest[i * 8 + 2] = (uint8_t)(ctx->state[i] >> 40);
        digest[i * 8 + 3] = (uint8_t)(ctx->state[i] >> 32);
        digest[i * 8 + 4] = (uint8_t)(ctx->state[i] >> 24);
        digest[i * 8 + 5] = (uint8_t)(ctx->state[i] >> 16);
        digest[i * 8 + 6] = (uint8_t)(ctx->state[i] >> 8);
        digest[i * 8 + 7] = (uint8_t)(ctx->state[i]);
    }
}

/* ── sha512 ──────────────────────────────────────────────────────────────── */
void sha512(const uint8_t *data, size_t len, uint8_t *digest) {
    sha512_ctx_t ctx;
    sha512_init(&ctx);
    sha512_update(&ctx, data, len);
    sha512_final(&ctx, digest);
}

/* ── hmac_sha512 ─────────────────────────────────────────────────────────── */
void hmac_sha512(const uint8_t *key, size_t key_len, const uint8_t *data, size_t data_len, uint8_t *digest) {
    sha512_ctx_t ctx;
    uint8_t k_ipad[SHA512_BLOCK_SIZE];
    uint8_t k_opad[SHA512_BLOCK_SIZE];
    uint8_t key_temp[SHA512_DIGEST_SIZE];
    size_t i;

    if (key_len > SHA512_BLOCK_SIZE) {
        sha512(key, key_len, key_temp);
        key = key_temp;
        key_len = SHA512_DIGEST_SIZE;
    }

    memset(k_ipad, 0, SHA512_BLOCK_SIZE);
    memset(k_opad, 0, SHA512_BLOCK_SIZE);
    memcpy(k_ipad, key, key_len);
    memcpy(k_opad, key, key_len);

    for (i = 0; i < SHA512_BLOCK_SIZE; i++) {
        k_ipad[i] ^= 0x36;
        k_opad[i] ^= 0x5C;
    }

    sha512_init(&ctx);
    sha512_update(&ctx, k_ipad, SHA512_BLOCK_SIZE);
    sha512_update(&ctx, data, data_len);
    sha512_final(&ctx, digest);

    sha512_init(&ctx);
    sha512_update(&ctx, k_opad, SHA512_BLOCK_SIZE);
    sha512_update(&ctx, digest, SHA512_DIGEST_SIZE);
    sha512_final(&ctx, digest);
}

/* ── hkdf_sha512_extract ──────────────────────────────────────────────────── */
void hkdf_sha512_extract(const uint8_t *salt, size_t salt_len, const uint8_t *ikm, size_t ikm_len, uint8_t *prk) {
    uint8_t zero_salt[SHA512_DIGEST_SIZE];
    if (salt == NULL || salt_len == 0) {
        memset(zero_salt, 0, SHA512_DIGEST_SIZE);
        salt = zero_salt;
        salt_len = SHA512_DIGEST_SIZE;
    }
    hmac_sha512(salt, salt_len, ikm, ikm_len, prk);
}

/* ── hkdf_sha512_expand ───────────────────────────────────────────────────── */
void hkdf_sha512_expand(const uint8_t *prk, const uint8_t *info, size_t info_len, uint8_t *okm, size_t okm_len) {
    uint8_t t[SHA512_DIGEST_SIZE];
    size_t hash_len = SHA512_DIGEST_SIZE;
    size_t N = (okm_len + hash_len - 1) / hash_len;
    size_t okm_offset = 0;
    uint8_t temp[SHA512_DIGEST_SIZE + 128 + 1];

    for (size_t i = 1; i <= N; i++) {
        size_t temp_len = 0;
        if (i > 1) {
            memcpy(temp, t, hash_len);
            temp_len += hash_len;
        }
        if (info && info_len > 0) {
            size_t copy_len = (info_len > 128) ? 128 : info_len;
            memcpy(temp + temp_len, info, copy_len);
            temp_len += copy_len;
        }
        temp[temp_len] = (uint8_t)i;
        temp_len += 1;

        hmac_sha512(prk, hash_len, temp, temp_len, t);

        size_t copy_bytes = hash_len;
        if (okm_offset + copy_bytes > okm_len) {
            copy_bytes = okm_len - okm_offset;
        }
        memcpy(okm + okm_offset, t, copy_bytes);
        okm_offset += copy_bytes;
    }
}

bool crypto_self_test(void) {
    serial_printf("[CRYPTO-TEST] Running cryptographic self-tests...\n");

    // 1. AES-256 Block Cipher Test Vector (FIPS 197)
    uint8_t aes_key[32];
    memset(aes_key, 0, 32);
    uint8_t plaintext[16];
    memset(plaintext, 0, 16);
    uint8_t ciphertext[16];
    
    aes256_ctx_t aes_ctx;
    aes256_init_encrypt(&aes_ctx, aes_key);
    aes256_encrypt_block(&aes_ctx, plaintext, ciphertext);

    uint8_t expected_cipher[16] = {
        0xdc, 0x95, 0xc0, 0x78, 0xa2, 0x40, 0x89, 0x89,
        0xad, 0x48, 0xa2, 0x14, 0x92, 0x84, 0x20, 0x87
    };

    if (memcmp(ciphertext, expected_cipher, 16) != 0) {
        serial_printf("[CRYPTO-TEST] AES-256 encrypt FAILED!\n");
        serial_printf("[CRYPTO-TEST] Expected: ");
        for (int i = 0; i < 16; i++) serial_printf("%02x ", expected_cipher[i]);
        serial_printf("\n[CRYPTO-TEST] Got:      ");
        for (int i = 0; i < 16; i++) serial_printf("%02x ", ciphertext[i]);
        serial_printf("\n");
        return false;
    }

    uint8_t decrypted[16];
    aes256_init_decrypt(&aes_ctx, aes_key);
    aes256_decrypt_block(&aes_ctx, ciphertext, decrypted);

    if (memcmp(decrypted, plaintext, 16) != 0) {
        serial_printf("[CRYPTO-TEST] AES-256 decrypt FAILED!\n");
        return false;
    }
    serial_printf("[CRYPTO-TEST] AES-256 Block Cipher: PASSED\n");

    // 2. AES-256-XTS Mode Sector Round-trip Test
    uint8_t xts_key[64];
    memset(xts_key, 0x55, 64);
    
    aes256_xts_ctx_t xts_ctx;
    aes256_xts_init(&xts_ctx, xts_key);

    uint8_t sector_in[512];
    uint8_t sector_enc[512];
    uint8_t sector_dec[512];

    for (int i = 0; i < 512; i++) {
        sector_in[i] = (uint8_t)(i & 0xFF);
    }

    aes256_xts_encrypt_sector(&xts_ctx, 12345, sector_in, sector_enc);
    aes256_xts_decrypt_sector(&xts_ctx, 12345, sector_enc, sector_dec);

    if (memcmp(sector_in, sector_dec, 512) != 0) {
        serial_printf("[CRYPTO-TEST] AES-256-XTS sector round-trip FAILED!\n");
        return false;
    }

    if (memcmp(sector_in, sector_enc, 512) == 0) {
        serial_printf("[CRYPTO-TEST] AES-256-XTS encrypt did not alter sector data!\n");
        return false;
    }
    serial_printf("[CRYPTO-TEST] AES-256-XTS Mode: PASSED\n");

    serial_printf("[CRYPTO-TEST] All cryptographic self-tests PASSED.\n");
    return true;
}
