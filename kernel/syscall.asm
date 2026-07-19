.intel_syntax noprefix

# syscall.asm — Assembly system call gate (SYSCALL / SYSRET)
#
# Calling convention (ring 3 -> ring 0):
#   RAX = syscall number
#   RDI = arg0
#   RSI = arg1
#   RDX = arg2
#   R10 = arg3 (RCX is overwritten by the CPU with user RIP)
#   R11 = user RFLAGS (saved by CPU)
#
# Return value in RAX.
#
# This stub manages the transition from user to kernel stack, saves
# registers, maps the System V ABI call parameters to syscall_dispatch,
# runs the handler, restores registers, and transitions back to user space.

.extern sched_current
.extern global_user_rsp
.extern global_scratch_rax
.extern syscall_dispatch

.global syscall_entry

.text

syscall_entry:
    # ── 1. Save user RAX and RSP to scratch variables ──
    mov [global_scratch_rax], rax
    mov [global_user_rsp], rsp

    # ── 2. Switch to the current task's kernel stack ──
    # sched_current contains task_t*
    mov rax, [sched_current]
    # kstack_top is at offset 32 in task_t
    mov rsp, [rax + 32]

    # ── 3. Push user register frame onto kernel stack ──
    # Align to 16-byte boundary by pushing RSP first, then RFLAGS, then RIP.
    # Frame format: user_rsp, r11 (rflags), rcx (rip)
    push [global_user_rsp]
    push r11
    push rcx

    # Push all general purpose registers to preserve user context
    push r15
    push r14
    push r13
    push r12
    push r11
    push r10
    push r9
    push r8
    push rdi
    push rsi
    push rdx
    push rcx
    push rbx
    push [global_scratch_rax]  # push user's original RAX
    push rbp

    # ── 4. Set up C call parameters for syscall_dispatch ──
    # C signature: syscall_dispatch(num, arg0, arg1, arg2, arg3)
    #   rdi = num  (from user RAX, saved in global_scratch_rax)
    #   rsi = arg0 (from user RDI)
    #   rdx = arg1 (from user RSI)
    #   rcx = arg2 (from user RDX)
    #   r8  = arg3 (from user R10)
    #
    # Move arguments in reverse order to prevent clobbering:
    mov r8, r10
    mov rcx, rdx
    mov rdx, rsi
    mov rsi, rdi
    mov rdi, [global_scratch_rax]

    # Clean the direction flag for System V ABI compliance
    cld

    # ── 5. Invoke C handler ──
    call syscall_dispatch       # return value in RAX

    # ── 6. Restore user context from kernel stack ──
    pop rbp
    # Skip restoring RAX so that RAX contains the return value of the syscall
    add rsp, 8
    pop rbx
    pop rcx
    pop rdx
    pop rsi
    pop rdi
    pop r8
    pop r9
    pop r10
    pop r11
    pop r12
    pop r13
    pop r14
    pop r15

    # Restore user control flow registers
    pop rcx                     # user RIP for SYSRET
    pop r11                     # user RFLAGS for SYSRET
    pop qword ptr [global_user_rsp] # user RSP

    # Switch back to user stack
    mov rsp, [global_user_rsp]

    # ── 7. Return to Ring 3 ──
    sysretq

.section .note.GNU-stack,"",@progbits
