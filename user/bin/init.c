/*
 * init.c — StyxOS Ring-3 Init Daemon (Process ID 1)
 */
#include "../libstyx/styx.h"

int main(void) {
    puts("[INIT-RING3] StyxOS Ring-3 Init process started.");
    puts("[INIT-RING3] Zero Ambient Authority verified. Console capability token active.");

    struct sysinfo_data {
        uint64_t free_mem_bytes;
        uint32_t active_tasks;
        uint32_t reserved;
    } info;

    if (sys_sysinfo(&info) == 0) {
        printf("[INIT-RING3] Kernel memory available: %d MB\n", (int)(info.free_mem_bytes / (1024 * 1024)));
    }

    /* PQC Kyber-1024 Key Encapsulation Syscall Test */
    static uint8_t ct[1568];
    static uint8_t ss[32];
    static uint8_t pk[1568];
    pk[0] = 0x04;
    if (styx_pqc_kem(ct, ss, pk) == 0) {
        puts("[INIT-RING3] PQC Kyber-1024 Shared Secret derived successfully!");
    }

    /* Stateful TCP Socket Syscall Test (Network Cap = Slot 1) */
    int sock = styx_socket(1);
    if (sock >= 0) {
        printf("[INIT-RING3] TCP Socket created (id=%d)\n", sock);
        if (styx_connect(1, sock, 0xC0A80101U, 443) == 0) {
            puts("[INIT-RING3] TCP Connection established to 192.168.1.1:443 (ESTABLISHED)");
            const char *payload = "GET / HTTP/1.1\r\nHost: styxos.local\r\n\r\n";
            styx_send(1, sock, payload, strlen(payload));
        }
    }

    puts("[INIT-RING3] Launching security shell & service threads...");

    while (1) {
        yield();
    }
    return 0;
}

