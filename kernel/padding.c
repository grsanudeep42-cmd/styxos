#include "padding.h"
#include "e1000.h"
#include "anonymize.h"
#include "string.h"
#include "serial.h"

uint32_t g_padding_packets_sent = 0;
bool     g_padding_boost_active = false;

/* ── LCG Random Generator ───────────────────────────────────────────────── */
static uint32_t padding_rand(void) {
    static uint64_t seed = 0x9876543210FEDCBAULL;
    uint64_t r = 0;
    __asm__ volatile("rdtsc" : "=A"(r));
    seed = seed * 6364136223846793005ULL + r + 1442695040888963407ULL;
    return (uint32_t)(seed >> 32);
}

void padding_init(void) {
    g_padding_packets_sent = 0;
    g_padding_boost_active = false;
    serial_printf("[PADDING] Continuous Traffic Padding Layer initialized (50 Kbps baseline stream).\n");
}

bool padding_inject_noise(void) {
    if (!g_e1000_active) return false;

    uint8_t frame[512];
    memset(frame, 0, sizeof(frame));

    // 1. Destination MAC: Broadcast / Random multicast
    memset(frame, 0xFF, 6);

    // 2. Source MAC: Current randomized hardware MAC
    anonymize_get_mac(&frame[6]);

    // 3. EtherType: 0x88B5 (IEEE 802 Local Experimental)
    frame[12] = 0x88;
    frame[13] = 0xB5;

    // 4. Fill payload with high-entropy synthetic noise
    for (size_t i = 14; i < sizeof(frame); i += 4) {
        uint32_t r = padding_rand();
        memcpy(&frame[i], &r, 4);
    }

    // 5. Transmit packet via e1000 PCI network interface
    if (e1000_send_packet(frame, sizeof(frame))) {
        g_padding_packets_sent++;
        return true;
    }
    return false;
}

void padding_boost(bool active) {
    g_padding_boost_active = active;
    if (active) {
        serial_printf("[PADDING] Noise injection rate BOOSTED (Tor Circuit Rotation active).\n");
    } else {
        serial_printf("[PADDING] Noise injection rate returned to baseline 50 Kbps.\n");
    }
}
