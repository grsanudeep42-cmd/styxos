#include "syscall.h"
#include "sched.h"
#include "task.h"
#include "serial.h"
#include "cap.h"
#include "keyboard.h"
#include "tor.h"
#include "pmm.h"
#include "fb_shell.h"

/*
 * syscall.c — System call handling.
 *
 * Program MSRs for SYSCALL/SYSRET:
 *   STAR   (0xC0000081) — CS selectors
 *   LSTAR  (0xC0000082) — RIP target (syscall_entry in syscall.asm)
 *   SFMASK (0xC0000084) — RFLAGS mask
 */

#define MSR_EFER     0xC0000080
#define MSR_STAR     0xC0000081
#define MSR_LSTAR    0xC0000082
#define MSR_SFMASK   0xC0000084

static inline uint64_t rdmsr(uint32_t msr) {
    uint32_t low, high;
    __asm__ volatile ("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return ((uint64_t)high << 32) | low;
}

static inline void wrmsr(uint32_t msr, uint64_t val) {
    uint32_t low = (uint32_t)val;
    uint32_t high = (uint32_t)(val >> 32);
    __asm__ volatile ("wrmsr" : : "c"(msr), "a"(low), "d"(high));
}

/* Global scratch variables for assembly syscall gate (single-core only) */
uint64_t global_user_rsp = 0;
uint64_t global_scratch_rax = 0;

extern void syscall_entry(void);

void syscall_init(void) {
    /* Enable SCE (System Call Extensions) in EFER MSR */
    uint64_t efer = rdmsr(MSR_EFER);
    efer |= 1ULL; // Set bit 0 (SCE)
    wrmsr(MSR_EFER, efer);

    /* STAR:
     *   bits 47:32 = kernel CS selector (0x08)
     *   bits 63:48 = user base selector (0x18)
     */
    uint64_t star = ((uint64_t)0x18 << 48) | ((uint64_t)0x08 << 32);
    wrmsr(MSR_STAR, star);

    /* LSTAR: entry point in assembly */
    wrmsr(MSR_LSTAR, (uint64_t)syscall_entry);

    /* SFMASK: mask interrupts (IF = 0x200), TF (0x100), VM (0x20000), etc. */
    wrmsr(MSR_SFMASK, 0x200ULL);

    serial_printf("[SYSCALL] System call MSRs programmed (STAR=%p, LSTAR=%p)\n",
                  (void *)star, (void *)syscall_entry);
}

int64_t syscall_dispatch(uint64_t num,
                          uint64_t arg0, uint64_t arg1,
                          uint64_t arg2, uint64_t arg3) {
    (void)arg3; // Reserved
    task_t *current = sched_get_current();
    if (!current) {
        return SYSRET_EACCESS;
    }

    switch (num) {
        case SYS_YIELD: {
            sched_yield();
            return SYSRET_OK;
        }

        case SYS_WRITE: {
            /*
             * SYS_WRITE:
             *   arg0 = capability slot index
             *   arg1 = virtual address of the string buffer (in user space)
             *   arg2 = length of string
             */
            cap_slot_t *slot;
            cap_err_t err = cap_lookup(current->cap_table, (uint32_t)arg0,
                                       CAP_TYPE_ENDPOINT, CAP_RIGHT_SEND, &slot);
            if (err != CAP_OK) {
                serial_printf("[SYSCALL] SYS_WRITE failed capability check: %s\n",
                              cap_err_str(err));
                return SYSRET_EACCESS;
            }

            if (arg1 >= 0x800000000000ULL || (arg1 + arg2) >= 0x800000000000ULL) {
                return SYSRET_EFAULT;
            }

            char *buf = (char *)arg1;
            for (uint64_t i = 0; i < arg2; i++) {
                if (buf[i] == '\0') break;
                write_serial_char(buf[i]);
                fb_shell_putchar(buf[i]);
            }
            return SYSRET_OK;
        }

        case SYS_CAP_SEND: {
            if (arg1 >= 0x800000000000ULL || (arg1 + sizeof(ipc_msg_t)) >= 0x800000000000ULL) {
                return SYSRET_EFAULT;
            }

            ipc_msg_t *msg = (ipc_msg_t *)arg1;
            cap_err_t err = cap_send(current->cap_table, (uint32_t)arg0, msg);
            if (err != CAP_OK) {
                return -(int64_t)err;
            }
            return SYSRET_OK;
        }

        case SYS_CAP_RECV: {
            if (arg1 >= 0x800000000000ULL || (arg1 + sizeof(ipc_msg_t)) >= 0x800000000000ULL) {
                return SYSRET_EFAULT;
            }

            ipc_msg_t *msg = (ipc_msg_t *)arg1;
            cap_err_t err = cap_recv(current->cap_table, (uint32_t)arg0, msg);
            if (err != CAP_OK) {
                return -(int64_t)err;
            }
            return SYSRET_OK;
        }

        case SYS_EXIT: {
            serial_printf("[SYSCALL] Task %d exited (code=%d)\n", current->id, (int)arg0);
            current->state = TASK_STATE_DEAD;
            sched_yield();
            return SYSRET_OK;
        }

        case SYS_READ_KEY: {
            /* Capability check: Console capability required in slot arg0 */
            cap_slot_t *slot;
            cap_err_t err = cap_lookup(current->cap_table, (uint32_t)arg0,
                                       CAP_TYPE_ENDPOINT, CAP_RIGHT_RECV, &slot);
            if (err != CAP_OK) {
                return SYSRET_EACCESS;
            }
            char c = keyboard_get_char();
            return (int64_t)(unsigned char)c;
        }

        case SYS_TOR_CELL: {
            /* Capability check: Network/Tor capability required in slot arg0 */
            cap_slot_t *slot;
            cap_err_t err = cap_lookup(current->cap_table, (uint32_t)arg0,
                                       CAP_TYPE_ENDPOINT, CAP_RIGHT_SEND, &slot);
            if (err != CAP_OK) {
                return SYSRET_EACCESS;
            }
            if (arg1 >= 0x800000000000ULL || (arg1 + arg2) >= 0x800000000000ULL || arg2 > 512) {
                return SYSRET_EFAULT;
            }
            bool ok = tor_send_cell((const uint8_t *)arg1, (uint16_t)arg2, (uint32_t)arg0);
            return ok ? SYSRET_OK : -1;
        }


        case SYS_SYSINFO: {
            if (arg0 >= 0x800000000000ULL) {
                return SYSRET_EFAULT;
            }
            struct sysinfo_data {
                uint64_t free_mem_bytes;
                uint32_t active_tasks;
                uint32_t reserved;
            } *info = (struct sysinfo_data *)arg0;

            info->free_mem_bytes = pmm_get_free_memory();
            info->active_tasks = 2; // Init + Shell
            info->reserved = 0;
            return SYSRET_OK;
        }

        default:
            serial_printf("[SYSCALL] Unknown system call number: %d\n", (int)num);
            return SYSRET_EBADCALL;
    }
}

