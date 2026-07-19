#pragma once

#include <stdbool.h>

// Launch the pre-boot authentication UI.
// Disables interrupts, prompts for password and FIDO2 touch/emulation,
// derives the master key via HKDF-SHA-512, and verifies it.
// If verification fails 3 times, permanently locks out and halts.
// Returns true if authenticated successfully.
bool auth_preboot(void);
