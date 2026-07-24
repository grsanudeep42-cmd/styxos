#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/*
 * pqc.h — StyxOS Post-Quantum Cryptography (PQC) Subsystem
 * Implements NIST Module-Lattice-Based Key Encapsulation (Kyber-1024)
 * and Digital Signatures (Dilithium-5) for freestanding kernel/user execution.
 */

#define KYBER_1024_PUBLICKEY_BYTES  1568
#define KYBER_1024_SECRETKEY_BYTES  3168
#define KYBER_1024_CIPHERTEXT_BYTES 1568
#define KYBER_1024_SS_BYTES         32

#define DILITHIUM_5_PUBLICKEY_BYTES 2592
#define DILITHIUM_5_SECRETKEY_BYTES 4896
#define DILITHIUM_5_SIG_BYTES       4595

/* Kyber-1024 API */
typedef struct {
    uint8_t pk[KYBER_1024_PUBLICKEY_BYTES];
    uint8_t sk[KYBER_1024_SECRETKEY_BYTES];
} kyber1024_keypair_t;

int kyber1024_keypair(kyber1024_keypair_t *kp);
int kyber1024_encapsulate(uint8_t ct[KYBER_1024_CIPHERTEXT_BYTES],
                          uint8_t ss[KYBER_1024_SS_BYTES],
                          const uint8_t pk[KYBER_1024_PUBLICKEY_BYTES]);
int kyber1024_decapsulate(uint8_t ss[KYBER_1024_SS_BYTES],
                          const uint8_t ct[KYBER_1024_CIPHERTEXT_BYTES],
                          const uint8_t sk[KYBER_1024_SECRETKEY_BYTES]);

/* Dilithium-5 API */
typedef struct {
    uint8_t pk[DILITHIUM_5_PUBLICKEY_BYTES];
    uint8_t sk[DILITHIUM_5_SECRETKEY_BYTES];
} dilithium5_keypair_t;

int dilithium5_keypair(dilithium5_keypair_t *kp);
int dilithium5_sign(uint8_t sig[DILITHIUM_5_SIG_BYTES],
                    size_t *sig_len,
                    const uint8_t *msg,
                    size_t msg_len,
                    const uint8_t sk[DILITHIUM_5_SECRETKEY_BYTES]);
int dilithium5_verify(const uint8_t sig[DILITHIUM_5_SIG_BYTES],
                      size_t sig_len,
                      const uint8_t *msg,
                      size_t msg_len,
                      const uint8_t pk[DILITHIUM_5_PUBLICKEY_BYTES]);

/* Boot Self-Tests */
bool pqc_selftest(void);
