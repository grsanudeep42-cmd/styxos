/* crt0.S — Ring-3 Process Entry Point for StyxOS */
.global _start
.extern main
.extern sys_exit

.text
_start:
    /* Clear stack frame for backtrace termination */
    xor %rbp, %rbp

    /* Call main(argc=0, argv=NULL) */
    xor %rdi, %rdi
    xor %rsi, %rsi
    call main

    /* Call sys_exit(main_return_code) */
    mov %rax, %rdi
    call sys_exit

.hang:
    hlt
    jmp .hang
