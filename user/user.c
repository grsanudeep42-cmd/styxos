#include <stdint.h>

#define SYS_YIELD      0
#define SYS_WRITE      1

static inline int64_t syscall(uint64_t num, uint64_t arg0, uint64_t arg1, uint64_t arg2) {
    int64_t ret;
    /*
     * x86-64 syscall instruction clobbers RCX and R11.
     * Arguments are passed in: rax (num), rdi (arg0), rsi (arg1), rdx (arg2).
     */
    __asm__ volatile (
        "mov %1, %%rax\n"
        "mov %2, %%rdi\n"
        "mov %3, %%rsi\n"
        "mov %4, %%rdx\n"
        "syscall\n"
        "mov %%rax, %0\n"
        : "=r"(ret)
        : "r"(num), "r"(arg0), "r"(arg1), "r"(arg2)
        : "rax", "rdi", "rsi", "rdx", "rcx", "r11", "memory"
    );
    return ret;
}

void _start(void) {
    const char *msg = "Hello from ring 3!\n";
    /*
     * Call SYS_WRITE.
     * Under StyxOS capability model, slot 0 holds the console output capability.
     */
    syscall(SYS_WRITE, 0, (uint64_t)msg, 19);

    /* Loop yielding CPU slice so we can observe preemption */
    while (1) {
        syscall(SYS_YIELD, 0, 0, 0);
    }
}
