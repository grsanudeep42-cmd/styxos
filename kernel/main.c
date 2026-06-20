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

/* ── Limine protocol requests ─────────────────────────────────────────── */

__attribute__((used, section(".limine_requests")))
LIMINE_BASE_REVISION(3)

__attribute__((used, section(".limine_requests")))
static volatile struct limine_framebuffer_request framebuffer_request = {
    .id       = LIMINE_FRAMEBUFFER_REQUEST,
    .revision = 0
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_memmap_request memmap_request = {
    .id       = LIMINE_MEMMAP_REQUEST,
    .revision = 0
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_hhdm_request hhdm_request = {
    .id       = LIMINE_HHDM_REQUEST,
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

    // 7. Initialize interrupt infrastructure in strict order
    serial_printf("Styx OS: Setting up IDT...\n");
    idt_init(); // Installs all 32 exception gates (0-31) and 16 IRQ gates (32-47)

    serial_printf("Styx OS: Remapping legacy 8259 PIC...\n");
    irq_init(); // Remaps PIC (master offset 32, slave offset 40)

    serial_printf("Styx OS: Initializing PIT (100Hz)...\n");
    pit_init(100); // 100 Hz

    serial_printf("Styx OS: Initializing PS/2 Keyboard driver...\n");
    keyboard_init();

    // 8. Enable interrupts with sti()
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

    serial_printf("Styx OS: Boot sequence complete. Entering idle loop.\n");



    // 9. Idle loop: CPU goes to low-power state and wakes up on interrupts
    for (;;) {
        __asm__ volatile ("hlt");
    }
}
