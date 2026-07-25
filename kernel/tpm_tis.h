#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* tpm_tis.h — TPM 2.0 TIS (TPM Interface Specification) MMIO driver
 *
 * Works with QEMU swtpm emulator:
 *   qemu-system-x86_64 ... \
 *     -chardev socket,id=chrtpm,path=/tmp/swtpm-sock \
 *     -tpmdev emulator,id=tpm0,chardev=chrtpm \
 *     -device tpm-tis,tpmdev=tpm0
 *
 * Locality 0 is used exclusively (pre-OS environment). */

/* TIS register offsets (base = 0xFED40000, locality 0) */
#define TIS_BASE          0xFED40000UL
#define TIS_ACCESS        0x0000   /* TPM_ACCESS_x */
#define TIS_INT_ENABLE    0x0008
#define TIS_STS           0x0018   /* TPM_STS_x */
#define TIS_DATA_FIFO     0x0024   /* TPM_DATA_FIFO_x */
#define TIS_INTF_CAPS     0x0030
#define TIS_VENDOR_ID     0x0F00

/* TPM_ACCESS bits */
#define TIS_ACCESS_VALID           (1 << 7)
#define TIS_ACCESS_ACTIVE_LOCALITY (1 << 5)
#define TIS_ACCESS_REQUEST_USE     (1 << 1)
#define TIS_ACCESS_ESTABLISHMENT   (1 << 0)

/* TPM_STS bits */
#define TIS_STS_VALID         (1 << 7)
#define TIS_STS_CMD_READY     (1 << 6)
#define TIS_STS_GO            (1 << 5)
#define TIS_STS_DATA_AVAIL    (1 << 4)
#define TIS_STS_EXPECT        (1 << 3)

/* TPM2 Command Codes */
#define TPM2_CC_PCR_EXTEND   0x00000182U
#define TPM2_CC_PCR_READ     0x0000017EU
#define TPM2_CC_GET_RANDOM   0x0000017BU
#define TPM2_CC_STARTUP      0x00000144U
#define TPM2_CC_SELF_TEST    0x00000143U
#define TPM2_CC_QUOTE        0x00000158U


/* TPM2 Response codes */
#define TPM2_RC_SUCCESS  0x00000000U

/* Handle types */
#define TPM2_RH_PLATFORM  0x4000000CU

bool tpm_tis_init(void);
bool tpm_tis_send_cmd(const uint8_t *cmd, size_t cmd_len,
                      uint8_t *resp, size_t *resp_len);

/* High-level helpers built on tpm_tis_send_cmd */
bool tpm2_startup(void);
bool tpm2_self_test(void);
bool tpm2_pcr_extend_real(uint32_t pcr_index,
                           const uint8_t sha256_digest[32]);
bool tpm2_pcr_read_real(uint32_t pcr_index, uint8_t digest_out[32]);
bool tpm2_get_random(uint8_t *out, size_t len);
bool tpm2_quote_real(uint32_t pcr_mask, const uint8_t nonce[32],
                     uint8_t quote_out[64], uint8_t sig_out[64]);

