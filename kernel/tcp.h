#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/*
 * tcp.h — StyxOS Stateful TCP Engine & Socket Manager
 * Manages TCB (Transmission Control Block) states, sequence numbers, and packet dispatching.
 */

#define MAX_TCP_SOCKETS 16
#define TCP_RX_BUF_SIZE 4096

typedef enum {
    TCP_STATE_CLOSED = 0,
    TCP_STATE_LISTEN,
    TCP_STATE_SYN_SENT,
    TCP_STATE_SYN_RCVD,
    TCP_STATE_ESTABLISHED,
    TCP_STATE_FIN_WAIT_1,
    TCP_STATE_FIN_WAIT_2,
    TCP_STATE_TIME_WAIT
} tcp_state_t;

typedef struct {
    int id;
    bool active;
    tcp_state_t state;
    uint32_t local_ip;
    uint32_t remote_ip;
    uint16_t local_port;
    uint16_t remote_port;
    uint32_t seq_num;       /* Next local sequence number */
    uint32_t ack_num;       /* Expected remote sequence number */
    uint16_t window_size;   /* Receive window size */
    uint8_t rx_buf[TCP_RX_BUF_SIZE];
    size_t rx_head;
    size_t rx_tail;
} tcp_socket_t;

void tcp_init(void);

int tcp_socket_create(void);
int tcp_socket_connect(int socket_id, uint32_t dst_ip, uint16_t dst_port);
int tcp_socket_send(int socket_id, const uint8_t *buf, size_t len);
int tcp_socket_recv(int socket_id, uint8_t *buf, size_t max_len);
int tcp_socket_close(int socket_id);

void tcp_handle_packet(const uint8_t *packet, size_t len);
