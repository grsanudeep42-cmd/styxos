/*
 * integrity.c — Sector integrity using Merkle tree
 * Replaces stub (always-true). Now delegates to merkle_verify().
 */
#include "integrity.h"
#include "merkle.h"
#include "serial.h"

bool g_tamper_simulate = false;


bool integrity_verify_sector(uint32_t lba, const uint8_t *data) {
    if (!data) return false;

    bool ok = merkle_verify(lba, data);
    if (!ok) {
        serial_printf("[INTEGRITY] TAMPER DETECTED at LBA %d — Merkle hash mismatch\n", lba);
    }
    return ok;
}
