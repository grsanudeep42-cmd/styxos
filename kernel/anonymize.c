#include "anonymize.h"
#include "string.h"
#include "serial.h"

uint8_t g_anonymized_mac[6] = {0};

/* ── High-Entropy Random Generator ───────────────────────────────────────── */
static uint32_t anonymize_rand(void) {
    static uint64_t seed = 0x8543219876543210ULL;
    uint64_t r = 0;
    __asm__ volatile("rdtsc" : "=A"(r));
    seed = seed * 6364136223846793005ULL + r + 1442695040888963407ULL;
    return (uint32_t)(seed >> 32);
}

void anonymize_init(void) {
    // Generate 6 random bytes
    uint32_t r1 = anonymize_rand();
    uint32_t r2 = anonymize_rand();

    g_anonymized_mac[0] = (uint8_t)(r1 & 0xFF);
    g_anonymized_mac[1] = (uint8_t)((r1 >> 8) & 0xFF);
    g_anonymized_mac[2] = (uint8_t)((r1 >> 16) & 0xFF);
    g_anonymized_mac[3] = (uint8_t)((r1 >> 24) & 0xFF);
    g_anonymized_mac[4] = (uint8_t)(r2 & 0xFF);
    g_anonymized_mac[5] = (uint8_t)((r2 >> 8) & 0xFF);

    // Enforce Locally Administered Unicast MAC:
    // Bit 0 of byte 0 = 0 (Unicast)
    // Bit 1 of byte 0 = 1 (Locally Administered)
    g_anonymized_mac[0] = (g_anonymized_mac[0] & 0xFE) | 0x02;

    serial_printf("[ANONYMIZE] Randomized MAC Address: %x:%x:%x:%x:%x:%x (Locally Administered Unicast)\n",
                  g_anonymized_mac[0], g_anonymized_mac[1], g_anonymized_mac[2],
                  g_anonymized_mac[3], g_anonymized_mac[4], g_anonymized_mac[5]);
}

void anonymize_get_mac(uint8_t mac_out[6]) {
    if (!mac_out) return;
    memcpy(mac_out, g_anonymized_mac, 6);
}

void anonymize_spoof_cpuid(uint32_t leaf, uint32_t *eax, uint32_t *ebx, uint32_t *ecx, uint32_t *edx) {
    if (!eax || !ebx || !ecx || !edx) return;

    if (leaf == 0) {
        // Vendor String: "StyxOS Workstation" -> 12 bytes
        // EBX: "Styx", EDX: "OS  ", ECX: "CPU "
        *eax = 1;
        *ebx = 0x78797453; // "Styx"
        *edx = 0x2020534F; // "OS  "
        *ecx = 0x20555043; // "CPU "
    } else if (leaf == 1) {
        // Generic family/model, mask hypervisor bit in ECX (bit 31)
        *eax = 0x000306C3; // Generic Core i7 Haswell family signature
        *ebx = 0x00020800;
        *ecx = 0x7ED83203 & ~(1U << 31); // Strip Hypervisor bit 31
        *edx = 0xBFEBFBFF;
    }
}
