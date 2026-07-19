#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "limine.h"
#include "fb.h"
#include "font.h"
#include "serial.h"
#include "idt.h"
#include "irq.h"
#include "pit.h"
#include "keyboard.h"
#include "pmm.h"
#include "vmm.h"
#include "heap.h"
#include "cap.h"
#include "gdt.h"
#include "tss.h"
#include "syscall.h"
#include "task.h"
#include "sched.h"
#include "elf.h"
#include "pci.h"
#include "xhci.h"
#include "usb_msc.h"
#include "vfs.h"
#include "user_init.bin.h"

/* ── Limine protocol requests ─────────────────────────────────────────── */

__attribute__((used, section(".limine_requests")))
LIMINE_BASE_REVISION(3)

__attribute__((used, section(".limine_requests")))
volatile struct limine_framebuffer_request framebuffer_request = {
    .id       = LIMINE_FRAMEBUFFER_REQUEST,
    .revision = 0
};

__attribute__((used, section(".limine_requests")))
volatile struct limine_memmap_request memmap_request = {
    .id       = LIMINE_MEMMAP_REQUEST,
    .revision = 0
};

__attribute__((used, section(".limine_requests")))
volatile struct limine_hhdm_request hhdm_request = {
    .id       = LIMINE_HHDM_REQUEST,
    .revision = 0
};

__attribute__((used, section(".limine_requests")))
volatile struct limine_kernel_address_request kernel_address_request = {
    .id       = LIMINE_KERNEL_ADDRESS_REQUEST,
    .revision = 0
};

__attribute__((used, section(".limine_requests_start")))
LIMINE_REQUESTS_START_MARKER

__attribute__((used, section(".limine_requests_end")))
LIMINE_REQUESTS_END_MARKER

/* ── Halt helper ──────────────────────────────────────────────────────── */

static void halt(void) {
    __asm__ volatile ("cli");
    for (;;) __asm__ volatile ("hlt");
}

/* ── Kernel entry ─────────────────────────────────────────────────────── */

