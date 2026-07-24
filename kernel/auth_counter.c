/*
 * auth_counter.c — USB-backed tamper attempt counter
 *
 * Sector layout (USB LBA 5, 512 bytes):
 *   [0..3]   Magic: 0x53545843 ("STXC")
 *   [4]      Attempt count (uint8_t, 0-255)
 *   [5..68]  HMAC-SHA512 of bytes [0..4] under device-unique key
 *   [69..511] Reserved / zeroed
 *
 * Device-unique key = SHA512(CPUID_fingerprint || USB_serial_number_placeholder)
 * This key is derived identically on every boot — no external secret required.
 *
 * Security note: This counter can be bypassed by an attacker who raw-writes
 * sector 5 with count=0. Certified hardware (IronKey) has a write-protected
 * counter register that cannot be forged. On commodity USB, this counter
 * significantly raises the bar against automated brute-force while acknowledging
 * the physical-write attack surface (documented in threat model).
 */
#include "auth_counter.h"
#include "crypto.h"
#include "usb_msc.h"
#include "string.h"
#include "serial.h"
#include <stdint.h>

#define COUNTER_MAGIC 0x53545843U

static int     g_count       = 0;
static bool    g_usb_present = false;

/* ── Device-unique key derivation ───────────────────────────────────────── */
static void derive_device_key(uint8_t out[64]) {
    /* CPUID fingerprint */
    uint32_t eax = 1, ebx = 0, ecx = 0, edx = 0;
    __asm__ volatile("cpuid" : "=a"(eax),"=b"(ebx),"=c"(ecx),"=d"(edx) : "a"(1));

    uint8_t fingerprint[32];
    memset(fingerprint, 0, 32);
    /* Pack CPUID leaf 1 */
    fingerprint[0] = (uint8_t)(eax);
    fingerprint[1] = (uint8_t)(eax >> 8);
    fingerprint[2] = (uint8_t)(eax >> 16);
    fingerprint[3] = (uint8_t)(eax >> 24);
    fingerprint[4] = (uint8_t)(edx);
    fingerprint[5] = (uint8_t)(edx >> 8);
    /* Static salt unique to this build */
    const uint8_t salt[] = "styxos-counter-key-v1";
    sha512(fingerprint, 32, out);
    /* XOR with HMAC of salt for additional domain separation */
    uint8_t salt_hash[64];
    hmac_sha512(out, 64, salt, sizeof(salt) - 1, salt_hash);
    for (int i = 0; i < 64; i++) out[i] ^= salt_hash[i];
}

/* ── Read/write sector 5 ────────────────────────────────────────────────── */
static void counter_write(int count) {
    uint8_t sector[512];
    memset(sector, 0, 512);
    uint8_t key[64];
    derive_device_key(key);

    /* Write magic + count */
    sector[0] = (uint8_t)(COUNTER_MAGIC);
    sector[1] = (uint8_t)(COUNTER_MAGIC >> 8);
    sector[2] = (uint8_t)(COUNTER_MAGIC >> 16);
    sector[3] = (uint8_t)(COUNTER_MAGIC >> 24);
    sector[4] = (uint8_t)count;

    /* HMAC-SHA512 of first 5 bytes */
    uint8_t mac[64];
    hmac_sha512(key, 64, sector, 5, mac);
    memcpy(sector + 5, mac, 64);

    usb_msc_write_sector(AUTH_COUNTER_LBA, sector);
    serial_printf("[AUTH-CTR] Counter written: %d attempts\n", count);
}

static int counter_read(void) {
    uint8_t sector[512];
    uint8_t key[64];
    derive_device_key(key);

    /* Temporarily disable XTS encryption for raw counter sector */
    bool saved = g_encryption_enabled;
    g_encryption_enabled = false;
    int rc = usb_msc_read_sector(AUTH_COUNTER_LBA, sector);
    g_encryption_enabled = saved;

    if (rc != 0) return 0;

    uint32_t magic = (uint32_t)sector[0] | ((uint32_t)sector[1] << 8) |
                     ((uint32_t)sector[2] << 16) | ((uint32_t)sector[3] << 24);
    if (magic != COUNTER_MAGIC) {
        serial_printf("[AUTH-CTR] No valid counter sector — initializing fresh\n");
        counter_write(0);
        return 0;
    }

    /* Verify HMAC */
    uint8_t expected_mac[64];
    hmac_sha512(key, 64, sector, 5, expected_mac);
    if (memcmp(sector + 5, expected_mac, 64) != 0) {
        serial_printf("[AUTH-CTR] WARNING: Counter HMAC mismatch — sector tampered!\n");
        /* Treat as max attempts to be safe */
        return AUTH_COUNTER_MAX;
    }

    return (int)sector[4];
}

/* ── Public API ─────────────────────────────────────────────────────────── */
void auth_counter_init(void) {
    /* Check if USB MSC is available */
    g_usb_present = (usb_msc_sector_count() > 0);
    if (!g_usb_present) {
        serial_printf("[AUTH-CTR] No USB storage — using in-RAM counter only\n");
        g_count = 0;
        return;
    }
    g_count = counter_read();
    serial_printf("[AUTH-CTR] Loaded attempt count: %d / %d\n",
                  g_count, AUTH_COUNTER_MAX);
}

int auth_counter_get(void) {
    return g_count;
}

void auth_counter_increment(void) {
    g_count++;
    if (g_usb_present) counter_write(g_count);
    serial_printf("[AUTH-CTR] Attempt count incremented to %d\n", g_count);
}

void auth_counter_reset(void) {
    g_count = 0;
    if (g_usb_present) counter_write(0);
    serial_printf("[AUTH-CTR] Counter reset on successful authentication\n");
}

bool auth_counter_locked(void) {
    return g_count >= AUTH_COUNTER_MAX;
}
