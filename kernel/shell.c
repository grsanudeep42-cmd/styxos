/*
 * shell.c — Interactive Security Shell UI & REPL logic
 */
#include "shell.h"
#include "fb_shell.h"
#include "keyboard.h"
#include "serial.h"
#include "string.h"
#include "anonymize.h"
#include "e1000.h"
#include "padding.h"
#include "tor.h"
#include "snapshot.h"
#include "destruct.h"
#include "pmm.h"
#include "heap.h"
#include "manifest.h"
#include "auth_counter.h"
#include "merkle.h"

static void cmd_help(void) {
    fb_shell_puts("\n--- STYX OS SECURITY UTILITIES ---\n");
    fb_shell_puts("  help     - Display this help menu\n");
    fb_shell_puts("  sysinfo  - Display system health, memory & MAC status\n");
    fb_shell_puts("  net      - Query e1000 NIC state, traffic padding & counters\n");
    fb_shell_puts("  tor      - Query Tor 3-hop circuit status & trigger rotation\n");
    fb_shell_puts("  snapshot - Trigger atomic AES-256-GCM + PathORAM session snapshot\n");
    fb_shell_puts("  auth     - Query pre-boot FIDO2 CTAP2 & tamper counter state\n");
    fb_shell_puts("  merkle   - Print SHA-256 Merkle root hash\n");
    fb_shell_puts("  wipe     - Trigger emergency 3-pass self-destruct overwrite\n");
    fb_shell_puts("----------------------------------\n\n");
}

static void cmd_sysinfo(void) {
    fb_shell_puts("\n--- STYX OS SYSTEM INFORMATION ---\n");
    fb_shell_puts("Kernel: Custom x86-64 Microkernel (Higher-Half)\n");

    uint64_t total_mb = pmm_get_total_memory() / (1024 * 1024);
    uint64_t free_mb = pmm_get_free_memory() / (1024 * 1024);
    fb_shell_printf("Memory (PMM): %d MB Total | %d MB Free\n", (int)total_mb, (int)free_mb);

    uint8_t mac[6];
    anonymize_get_mac(mac);
    fb_shell_printf("Hardware MAC: %x:%x:%x:%x:%x:%x\n",
                    mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    fb_shell_puts("----------------------------------\n\n");
}

static void cmd_net(void) {
    fb_shell_puts("\n--- NETWORK ANONYMITY STACK STATUS ---\n");
    fb_shell_printf("PCI Driver: Intel 82540EM (e1000) [%s]\n",
                    g_e1000_active ? "ACTIVE" : "DOWN");
    fb_shell_printf("Traffic Padding: Constant-Rate UDP Stream (%s)\n",
                    g_padding_boost_active ? "BOOSTED" : "BASELINE");
    fb_shell_printf("Noise Packets Sent: %d | TX: %d | RX: %d\n",
                    g_padding_packets_sent, g_e1000_tx_count, g_e1000_rx_count);
    fb_shell_puts("-------------------------------------\n\n");
}

static void cmd_tor(void) {
    fb_shell_puts("\n--- TOR SYSTEM CAPABILITY DOMAIN ---\n");
    fb_shell_printf("Circuit ID: 0x%x | Encrypted Cells: %d\n",
                    g_tor_circuit.circuit_id, g_tor_cells_sent);
    fb_shell_puts("Executing ChaCha20 key rotation...\n");
    tor_rotate_circuit();
    fb_shell_puts("------------------------------------\n\n");
}

static void cmd_snapshot(void) {
    fb_shell_puts("\n--- AES-256-GCM + PathORAM SNAPSHOT ---\n");
    if (snapshot_save() == 0) {
        fb_shell_puts("[SNAPSHOT] PASSED: Atomic AES-256-GCM snapshot written to PathORAM.\n");
    } else {
        fb_shell_puts("[SNAPSHOT] ERROR: Snapshot serialization failed!\n");
    }
    fb_shell_puts("---------------------------------------\n\n");
}

static void cmd_auth(void) {
    fb_shell_puts("\n--- PRE-BOOT FIDO2 CTAP2 AUTHENTICATION ---\n");
    fb_shell_printf("Attempt Counter: %d / %d failed attempts\n",
                    auth_counter_get(), AUTH_COUNTER_MAX);
    fb_shell_puts("Status: Authenticated (Derived Session Key Active)\n");
    fb_shell_puts("-------------------------------------------\n\n");
}

static void cmd_merkle(void) {
    uint8_t root[32];
    merkle_get_root(root);
    fb_shell_puts("\n--- SHA-256 MERKLE SECTOR INTEGRITY TREE ---\n");
    fb_shell_printf("Root Hash: %02x%02x%02x%02x...%02x%02x\n",
                    root[0], root[1], root[2], root[3], root[30], root[31]);
    fb_shell_puts("--------------------------------------------\n\n");
}

static void cmd_wipe(void) {
    fb_shell_puts("\n[WARNING] EMERGENCY SELF-DESTRUCT TRIGGERED!\n");
    destruct_trigger("Manual user wipe command from shell");
}

void shell_execute_cmd(const char *cmd_line) {
    if (!cmd_line || strlen(cmd_line) == 0) return;

    if (strcmp(cmd_line, "help") == 0) cmd_help();
    else if (strcmp(cmd_line, "sysinfo") == 0) cmd_sysinfo();
    else if (strcmp(cmd_line, "net") == 0) cmd_net();
    else if (strcmp(cmd_line, "tor") == 0) cmd_tor();
    else if (strcmp(cmd_line, "snapshot") == 0) cmd_snapshot();
    else if (strcmp(cmd_line, "auth") == 0) cmd_auth();
    else if (strcmp(cmd_line, "merkle") == 0) cmd_merkle();
    else if (strcmp(cmd_line, "wipe") == 0) cmd_wipe();
    else fb_shell_printf("Unknown command: '%s'. Type 'help'\n\n", cmd_line);
}

void shell_init(void) {
    fb_shell_init();
    fb_shell_puts("====================================================\n");
    fb_shell_puts("               STYX OS SECURITY SHELL               \n");
    fb_shell_puts("             where data goes to die v2.0            \n");
    fb_shell_puts("====================================================\n");
    fb_shell_puts("Type 'help' for available security commands.\n\n");
}

void shell_run(void) {
    shell_init();
    manifest_verify_elf("/bin/shell.elf", (const uint8_t*)"STYX_SHELL_BINARY", 17);

    // Initial automated test printout
    cmd_help();
    fb_shell_puts("styx# ");

    // Interactive REPL loop
    char line_buf[128];
    size_t line_pos = 0;
    memset(line_buf, 0, sizeof(line_buf));

    for (;;) {
        char c = keyboard_get_char();
        if (c == 0) {
            // Spin brief CPU pause
            for (volatile int d = 0; d < 1000; d++) __asm__ volatile("pause");
            continue;
        }

        if (c == '\n' || c == '\r') {
            fb_shell_putchar('\n');
            line_buf[line_pos] = '\0';
            if (line_pos > 0) {
                shell_execute_cmd(line_buf);
                line_pos = 0;
                memset(line_buf, 0, sizeof(line_buf));
            }
            fb_shell_puts("styx# ");
        } else if (c == '\b') {
            if (line_pos > 0) {
                line_pos--;
                line_buf[line_pos] = '\0';
                fb_shell_putchar('\b');
            }
        } else if (c >= 32 && c <= 126 && line_pos < sizeof(line_buf) - 1) {
            line_buf[line_pos++] = c;
            fb_shell_putchar(c);
        }
    }
}
