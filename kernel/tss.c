#include "tss.h"
#include "gdt.h"
#include "serial.h"

/*
 * tss.c — TSS implementation.
 *
 * One global TSS instance for the boot CPU.
 * SMP would require one TSS per CPU — out of scope for M6.
 */

/* ── Single global TSS (16-byte aligned) ────────────────────────────────── */

static tss_t tss __attribute__((aligned(16)));

/* ── tss_init ─────────────────────────────────────────────────────────────── */

void tss_init(void) {
    /* Zero the entire TSS */
    uint8_t *p = (uint8_t *)&tss;
    for (int i = 0; i < (int)sizeof(tss_t); i++)
        p[i] = 0;

    /*
     * IOPB offset points past the end of the TSS — means all I/O port
     * access from ring 3 is denied. Ring-3 drivers go through capability
     * IPC, not direct port access. Architecture Bible: §11 Driver Model.
     */
    tss.iopb_offset = (uint16_t)sizeof(tss_t);

    /*
     * RSP0 is 0 for now — tss_set_kernel_stack() updates it before the
     * first ring-3 task runs. A zero RSP0 is safe here because we have
     * not yet entered ring 3.
     */
    tss.rsp0 = 0;

    /* Install the TSS descriptor into GDT slots 6+7 */
    gdt_install_tss((uint64_t)&tss, (uint16_t)(sizeof(tss_t) - 1));

    /* Load the Task Register — tells CPU where to find the TSS */
    __asm__ volatile ("ltr %0" : : "r"((uint16_t)GDT_SEL_TSS));

    serial_printf("[TSS]  TSS initialized — base=%p size=%d bytes RSP0 set on first task switch\n",
                  (void *)&tss, (int)sizeof(tss_t));
}

/* ── tss_set_kernel_stack ────────────────────────────────────────────────── */

void tss_set_kernel_stack(uint64_t rsp0) {
    tss.rsp0 = rsp0;
}
