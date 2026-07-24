; syscalls.s — Ring-3 System Call Assembly Wrappers
bits 64

global sys_yield
global sys_write
global sys_cap_send
global sys_cap_recv
global sys_exit
global sys_read_key
global sys_tor_cell
global sys_sysinfo

section .text

; int64_t sys_yield(void)
sys_yield:
    mov rax, 0
    syscall
    ret

; int64_t sys_write(uint32_t slot, const char *buf, size_t len)
; RDI = slot, RSI = buf, RDX = len
sys_write:
    mov rax, 1
    syscall
    ret

; int64_t sys_cap_send(uint32_t slot, const void *msg)
sys_cap_send:
    mov rax, 2
    syscall
    ret

; int64_t sys_cap_recv(uint32_t slot, void *msg)
sys_cap_recv:
    mov rax, 3
    syscall
    ret

; int64_t sys_exit(int code)
sys_exit:
    mov rax, 4
    syscall
    ret

; int64_t sys_read_key(uint32_t slot)
sys_read_key:
    mov rax, 5
    syscall
    ret

; int64_t sys_tor_cell(uint32_t slot, const void *buf, size_t len)
sys_tor_cell:
    mov rax, 6
    syscall
    ret

; int64_t sys_sysinfo(void *info_buf)
sys_sysinfo:
    mov rax, 7
    syscall
    ret
