#include "isr.h"
#include "serial.h"
#include "fb.h"
#include <stddef.h>

static const char *exception_messages[] = {
    "Division By Zero",
    "Debug",
    "Non Maskable Interrupt",
    "Breakpoint",
    "Into Detected Overflow",
    "Out of Bounds",
    "Invalid Opcode",
    "No Coprocessor",
    "Double Fault",
    "Coprocessor Segment Overrun",
    "Bad TSS",
    "Segment Not Present",
    "Stack Fault",
    "General Protection Fault",
    "Page Fault",
    "Unknown Interrupt",
    "Coprocessor Fault",
    "Alignment Check",
    "Machine Check",
    "SIMD Floating-Point",
    "Virtualization",
    "Control Protection",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Hypervisor Injection",
    "VMM Communication",
    "Security Exception",
    "Reserved"
};

void isr_handler(uint64_t vector, struct registers *regs) {
    serial_printf("\n================ EXCEPTION OCCURRED ================\n");
    if (vector < 32) {
        serial_printf("Exception: %s (%d)\n", exception_messages[vector], vector);
    } else {
        serial_printf("Exception: Unknown (%d)\n", vector);
    }
    serial_printf("Error Code: %x\n", regs->error_code);
    serial_printf("RIP: %p   CS: %x   RFLAGS: %p\n", regs->rip, regs->cs, regs->rflags);
    serial_printf("RSP: %p   SS: %x\n", regs->rsp, regs->ss);
    serial_printf("RAX: %p   RBX: %p   RCX: %p   RDX: %p\n", regs->rax, regs->rbx, regs->rcx, regs->rdx);
    serial_printf("RSI: %p   RDI: %p   RBP: %p\n", regs->rsi, regs->rdi, regs->rbp);
    serial_printf("R8:  %p   R9:  %p   R10: %p   R11: %p\n", regs->r8, regs->r9, regs->r10, regs->r11);
    serial_printf("R12: %p   R13: %p   R14: %p   R15: %p\n", regs->r12, regs->r13, regs->r14, regs->r15);
    serial_printf("====================================================\n");

    // Clear screen and show panic message in white on dark red
    fb_clear(0x00880000); // Dark red background
    fb_draw_string(20, 20, "!!! KERNEL PANIC !!!", 0x00FFFFFF);
    
    if (vector < 32) {
        fb_draw_string(20, 40, exception_messages[vector], 0x00FFFFFF);
    } else {
        fb_draw_string(20, 40, "Unknown exception", 0x00FFFFFF);
    }
    fb_draw_string(20, 60, "System halted. Check serial log.", 0x00FFFFFF);

    // Halt the CPU completely
    __asm__ volatile ("cli");
    for (;;) __asm__ volatile ("hlt");
}
