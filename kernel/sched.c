#include "sched.h"
#include "task.h"
#include "serial.h"
#include "tss.h"

/*
 * sched.c — Round-robin scheduler implementation.
 *
 * Run queue: simple fixed array of task_t pointers.
 * Current task is tracked by index into the array.
 * task_switch() (in task.asm) does the actual register save/restore.
 */

/* ── Run queue ───────────────────────────────────────────────────────────── */

#define SCHED_QUEUE_SIZE  TASK_MAX_TASKS

static task_t  *run_queue[SCHED_QUEUE_SIZE];
static int      queue_count   = 0;
static int      current_idx   = 0;
task_t         *sched_current  = (task_t *)0;

/* Forward declaration of context switch (defined in task.asm) */
extern void task_switch(task_t *from, task_t *to);
extern void task_entry_trampoline(void);

/* ── sched_init ──────────────────────────────────────────────────────────── */

void sched_init(void) {
    for (int i = 0; i < SCHED_QUEUE_SIZE; i++)
        run_queue[i] = (task_t *)0;
    queue_count   = 0;
    current_idx   = 0;
    sched_current = (task_t *)0;
    serial_printf("[SCHED] Scheduler initialized\n");
}

/* ── sched_add ───────────────────────────────────────────────────────────── */

void sched_add(task_t *task) {
    if (queue_count >= SCHED_QUEUE_SIZE) {
        serial_printf("[SCHED] ERROR: run queue full\n");
        return;
    }
    run_queue[queue_count++] = task;
    serial_printf("[SCHED] Task %d added to run queue (slot %d)\n",
                  (int)task->id, queue_count - 1);
}

/* ── sched_get_current ───────────────────────────────────────────────────── */

task_t *sched_get_current(void) {
    return sched_current;
}

int sched_get_tasks(task_t **tasks, int max_tasks) {
    int count = (queue_count < max_tasks) ? queue_count : max_tasks;
    for (int i = 0; i < count; i++) {
        tasks[i] = run_queue[i];
    }
    return count;
}

void sched_set_tasks(task_t **tasks, int count) {
    queue_count = count;
    for (int i = 0; i < count; i++) {
        run_queue[i] = tasks[i];
    }
}

/* ── Internal: find next READY task ─────────────────────────────────────── */

static task_t *next_ready(void) {
    for (int i = 1; i <= queue_count; i++) {
        int idx = (current_idx + i) % queue_count;
        if (run_queue[idx] && run_queue[idx]->state == TASK_STATE_READY)
            return run_queue[idx];
    }
    /* Fallback: keep running current task if nothing else is READY */
    return sched_current;
}

static int idx_of(task_t *t) {
    for (int i = 0; i < queue_count; i++)
        if (run_queue[i] == t) return i;
    return 0;
}

/* ── sched_start ─────────────────────────────────────────────────────────── */

void sched_start(void) {
    if (queue_count == 0) {
        serial_printf("[SCHED] ERROR: no tasks — cannot start\n");
        return;
    }

    /* First task in queue is always the kernel idle task (task 0) */
    sched_current      = run_queue[0];
    current_idx       = 0;
    sched_current->state = TASK_STATE_RUNNING;

    serial_printf("[SCHED] Starting — initial task id=%d\n",
                  (int)sched_current->id);

    /*
     * If there is a second task (the ring-3 user task), switch to it
     * immediately so it gets its first slice. The kernel idle task will
     * run via the normal round-robin once the user task yields or ticks out.
     */
    if (queue_count > 1 && run_queue[1] &&
        run_queue[1]->state == TASK_STATE_READY) {

        task_t *first_user = run_queue[1];
        task_t *prev = sched_current;
        sched_current = first_user;
        current_idx = 1;

        /*
         * A brand-new task has regs.rip = entry point and regs.rsp = kstack_top.
         * We set its saved RIP to task_entry_trampoline so that task_switch's
         * final "jmp [rsi+88]" lands in the trampoline, which builds the IRETQ
         * frame and enters ring 3.
         */
        first_user->regs.rip = (uint64_t)task_entry_trampoline;
        first_user->state    = TASK_STATE_RUNNING;
        first_user->ticks    = 10;

        /* Update TSS so the CPU knows the new kernel stack */
        tss_set_kernel_stack(first_user->kstack_top);

        serial_printf("[SCHED] First switch: kernel task -> user task id=%d\n",
                      (int)first_user->id);

        prev->state = TASK_STATE_READY;
        task_switch(prev, first_user);
        /* Control returns here when the user task yields or is preempted */
        sched_current = run_queue[0];
        current_idx  = 0;
    }
}

/* ── sched_tick ──────────────────────────────────────────────────────────── */

void sched_tick(void) {
    if (!sched_current) return;

    /* Decrement quantum */
    if (sched_current->ticks > 0)
        sched_current->ticks--;

    if (sched_current->ticks > 0)
        return;  /* still has time left */

    /* Quantum expired — find next READY task */
    task_t *next = next_ready();
    if (!next || next == sched_current) {
        /* Nobody else to run — reset quantum and continue */
        sched_current->ticks = 10;
        return;
    }

    task_t *prev    = sched_current;
    current_idx     = idx_of(next);
    sched_current    = next;

    prev->state     = TASK_STATE_READY;
    next->state     = TASK_STATE_RUNNING;
    next->ticks     = 10;  /* fresh quantum */

    /*
     * If the next task has never run (rip still points to entry_point),
     * redirect to the trampoline so it gets a proper IRETQ launch.
     * We detect this by checking if rsp == kstack_top (untouched stack).
     */
    if (next->regs.rsp == next->kstack_top) {
        next->regs.rip = (uint64_t)task_entry_trampoline;
    }

    tss_set_kernel_stack(next->kstack_top);
    task_switch(prev, next);
}

/* ── sched_yield ─────────────────────────────────────────────────────────── */

void sched_yield(void) {
    if (!sched_current) return;

    task_t *next = next_ready();
    if (!next || next == sched_current) return;

    task_t *prev = sched_current;
    current_idx  = idx_of(next);
    sched_current = next;

    prev->state  = TASK_STATE_READY;
    next->state  = TASK_STATE_RUNNING;
    next->ticks  = 10;

    if (next->regs.rsp == next->kstack_top)
        next->regs.rip = (uint64_t)task_entry_trampoline;

    tss_set_kernel_stack(next->kstack_top);
    task_switch(prev, next);
}

/* ── sched_block / sched_unblock ─────────────────────────────────────────── */

void sched_block(task_t *task) {
    if (!task) return;
    task->state = TASK_STATE_BLOCKED;

    /* If blocking the current task, schedule the next one */
    if (task == sched_current)
        sched_yield();
}

void sched_unblock(task_t *task) {
    if (!task) return;
    if (task->state == TASK_STATE_BLOCKED)
        task->state = TASK_STATE_READY;
}
