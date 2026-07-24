#pragma once

#include <stdint.h>
#include <stdbool.h>

#define E1000_NUM_RX_DESC 64
#define E1000_NUM_TX_DESC 64
#define E1000_BUFFER_SIZE 2048

typedef struct {
    uint64_t addr;
    uint16_t length;
    uint16_t checksum;
    uint8_t  status;
    uint8_t  errors;
    uint16_t special;
} __attribute__((packed)) e1000_rx_desc_t;

typedef struct {
    uint64_t addr;
    uint16_t length;
    uint8_t  cso;
    uint8_t  cmd;
    uint8_t  status;
    uint8_t  css;
    uint16_t special;
} __attribute__((packed)) e1000_tx_desc_t;

extern bool g_e1000_active;
extern uint32_t g_e1000_tx_count;
extern uint32_t g_e1000_rx_count;

bool e1000_init(void);
bool e1000_send_packet(const uint8_t *data, uint16_t len);
bool e1000_poll_packet(uint8_t *buffer_out, uint16_t *len_out);
void e1000_disable(void);
