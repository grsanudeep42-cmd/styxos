#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "syscalls.h"

/* Standard C runtime utilities for StyxOS Ring-3 processes */

size_t strlen(const char *s);
int strcmp(const char *s1, const char *s2);
int strncmp(const char *s1, const char *s2, size_t n);
char *strcpy(char *dest, const char *src);
void *memset(void *s, int c, size_t n);
void *memcpy(void *dest, const void *src, size_t n);

int puts(const char *s);
int putchar(char c);
int printf(const char *fmt, ...);
char getchar(void);

void *malloc(size_t size);
void free(void *ptr);

void yield(void);
void exit(int code);

int styx_socket(uint32_t net_slot);
int styx_connect(uint32_t net_slot, int sock, uint32_t ip, uint16_t port);
int styx_send(uint32_t net_slot, int sock, const void *buf, size_t len);
int styx_recv(uint32_t net_slot, int sock, void *buf, size_t max_len);
int styx_pqc_kem(void *ct, void *ss, const void *pk);
int styx_emergency_wipe(void);


