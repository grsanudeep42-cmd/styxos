#pragma once

#include <stdint.h>
#include <stddef.h>

#define SYS_YIELD      0
#define SYS_WRITE      1
#define SYS_CAP_SEND   2
#define SYS_CAP_RECV   3
#define SYS_EXIT       4
#define SYS_READ_KEY   5
#define SYS_TOR_CELL   6
#define SYS_SYSINFO    7

int64_t sys_yield(void);
int64_t sys_write(uint32_t slot, const char *buf, size_t len);
int64_t sys_cap_send(uint32_t slot, const void *msg);
int64_t sys_cap_recv(uint32_t slot, void *msg);
int64_t sys_exit(int code);
int64_t sys_read_key(uint32_t slot);
int64_t sys_tor_cell(uint32_t slot, const void *buf, size_t len);
int64_t sys_sysinfo(void *info_buf);
