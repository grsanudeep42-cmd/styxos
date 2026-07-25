/*
 * destruct.c — Tamper-response self-destruct sequence
 *
 * Sequence (hardened):
 *   1. Send real UDP distress beacon via e1000 (net_proto.c)
 *   2. 500ms transmission window
 *   3. 3-pass secure overwrite of key sectors (CSPRNG → zero → CSPRNG)
 *   4. Zero in-RAM key material
 *   5. cli + hlt
 */
#include "destruct.h"
#include "net_proto.h"
#include "usb_msc.h"
#include "csprng.h"
#include "string.h"
#include "serial.h"
#include <stdint.h>

/* ── 3-pass secure sector wipe ──────────────────────────────────────────── */
static void wipe_sectors(uint32_t start_lba, uint32_t count) {
    uint8_t buf[512];

    serial_printf("[DESTRUCT] Wiping LBA %d-%d (%d sectors, 3 passes)...\n",
                  start_lba, start_lba + count - 1, count);

    /* Pass 1: CSPRNG noise */
    serial_printf("[DESTRUCT] Pass 1: CSPRNG noise\n");
    for (uint32_t lba = start_lba; lba < start_lba + count; lba++) {
        csprng_get_bytes(buf, 512);
        usb_msc_write_sector(lba, buf);
    }

    /* Pass 2: Zeroes */
    serial_printf("[DESTRUCT] Pass 2: Zeroes\n");
    memset(buf, 0x00, 512);
    for (uint32_t lba = start_lba; lba < start_lba + count; lba++) {
        usb_msc_write_sector(lba, buf);
    }

    /* Pass 3: CSPRNG noise again */
    serial_printf("[DESTRUCT] Pass 3: CSPRNG noise\n");
    for (uint32_t lba = start_lba; lba < start_lba + count; lba++) {
        csprng_get_bytes(buf, 512);
        usb_msc_write_sector(lba, buf);
    }
}

/* ── destruct_trigger ───────────────────────────────────────────────────── */
void destruct_trigger(const char *reason) {
    serial_printf("\n[DESTRUCT] !!! CRITICAL TAMPER: %s !!!\n", reason ? reason : "unknown");

    /* 1. Send real UDP distress beacon before wiping */
    serial_printf("[DESTRUCT] Transmitting distress beacon...\n");
    bool beacon_sent = net_send_beacon(reason);
    if (beacon_sent) {
        serial_printf("[DESTRUCT] Beacon transmitted. Waiting 500ms for propagation...\n");
    } else {
        serial_printf("[DESTRUCT] Beacon failed (no NIC or network). Proceeding to wipe.\n");
    }

    /* 500ms window: spin-wait */
    for (volatile int d = 0; d < 500000; d++) __asm__ volatile("pause");


    /* 2. Disable XTS/GCM to write raw wipe data to USB */
    bool saved_enc = g_encryption_enabled;
    g_encryption_enabled = false;

    /* Wipe key material zone: sectors 1-9 (boot, counter, merkle root, key slots) */
    wipe_sectors(1, 9);

    /* Wipe ORAM region header: sector 500 */
    wipe_sectors(500, 1);

    g_encryption_enabled = saved_enc;

    /* 3. Zero in-RAM key material */
    extern uint8_t g_auth_derived_key[64];
    memset(g_auth_derived_key, 0, 64);

    serial_printf("[DESTRUCT] All key material wiped. System halted.\n");

    __asm__ volatile("cli");
    for (;;) __asm__ volatile("hlt");
}
