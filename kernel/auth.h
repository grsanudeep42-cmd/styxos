#pragma once

#include <stdbool.h>
#include <stdint.h>

// Launch the pre-boot authentication UI.
// Disables interrupts, prompts for password and FIDO2 touch/emulation,
// derives the master key via HKDF-SHA-512, and verifies it.
// If verification fails 3 times, permanently locks out and halts.
// Returns true if authenticated successfully.
bool auth_preboot(void);

// 64-byte derived key — exported for snapshot_set_key() after auth success.
// Zeroed by snapshot_set_key() after use to limit key exposure.
extern uint8_t g_auth_derived_key[64];
