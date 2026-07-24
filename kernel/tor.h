#pragma once

#include <stdint.h>
#include <stdbool.h>

#define TOR_CELL_SIZE 512
#define TOR_CAPABILITY_TOKEN 0x544F5231 // 'TOR1'

typedef struct {
    uint32_t circuit_id;
    uint8_t  entry_key[32];
    uint8_t  middle_key[32];
    uint8_t  exit_key[32];
    bool     established;
    uint32_t last_rotation_ticks;
} tor_circuit_t;

extern tor_circuit_t g_tor_circuit;
extern uint32_t      g_tor_cells_sent;

bool tor_init(void);
bool tor_send_cell(const uint8_t *payload, uint16_t len, uint32_t cap_token);
void tor_rotate_circuit(void);
bool tor_check_dead_reckoning(bool simulate_drop);
void tor_tick(uint64_t ticks);

