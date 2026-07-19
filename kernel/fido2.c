#include "fido2.h"
#include "usb_hid.h"
#include "crypto.h"
#include "serial.h"
#include "string.h"

// Hardcoded mock private credential key for software emulation
static const uint8_t g_mock_private_key[64] = {
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10,
    0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
    0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F, 0x20,
    0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28,
    0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F, 0x30,
    0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38,
    0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F, 0x40
};

/* ── fido2_device_detect ─────────────────────────────────────────────────── */
bool fido2_device_detect(void) {
    return usb_hid_init();
}

/* ── fido2_get_assertion ─────────────────────────────────────────────────── */
int fido2_get_assertion(const uint8_t *challenge, const char *pin, uint8_t *secret_out) {
    if (!fido2_device_detect()) {
        serial_printf("[FIDO2] ERROR: No physical FIDO2 key detected\n");
        return -1;
    }
    
    // In a physical environment, we would build the CTAP2 CBOR payload:
    // 1. Send CTAP_INIT to negotiate channel ID.
    // 2. Build authenticatorGetAssertion CBOR command with:
    //    - rpId = "styxos.io"
    //    - clientDataHash = challenge (32 bytes)
    //    - option hmac-secret = true
    //    - pinAuth (derived if pin is provided)
    // 3. Loop reading reports from USB-HID until key is touched (response status = 0).
    // 4. Extract assertion signature and derive master entropy.
    
    (void)challenge;
    (void)pin;
    (void)secret_out;
    return -2; // Not implemented on physical mock
}

/* ── fido2_emulate_assertion ─────────────────────────────────────────────── */
int fido2_emulate_assertion(const uint8_t *challenge, const char *pin, uint8_t *secret_out) {
    serial_printf("[FIDO2] (EMU) Emulating CTAP2 assertion request...\n");
    
    // Simulate slight processing delay
    for (volatile int d = 0; d < 20000000; d++);
    
    // We derive the FIDO2 secret by computing HMAC-SHA-512 over the challenge
    // using our mock private key. This mimics the cryptographic binding of FIDO2 signatures.
    uint8_t input_buf[128];
    memset(input_buf, 0, 128);
    memcpy(input_buf, challenge, FIDO2_CHALLENGE_SIZE);
    
    size_t input_len = FIDO2_CHALLENGE_SIZE;
    if (pin && strlen(pin) > 0) {
        size_t pin_len = strlen(pin);
        if (pin_len > 64) pin_len = 64;
        memcpy(input_buf + FIDO2_CHALLENGE_SIZE, pin, pin_len);
        input_len += pin_len;
    }
    
    hmac_sha512(g_mock_private_key, 64, input_buf, input_len, secret_out);
    
    serial_printf("[FIDO2] (EMU) Assertion successfully generated.\n");
    return 0;
}
