/*
 * net_proto.c — Ethernet / IPv4 / UDP frame builder
 * Constructs real wire-format frames for e1000_send_packet().
 */
#include "net_proto.h"
#include "e1000.h"
#include "anonymize.h"
#include "string.h"
#include "serial.h"
#include <stdint.h>

/* ── Byte-order helpers (big-endian on wire) ───────────────────────────── */
static inline uint16_t hton16(uint16_t v) {
    return (uint16_t)((v >> 8) | (v << 8));
}
static inline uint32_t hton32(uint32_t v) {
    return ((v & 0x000000FFU) << 24) |
           ((v & 0x0000FF00U) <<  8) |
           ((v & 0x00FF0000U) >>  8) |
           ((v & 0xFF000000U) >> 24);
}

/* ── IPv4 checksum ─────────────────────────────────────────────────────── */
static uint16_t ip_checksum(const uint8_t *hdr, size_t len) {
    uint32_t sum = 0;
    for (size_t i = 0; i + 1 < len; i += 2)
        sum += (uint16_t)((hdr[i] << 8) | hdr[i + 1]);
    if (len & 1) sum += (uint16_t)(hdr[len - 1] << 8);
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)(~sum);
}

/* ── Build UDP frame ───────────────────────────────────────────────────── */
size_t net_build_udp(const uint8_t src_mac[6],
                     const uint8_t dst_mac[6],
                     uint32_t      src_ip,
                     uint32_t      dst_ip,
                     uint16_t      src_port,
                     uint16_t      dst_port,
                     const uint8_t *payload,
                     size_t         payload_len,
                     uint8_t       *frame_out) {
    if (!frame_out || !payload || payload_len == 0 ||
        payload_len > (size_t)NET_UDP_PAYLOAD_MAX)
        return 0;

    uint8_t *p = frame_out;

    /* ── Ethernet header (14 bytes) ── */
    memcpy(p, dst_mac, 6); p += 6;
    memcpy(p, src_mac, 6); p += 6;
    p[0] = 0x08; p[1] = 0x00; p += 2;  /* EtherType IPv4 */

    /* ── IPv4 header (20 bytes) ── */
    uint8_t *ip_start = p;
    uint16_t total_len = (uint16_t)(NET_IP_HDR_LEN + NET_UDP_HDR_LEN + payload_len);

    p[0]  = 0x45;                          /* Version=4, IHL=5 */
    p[1]  = 0x00;                          /* DSCP/ECN */
    p[2]  = (uint8_t)(total_len >> 8);
    p[3]  = (uint8_t)(total_len);
    p[4]  = 0x00; p[5] = 0x01;            /* ID = 1 */
    p[6]  = 0x40; p[7] = 0x00;            /* Flags = DF, Frag Offset = 0 */
    p[8]  = 64;                            /* TTL */
    p[9]  = 17;                            /* Protocol = UDP */
    p[10] = 0; p[11] = 0;                 /* Checksum (computed below) */
    p[12] = (uint8_t)(src_ip >> 24);
    p[13] = (uint8_t)(src_ip >> 16);
    p[14] = (uint8_t)(src_ip >>  8);
    p[15] = (uint8_t)(src_ip);
    p[16] = (uint8_t)(dst_ip >> 24);
    p[17] = (uint8_t)(dst_ip >> 16);
    p[18] = (uint8_t)(dst_ip >>  8);
    p[19] = (uint8_t)(dst_ip);
    uint16_t ip_csum = ip_checksum(ip_start, NET_IP_HDR_LEN);
    p[10] = (uint8_t)(ip_csum >> 8);
    p[11] = (uint8_t)(ip_csum);
    p += NET_IP_HDR_LEN;

    /* ── UDP header (8 bytes) ── */
    uint16_t udp_len = (uint16_t)(NET_UDP_HDR_LEN + payload_len);
    p[0] = (uint8_t)(src_port >> 8); p[1] = (uint8_t)src_port;
    p[2] = (uint8_t)(dst_port >> 8); p[3] = (uint8_t)dst_port;
    p[4] = (uint8_t)(udp_len  >> 8); p[5] = (uint8_t)udp_len;
    p[6] = 0; p[7] = 0;              /* Checksum optional for UDP/IPv4 */
    p += NET_UDP_HDR_LEN;

    /* ── Payload ── */
    memcpy(p, payload, payload_len);
    p += payload_len;

    return (size_t)(p - frame_out);
}

size_t net_build_udp_broadcast(const uint8_t src_mac[6],
                               uint16_t src_port,
                               uint16_t dst_port,
                               const uint8_t *payload,
                               size_t payload_len,
                               uint8_t *frame_out) {
    static const uint8_t bcast_mac[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
    return net_build_udp(src_mac, bcast_mac,
                         0xC0A80164U,  /* 192.168.1.100 (StyxOS source) */
                         0xFFFFFFFFU,  /* 255.255.255.255 */
                         src_port, dst_port,
                         payload, payload_len, frame_out);
}

bool net_send_beacon(const char *reason) {
    if (!g_e1000_active) {
        serial_printf("[BEACON] WARNING: e1000 inactive, beacon cannot be sent\n");
        return false;
    }

    /* Build JSON beacon payload */
    char payload[256];
    memset(payload, 0, sizeof(payload));
    size_t pos = 0;

    const char *part1 = "{\"event\":\"TAMPER\",\"node\":\"STYXOS_NODE\",\"reason\":\"";
    size_t p1len = strlen(part1);
    memcpy(payload + pos, part1, p1len); pos += p1len;

    size_t rlen = reason ? strlen(reason) : 0;
    if (rlen > 128) rlen = 128;
    if (reason) { memcpy(payload + pos, reason, rlen); pos += rlen; }

    const char *part2 = "\",\"action\":\"SELF_DESTRUCT\"}";
    size_t p2len = strlen(part2);
    memcpy(payload + pos, part2, p2len); pos += p2len;

    uint8_t frame[NET_MAX_FRAME];
    uint8_t src_mac[6];
    anonymize_get_mac(src_mac);

    static const uint8_t gw_mac[6] = {0x52,0x55,0x0A,0x00,0x02,0x02}; /* QEMU gateway */

    size_t flen = net_build_udp(src_mac, gw_mac,
                                0xC0A80164U,       /* 192.168.1.100 */
                                BEACON_DST_IP,
                                12345, BEACON_DST_PORT,
                                (const uint8_t *)payload, pos,
                                frame);
    if (flen == 0) return false;

    bool ok = e1000_send_packet(frame, (uint16_t)flen);
    serial_printf("[BEACON] UDP distress frame %s (%d bytes) to %d.%d.%d.%d:%d\n",
                  ok ? "SENT" : "FAILED", (int)flen,
                  (BEACON_DST_IP >> 24) & 0xFF, (BEACON_DST_IP >> 16) & 0xFF,
                  (BEACON_DST_IP >>  8) & 0xFF,  BEACON_DST_IP        & 0xFF,
                  BEACON_DST_PORT);
    return ok;
}
