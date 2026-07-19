.intel_syntax noprefix

# task.asm — Context switch and ring-3 entry trampoline
#
# GAS Intel syntax (matches isr.asm / irq.asm convention).
#
# task_switch(from, to):
#   rdi = task_t* of current (from) task
#   rsi = task_t* of next    (to)   task
#
#   Saves callee-saved registers + return address + RSP into from->regs,
#   optionally switches CR3, updates TSS RSP0, then restores to->regs.
#
# task_entry_trampoline:
#   Called when a brand-new ring-3 task runs for the first time.
#   Builds an IRETQ frame and jumps to ring 3.
#
# task_t struct offsets (must match task.h exactly):
#   id            : +0   (uint32_t)
#   state         : +4   (uint32_t)
#   cap_table     : +8   (pointer, 8 bytes)
#   pml4          : +16  (pointer, 8 bytes)
#   kstack_base   : +24  (pointer, 8 bytes)
#   kstack_top    : +32  (uint64_t)
#   regs.rbx      : +40
#   regs.rbp      : +48
#   regs.r12      : +56
#   regs.r13      : +64
#   regs.r14      : +72
#   regs.r15      : +80
#   regs.rip      : +88
#   regs.rsp      : +96

# GDT selectors (RPL=3 for ring-3):
#   GDT_USER_DATA64 = 0x20 | 3 = 0x23
#   GDT_USER_CODE64 = 0x28 | 3 = 0x2B

.extern tss_set_kernel_stack
.extern vmm_get_hhdm_offset
.extern sched_get_current

.global task_switch
.global task_entry_trampoline

.text

# ── task_switch(from, to) ────────────────────────────────────────────────
task_switch:
    # Save callee-saved registers into from->regs
    mov  [rdi + 40], rbx
    mov  [rdi + 48], rbp
    mov  [rdi + 56], r12
    mov  [rdi + 64], r13
    mov  [rdi + 72], r14
    mov  [rdi + 80], r15

    # Save return address (at [rsp] since we arrived via CALL)
    mov  rax, [rsp]
    mov  [rdi + 88], rax

    # Save kernel RSP
    mov  [rdi + 96], rsp

    # Switch page tables if to->pml4 != NULL (kernel task has pml4=0)
    mov  rax, [rsi + 16]
    test rax, rax
    jz   .skip_cr3

    # We need the physical address of the PML4 for CR3.
    # pml4 field = virtual (HHDM-mapped) address.
    # physical = virtual - hhdm_offset.
    push rdi
    push rsi
    mov  r15, rax                  # save pml4 virt addr
    call vmm_get_hhdm_offset       # returns hhdm_offset in rax
    sub  r15, rax                  # r15 = physical PML4 addr
    pop  rsi
    pop  rdi
    mov  cr3, r15

.skip_cr3:
    # Update TSS RSP0 to to->kstack_top before entering to's context
    push rdi
    push rsi
    mov  rdi, [rsi + 32]           # kstack_top of "to" task
    call tss_set_kernel_stack
    pop  rsi
    pop  rdi

    # Restore to->regs callee-saved registers
    mov  rbx, [rsi + 40]
    mov  rbp, [rsi + 48]
    mov  r12, [rsi + 56]
    mov  r13, [rsi + 64]
    mov  r14, [rsi + 72]
    mov  r15, [rsi + 80]

    # Restore kernel RSP
    mov  rsp, [rsi + 96]

    # Jump to saved RIP (where "to" last left off, or task_entry_trampoline)
    jmp  qword ptr [rsi + 88]


# ── task_entry_trampoline ────────────────────────────────────────────────
#
# Entered when a brand-new ring-3 task is first scheduled.
# Builds IRETQ frame and transitions to ring 3.
#
# IRETQ stack layout (CPU pops top-to-bottom):
#   [rsp+ 0]  RIP    — ring-3 entry point
#   [rsp+ 8]  CS     — 0x2B (user code 64-bit, RPL=3)
#   [rsp+16]  RFLAGS — 0x202 (IF=1, reserved bit)
#   [rsp+24]  RSP    — 0x400000 (user stack top)
#   [rsp+32]  SS     — 0x23 (user data, RPL=3)
#
task_entry_trampoline:
    call sched_get_current         # returns task_t* in rax
    mov  rbx, [rax + 40]           # rbx = ring-3 entry point (regs.rbx)

    # Build IRETQ frame (push in reverse order: SS first, RIP last)
    push 0x23                      # SS  — user data (0x20 | 3)
    push 0x400000                  # RSP — user stack top (TASK_USER_STACK_TOP)
    push 0x202                     # RFLAGS — IF=1
    push 0x2B                      # CS  — user code 64-bit (0x28 | 3)
    push rbx                       # RIP — ring-3 entry point

    # Zero all registers before handing control to user code
    xor  rax, rax
    xor  rbx, rbx
    xor  rcx, rcx
    xor  rdx, rdx
    xor  rsi, rsi
    xor  rdi, rdi
    xor  r8,  r8
    xor  r9,  r9
    xor  r10, r10
    xor  r11, r11
    xor  r12, r12
    xor  r13, r13
    xor  r14, r14
    xor  r15, r15
    xor  rbp, rbp

    # Load user data segment selectors
    mov  ax,  0x23
    mov  ds,  ax
    mov  es,  ax
    xor  ax,  ax
    mov  fs,  ax
    mov  gs,  ax

    iretq                          # enter ring 3

.section .note.GNU-stack,"",@progbits
