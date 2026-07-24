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
