#include "pit.h"
#include "io.h"
#include "irq.h"
#include "serial.h"

static volatile uint64_t pit_ticks = 0;

static void pit_callback(struct registers *regs) {
    (void)regs;
    pit_ticks++;
    // Log tick counts to serial periodically for debugging (every 200 ticks = ~2 seconds)
    if (pit_ticks % 200 == 0) {
        serial_printf("PIT: %d ticks\n", pit_ticks);
    }
}

void pit_init(uint32_t frequency) {
    // 1. Install PIT IRQ0 handler (master PIC IRQ0 corresponds to vector 32)
    irq_install_handler(0, pit_callback);

    // 2. Set up PIT frequency
    // The oscillator runs at 1193182 Hz
    uint32_t divisor = 1193182 / frequency;

    // Send command: Channel 0, access lobyte/hibyte, square wave generator, binary mode (0x36)
    outb(0x43, 0x36);
    io_wait();

    // Send divisor bytes
    outb(0x40, (uint8_t)(divisor & 0xFF));
    io_wait();
    outb(0x40, (uint8_t)((divisor >> 8) & 0xFF));
    io_wait();
    
    serial_printf("PIT Init: Timer set to %d Hz (divisor %d)\n", frequency, divisor);
}

uint64_t pit_get_ticks(void) {
    return pit_ticks;
}
