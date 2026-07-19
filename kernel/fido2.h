#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define FIDO2_CHALLENGE_SIZE   32
#define FIDO2_SECRET_SIZE      64

// Returns true if a physical USB FIDO2 key is detected
bool fido2_device_detect(void);

// Perform FIDO2 assertion via physical USB FIDO2 device
// Returns 0 on success, negative on error.
int fido2_get_assertion(const uint8_t *challenge, const char *pin, uint8_t *secret_out);

// Perform emulated FIDO2 assertion (mocking CTAP2 touch & signature)
int fido2_emulate_assertion(const uint8_t *challenge, const char *pin, uint8_t *secret_out);
