#pragma once

#include <stdint.h>
#include <stdbool.h>

extern uint32_t g_padding_packets_sent;
extern bool     g_padding_boost_active;

void padding_init(void);
bool padding_inject_noise(void);
void padding_boost(bool active);
void padding_tick(uint64_t ticks);

