#pragma once

#include <stdint.h>
#include "cap.h"

/*
 * syscall.h — System call interface.
 *
 * StyxOS uses the x86-64 SYSCALL/SYSRET fast path:
 *   - LSTAR MSR points to syscall_entry (in syscall.asm)
 *   - STAR  MSR encodes kernel CS (0x08) and SYSRET base (0x18)
 *   - SFMASK MSR clears IF on entry (disable interrupts during dispatch)
 *
 * Calling convention (ring-3 → kernel):
 *   RAX = syscall number
 *   RDI = arg0
 *   RSI = arg1
 *   RDX = arg2
 *   R10 = arg3  (RCX is overwritten by SYSCALL with return RIP)
 *
 * Return value in RAX (negative = error).
 *
 * EVERY syscall goes through a capability check first.
 * No capability = no operation. No exceptions.
 *
 * Architecture Bible ref: §3 L4 (No ambient authority), §9 M6
 */

/* ── Syscall numbers ────────────────────────────────────────────────────── */

#define SYS_YIELD      0   /* give up CPU slice                            */
#define SYS_WRITE      1   /* write bytes to serial (debug; M7 adds real I/O) */
#define SYS_CAP_SEND   2   /* IPC send via capability slot                 */
#define SYS_CAP_RECV   3   /* IPC recv via capability slot                 */

#define SYS_MAX        4   /* total number of syscalls in M6 table         */

/* ── Return codes ───────────────────────────────────────────────────────── */

#define SYSRET_OK          0
#define SYSRET_EBADCALL   -1   /* unknown syscall number                  */
#define SYSRET_EACCESS    -2   /* capability check failed                 */
#define SYSRET_EFAULT     -3   /* bad pointer or message                  */

/* ── Public API ─────────────────────────────────────────────────────────── */

/*
 * syscall_init() — program STAR, LSTAR, SFMASK MSRs to enable SYSCALL.
 * Must be called after gdt_init() (STAR needs valid GDT selectors).
 */
void syscall_init(void);

/*
 * syscall_dispatch() — C-level dispatcher, called from syscall_entry (asm).
 *
 * num     = syscall number (from RAX)
 * arg0–3  = arguments (RDI, RSI, RDX, R10)
 *
 * Returns the value to put into RAX before SYSRET.
 */
int64_t syscall_dispatch(uint64_t num,
                          uint64_t arg0, uint64_t arg1,
                          uint64_t arg2, uint64_t arg3);
