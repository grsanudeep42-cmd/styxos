#pragma once

#include <stdint.h>

/*
 * ipc.h — IPC message format used by the StyxOS capability system.
 *
 * A message carries:
 *   tag       — caller-defined discriminant (e.g. syscall number, request type)
 *   words[4]  — 4 × 64-bit payload words (32 bytes of data)
 *   cap_count — number of capability slots being transferred with this message.
 *               Reserved and always 0 in M5; wired in M6 when userspace exists.
 *   _pad      — explicit padding so sizeof(ipc_msg_t) is 64 bytes on x86-64.
 *
 * Architecture Bible ref: §3 L4 — Process Isolation
 *   "Tokens define exactly which memory regions, devices, and syscalls
 *    a process may access. No ambient authority exists."
 */

#define IPC_MSG_WORDS  4

typedef struct {
    uint64_t  tag;
    uint64_t  words[IPC_MSG_WORDS];
    uint32_t  cap_count;   /* reserved — always 0 in M5 */
    uint32_t  _pad;
} ipc_msg_t;
