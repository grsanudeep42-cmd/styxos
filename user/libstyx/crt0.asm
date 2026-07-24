; crt0.asm — Ring-3 process entry point for StyxOS
bits 64

global _start
extern main
extern sys_exit

section .text
_start:
    ; Terminate stack frame backtrace
    xor rbp, rbp

    ; Call main(argc=0, argv=NULL)
    xor rdi, rdi
    xor rsi, rsi
    call main

    ; Exit process with main return code (RAX)
    mov rdi, rax
    call sys_exit

    ; Infinite fallback loop if sys_exit returns
.hang:
    hlt
    jmp .hang
