#pragma once

#include <stdint.h>
#include <stdbool.h>

extern bool g_tamper_simulate;

// Verify the integrity of a decrypted sector.
// Returns true if valid, false if tampered/corrupt.
bool integrity_verify_sector(uint32_t lba, const uint8_t *data);
