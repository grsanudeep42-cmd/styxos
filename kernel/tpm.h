#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define TPM_MMIO_BASE      0xFED40000
#define TPM_PCR_COUNT      24
#define SHA256_DIGEST_SIZE 32

typedef struct {
    uint8_t pcr[TPM_PCR_COUNT][SHA256_DIGEST_SIZE];
    bool initialized;
} tpm2_state_t;

extern tpm2_state_t g_tpm2_state;

void tpm2_init(void);
bool tpm2_pcr_extend(uint32_t pcr_index, const uint8_t *data, size_t len);
void tpm2_get_pcr(uint32_t pcr_index, uint8_t *out_digest);
bool tpm2_verify_attestation(void);
