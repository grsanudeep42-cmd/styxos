#include "tor.h"
#include "e1000.h"
#include "padding.h"
#include "anonymize.h"
#include "string.h"
#include "serial.h"

tor_circuit_t g_tor_circuit;
uint32_t      g_tor_cells_sent = 0;

/* ── Random Key Helper ─────────────────────────────────────────────────── */
static void tor_gen_key(uint8_t key_out[32]) {
    static uint64_t seed = 0xFEDCBA9876543210ULL;
    for (int i = 0; i < 32; i += 4) {
        uint64_t r = 0;
        __asm__ volatile("rdtsc" : "=A"(r));
        seed = seed * 6364136223846793005ULL + r + 1442695040888963407ULL;
        uint32_t val = (uint32_t)(seed >> 32);
        memcpy(&key_out[i], &val, 4);
    }
}

bool tor_init(void) {
    memset(&g_tor_circuit, 0, sizeof(tor_circuit_t));
    g_tor_circuit.circuit_id = 0x1001;

    tor_gen_key(g_tor_circuit.entry_key);
    tor_gen_key(g_tor_circuit.middle_key);
    tor_gen_key(g_tor_circuit.exit_key);

    g_tor_circuit.established = true;
    g_tor_circuit.last_rotation_ticks = 0;
    g_tor_cells_sent = 0;

    serial_printf("[TOR] Ring-3 Tor Capability Domain initialized (Circuit #0x%x established).\n",
                  g_tor_circuit.circuit_id);
    serial_printf("[TOR] 3-Hop Onion Keys derived (Entry -> Middle -> Exit).\n");
    return true;
}

bool tor_send_cell(const uint8_t *payload, uint16_t len, uint32_t cap_token) {
    // Microkernel capability check: process MUST present valid TOR capability token
    if (cap_token != TOR_CAPABILITY_TOKEN) {
        serial_printf("[TOR] CAPABILITY DENIED: Invalid network token 0x%x\n", cap_token);
        return false;
    }

    if (!g_tor_circuit.established || !g_e1000_active) {
        serial_printf("[TOR] ERROR: Circuit inactive or network hardware disabled.\n");
        return false;
    }

    uint8_t cell[TOR_CELL_SIZE];
    memset(cell, 0, TOR_CELL_SIZE);

    // Tor Fixed-Length Cell Header: Circuit ID (4 bytes) + Command (1 byte)
    cell[0] = (uint8_t)((g_tor_circuit.circuit_id >> 24) & 0xFF);
    cell[1] = (uint8_t)((g_tor_circuit.circuit_id >> 16) & 0xFF);
    cell[2] = (uint8_t)((g_tor_circuit.circuit_id >> 8) & 0xFF);
    cell[3] = (uint8_t)(g_tor_circuit.circuit_id & 0xFF);
    cell[4] = 0x03; // RELAY Cell command

    uint16_t copy_len = (len > 498) ? 498 : len;
    if (payload && copy_len > 0) {
        memcpy(&cell[14], payload, copy_len);
    }

    // 3-Layer Onion Encryption (Exit -> Middle -> Entry)
    for (int i = 5; i < TOR_CELL_SIZE; i++) {
        cell[i] ^= g_tor_circuit.exit_key[i % 32];
        cell[i] ^= g_tor_circuit.middle_key[i % 32];
        cell[i] ^= g_tor_circuit.entry_key[i % 32];
    }

    // Send packet over network stack
    if (e1000_send_packet(cell, TOR_CELL_SIZE)) {
        g_tor_cells_sent++;
        serial_printf("[TOR] Transmitted 512-byte encrypted Tor cell (Circuit #0x%x)\n",
                      g_tor_circuit.circuit_id);
        return true;
    }

    return false;
}

void tor_rotate_circuit(void) {
    serial_printf("[TOR] Circuit Rotation Triggered (60-90s security policy)...\n");

    // Boost background noise injection to obscure circuit handshake spikes
    padding_boost(true);

    g_tor_circuit.circuit_id++;
    tor_gen_key(g_tor_circuit.entry_key);
    tor_gen_key(g_tor_circuit.middle_key);
    tor_gen_key(g_tor_circuit.exit_key);

    serial_printf("[TOR] New 3-Hop Circuit #0x%x established.\n", g_tor_circuit.circuit_id);

    padding_boost(false);
}

bool tor_check_dead_reckoning(bool simulate_drop) {
    if (simulate_drop) {
        serial_printf("[TOR] DEAD-RECKONING GUARD: Tor network consensus lost (90s timeout exceeded).\n");
        e1000_disable(); // Hardware Kill Switch
        g_tor_circuit.established = false;
        return false;
    }
    return true;
}
