#include "gdt.h"
#include "serial.h"

/*
 * gdt.c — GDT implementation.
 *
 * We build an 8-entry GDT:
 *   [0] null
 *   [1] kernel code 64-bit  (ring 0)
 *   [2] kernel data          (ring 0)
 *   [3] user code 32-bit     (ring 3, placeholder for SYSRET math)
 *   [4] user data            (ring 3)
 *   [5] user code 64-bit    (ring 3)
 *   [6] TSS low              (16-byte TSS descriptor, first half)
 *   [7] TSS high             (16-byte TSS descriptor, second half)
 */

/* ── GDT storage (static, 8 normal entries + TSS takes slots 6+7) ──────── */

#define GDT_ENTRIES  8

static gdt_entry_t    gdt[GDT_ENTRIES];
static gdt_ptr_t      gdt_ptr;

/* ── Helpers ─────────────────────────────────────────────────────────────── */

/*
 * encode_entry() — fill one 8-byte GDT descriptor.
 *
 * In 64-bit long mode the base and limit fields in code/data descriptors
 * are ignored by the CPU — only the access and granularity bytes matter.
 * We still fill them correctly for spec compliance.
 *
 * access byte encoding:
 *   0x9A = 1001_1010b → P=1 DPL=0 S=1 type=1010 (kernel execute/read)
 *   0x92 = 1001_0010b → P=1 DPL=0 S=1 type=0010 (kernel read/write)
 *   0xFA = 1111_1010b → P=1 DPL=3 S=1 type=1010 (user execute/read)
 *   0xF2 = 1111_0010b → P=1 DPL=3 S=1 type=0010 (user read/write)
 *
 * granularity byte encoding:
 *   0xA0 = 1010_0000b → G=1 D/B=0 L=1 AVL=0 limit_hi=0 (64-bit code)
 *   0xC0 = 1100_0000b → G=1 D/B=1 L=0 AVL=0 limit_hi=0 (data/32-bit)
 */
static void encode_entry(int idx, uint8_t access, uint8_t granularity) {
    gdt[idx].limit_low   = 0xFFFF;
    gdt[idx].base_low    = 0x0000;
    gdt[idx].base_mid    = 0x00;
    gdt[idx].access      = access;
    gdt[idx].granularity = granularity;
    gdt[idx].base_high   = 0x00;
}

/* ── gdt_init ─────────────────────────────────────────────────────────────── */

/* Assembly helpers defined at the bottom of this file (see __asm__ blocks). */
extern void gdt_flush(uint64_t gdt_ptr_addr);
extern void gdt_reload_segments(void);

void gdt_init(void) {
    /* [0] Null descriptor — required */
    gdt[0].limit_low = gdt[0].base_low = 0;
    gdt[0].base_mid  = gdt[0].access   = 0;
    gdt[0].granularity = gdt[0].base_high = 0;

    /* [1] Kernel code — ring 0, 64-bit (L=1, D/B=0) */
    encode_entry(1, 0x9A, 0xA0);

    /* [2] Kernel data — ring 0 */
    encode_entry(2, 0x92, 0xC0);

    /*
     * [3] User code 32-bit placeholder — ring 3.
     * Not executed; exists only so SYSRETQ selector math is correct.
     * STAR[63:48] = 0x18 → SS = 0x18+8=0x20|3, CS = 0x18+16=0x28|3.
     */
    encode_entry(3, 0xFA, 0xC0);

    /* [4] User data — ring 3 */
    encode_entry(4, 0xF2, 0xC0);

    /* [5] User code 64-bit — ring 3 (L=1, D/B=0) */
    encode_entry(5, 0xFA, 0xA0);

    /* [6] and [7] — TSS descriptor filled in by gdt_install_tss() */
    gdt[6].limit_low = gdt[6].base_low = 0;
    gdt[6].base_mid  = gdt[6].access   = 0;
    gdt[6].granularity = gdt[6].base_high = 0;
    gdt[7].limit_low = gdt[7].base_low = 0;
    gdt[7].base_mid  = gdt[7].access   = 0;
    gdt[7].granularity = gdt[7].base_high = 0;

    /* Set up the GDT pointer */
    gdt_ptr.limit = (uint16_t)(sizeof(gdt) - 1);
    gdt_ptr.base  = (uint64_t)&gdt;

    /* Load GDT via LGDT, then reload all segment registers */
    gdt_flush((uint64_t)&gdt_ptr);

    serial_printf("[GDT]  GDT installed — 8 entries, base=%p limit=%d\n",
                  (void *)gdt_ptr.base, (int)gdt_ptr.limit + 1);
    serial_printf("[GDT]  Selectors: K_CODE=0x08 K_DATA=0x10 "
                  "U_DATA=0x23 U_CODE64=0x2B TSS=0x30\n");
}

/* ── gdt_install_tss ─────────────────────────────────────────────────────── */

void gdt_install_tss(uint64_t tss_base, uint16_t tss_limit) {
    /*
     * The 64-bit TSS descriptor is 16 bytes wide and occupies slots 6 + 7.
     * We reinterpret those two gdt_entry_t slots as a single gdt_tss_entry_t.
     */
    gdt_tss_entry_t *tss_desc = (gdt_tss_entry_t *)&gdt[6];

    tss_desc->length      = tss_limit;
    tss_desc->base_low    = (uint16_t)(tss_base & 0xFFFF);
    tss_desc->base_mid    = (uint8_t)((tss_base >> 16) & 0xFF);
    tss_desc->flags1      = 0x89;  /* P=1 DPL=0 type=0b1001 (TSS available) */
    tss_desc->flags2      = 0x00;  /* G=0, limit_high=0 */
    tss_desc->base_high   = (uint8_t)((tss_base >> 24) & 0xFF);
    tss_desc->base_upper  = (uint32_t)((tss_base >> 32) & 0xFFFFFFFF);
    tss_desc->reserved    = 0;

    serial_printf("[GDT]  TSS descriptor installed at selector 0x30 "
                  "(base=%p limit=%d)\n",
                  (void *)tss_base, (int)tss_limit);
}

/* ── Assembly stubs ──────────────────────────────────────────────────────── */

/*
 * gdt_flush — load the new GDT pointer via LGDT, then do a far-return to
 * reload CS with the new kernel code selector (0x08).
 *
 * We cannot use a far-jump in a position-independent higher-half kernel with
 * -mcmodel=kernel, so we use the RETFQ (far return, 64-bit) trick:
 *   push new CS
 *   push return address
 *   retfq
 */
__attribute__((naked)) void gdt_flush(uint64_t gdt_ptr_addr __attribute__((unused))) {
    __asm__ volatile (
        "lgdt  (%rdi)          \n"  /* load new GDT from pointer in rdi     */
        "mov   $0x10, %ax      \n"  /* kernel data selector                 */
        "mov   %ax, %ds        \n"
        "mov   %ax, %es        \n"
        "mov   %ax, %ss        \n"
        "xor   %ax, %ax        \n"
        "mov   %ax, %fs        \n"  /* FS = null (SWAPGS sets FS.base)      */
        "mov   %ax, %gs        \n"  /* GS = null (SWAPGS sets GS.base)      */
        /* Far-return to reload CS = 0x08 */
        "pop   %rdi            \n"  /* save caller's return address         */
        "push  $0x08           \n"  /* new CS (kernel code, ring 0)         */
        "push  %rdi            \n"  /* return address                       */
        "retfq                 \n"  /* far return: pop RIP then CS          */
    );
}
