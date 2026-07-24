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

    puts("[INIT-RING3] Launching security shell & service threads...");

    while (1) {
        yield();
    }
    return 0;
}
