#include "irq.h"
#include "io.h"
#include "serial.h"
#include <stddef.h>

static irq_handler_t irq_handlers[16] = {NULL};

void irq_install_handler(int irq, irq_handler_t handler) {
    if (irq >= 0 && irq < 16) {
        irq_handlers[irq] = handler;
    }
}

void irq_uninstall_handler(int irq) {
    if (irq >= 0 && irq < 16) {
        irq_handlers[irq] = NULL;
    }
}

void irq_init(void) {
    // ICW1: Init command to both master and slave PICs
    outb(0x20, 0x11);
    io_wait();
    outb(0xA0, 0x11);
    io_wait();

    // ICW2: Remap vectors
    // Master offset: 32 (vectors 32-39)
    // Slave offset:  40 (vectors 40-47)
    outb(0x21, 0x20);
    io_wait();
    outb(0xA1, 0x28);
    io_wait();

    // ICW3: Cascading setup
    outb(0x21, 0x04); // Slave is at IRQ2
    io_wait();
    outb(0xA1, 0x02); // Slave identity
    io_wait();

    // ICW4: Environment setup (8086 mode)
    outb(0x21, 0x01);
    io_wait();
    outb(0xA1, 0x01);
    io_wait();

    // Mask all interrupts initially, enabling only PIT (IRQ0) and Keyboard (IRQ1)
    // PIT is bit 0, Keyboard is bit 1. So mask is ~(1 | 2) = ~3 = 0xFC.
    // Slave PIC is fully masked (0xFF)
    outb(0x21, 0xFC);
    outb(0xA1, 0xFF);
}

void irq_handler(uint64_t vector, struct registers *regs) {
    uint64_t irq = vector - 32;

    if (irq < 16) {
        if (irq_handlers[irq] != NULL) {
            irq_handlers[irq](regs);
        }
    }

    // Send EOI (End of Interrupt) to PICs
    // If IRQ came from Slave PIC (IRQ 8-15), we must send EOI to both Slave and Master
    if (irq >= 8) {
        outb(0xA0, 0x20);
    }
    outb(0x20, 0x20);
}
