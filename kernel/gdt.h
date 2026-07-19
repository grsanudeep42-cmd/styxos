#pragma once

#include <stdint.h>

/*
 * gdt.h — Global Descriptor Table for StyxOS (64-bit long mode).
 *
 * We build our own GDT to replace Limine's minimal one.
 * This is required for M6 because:
 *   1. We need ring-3 (DPL=3) code + data descriptors for userspace tasks.
 *   2. We need a TSS descriptor so the CPU knows which kernel stack to use
 *      when a ring-3 task takes a fault or issues SYSCALL.
 *
 * GDT layout (8 bytes per entry, TSS takes 16 bytes / 2 entries):
 *
 *   Index  Selector  Description
 *   ─────  ────────  ──────────────────────────────────────────
 *     0    0x00      Null descriptor (required by CPU spec)
 *     1    0x08      Kernel code  (ring 0, 64-bit)
 *     2    0x10      Kernel data  (ring 0)
 *     3    0x18      User code 32-bit compat (ring 3) — placeholder
 *                      Required so SYSRETQ math works:
 *                      SS = STAR[63:48] + 8  = 0x20 | 3 = 0x23
 *                      CS = STAR[63:48] + 16 = 0x28 | 3 = 0x2B
 *     4    0x20      User data    (ring 3)
 *     5    0x28      User code 64-bit (ring 3)
 *     6    0x30      TSS low  (first  8 bytes of 16-byte TSS descriptor)
 *     7    0x38      TSS high (second 8 bytes of 16-byte TSS descriptor)
 *
 * STAR MSR encoding:
 *   STAR[47:32] = 0x08  (kernel CS — loaded on SYSCALL)
 *   STAR[63:48] = 0x18  (SYSRETQ base — CPU adds 8 for SS, 16 for CS)
 *
 * Architecture Bible ref: §3 L4 (Process Isolation), §9 M6
 */

/* ── Segment selectors ─────────────────────────────────────────────────── */

#define GDT_SEL_NULL         0x00
#define GDT_SEL_KERNEL_CODE  0x08
#define GDT_SEL_KERNEL_DATA  0x10
#define GDT_SEL_USER_CODE32  0x18   /* placeholder for SYSRET math */
#define GDT_SEL_USER_DATA    0x20
#define GDT_SEL_USER_CODE64  0x28
#define GDT_SEL_TSS          0x30

/* ── Structures ────────────────────────────────────────────────────────── */

/*
 * Standard 8-byte GDT entry (code/data descriptors).
 *
 * access byte (byte 5):
 *   bit 7   P   — segment present
 *   bits 6:5 DPL — privilege level (0=kernel, 3=user)
 *   bit 4   S   — descriptor type (1=code/data)
 *   bits 3:0 type — segment type:
 *                   0b1010 = execute/read (code)
 *                   0b0010 = read/write   (data)
 *
 * granularity byte (byte 6):
 *   bit 7   G   — granularity (1=4KB page units)
 *   bit 6   D/B — default size (0 when L=1 for 64-bit code)
 *   bit 5   L   — 64-bit code segment (1 for long mode code)
 *   bit 4   AVL — available (0)
 *   bits 3:0    — limit high (ignored in long mode)
 */
typedef struct {
    uint16_t limit_low;    /* segment limit bits 15:0   (ignored in 64-bit) */
    uint16_t base_low;     /* base address bits 15:0    (ignored in 64-bit) */
    uint8_t  base_mid;     /* base address bits 23:16   (ignored in 64-bit) */
    uint8_t  access;       /* P | DPL | S | Type                            */
    uint8_t  granularity;  /* G | D/B | L | AVL | limit_high               */
    uint8_t  base_high;    /* base address bits 31:24   (ignored in 64-bit) */
} __attribute__((packed)) gdt_entry_t;

/*
 * 16-byte TSS descriptor (occupies two consecutive GDT slots).
 *
 * flags1:  0x89 = Present (P=1), DPL=0, type=0b1001 (64-bit TSS available)
 * flags2:  upper 4 bits must be zero; lower 4 bits = limit_high
 */
typedef struct {
    uint16_t length;       /* sizeof(tss_t) - 1                          */
    uint16_t base_low;     /* TSS base bits 15:0                         */
    uint8_t  base_mid;     /* TSS base bits 23:16                        */
    uint8_t  flags1;       /* P | DPL | 0 | type (0x89)                  */
    uint8_t  flags2;       /* G | 0 | AVL | 0 | limit_high              */
    uint8_t  base_high;    /* TSS base bits 31:24                        */
    uint32_t base_upper;   /* TSS base bits 63:32                        */
    uint32_t reserved;     /* must be zero                               */
} __attribute__((packed)) gdt_tss_entry_t;

/* GDT pointer loaded via LGDT */
typedef struct {
    uint16_t limit;        /* sizeof(GDT array) - 1                      */
    uint64_t base;         /* virtual address of the GDT array           */
} __attribute__((packed)) gdt_ptr_t;

/* ── Public API ────────────────────────────────────────────────────────── */

/*
 * gdt_init() — build our GDT, install it via LGDT, reload all segment
 * registers (CS via far-return, DS/ES/FS/GS/SS directly).
 *
 * After this call:
 *   CS = 0x08 (kernel code, ring 0)
 *   DS = SS = 0x10 (kernel data, ring 0)
 *   FS = GS = 0x00 (null — set by SWAPGS in syscall path)
 *
 * The TSS is NOT loaded here — call tss_init() first to populate the TSS
 * struct, then gdt_install_tss() to write the TSS descriptor into the GDT,
 * then ltr(GDT_SEL_TSS) to load it into the TR register.
 */
void gdt_init(void);

/*
 * gdt_install_tss(base) — write the 16-byte TSS descriptor into the GDT
 * at slots 6 and 7. Called by tss_init() after the TSS struct is ready.
 */
void gdt_install_tss(uint64_t tss_base, uint16_t tss_limit);
