#pragma once

#include "task.h"

/*
 * sched.h — Round-robin scheduler.
 *
 * Design (M6):
 *   - Fixed array of TASK_MAX_TASKS slots, same as task_table.
 *   - Round-robin: each task gets a quantum of 10 PIT ticks (100ms at 100Hz).
 *   - sched_tick() is called from the PIT IRQ handler every tick.
 *   - When a task's ticks reach zero, the next READY task runs.
 *   - sched_yield() allows a task to voluntarily give up its slice early.
 *   - sched_block() / sched_unblock() wire into cap_send/cap_recv IPC parking.
 *
 * Architecture Bible ref: §3 L4 (Process Isolation), §9 M6
 */

/* ── Public API ─────────────────────────────────────────────────────────── */

/*
 * sched_init() — register tasks with the scheduler and set the initial
 * current task. Must be called after task_create_kernel() and all
 * task_create_user() calls, before sched_start().
 */
void sched_init(void);

/*
 * sched_add(task) — add a task to the run queue.
 * Tasks start in READY state; the scheduler will pick them up.
 */
void sched_add(task_t *task);

/*
 * sched_start() — hand control to the first scheduled task.
 * Does not return. Called once from main() after all init is complete.
 */
void sched_start(void);

/*
 * sched_tick() — called from the PIT IRQ handler every timer tick.
 * Decrements the current task's quantum. If it reaches zero, picks
 * the next READY task and calls task_switch().
 *
 * Must be safe to call from interrupt context (ring 0, interrupts off).
 */
void sched_tick(void);

/*
 * sched_yield() — called via SYS_YIELD syscall.
 * Immediately schedules the next READY task.
 */
void sched_yield(void);

/*
 * sched_block(task) — move a task to BLOCKED state.
 * Called by cap_send/cap_recv when a task parks waiting for IPC.
 */
void sched_block(task_t *task);

/*
 * sched_unblock(task) — move a task from BLOCKED back to READY.
 * Called when the IPC rendezvous completes.
 */
void sched_unblock(task_t *task);

/*
 * sched_get_current() — return the currently running task_t*.
 * Used by task_entry_trampoline (in task.asm) and syscall dispatcher.
 */
task_t *sched_get_current(void);
