/*
 * string.c – freestanding libc replacements.
 * The kernel links with -ffreestanding so the compiler will NOT supply these;
 * we must define them ourselves.
 */

#include "string.h"

void *memcpy(void *dest, const void *src, size_t n) {
    unsigned char       *d = dest;
    const unsigned char *s = src;
    while (n--) *d++ = *s++;
    return dest;
}

void *memset(void *dest, int c, size_t n) {
    unsigned char *d = dest;
    while (n--) *d++ = (unsigned char)c;
    return dest;
}

void *memmove(void *dest, const void *src, size_t n) {
    unsigned char       *d = dest;
    const unsigned char *s = src;
    if (d < s) {
        while (n--) *d++ = *s++;
    } else {
        d += n; s += n;
        while (n--) *--d = *--s;
    }
    return dest;
}

int memcmp(const void *a, const void *b, size_t n) {
    const unsigned char *pa = a, *pb = b;
    while (n--) {
        if (*pa != *pb) return (int)*pa - (int)*pb;
        pa++; pb++;
    }
    return 0;
}
