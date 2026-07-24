/* syscalls.S — Ring-3 System Call Assembly Wrappers */
.global sys_yield
.global sys_write
.global sys_cap_send
.global sys_cap_recv
.global sys_exit
.global sys_read_key
.global sys_tor_cell
.global sys_sysinfo

.text

/* int64_t sys_yield(void) */
sys_yield:
    mov $0, %rax
    syscall
    ret

/* int64_t sys_write(uint32_t slot, const char *buf, size_t len) */
sys_write:
    mov $1, %rax
    syscall
    ret

/* int64_t sys_cap_send(uint32_t slot, const void *msg) */
sys_cap_send:
    mov $2, %rax
    syscall
    ret

/* int64_t sys_cap_recv(uint32_t slot, void *msg) */
sys_cap_recv:
    mov $3, %rax
    syscall
    ret

/* int64_t sys_exit(int code) */
sys_exit:
    mov $4, %rax
    syscall
    ret

/* int64_t sys_read_key(uint32_t slot) */
sys_read_key:
    mov $5, %rax
    syscall
    ret

/* int64_t sys_tor_cell(uint32_t slot, const void *buf, size_t len) */
sys_tor_cell:
    mov $6, %rax
    syscall
    ret

/* int64_t sys_sysinfo(void *info_buf) */
sys_sysinfo:
    mov $7, %rax
    syscall
    ret
