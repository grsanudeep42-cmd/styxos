#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* net_proto.h — Ethernet/IP/UDP frame builder
 * Used by: traffic padding (real UDP noise), distress beacon, Tor cell dispatch. */

/* Maximum raw frame size (Ethernet MTU 1500 + 14 byte header) */
#define NET_MAX_FRAME   1514
#define NET_ETH_HDR_LEN   14
#define NET_IP_HDR_LEN    20
#define NET_UDP_HDR_LEN    8
#define NET_UDP_PAYLOAD_MAX (NET_MAX_FRAME - NET_ETH_HDR_LEN - NET_IP_HDR_LEN - NET_UDP_HDR_LEN)

/* Default dead-drop beacon destination (pre-configured; change before deployment) */
#define BEACON_DST_IP    0xC0A80101U  /* 192.168.1.1 (gateway in QEMU user-net) */
#define BEACON_DST_PORT  4444U

/* Build a complete Ethernet/IPv4/UDP frame into `frame_out` (caller provides ≥ NET_MAX_FRAME buf).
 * Returns total frame length, or 0 on error. */
size_t net_build_udp(const uint8_t src_mac[6],
                     const uint8_t dst_mac[6],
                     uint32_t      src_ip,
                     uint32_t      dst_ip,
                     uint16_t      src_port,
                     uint16_t      dst_port,
                     const uint8_t *payload,
                     size_t         payload_len,
                     uint8_t       *frame_out);

/* Broadcast UDP (dst_mac = ff:ff:ff:ff:ff:ff, dst_ip = 255.255.255.255) */
size_t net_build_udp_broadcast(const uint8_t src_mac[6],
                               uint16_t src_port,
                               uint16_t dst_port,
                               const uint8_t *payload,
                               size_t payload_len,
                               uint8_t *frame_out);

/* Send distress beacon payload via e1000 to BEACON_DST_IP:BEACON_DST_PORT */
bool net_send_beacon(const char *reason);
