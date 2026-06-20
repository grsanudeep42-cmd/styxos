.intel_syntax noprefix

.extern irq_handler

.macro IRQ num
.global irq\num
irq\num:
    push 0
    push \num
    jmp irq_common_stub
.endm

# Define all 16 IRQs (vectors 32-47)
IRQ 32
IRQ 33
IRQ 34
IRQ 35
IRQ 36
IRQ 37
IRQ 38
IRQ 39
IRQ 40
IRQ 41
IRQ 42
IRQ 43
IRQ 44
IRQ 45
IRQ 46
IRQ 47

.global irq_common_stub
irq_common_stub:
    # 1. Save all general-purpose registers
    push rdi
    push rsi
    push rdx
    push rcx
    push rax
    push r8
    push r9
    push r10
    push r11
    push rbx
    push rbp
    push r12
    push r13
    push r14
    push r15

    # 2. Pass arguments to irq_handler(vector, regs)
    mov rdi, [rsp + 120]  # vector number (RSP + 15 * 8)
    mov rsi, rsp          # struct registers* pointer
    cld                   # Clear direction flag (System V ABI requirement)
    call irq_handler

    # 3. Restore all general-purpose registers
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbp
    pop rbx
    pop r11
    pop r10
    pop r9
    pop r8
    pop rax
    pop rcx
    pop rdx
    pop rsi
    pop rdi

    # 4. Clean up the vector number and dummy error code from the stack
    add rsp, 16

    # 5. Return from interrupt (64-bit return)
    iretq

.section .note.GNU-stack,"",@progbits

