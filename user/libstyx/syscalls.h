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
#define SYS_SOCKET     8
#define SYS_CONNECT    9
#define SYS_SEND       10
#define SYS_RECV       11
#define SYS_PQC_KEM    12

int64_t sys_yield(void);
int64_t sys_write(uint32_t slot, const char *buf, size_t len);
int64_t sys_cap_send(uint32_t slot, const void *msg);
int64_t sys_cap_recv(uint32_t slot, void *msg);
int64_t sys_exit(int code);
int64_t sys_read_key(uint32_t slot);
int64_t sys_tor_cell(uint32_t slot, const void *buf, size_t len);
int64_t sys_sysinfo(void *info_buf);
int64_t sys_socket(uint32_t slot);
int64_t sys_connect(uint32_t slot, int sock, uint32_t ip, uint16_t port);
int64_t sys_send(uint32_t slot, int sock, const void *buf, size_t len);
int64_t sys_recv(uint32_t slot, int sock, void *buf, size_t max_len);
int64_t sys_pqc_kem(void *ct, void *ss, const void *pk);

