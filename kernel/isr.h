#pragma once

#include <stdint.h>

struct registers {
    uint64_t r15, r14, r13, r12, rbp, rbx, r11, r10, r9, r8, rax, rcx, rdx, rsi, rdi;
    uint64_t vector_number;
    uint64_t error_code;
    uint64_t rip, cs, rflags, rsp, ss;
};

void isr_handler(uint64_t vector, struct registers *regs);
