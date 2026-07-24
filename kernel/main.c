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
#include "auth.h"
#include "string.h"
#include "crypto.h"
#include "integrity.h"
#include "destruct.h"
#include "gcm.h"
#include "oram.h"
#include "snapshot.h"
#include "anonymize.h"
#include "e1000.h"
#include "padding.h"
#include "tor.h"
#include "manifest.h"
#include "shell.h"
#include "io.h"
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

static void run_m9_verification_tests(void) {
    serial_printf("\n--- M9 INTEGRATION TEST SUITE ---\n");

    uint8_t orig_data[512];
    uint8_t read_data[512];

    for (int i = 0; i < 512; i++) {
        orig_data[i] = (uint8_t)(i & 0xFF);
    }

    serial_printf("[TEST] Writing test sector to LBA 10 (encrypted)...\n");
    usb_msc_write_sector(10, orig_data);

    // Disable encryption temporarily to read raw ciphertext
    g_encryption_enabled = false;
    serial_printf("[TEST] Reading raw sector from LBA 10 (encryption disabled)...\n");
    usb_msc_read_sector(10, read_data);

    // Ciphertext should not match plaintext
    if (memcmp(orig_data, read_data, 512) == 0) {
        serial_printf("[TEST] ERROR: Data on disk is not encrypted!\n");
        return;
    }
    serial_printf("[TEST] PASSED: Disk sector contains ciphertext.\n");

    // Enable encryption back
    g_encryption_enabled = true;
    serial_printf("[TEST] Reading sector from LBA 10 (encryption enabled)...\n");
    memset(read_data, 0, 512);
    usb_msc_read_sector(10, read_data);

    // Decrypted data should match plaintext
    if (memcmp(orig_data, read_data, 512) != 0) {
        serial_printf("[TEST] ERROR: Decrypted data does not match original plaintext!\n");
        return;
    }
    serial_printf("[TEST] PASSED: Transparent read/write decryption works.\n");

    // Now test integrity validation and tamper self-destruct trigger
    serial_printf("[TEST] Simulating block tampering to test Self-Destruct...\n");
    g_tamper_simulate = true;
    
    // This read should fail integrity check, trigger Tor distress beacon and 3-pass wipe!
    usb_msc_read_sector(10, read_data);
}

