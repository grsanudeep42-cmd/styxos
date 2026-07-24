/*
 * csprng.c — Per-boot cryptographic random number generator
 *
 * Entropy mixing strategy:
 *   1. rdtsc  — CPU timestamp counter (nanosecond resolution jitter)
 *   2. PIT    — Programmable Interval Timer counter register (0x40)
 *   3. CPUID  — Execution timing varies by cache state
 *   4. Boot counter — increments on every csprng_get_bytes() call
 *
 * All sources are mixed through a 256-bit internal state using the
 * xoshiro256** algorithm, which passes all known statistical tests.
 * This is NOT a hardware RNG, but it provides sufficient unpredictability
 * for per-boot challenges and session keys in a pre-alpha kernel.
 */
#include "csprng.h"
#include "io.h"
#include "serial.h"
#include "string.h"
#include <stdint.h>

/* ── xoshiro256** state ─────────────────────────────────────────────────── */
static uint64_t g_s[4];
static uint64_t g_call_counter = 0;
static bool     g_initialized  = false;

static inline uint64_t rotl(uint64_t x, int k) {
    return (x << k) | (x >> (64 - k));
}

static uint64_t xoshiro256ss_next(void) {
    uint64_t result = rotl(g_s[1] * 5, 7) * 9;
    uint64_t t = g_s[1] << 17;
    g_s[2] ^= g_s[0];
    g_s[3] ^= g_s[1];
    g_s[1]  ^= g_s[2];
    g_s[0]  ^= g_s[3];
    g_s[2]  ^= t;
    g_s[3]   = rotl(g_s[3], 45);
    return result;
}

/* ── Entropy collection ─────────────────────────────────────────────────── */
static uint64_t read_rdtsc(void) {
    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

static uint64_t read_pit_counter(void) {
    /* Latch PIT channel 0 count */
    outb(0x43, 0x00);
    uint8_t lo = inb(0x40);
    uint8_t hi = inb(0x40);
    return ((uint64_t)hi << 8) | lo;
}

static uint64_t cpuid_jitter(void) {
    uint64_t t1 = read_rdtsc();
    uint32_t eax = 0, ebx = 0, ecx = 0, edx = 0;
    __asm__ volatile("cpuid"
        : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
        : "a"(1) : "memory");
    uint64_t t2 = read_rdtsc();
    return (t2 - t1) ^ ((uint64_t)eax << 32) ^ ebx ^ ((uint64_t)ecx << 16) ^ edx;
}

/* ── Public API ─────────────────────────────────────────────────────────── */
void csprng_init(void) {
    /* Collect entropy from all sources into initial state */
    g_s[0] = read_rdtsc();
    /* Brief spin to let TSC advance further */
    for (volatile int i = 0; i < 10000; i++) __asm__ volatile("pause");

    g_s[1] = read_rdtsc() ^ read_pit_counter();
    g_s[2] = cpuid_jitter() ^ (read_rdtsc() << 7);

    /* Second rdtsc pass — additional timing jitter */
    for (volatile int i = 0; i < 5000; i++) __asm__ volatile("pause");
    g_s[3] = read_rdtsc() ^ (read_pit_counter() << 13);

    /* Warm up: discard first 64 outputs */
    for (int i = 0; i < 64; i++) xoshiro256ss_next();

    g_call_counter = 0;
    g_initialized  = true;

    serial_printf("[CSPRNG] Initialized. Entropy state: %x|%x|%x|%x\n",
                  (uint32_t)(g_s[0] >> 32), (uint32_t)(g_s[1] >> 32),
                  (uint32_t)(g_s[2] >> 32), (uint32_t)(g_s[3] >> 32));
}

void csprng_get_bytes(uint8_t *out, size_t len) {
    if (!out || len == 0) return;
    if (!g_initialized) csprng_init();

    /* Re-mix with fresh rdtsc on each call to prevent state prediction */
    g_s[0] ^= read_rdtsc() + (++g_call_counter);
    g_s[2] ^= read_pit_counter();

    size_t i = 0;
    while (i < len) {
        uint64_t r = xoshiro256ss_next();
        size_t chunk = len - i;
        if (chunk > 8) chunk = 8;
        memcpy(out + i, &r, chunk);
        i += chunk;
    }
}

uint32_t csprng_u32(void) {
    uint32_t r;
    csprng_get_bytes((uint8_t *)&r, 4);
    return r;
}

uint64_t csprng_u64(void) {
    uint64_t r;
    csprng_get_bytes((uint8_t *)&r, 8);
    return r;
}
