#include "task.h"
#include "vmm.h"
#include "pmm.h"
#include "heap.h"
#include "serial.h"
#include "tss.h"
#include "string.h"

/*
 * task.c — Task creation and management.
 *
 * Global task table: TASK_MAX_TASKS slots, statically allocated.
 * In M6, heap-allocated cap_table_t per user task replaces M5's static global.
 */

/* ── Global task table ───────────────────────────────────────────────────── */

static task_t task_table[TASK_MAX_TASKS];
static uint32_t next_task_id = 0;

/* ── task_init_table ─────────────────────────────────────────────────────── */

void task_init_table(void) {
    for (int i = 0; i < TASK_MAX_TASKS; i++) {
        task_table[i].state = TASK_STATE_UNUSED;
        task_table[i].id    = 0;
    }
    serial_printf("[TASK] Task table initialized — %d slots\n", TASK_MAX_TASKS);
}

/* ── Internal: allocate a free slot ─────────────────────────────────────── */

static task_t *alloc_task_slot(void) {
    for (int i = 0; i < TASK_MAX_TASKS; i++) {
        if (task_table[i].state == TASK_STATE_UNUSED) {
            task_table[i].id    = next_task_id++;
            task_table[i].state = TASK_STATE_READY;
            task_table[i].ticks = 10;  /* initial quantum: 10 PIT ticks */
            return &task_table[i];
        }
    }
    return (task_t *)0;
}

/* ── task_create_kernel ──────────────────────────────────────────────────── */

task_t *task_create_kernel(void) {
    task_t *t = alloc_task_slot();
    if (!t) {
        serial_printf("[TASK] ERROR: no free slot for kernel task\n");
        return (task_t *)0;
    }

    /*
     * The kernel idle task runs in the current address space.
     * We do not allocate a new PML4 — it uses whatever CR3 is already loaded.
     * We do not allocate a kernel stack — it uses the existing boot stack.
     * cap_table is NULL for the kernel task (it uses direct kernel authority).
     */
    t->pml4        = vmm_get_pml4();
    t->kstack_base = (void *)0;   /* uses existing boot stack */
    t->kstack_top  = 0;
    t->cap_table   = (cap_table_t *)0;

    /* Context is "current" — no saved regs needed until first switch away */
    t->regs.rsp = 0;
    t->regs.rip = 0;

    serial_printf("[TASK] Kernel idle task created — id=%d\n", (int)t->id);
    return t;
}

/* ── task_create_user ────────────────────────────────────────────────────── */

task_t *task_create_user(uint64_t entry_point) {
    task_t *t = alloc_task_slot();
    if (!t) {
        serial_printf("[TASK] ERROR: no free slot for user task\n");
        return (task_t *)0;
    }

    /* ── 1. Allocate and initialize capability table ── */
    t->cap_table = (cap_table_t *)kmalloc(sizeof(cap_table_t));
    if (!t->cap_table) {
        serial_printf("[TASK] ERROR: kmalloc failed for cap_table\n");
        t->state = TASK_STATE_UNUSED;
        return (task_t *)0;
    }
    cap_table_init(t->cap_table);

    /* ── 2. Allocate kernel stack (16 KB) ── */
    t->kstack_base = kmalloc(TASK_KERNEL_STACK_SIZE);
    if (!t->kstack_base) {
        serial_printf("[TASK] ERROR: kmalloc failed for kernel stack\n");
        kfree(t->cap_table);
        t->state = TASK_STATE_UNUSED;
        return (task_t *)0;
    }
    /* RSP0 = top of kernel stack (stacks grow down) */
    t->kstack_top = (uint64_t)t->kstack_base + TASK_KERNEL_STACK_SIZE;

    /* ── 3. Allocate new PML4 and copy kernel mappings ── */
    void *pml4_phys = pmm_alloc_frame();
    if (!pml4_phys) {
        serial_printf("[TASK] ERROR: pmm_alloc_frame failed for PML4\n");
        kfree(t->kstack_base);
        kfree(t->cap_table);
        t->state = TASK_STATE_UNUSED;
        return (task_t *)0;
    }

    /*
     * Copy the kernel's current PML4 into the new page table.
     * This gives the task access to all kernel higher-half mappings
     * (HHDM, kernel text, heap) while keeping the lower-half clean
     * for user address space. The kernel mappings are shared across
     * all tasks — writes to kernel memory are visible everywhere.
     *
     * The HHDM offset lets us write to the physical frame directly.
     */
    uint64_t hhdm_off = vmm_get_hhdm_offset();
    uint64_t *new_pml4 = (uint64_t *)((uint64_t)pml4_phys + hhdm_off);
    uint64_t *cur_pml4 = vmm_get_pml4();

    /* Zero first half (user space entries 0–255) */
    for (int i = 0; i < 256; i++)
        new_pml4[i] = 0;

    /* Copy upper half (kernel entries 256–511) from current PML4 */
    for (int i = 256; i < 512; i++)
        new_pml4[i] = cur_pml4[i];

    t->pml4 = new_pml4;

    /* ── 4. Map user stack page (one 4KB page at TASK_USER_STACK_PAGE) ── */
    void *ustack_phys = pmm_alloc_frame();
    if (!ustack_phys) {
        serial_printf("[TASK] ERROR: pmm_alloc_frame failed for user stack\n");
        pmm_free_frame(pml4_phys);
        kfree(t->kstack_base);
        kfree(t->cap_table);
        t->state = TASK_STATE_UNUSED;
        return (task_t *)0;
    }

    /*
     * Temporarily switch to the new PML4 to map the user stack page.
     * We save CR3, switch, map, then restore.
     */
    uint64_t *old_pml4 = vmm_get_pml4();
    uint64_t old_cr3;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(old_cr3));

    /* Convert new_pml4 virtual addr back to physical for CR3 */
    uint64_t new_cr3 = (uint64_t)pml4_phys;
    vmm_set_pml4(new_pml4);
    __asm__ volatile ("mov %0, %%cr3" : : "r"(new_cr3) : "memory");

    vmm_map_page(TASK_USER_STACK_PAGE,
                 (uint64_t)ustack_phys,
                 PTE_PRESENT | PTE_WRITABLE | PTE_USER);

    /* Restore kernel's own CR3 and VMM active PML4 */
    __asm__ volatile ("mov %0, %%cr3" : : "r"(old_cr3) : "memory");
    vmm_set_pml4(old_pml4);

    /* ── 5. Set up initial register state for task_switch ── */
    /*
     * task_switch() (in task.asm) restores callee-saved regs from the
     * kernel stack and then returns. For a brand-new task, we pre-load
     * the kernel stack so that when task_switch() "returns", it pops
     * into task_entry_trampoline() which sets up the IRETQ to ring 3.
     */
    t->regs.rip = entry_point;    /* stored for the trampoline to use */
    t->regs.rbx = entry_point;    /* duplicate in RBX for trampoline safety */
    t->regs.rsp = t->kstack_top;  /* kernel RSP at time of first switch */

    serial_printf("[TASK] User task created — id=%d entry=%p kstack_top=%p pml4=%p\n",
                  (int)t->id,
                  (void *)entry_point,
                  (void *)t->kstack_top,
                  (void *)t->pml4);
    return t;
}

/* ── task_get ──────────────────────────────────────────────────────────── */

task_t *task_get(uint32_t id) {
    for (int i = 0; i < TASK_MAX_TASKS; i++) {
        if (task_table[i].state != TASK_STATE_UNUSED &&
            task_table[i].id == id)
            return &task_table[i];
    }
    return (task_t *)0;
}