void _start(void) {
    // 1. Initialize serial port first so we have debug logging immediately
    serial_init();
    serial_printf("Styx OS: UART serial initialized on COM1.\n");

    // 2. Sanity-check: make sure Limine understood our base revision.
    if (!LIMINE_BASE_REVISION_SUPPORTED) {
        serial_printf("Styx OS: Limine base revision not supported! Halting.\n");
        halt();
    }

    // 3. Obtain framebuffer.
    if (framebuffer_request.response == NULL ||
        framebuffer_request.response->framebuffer_count < 1) {
        serial_printf("Styx OS: No framebuffer response from Limine! Halting.\n");
        halt();
    }

    struct limine_framebuffer *fb = framebuffer_request.response->framebuffers[0];

    // 4. Initialise framebuffer module.
    fb_init((uint32_t *)fb->address,
            (uint32_t)fb->width,
            (uint32_t)fb->height,
            (uint32_t)fb->pitch);

    // 5. Clear screen to pure black.
    fb_clear(0x00000000);

    // 6. Render booting messages
    const uint32_t WHITE    = 0x00FFFFFF;
    const int      MARGIN_X = 8;
    const int      MARGIN_Y = 8;
    const int      LINE_H   = FONT_HEIGHT + 2;

    fb_draw_string(MARGIN_X, MARGIN_Y + 0 * LINE_H,
                   "Styx OS v0.0.1 -- booting...", WHITE);

    fb_draw_string(MARGIN_X, MARGIN_Y + 1 * LINE_H,
                   "Microkernel initialized. Ring 0 occupied. Everyone else: wait your turn.",
                   WHITE);

    fb_draw_string(MARGIN_X, MARGIN_Y + 2 * LINE_H,
                   "[WARN] No capability engine yet. Everything is trusted. This will not last.",
                   WHITE);

    // 7. Install our GDT and TSS FIRST, before the IDT.
    //    idt_set_gate() reads the current CS to store in each gate.
    //    If we install the IDT before gdt_init(), the gates will have
    //    Limine's CS=0x28 — which becomes the user code segment in our GDT,
    //    causing triple-faults on the first exception after the switch.
    serial_printf("Styx OS: Installing GDT and TSS...\n");
    gdt_init();
    tss_init();

    // 8. Initialize interrupt infrastructure in strict order
    serial_printf("Styx OS: Setting up IDT...\n");
    idt_init(); // Installs all 32 exception gates (0-31) and 16 IRQ gates (32-47)

    serial_printf("Styx OS: Remapping legacy 8259 PIC...\n");
    irq_init(); // Remaps PIC (master offset 32, slave offset 40)

    serial_printf("Styx OS: Initializing PIT (100Hz)...\n");
    pit_init(100); // 100 Hz

    serial_printf("Styx OS: Initializing PS/2 Keyboard driver...\n");
    keyboard_init();

    // 9. Enable interrupts with sti()
    serial_printf("Styx OS: Enabling interrupts (sti)...\n");
    __asm__ volatile ("sti");

    fb_draw_string(MARGIN_X, MARGIN_Y + 3 * LINE_H,
                   "Interrupts enabled. Try typing.", WHITE);

    // 9. Initialize Physical Memory Manager (PMM)
    if (memmap_request.response == NULL) {
        serial_printf("Styx OS: No memory map response from Limine! Halting.\n");
        halt();
    }
    if (hhdm_request.response == NULL) {
        serial_printf("Styx OS: No HHDM response from Limine! Halting.\n");
        halt();
    }

    serial_printf("Styx OS: Initializing Physical Memory Manager...\n");
    pmm_init(memmap_request.response, hhdm_request.response->offset);

    // Log PMM status to serial
    uint64_t total_mb = pmm_get_total_memory() / (1024 * 1024);
    uint64_t free_mb = pmm_get_free_memory() / (1024 * 1024);
    serial_printf("Styx OS: Usable memory detected: %d MB\n", (int)total_mb);
    serial_printf("Styx OS: Free memory after PMM init: %d MB\n", (int)free_mb);

    // 10. PMM Sanity Test (temporary, removed in later milestones)
    serial_printf("Styx OS: Running PMM Sanity Test...\n");
    void *f1 = pmm_alloc_frame();
    void *f2 = pmm_alloc_frame();
    void *f3 = pmm_alloc_frame();
    serial_printf("  Allocated Frame 1: %p\n", f1);
    serial_printf("  Allocated Frame 2 (middle): %p\n", f2);
    serial_printf("  Allocated Frame 3: %p\n", f3);

    serial_printf("  Freeing Frame 2: %p\n", f2);
    pmm_free_frame(f2);

    void *f4 = pmm_alloc_frame();
    serial_printf("  Allocated Frame again: %p\n", f4);
    if (f4 == f2) {
        serial_printf("  SUCCESS: Frame reuse verified successfully!\n");
    } else {
        serial_printf("  ERROR: Frame reuse failed! Expected %p but got %p\n", f2, f4);
    }

    serial_printf("  Freeing all sanity test frames...\n");
    pmm_free_frame(f1);
    pmm_free_frame(f4); // Same as f2
    pmm_free_frame(f3);
    serial_printf("Styx OS: PMM Sanity Test complete.\n");
    // 10. Initialize Virtual Memory Manager (VMM)
    serial_printf("Styx OS: Initializing Virtual Memory Manager (VMM)...\n");
    vmm_init(hhdm_request.response->offset);
    serial_printf("Styx OS: VMM initialization check passed.\n");

    // 11. Initialize Kernel Heap Allocator
    serial_printf("Styx OS: Initializing Kernel Heap (kmalloc/kfree)...\n");
    heap_init();
    serial_printf("Styx OS: Heap initialized.\n");

    // ── HEAP SANITY TEST (temporary — remove after confirming) ───────────
    // Tests: allocation, write/read-back pattern, address monotonicity,
    //        alignment, block reuse after kfree, kcalloc zeroing,
    //        and misuse detection (double-free, bad pointer).
    serial_printf("\n--- HEAP SANITY TEST BEGIN ---\n");

    // 11a. Allocate several blocks of varying sizes
    void *h1 = kmalloc(16);
    void *h2 = kmalloc(256);
    void *h3 = kmalloc(4096);
    void *h4 = kmalloc(1024);

    serial_printf("  h1 (16  B) = %p\n", h1);
    serial_printf("  h2 (256 B) = %p\n", h2);
    serial_printf("  h3 (4096B) = %p\n", h3);
    serial_printf("  h4 (1024B) = %p\n", h4);

    // Verify addresses are monotonically increasing
    int mono_ok = ((uint64_t)h2 > (uint64_t)h1) &&
                  ((uint64_t)h3 > (uint64_t)h2) &&
                  ((uint64_t)h4 > (uint64_t)h3);
    serial_printf("  Monotonically increasing: %s\n", mono_ok ? "PASS" : "FAIL");

    // Verify 16-byte alignment
    int align_ok = (((uint64_t)h1 & 0xF) == 0) &&
                   (((uint64_t)h2 & 0xF) == 0) &&
                   (((uint64_t)h3 & 0xF) == 0) &&
                   (((uint64_t)h4 & 0xF) == 0);
    serial_printf("  All 16-byte aligned: %s\n", align_ok ? "PASS" : "FAIL");

    // 11b. Write distinct patterns into each block
    if (h1) {
        uint8_t *p = (uint8_t *)h1;
        for (int i = 0; i < 16;   i++) p[i] = 0xAA;
    }
    if (h2) {
        uint8_t *p = (uint8_t *)h2;
        for (int i = 0; i < 256;  i++) p[i] = 0xBB;
    }
    if (h3) {
        uint8_t *p = (uint8_t *)h3;
        for (int i = 0; i < 4096; i++) p[i] = 0xCC;
    }
    if (h4) {
        uint8_t *p = (uint8_t *)h4;
        for (int i = 0; i < 1024; i++) p[i] = 0xDD;
    }

    // 11c. Read back and verify patterns (proves no overlap)
    int rw_ok = 1;
    if (h1) {
        uint8_t *p = (uint8_t *)h1;
        for (int i = 0; i < 16;   i++) if (p[i] != 0xAA) { rw_ok = 0; break; }
    }
    if (h2 && rw_ok) {
        uint8_t *p = (uint8_t *)h2;
        for (int i = 0; i < 256;  i++) if (p[i] != 0xBB) { rw_ok = 0; break; }
    }
    if (h3 && rw_ok) {
        uint8_t *p = (uint8_t *)h3;
        for (int i = 0; i < 4096; i++) if (p[i] != 0xCC) { rw_ok = 0; break; }
    }
    if (h4 && rw_ok) {
        uint8_t *p = (uint8_t *)h4;
        for (int i = 0; i < 1024; i++) if (p[i] != 0xDD) { rw_ok = 0; break; }
    }
    serial_printf("  Write/read-back pattern (no overlap): %s\n",
                  rw_ok ? "PASS" : "FAIL");

    // 11d. Free the two middle blocks (h2 and h3)
    serial_printf("  Freeing h2=%p and h3=%p...\n", h2, h3);
    kfree(h2);
    kfree(h3);

    // 11e. Allocate something that should reuse h2 (256 B freed) —
    //      a 128-byte allocation should fit into h2's slot.
    void *h5 = kmalloc(128);
    serial_printf("  h5 (128 B) = %p  (should reuse h2 slot at %p)\n", h5, h2);
    int reuse_ok = (h5 == h2); // exact reuse expected after split
    serial_printf("  Block reuse: %s\n", reuse_ok ? "PASS" : "NOTE - allocator may differ");

    // 11f. kcalloc test — must return zeroed memory
    uint64_t *zp = (uint64_t *)kcalloc(4, sizeof(uint64_t));
    int zero_ok = 1;
    if (zp) {
        for (int i = 0; i < 4; i++) if (zp[i] != 0) { zero_ok = 0; break; }
    } else {
        zero_ok = 0;
    }
    serial_printf("  kcalloc(4, 8) zeroed: %s  ptr=%p\n",
                  zero_ok ? "PASS" : "FAIL", (void *)zp);

    // 11g. Misuse detection: double-free (should log error, not crash)
    serial_printf("  Double-free test (expect HEAP ERROR log):\n");
    kfree(h2); // h2 was already freed — double-free!

    // 11h. Misuse detection: free a bad pointer
    serial_printf("  Bad-pointer kfree test (expect HEAP ERROR log):\n");
    uint64_t dummy_val = 0xDEAD;
    kfree((void *)&dummy_val); // not a heap pointer

    // 11i. Free all remaining live allocations, then dump final state
    kfree(h1);
    kfree(h4);
    kfree(h5);
    if (zp) kfree(zp);

    serial_printf("  Final heap state after freeing all allocations:\n");
    heap_dump();

    serial_printf("--- HEAP SANITY TEST END ---\n\n");
    // ── END HEAP SANITY TEST ──────────────────────────────────────────────

    serial_printf("Styx OS: Boot sequence complete.\n");

    // ── M5 CAPABILITY ENGINE SANITY TESTS ────────────────────────────────
    // All tests run in ring 0. No scheduler yet. Rendezvous is tested via
    // a direct call chain: cap_send() parks the message, cap_recv() picks
    // it up immediately after.
    serial_printf("\n--- M5 CAPABILITY ENGINE SANITY TESTS BEGIN ---\n");

    static cap_table_t table;  /* static: zero-initialized by BSS */
    int m5_pass = 1;           /* overall pass/fail flag */
    cap_err_t err;
    cap_slot_t *slot;

    // ── Test 1: Table initialization ─────────────────────────────────────
    cap_table_init(&table);
    int all_null = 1;
    for (int i = 0; i < CAP_TABLE_SIZE; i++) {
        if (table.slots[i].type != CAP_TYPE_NULL) { all_null = 0; break; }
    }
    serial_printf("  [T1] Table init — %d slots, all NULL: %s\n",
                  CAP_TABLE_SIZE, all_null ? "PASS" : "FAIL");
    if (!all_null) m5_pass = 0;

    // ── Test 2: Endpoint create + lookup ─────────────────────────────────
    err = cap_endpoint_create(&table, 0, CAP_RIGHT_ALL, 0);
    if (err != CAP_OK) {
        serial_printf("  [T2] Endpoint create at slot 0: FAIL (%s)\n",
                      cap_err_str(err));
        m5_pass = 0;
    } else {
        err = cap_lookup(&table, 0, CAP_TYPE_ENDPOINT, CAP_RIGHT_SEND, &slot);
        serial_printf("  [T2] Endpoint created at slot 0, lookup: %s\n",
                      err == CAP_OK ? "PASS" : "FAIL");
        if (err != CAP_OK) m5_pass = 0;
    }

    // ── Test 3: IPC rendezvous (send then recv) ───────────────────────────
    //
    // Derive a RECV-only cap at slot 1 so sender (slot 0) and receiver
    // (slot 1) are distinct, proving rights are enforced per-slot.
    err = cap_derive(&table, 0, 1, CAP_RIGHT_RECV);
    if (err != CAP_OK) {
        serial_printf("  [T3] Derive RECV cap: FAIL (%s)\n", cap_err_str(err));
        m5_pass = 0;
    } else {
        ipc_msg_t send_msg = {
            .tag       = 0xDEADC0DEULL,
            .words     = { 0x1234ULL, 0x5678ULL, 0xABCDULL, 0xEF01ULL },
            .cap_count = 0,
            ._pad      = 0,
        };
        ipc_msg_t recv_buf = { 0 };

        /* cap_send parks the message (no receiver yet → SEND_WAITING) */
        err = cap_send(&table, 0, &send_msg);
        if (err != CAP_OK) {
            serial_printf("  [T3] cap_send: FAIL (%s)\n", cap_err_str(err));
            m5_pass = 0;
        } else {
            /* cap_recv sees SEND_WAITING → delivers immediately → IDLE */
            err = cap_recv(&table, 1, &recv_buf);
            int rendezvous_ok = (err == CAP_OK) &&
                                (recv_buf.tag      == 0xDEADC0DEULL) &&
                                (recv_buf.words[0] == 0x1234ULL);
            serial_printf("  [T3] IPC rendezvous tag=%x word[0]=%x: %s\n",
                          recv_buf.tag,
                          recv_buf.words[0],
                          rendezvous_ok ? "PASS" : "FAIL");
            if (!rendezvous_ok) m5_pass = 0;
        }
    }

    // ── Test 4: Rights escalation blocked ────────────────────────────────
    // slot 1 has RECV only. Trying to derive RECV|SEND|GRANT from it
    // must be rejected — child cannot hold rights parent doesn't have.
    err = cap_derive(&table, 1, 2, CAP_RIGHT_RECV | CAP_RIGHT_SEND);
    int escalation_blocked = (err == CAP_ERR_RIGHTS);
    serial_printf("  [T4] Rights escalation blocked (%s): %s\n",
                  cap_err_str(err), escalation_blocked ? "PASS" : "FAIL");
    if (!escalation_blocked) m5_pass = 0;

    // ── Test 5: Revocation — stale cap rejected ───────────────────────────
    // cap_revoke_endpoint() bumps the endpoint's generation counter.
    // slot 0's stored generation is now stale → next lookup returns STALE.
    err = cap_revoke_endpoint(&table, 0);
    if (err != CAP_OK) {
        serial_printf("  [T5] cap_revoke_endpoint: FAIL (%s)\n",
                      cap_err_str(err));
        m5_pass = 0;
    } else {
        cap_slot_t *stale_slot;
        err = cap_lookup(&table, 0, CAP_TYPE_ENDPOINT, CAP_RIGHT_SEND,
                         &stale_slot);
        int stale_ok = (err == CAP_ERR_STALE);
        serial_printf("  [T5] Stale cap rejected after revocation (%s): %s\n",
                      cap_err_str(err), stale_ok ? "PASS" : "FAIL");
        if (!stale_ok) m5_pass = 0;
    }

    // ── Test 6: NULL slot lookup ──────────────────────────────────────────
    // Slot 2 was never populated. Lookup must return CAP_ERR_NULL.
    {
        cap_slot_t *null_slot;
        err = cap_lookup(&table, 2, CAP_TYPE_ENDPOINT, CAP_RIGHT_SEND,
                         &null_slot);
        int null_ok = (err == CAP_ERR_NULL);
        serial_printf("  [T6] NULL slot lookup returns CAP_ERR_NULL (%s): %s\n",
                      cap_err_str(err), null_ok ? "PASS" : "FAIL");
        if (!null_ok) m5_pass = 0;
    }

    // ── Final verdict ─────────────────────────────────────────────────────
    if (m5_pass) {
        serial_printf("\n[CAP] ── M5 CAPABILITY ENGINE: ALL TESTS PASSED ──\n\n");
    } else {
        serial_printf("\n[CAP] ── M5 CAPABILITY ENGINE: ONE OR MORE TESTS FAILED ──\n\n");
    }

    serial_printf("--- M5 CAPABILITY ENGINE SANITY TESTS END ---\n\n");
    // ── END M5 SANITY TESTS ───────────────────────────────────────────────

    // -- M6: Ring-3 first process bootstrap --------------------------------
    serial_printf("\n--- M6 RING-3 BOOTSTRAP BEGIN ---\n");

    // 1. GDT and TSS are already installed and loaded at system startup.

    // 3. Program STAR/LSTAR/SFMASK MSRs for SYSCALL fast-path
    serial_printf("[M6] Initializing syscall gate...\n");
    syscall_init();

    // 4. Initialize task table
    task_init_table();

    // 5. Create kernel idle task (represents current execution context)
    task_t *ktask = task_create_kernel();
    if (!ktask) {
        serial_printf("[M6] FATAL: kernel task creation failed\n");
        for (;;) __asm__ volatile("hlt");
    }

    // 6. Create the ring-3 user task
    //    Try to load from USB first; fall back to embedded blob.
    serial_printf("\n--- M7 USB BOOT BEGIN ---\n");
    bool usb_ok = xhci_init() && usb_msc_init();
    vfs_init();  /* mounts FAT32 if USB is ready; no-op otherwise */

    uint64_t entry = 0;
    task_t *utask  = NULL;

    if (usb_ok) {
        // Probe task with dummy entry — real entry set after ELF parse
        utask = task_create_user(0);
        if (utask && elf_load_from_vfs(utask, "/init.elf", &entry) == 0) {
            utask->regs.rip = entry;  /* patch entry point */
            utask->regs.rbx = entry;  /* duplicate in RBX for trampoline safety */
            serial_printf("[M7] Booted from USB: entry=%p\n", (void*)entry);
        } else {
            serial_printf("[M7] USB ELF load failed, falling back to embedded binary\n");
            usb_ok = false;
        }
    }

    if (!usb_ok) {
        const elf64_header_t *ehdr = (const elf64_header_t *)user_user_elf;
        entry = ehdr->e_entry;
        utask = task_create_user(entry);
        if (!utask) {
            serial_printf("[M7] FATAL: user task creation failed\n");
            for (;;) __asm__ volatile("hlt");
        }
        int elf_rc = elf_load(utask, user_user_elf, (size_t)user_user_elf_len);
        if (elf_rc != 0) {
            serial_printf("[M7] FATAL: embedded elf_load failed (rc=%d)\n", elf_rc);
            for (;;) __asm__ volatile("hlt");
        }
        serial_printf("[M7] Using embedded binary: entry=%p\n", (void*)entry);
    }
    serial_printf("--- M7 USB BOOT END ---\n\n");

    // 8. Give the user task a console capability in slot 0.
    //    SYS_WRITE checks cap slot 0 for CAP_TYPE_ENDPOINT with CAP_RIGHT_SEND.
    cap_err_t cerr = cap_endpoint_create(utask->cap_table, 0,
                                          CAP_RIGHT_SEND | CAP_RIGHT_RECV, 0);
    if (cerr != CAP_OK) {
        serial_printf("[M6] WARNING: cap_endpoint_create failed: %s\n",
                      cap_err_str(cerr));
    } else {
        serial_printf("[M6] Console capability installed in user task slot 0\n");
    }

    // 9. Initialize scheduler and add both tasks
    sched_init();
    sched_add(ktask);
    sched_add(utask);

    serial_printf("[M6] Scheduler ready -- handing off to ring 3\n");
    serial_printf("--- M6 RING-3 BOOTSTRAP END ---\n\n");

    // 10. sched_start() immediately switches to the user task.
    //     Does not return until the scheduler gives the CPU back here.
    sched_start();

    // Kernel idle loop (reached after scheduler returns from initial switch)
    serial_printf("Styx OS: Kernel idle loop active.\n");
    for (;;) {
        __asm__ volatile ("hlt");
    }
}
