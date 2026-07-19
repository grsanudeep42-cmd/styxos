#pragma once

#include <stdint.h>
#include <stddef.h>
#include "cap.h"

/*
 * task.h — Task (process) structure for StyxOS.
 *
 * A task is the unit of isolation. Each task has:
 *   - Its own page table (PML4) — no shared memory without a capability
 *   - Its own capability table — no ambient authority
 *   - Its own kernel stack  — used during syscall / exception handling
 *   - Its own user stack    — lives in the task's address space
 *   - Saved register state  — for context switching
 *
 * M6 task memory layout (virtual addresses):
 *
 *   0x400000              — ELF load base (code + data segments)
 *   0x3FF000              — user stack page (grows down from 0x400000)
 *   kernel heap           — kernel stack, allocated via kmalloc()
 *
 * Architecture Bible ref: §3 L4 (Process Isolation), §9 M6, §11 Driver Model
 */

/* ── Constants ──────────────────────────────────────────────────────────── */

#define TASK_USER_LOAD_BASE   0x400000ULL      /* ELF load base (ring-3 code) */
#define TASK_USER_STACK_TOP   0x400000ULL      /* user stack grows down from here */
#define TASK_USER_STACK_PAGE  0x3FF000ULL      /* bottom of user stack page */
#define TASK_KERNEL_STACK_SIZE (16 * 1024)     /* 16 KB kernel stack per task */
#define TASK_MAX_TASKS        8                /* max simultaneous tasks (M6) */

/* ── Task state ─────────────────────────────────────────────────────────── */

typedef enum {
    TASK_STATE_UNUSED   = 0,   /* slot is free                              */
    TASK_STATE_READY    = 1,   /* on the run queue, waiting for CPU         */
    TASK_STATE_RUNNING  = 2,   /* currently executing                       */
    TASK_STATE_BLOCKED  = 3,   /* blocked on IPC (cap_send/cap_recv)        */
    TASK_STATE_DEAD     = 4,   /* terminated, slot can be reclaimed         */
} task_state_t;

/* ── Saved register frame for context switch ───────────────────────────── */

/*
 * We save only the callee-saved registers (System V AMD64 ABI):
 *   RBX, RBP, R12, R13, R14, R15
 * plus RIP (return address pushed by the call to task_switch) and RSP.
 *
 * Caller-saved registers (RAX, RCX, RDX, RSI, RDI, R8–R11) are the
 * caller's responsibility — they are on the stack or don't need saving.
 */
typedef struct {
    uint64_t rbx;
    uint64_t rbp;
    uint64_t r12;
    uint64_t r13;
    uint64_t r14;
    uint64_t r15;
    uint64_t rip;   /* return address — where this task resumes             */
    uint64_t rsp;   /* kernel stack pointer at the time of switch           */
} task_regs_t;

/* ── Task descriptor ────────────────────────────────────────────────────── */

typedef struct {
    uint32_t        id;           /* unique task ID (0 = kernel idle task)  */
    task_state_t    state;

    /* Capability table — the task's authority (Architecture Bible §3 L4)  */
    cap_table_t    *cap_table;    /* heap-allocated in M6 via kmalloc()     */

    /* Virtual memory — each task's private address space                  */
    uint64_t       *pml4;         /* physical addr of task's PML4 page table*/

    /* Kernel stack — used during syscall / fault handling                 */
    void           *kstack_base;  /* base of allocated kernel stack region  */
    uint64_t        kstack_top;   /* RSP0 value (top = base + size)         */

    /* Saved context — valid when state != RUNNING                        */
    task_regs_t     regs;

    /* Scheduling fields */
    uint32_t        ticks;        /* PIT ticks remaining in current quantum  */
} task_t;

/* ── Public API ─────────────────────────────────────────────────────────── */

/*
 * task_init_table() — zero the global task table.
 * Must be called once before any task_create() call.
 */
void task_init_table(void);

/*
 * task_create_kernel() — create the kernel idle task (task 0).
 *
 * The kernel idle task uses the existing kernel page tables and stack.
 * It does not go through the ELF loader. It simply represents "the kernel
 * running in its current context" so the scheduler has something to
 * switch away from and back to.
 *
 * Returns a pointer to the task_t, or NULL on failure.
 */
task_t *task_create_kernel(void);

/*
 * task_create_user() — allocate a new task with its own PML4, kernel stack,
 * and capability table.
 *
 * entry_point: ring-3 virtual address where execution begins (RIP).
 * The task starts in READY state. The scheduler will pick it up.
 *
 * Returns a pointer to the task_t, or NULL on failure.
 */
task_t *task_create_user(uint64_t entry_point);

/*
 * task_get(id) — look up a task by ID. Returns NULL if not found.
 */
task_t *task_get(uint32_t id);
