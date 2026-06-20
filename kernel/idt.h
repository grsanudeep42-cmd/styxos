#pragma once

#include <stdint.h>

struct idt_entry {
    uint16_t isr_low;      // Low 16 bits of handler address
    uint16_t kernel_cs;    // Code segment selector
    uint8_t  ist;          // Interrupt Stack Table offset (usually 0)
    uint8_t  attributes;   // Type and attributes (e.g. 0x8E)
    uint16_t isr_mid;      // Bits 16-31 of handler address
    uint32_t isr_high;     // Bits 32-63 of handler address
    uint32_t reserved;     // Set to 0
} __attribute__((packed));

struct idt_ptr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

void idt_init(void);
void idt_set_gate(uint8_t vector, void (*isr)(void), uint8_t attributes);