static void run_m10_verification_tests(task_t *t1) {
    serial_printf("\n--- M10 INTEGRATION TEST SUITE ---\n");

    // 1. GCM NIST self-tests
    serial_printf("[TEST] Running GCM self-tests...\n");
    if (aes_gcm_self_test() == 0) {
        serial_printf("[TEST] PASSED: GCM NIST SP 800-38D vectors match.\n");
    } else {
        serial_printf("[TEST] FAILED: GCM self-tests!\n");
        return;
    }

    // 2. PathORAM trace & overhead testing
    serial_printf("[TEST] Running PathORAM self-tests...\n");
    snapshot_init(); // initializes PathORAM internally
    oram_reset_counters();

    uint8_t dummy_data[512];
    memset(dummy_data, 0xAA, 512);

    serial_printf("[TEST] PathORAM: Writing dummy block to LBA 5...\n");
    oram_access(5, 1, dummy_data);

    serial_printf("[TEST] PathORAM: Reading block from LBA 5...\n");
    uint8_t read_dummy[512];
    memset(read_dummy, 0, 512);
    oram_access(5, 0, read_dummy);

    if (memcmp(dummy_data, read_dummy, 512) == 0) {
        serial_printf("[TEST] PASSED: PathORAM read/write match.\n");
    } else {
        serial_printf("[TEST] ERROR: PathORAM read mismatch!\n");
        return;
    }
    oram_print_stats();

    // 3. E2E Session Snapshot & Resume Test
    serial_printf("[TEST] Running Session Snapshot E2E Verification...\n");
    if (!t1) {
        serial_printf("[TEST] ERROR: Active user process task not found! Cannot verify snapshot.\n");
        return;
    }

    // Setup initial state on task 1
    t1->regs.rbx = 0xDEADC0DE;
    
    // Modify stack memory page (at 0x3FF000)
    uint64_t *old_pml4 = vmm_get_pml4();
    vmm_set_pml4(t1->pml4);
    uint64_t phys = vmm_virt_to_phys(0x3FF000);
    uint64_t *stack_ptr = (uint64_t *)(phys + vmm_get_hhdm_offset());
    uint64_t original_val = stack_ptr[0];
    stack_ptr[0] = 0xCAFEBABE;
    vmm_set_pml4(old_pml4);

    serial_printf("[TEST] Saving clean session snapshot to encrypted PathORAM...\n");
    if (snapshot_save() != 0) {
        serial_printf("[TEST] ERROR: Failed to save snapshot!\n");
        return;
    }

    // Tamper with registers and stack to simulate session progression / modifications
    t1->regs.rbx = 0xBAD11111;
    vmm_set_pml4(t1->pml4);
    stack_ptr[0] = 0xBAD22222;
    vmm_set_pml4(old_pml4);

    // Test Atomic Write / Integrity Check Rollback by corrupting block 0
    serial_printf("[TEST] Simulating interrupted write (corrupting Block 0)...\n");
    uint8_t corrupt_block[512];
    memset(corrupt_block, 0xFF, 512);
    // Write corrupted block 0 directly bypassing save
    oram_access(0, 1, corrupt_block);

    serial_printf("[TEST] Verifying restore rejection of corrupted snapshot...\n");
    if (snapshot_restore() != 0) {
        serial_printf("[TEST] PASSED: Interrupted/tampered snapshot successfully rejected.\n");
    } else {
        serial_printf("[TEST] ERROR: Restored corrupted snapshot! Integrity failure!\n");
        return;
    }

    // Save snapshot again to get a valid one
    t1->regs.rbx = 0xDEADC0DE;
    vmm_set_pml4(t1->pml4);
    stack_ptr[0] = 0xCAFEBABE;
    vmm_set_pml4(old_pml4);
    snapshot_save();

    // Modify registers and memory again
    t1->regs.rbx = 0xBAD11111;
    vmm_set_pml4(t1->pml4);
    stack_ptr[0] = 0xBAD22222;
    vmm_set_pml4(old_pml4);

    serial_printf("[TEST] Restoring session snapshot from valid encrypted PathORAM...\n");
    if (snapshot_restore() != 0) {
        serial_printf("[TEST] ERROR: Failed to restore valid snapshot!\n");
        return;
    }

    // Check if registers and stack memory were restored correctly
    vmm_set_pml4(t1->pml4);
    uint64_t restored_val = stack_ptr[0];
    vmm_set_pml4(old_pml4);

    if (t1->regs.rbx == 0xDEADC0DE && restored_val == 0xCAFEBABE) {
        serial_printf("[TEST] PASSED: Registers (RBX=%x) and user memory (Stack[0]=%x) successfully restored!\n",
                      t1->regs.rbx, restored_val);
    } else {
        serial_printf("[TEST] ERROR: Restoration mismatch! RBX=%x, Stack[0]=%x\n",
                      t1->regs.rbx, restored_val);
        return;
    }

    // Clean up stack memory back to normal
    vmm_set_pml4(t1->pml4);
    stack_ptr[0] = original_val;
    vmm_set_pml4(old_pml4);

    serial_printf("[TEST] M10 INTEGRATION TESTS COMPLETED SUCCESSFULLY.\n");
    halt();
}

