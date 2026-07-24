/*
 * padding.c — Traffic padding module generating real UDP noise packets
 */
#include "padding.h"
#include "e1000.h"
#include "net_proto.h"
#include "anonymize.h"
#include "csprng.h"
#include "serial.h"
#include "string.h"

uint32_t g_padding_packets_sent = 0;
bool     g_padding_boost_active = false;
static uint64_t g_last_padding_ticks = 0;

void padding_init(void) {
    g_padding_packets_sent = 0;
    g_padding_boost_active = false;
    g_last_padding_ticks = 0;
    serial_printf("[PADDING] Real UDP Traffic Constant-Rate Padding module initialized.\n");
}

void padding_boost(bool boost) {
    g_padding_boost_active = boost;
    if (boost) {
        serial_printf("[PADDING] Traffic injection BOOST ENABLED (Masking Circuit Handshake Spike).\n");
    } else {
        serial_printf("[PADDING] Traffic injection BOOST DISABLED (Normal Constant Rate).\n");
    }
}

bool padding_inject_noise(void) {
    uint32_t len = 64 + (csprng_u32() % 193);
    uint8_t payload[256];
    csprng_get_bytes(payload, len);

    uint8_t frame[NET_MAX_FRAME];
    uint8_t src_mac[6];
    anonymize_get_mac(src_mac);
    static const uint8_t dst_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

    uint16_t src_port = 10000 + (csprng_u32() % 5000);
    uint16_t dst_port = 20000 + (csprng_u32() % 5000);

    size_t frame_len = net_build_udp(src_mac, dst_mac,
                                     0xC0A80164U, 0xFFFFFFFFU,
                                     src_port, dst_port,
                                     payload, len, frame);

    if (frame_len > 0 && e1000_send_packet(frame, (uint16_t)frame_len)) {
        g_padding_packets_sent++;
        return true;
    }
    return false;
}


void padding_tick(uint64_t ticks) {
    if (!g_e1000_active) return;

    // Interval: 20 ticks (200ms) normally, or 5 ticks (50ms) during boost
    uint64_t interval = g_padding_boost_active ? 5 : 20;


    if (ticks - g_last_padding_ticks >= interval) {
        g_last_padding_ticks = ticks;

        // Generate random length between 64 and 256 bytes
        uint32_t len = 64 + (csprng_u32() % 193);
        uint8_t payload[256];
        csprng_get_bytes(payload, len);

        uint8_t frame[NET_MAX_FRAME];
        uint8_t src_mac[6];
        anonymize_get_mac(src_mac);
        static const uint8_t dst_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

        uint16_t src_port = 10000 + (csprng_u32() % 5000);
        uint16_t dst_port = 20000 + (csprng_u32() % 5000);

        size_t frame_len = net_build_udp(src_mac, dst_mac,
                                         0xC0A80164U, 0xFFFFFFFFU,
                                         src_port, dst_port,
                                         payload, len, frame);

        if (frame_len > 0 && e1000_send_packet(frame, (uint16_t)frame_len)) {
            g_padding_packets_sent++;
            if (g_padding_packets_sent % 50 == 0) {
                serial_printf("[PADDING] Injected %d real UDP noise packets over e1000\n",
                              g_padding_packets_sent);
            }
        }
    }
}
