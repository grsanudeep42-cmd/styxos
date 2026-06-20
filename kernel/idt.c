#include "idt.h"
#include "serial.h"
#include <stddef.h>

static struct idt_entry idt[256];
static struct idt_ptr idt_p;

// Declare all exception ISRs
extern void isr0(void);
extern void isr1(void);
extern void isr2(void);
extern void isr3(void);
extern void isr4(void);
extern void isr5(void);
extern void isr6(void);
extern void isr7(void);
extern void isr8(void);
extern void isr9(void);
extern void isr10(void);
extern void isr11(void);
extern void isr12(void);
extern void isr13(void);
extern void isr14(void);
extern void isr15(void);
extern void isr16(void);
extern void isr17(void);
extern void isr18(void);
extern void isr19(void);
extern void isr20(void);
extern void isr21(void);
extern void isr22(void);
extern void isr23(void);
extern void isr24(void);
extern void isr25(void);
extern void isr26(void);
extern void isr27(void);
extern void isr28(void);
extern void isr29(void);
extern void isr30(void);
extern void isr31(void);

// Declare all IRQ ISRs
extern void irq32(void);
extern void irq33(void);
extern void irq34(void);
extern void irq35(void);
extern void irq36(void);
extern void irq37(void);
extern void irq38(void);
extern void irq39(void);
extern void irq40(void);
extern void irq41(void);
extern void irq42(void);
extern void irq43(void);
extern void irq44(void);
extern void irq45(void);
extern void irq46(void);
extern void irq47(void);

// Helper arrays
static void (*isr_table[32])(void) = {
    isr0, isr1, isr2, isr3, isr4, isr5, isr6, isr7,
    isr8, isr9, isr10, isr11, isr12, isr13, isr14, isr15,
    isr16, isr17, isr18, isr19, isr20, isr21, isr22, isr23,
    isr24, isr25, isr26, isr27, isr28, isr29, isr30, isr31
};

static void (*irq_table[16])(void) = {
    irq32, irq33, irq34, irq35, irq36, irq37, irq38, irq39,
    irq40, irq41, irq42, irq43, irq44, irq45, irq46, irq47
};

void idt_set_gate(uint8_t vector, void (*isr)(void), uint8_t attributes) {
    uint64_t addr = (uint64_t)isr;
    uint16_t cs;
    __asm__ volatile ("mov %%cs, %0" : "=r"(cs));

    idt[vector].isr_low = addr & 0xFFFF;
    idt[vector].kernel_cs = cs;
    idt[vector].ist = 0;
    idt[vector].attributes = attributes;
    idt[vector].isr_mid = (addr >> 16) & 0xFFFF;
    idt[vector].isr_high = (addr >> 32) & 0xFFFFFFFF;
    idt[vector].reserved = 0;
}

void idt_init(void) {
    uint16_t cs;
    __asm__ volatile ("mov %%cs, %0" : "=r"(cs));
    serial_printf("IDT Init: Detected Code Segment (CS) selector: %x\n", (uint32_t)cs);

    // Initialize IDT to zeroes
    for (int i = 0; i < 256; i++) {
        idt[i].isr_low = 0;
        idt[i].kernel_cs = 0;
        idt[i].ist = 0;
        idt[i].attributes = 0;
        idt[i].isr_mid = 0;
        idt[i].isr_high = 0;
        idt[i].reserved = 0;
    }

    // Set up exception handlers (0-31)
    for (int i = 0; i < 32; i++) {
        if (isr_table[i] != NULL) {
            idt_set_gate(i, isr_table[i], 0x8E); // Present, Ring 0, 64-bit Interrupt Gate
        }
    }

    // Set up IRQ handlers (32-47)
    for (int i = 0; i < 16; i++) {
        if (irq_table[i] != NULL) {
            idt_set_gate(32 + i, irq_table[i], 0x8E); // Present, Ring 0, 64-bit Interrupt Gate
        }
    }

    // Load IDT
    idt_p.limit = sizeof(idt) - 1;
    idt_p.base = (uint64_t)&idt;
    __asm__ volatile ("lidt %0" : : "m"(idt_p));
}