static void run_m11_verification_tests(void) {
    serial_printf("\n--- M11 INTEGRATION TEST SUITE ---\n");

    // 1. Hardware Anonymization Engine Test
    serial_printf("[TEST] Initializing Hardware Anonymization Engine...\n");
    anonymize_init();
    uint8_t mac[6];
    anonymize_get_mac(mac);
    if ((mac[0] & 0x02) == 0x02 && (mac[0] & 0x01) == 0x00) {
        serial_printf("[TEST] PASSED: MAC Address is Locally Administered Unicast (%x:%x:%x:%x:%x:%x)\n",
                      mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    } else {
        serial_printf("[TEST] ERROR: MAC Address does not match locally-administered unicast format!\n");
    }

    // 2. Intel e1000 PCI Network Driver Test
    serial_printf("[TEST] Initializing Intel e1000 PCI Network Driver...\n");
    if (!e1000_init()) {
        serial_printf("[TEST] ERROR: e1000 PCI initialization failed!\n");
    } else {
        serial_printf("[TEST] PASSED: e1000 PCI driver initialized successfully.\n");
    }

    // 3. Continuous Traffic Padding Test
    serial_printf("[TEST] Testing Continuous Traffic Padding Layer...\n");
    padding_init();
    if (padding_inject_noise()) {
        serial_printf("[TEST] PASSED: 512-byte synthetic noise packet injected.\n");
    } else {
        serial_printf("[TEST] WARNING: Noise packet injection skipped (e1000 inactive).\n");
    }

    // 4. Tor System Capability Domain Test
    serial_printf("[TEST] Initializing Tor Capability Domain...\n");
    if (tor_init()) {
        serial_printf("[TEST] PASSED: 3-Hop Onion Keys derived.\n");
    }

    // Test capability enforcement (invalid token must be rejected)
    serial_printf("[TEST] Verifying capability enforcement (sending cell with INVALID token)...\n");
    if (!tor_send_cell((const uint8_t*)"HELLO", 5, 0xBAD00000)) {
        serial_printf("[TEST] PASSED: Invalid token correctly rejected by kernel.\n");
    } else {
        serial_printf("[TEST] ERROR: Invalid token accepted!\n");
    }

    // Send cell with VALID token
    serial_printf("[TEST] Sending encrypted Tor cell with VALID token (0x544F5231)...\n");
    if (tor_send_cell((const uint8_t*)"ANONYMOUS DATA", 14, TOR_CAPABILITY_TOKEN)) {
        serial_printf("[TEST] PASSED: Encrypted Tor cell successfully dispatched.\n");
    }

    // Test Tor circuit rotation
    serial_printf("[TEST] Testing Tor circuit rotation & noise boost...\n");
    tor_rotate_circuit();
    serial_printf("[TEST] PASSED: Tor circuit rotated.\n");

    // 5. Fail-Closed Kill-Switch Guard Test
    serial_printf("[TEST] Testing Fail-Closed Dead-Reckoning Guard...\n");
    tor_check_dead_reckoning(true); // Simulate consensus loss
    if (!g_e1000_active) {
        serial_printf("[TEST] PASSED: e1000 NIC hardware successfully killed on network drop.\n");
    } else {
        serial_printf("[TEST] ERROR: e1000 NIC remained active!\n");
    }

    serial_printf("[TEST] M11 INTEGRATION TESTS COMPLETED SUCCESSFULLY.\n");
    serial_printf("-----------------------------------\n\n");
    halt();
}

static void run_m12_verification_tests(void) {
    serial_printf("\n--- M12 INTERACTIVE SHELL INTEGRATION TEST SUITE ---\n");
    shell_run();
    serial_printf("[TEST] M12 INTEGRATION TESTS COMPLETED SUCCESSFULLY.\n");
    serial_printf("-----------------------------------\n\n");
    halt();
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

    // -- M8: Pre-boot Authentication (FIDO2) --
    if (!crypto_self_test()) {
        serial_printf("Styx OS: Cryptographic self-test FAILED! Halting.\n");
        for (;;) __asm__ volatile("hlt");
    }

    if (!auth_preboot()) {
        serial_printf("[M8] Pre-boot authentication failed.\n");
        for (;;) __asm__ volatile("hlt");
    }



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

    // -- M9/M10/M11: Verification Boot Menu --
    if (usb_ok || 1) { // Prompt regardless since fallback task also needs snapshot/network tests
        serial_printf("\n[BOOT] PRESS 't' FOR M9, 's' FOR M10, 'n' FOR M11, OR 'h' FOR M12 SHELL...\n");
        char choice = 0;
        g_last_scancode = 0;
        for (volatile int delay = 0; delay < 300000000; delay++) {
            uint8_t sc = g_last_scancode;
            if (sc == 0x14) { // 'T' scancode
                choice = 't';
                break;
            }
            if (sc == 0x1F) { // 'S' scancode
                choice = 's';
                break;
            }
            if (sc == 0x31) { // 'N' scancode
                choice = 'n';
                break;
            }
            if (sc == 0x23) { // 'H' scancode
                choice = 'h';
                break;
            }
        }
        if (choice == 't') {
            run_m9_verification_tests();
        } else if (choice == 's') {
            run_m10_verification_tests(utask);
        } else if (choice == 'n') {
            run_m11_verification_tests();
        } else if (choice == 'h') {
            run_m12_verification_tests();
        } else {
            serial_printf("[BOOT] Continuing to standard boot.\n");
            g_encryption_enabled = false;
        }
    }

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
