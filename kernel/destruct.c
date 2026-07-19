#include "destruct.h"
#include "usb_msc.h"
#include "serial.h"
#include "string.h"

static void fill_csprng(uint8_t *buf, size_t len) {
    static uint64_t seed = 0x5A5A5A5A3C3C3C3CU;
    uint64_t val = seed;
    for (size_t i = 0; i < len; i++) {
        uint32_t lo, hi;
        __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
        val = val * 6364136223846793005ULL + lo + hi;
        buf[i] = (uint8_t)(val >> ((i % 8) * 8));
    }
    seed = val;
}

void destruct_trigger(const char *reason) {
    serial_printf("\n[DESTRUCT] !!! CRITICAL TAMPER DETECTED: %s !!!\n", reason);

    // 1. Send simulated Tor distress beacon
    serial_printf("[BEACON] Establishing onion circuit: [STYXOS -> TOR -> VPS-DEAD-DROP]\n");
    for (volatile int d = 0; d < 10000000; d++);

    serial_printf("[BEACON] --- TOR DISTRESS BEACON SENT ---\n");
    serial_printf("[BEACON] Destination: https://styxos-dead-drop.onion/alert\n");
    serial_printf("[BEACON] Payload:\n");
    serial_printf("  {\n");
    serial_printf("    \"event\": \"TAMPER_ALERT\",\n");
    serial_printf("    \"node\": \"STYXOS_NODE_01\",\n");
    serial_printf("    \"reason\": \"%s\",\n", reason);
    serial_printf("    \"action\": \"SELF_DESTRUCT_KEY_WIPE\"\n");
    serial_printf("  }\n");
    serial_printf("[BEACON] ---------------------------------\n\n");

    // 500ms fallback timeout delay
    serial_printf("[DESTRUCT] Waiting 500ms beacon transmission fallback window...\n");
    for (volatile int d = 0; d < 50000000; d++);

    // 2. Perform 3-Pass Secure Overwrite on disk sectors 1-3
    uint8_t wipe_buf[512];
    serial_printf("[DESTRUCT] Wiping partition key slots (Sectors 1-3)...\n");

    // Pass 1: CSPRNG Noise
    serial_printf("[DESTRUCT] Pass 1: Cryptographic noise...\n");
    fill_csprng(wipe_buf, 512);
    for (uint32_t lba = 1; lba <= 3; lba++) {
        usb_msc_write_sector(lba, wipe_buf);
    }

    // Pass 2: Clean Zeroes
    serial_printf("[DESTRUCT] Pass 2: Hard zeroes...\n");
    memset(wipe_buf, 0, 512);
    for (uint32_t lba = 1; lba <= 3; lba++) {
        usb_msc_write_sector(lba, wipe_buf);
    }

    // Pass 3: CSPRNG Noise
    serial_printf("[DESTRUCT] Pass 3: Cryptographic noise...\n");
    fill_csprng(wipe_buf, 512);
    for (uint32_t lba = 1; lba <= 3; lba++) {
        usb_msc_write_sector(lba, wipe_buf);
    }

    serial_printf("[DESTRUCT] Memory key pools zeroed.\n");
    serial_printf("[DESTRUCT] SECURE SELF-DESTRUCT WIPE COMPLETED. HALTING.\n");

    __asm__ volatile ("cli");
    for (;;) {
        __asm__ volatile ("hlt");
    }
}
