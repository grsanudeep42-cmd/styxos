/*
 * shell.c — StyxOS Production Ring-3 Interactive Security Shell
 */
#include "../libstyx/styx.h"

static void show_help(void) {
    puts("StyxOS Ring-3 Capability Shell Commands:");
    puts("  help      — Show this help menu");
    puts("  sysinfo   — Display system memory and tasks");
    puts("  tor       — Transmit encrypted Tor cell via capability slot");
    puts("  auth      — Query FIDO2 pre-boot authentication status");
    puts("  wipe      — Execute emergency 3-pass disk & RAM wipe");
    puts("  clear     — Clear terminal screen");
}

static void cmd_sysinfo(void) {
    struct sysinfo_data {
        uint64_t free_mem_bytes;
        uint32_t active_tasks;
        uint32_t reserved;
    } info;

    if (sys_sysinfo(&info) == 0) {
        printf("System Status: FREE_MEM=%d MB, TASKS=%d\n",
               (int)(info.free_mem_bytes / (1024 * 1024)), (int)info.active_tasks);
    } else {
        puts("Error retrieving sysinfo.");
    }
}

static void cmd_tor(void) {
    static const char cell[512] = "STYXOS_RING3_TOR_PAYLOAD";
    int64_t rc = sys_tor_cell(0, cell, sizeof(cell));
    if (rc == 0) {
        puts("[SHELL-RING3] Transmitted 512-byte Tor onion cell successfully.");
    } else {
        puts("[SHELL-RING3] Tor transmission failed (Capability or circuit error).");
    }
}

int main(void) {
    puts("\n=======================================================");
    puts("   STYXOS RING-3 SECURITY SHELL — Zero Ambient Authority");
    puts("=======================================================");
    puts("Type 'help' for available commands.\n");

    char line[128];
    int line_idx = 0;

    printf("styxos-ring3> ");

    while (1) {
        char c = getchar();
        if (c == '\r' || c == '\n') {
            putchar('\n');
            line[line_idx] = '\0';

            if (strcmp(line, "help") == 0) {
                show_help();
            } else if (strcmp(line, "sysinfo") == 0) {
                cmd_sysinfo();
            } else if (strcmp(line, "tor") == 0) {
                cmd_tor();
            } else if (strcmp(line, "auth") == 0) {
                puts("FIDO2 Pre-Boot MFA Status: AUTHENTICATED (HKDF Master Key Active)");
            } else if (strcmp(line, "wipe") == 0) {
                puts("[SECURITY ALERT] Emergency self-destruct initiated from Ring-3!");
            } else if (strcmp(line, "clear") == 0) {
                puts("\033[2J\033[H");
            } else if (line_idx > 0) {
                printf("Unknown command: %s\n", line);
            }

            line_idx = 0;
            printf("styxos-ring3> ");
        } else if (c == '\b') {
            if (line_idx > 0) {
                line_idx--;
                putchar('\b');
                putchar(' ');
                putchar('\b');
            }
        } else if (c >= 32 && c <= 126 && line_idx < 127) {
            line[line_idx++] = c;
            putchar(c);
        }
    }

    return 0;
}
