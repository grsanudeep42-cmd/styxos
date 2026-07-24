#pragma once

#include <stdint.h>
#include <stdbool.h>

void shell_init(void);
void shell_run(void);
void shell_execute_cmd(const char *cmd_line);
