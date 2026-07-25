/*
 * styx.c — StyxOS Ring-3 C Runtime Implementation
 */
#include "styx.h"
#include <stdarg.h>

size_t strlen(const char *s) {
    size_t len = 0;
    while (s && s[len]) len++;
    return len;
}

int strcmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(unsigned char *)s1 - *(unsigned char *)s2;
}

int strncmp(const char *s1, const char *s2, size_t n) {
    while (n > 0 && *s1 && (*s1 == *s2)) {
        s1++;
        s2++;
        n--;
    }
    if (n == 0) return 0;
    return *(unsigned char *)s1 - *(unsigned char *)s2;
}

char *strcpy(char *dest, const char *src) {
    char *orig = dest;
    while ((*dest++ = *src++));
    return orig;
}

void *memset(void *s, int c, size_t n) {
    unsigned char *p = (unsigned char *)s;
    while (n--) *p++ = (unsigned char)c;
    return s;
}

void *memcpy(void *dest, const void *src, size_t n) {
    unsigned char *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;
    while (n--) *d++ = *s++;
    return dest;
}

int putchar(char c) {
    sys_write(0, &c, 1);
    return (unsigned char)c;
}

int puts(const char *s) {
    size_t len = strlen(s);
    sys_write(0, s, len);
    putchar('\n');
    return 0;
}

static void print_num(unsigned long num, int base) {
    static const char digits[] = "0123456789ABCDEF";
    char buf[32];
    int i = 0;
    if (num == 0) {
        putchar('0');
        return;
    }
    while (num > 0) {
        buf[i++] = digits[num % base];
        num /= base;
    }
    while (i > 0) {
        putchar(buf[--i]);
    }
}

int printf(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    while (*fmt) {
        if (*fmt == '%') {
            fmt++;
            if (*fmt == 's') {
                const char *s = va_arg(args, const char *);
                if (!s) s = "(null)";
                sys_write(0, s, strlen(s));
            } else if (*fmt == 'd') {
                long d = va_arg(args, long);
                if (d < 0) {
                    putchar('-');
                    d = -d;
                }
                print_num((unsigned long)d, 10);
            } else if (*fmt == 'x' || *fmt == 'p') {
                unsigned long x = va_arg(args, unsigned long);
                print_num(x, 16);
            } else if (*fmt == 'c') {
                char c = (char)va_arg(args, int);
                putchar(c);
            } else if (*fmt == '%') {
                putchar('%');
            }
        } else {
            putchar(*fmt);
        }
        fmt++;
    }
    va_end(args);
    return 0;
}

char getchar(void) {
    while (1) {
        int64_t ret = sys_read_key(0);
        if (ret > 0) return (char)ret;
        sys_yield();
    }
}

/* Userland simple bump arena allocator */
static uint8_t g_user_heap[64 * 1024];
static size_t  g_user_heap_idx = 0;

void *malloc(size_t size) {
    /* Align to 8 bytes */
    size = (size + 7) & ~7;
    if (g_user_heap_idx + size > sizeof(g_user_heap)) {
        return NULL;
    }
    void *ptr = &g_user_heap[g_user_heap_idx];
    g_user_heap_idx += size;
    return ptr;
}

void free(void *ptr) {
    (void)ptr;
}

void yield(void) {
    sys_yield();
}

void exit(int code) {
    sys_exit(code);
}

int styx_socket(uint32_t net_slot) {
    return (int)sys_socket(net_slot);
}

int styx_connect(uint32_t net_slot, int sock, uint32_t ip, uint16_t port) {
    return (int)sys_connect(net_slot, sock, ip, port);
}

int styx_send(uint32_t net_slot, int sock, const void *buf, size_t len) {
    return (int)sys_send(net_slot, sock, buf, len);
}

int styx_recv(uint32_t net_slot, int sock, void *buf, size_t max_len) {
    return (int)sys_recv(net_slot, sock, buf, max_len);
}

int styx_pqc_kem(void *ct, void *ss, const void *pk) {
    return (int)sys_pqc_kem(ct, ss, pk);
}

int styx_emergency_wipe(void) {
    return (int)sys_emergency_wipe(0); /* Console slot 0 authorization */
}


