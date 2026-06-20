#pragma once

#include <stdint.h>
#include "isr.h"

typedef void (*irq_handler_t)(struct registers *regs);

void irq_init(void);
void irq_install_handler(int irq, irq_handler_t handler);
void irq_uninstall_handler(int irq);
void irq_handler(uint64_t vector, struct registers *regs);
