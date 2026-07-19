#pragma once

#include <stdint.h>

/*
 * tss.h — x86-64 Task State Segment.
 *
 * The TSS is mandatory in 64-bit mode even though hardware task-switching
 * is not used. Its only job is to hold RSP0: the kernel stack pointer that
 * the CPU switches to automatically when a ring-3 task takes a fault or
 * issues the SYSCALL instruction.
 *
 * Without a valid RSP0 in the TSS, any interrupt or exception that fires
 * while the CPU is in ring 3 triple-faults immediately.
 *
 * IST entries (IST1–IST7) are also available for dedicated exception stacks
 * (NMI, double fault, etc.). We leave them zeroed for M6 — unused.
 *
 * Architecture Bible ref: §3 L4 (Process Isolation), §9 M6
 */

/*
 * x86-64 TSS hardware layout — exactly as the CPU expects it.
 * Total size: 104 bytes. Must be 4-byte aligned (we use 16-byte to be safe).
 */
typedef struct {
    uint32_t reserved0;     /* 0x00 */
    uint64_t rsp0;          /* 0x04 — kernel stack for ring 0 (used on SYSCALL/fault) */
    uint64_t rsp1;          /* 0x0C — unused (ring 1 not used) */
    uint64_t rsp2;          /* 0x14 — unused (ring 2 not used) */
    uint64_t reserved1;     /* 0x1C */
    uint64_t ist1;          /* 0x24 — interrupt stack table entry 1 (unused M6) */
    uint64_t ist2;          /* 0x2C */
    uint64_t ist3;          /* 0x34 */
    uint64_t ist4;          /* 0x3C */
    uint64_t ist5;          /* 0x44 */
    uint64_t ist6;          /* 0x4C */
    uint64_t ist7;          /* 0x54 */
    uint64_t reserved2;     /* 0x5C */
    uint16_t reserved3;     /* 0x64 */
    uint16_t iopb_offset;   /* 0x66 — I/O Permission Bitmap offset (set to sizeof(tss)) */
} __attribute__((packed)) tss_t;

/* ── Public API ─────────────────────────────────────────────────────────── */

/*
 * tss_init() — zero the TSS, install its descriptor into the GDT via
 * gdt_install_tss(), then load the TSS register (LTR instruction).
 *
 * Must be called AFTER gdt_init() (needs a valid GDT with TSS slots).
 * Must be called BEFORE enabling ring-3 tasks.
 */
void tss_init(void);

/*
 * tss_set_kernel_stack(rsp0) — update RSP0 in the TSS.
 *
 * Called by the scheduler on every context switch TO a ring-3 task, so
 * the CPU knows which kernel stack to switch to if that task faults or
 * calls SYSCALL. Each task has its own kernel stack; RSP0 must reflect
 * the stack of the task about to run.
 */
void tss_set_kernel_stack(uint64_t rsp0);
