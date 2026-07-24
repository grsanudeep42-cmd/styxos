#pragma once

#include <stdint.h>
#include <stdbool.h>

extern uint8_t g_anonymized_mac[6];

void anonymize_init(void);
void anonymize_get_mac(uint8_t mac_out[6]);
void anonymize_spoof_cpuid(uint32_t leaf, uint32_t *eax, uint32_t *ebx, uint32_t *ecx, uint32_t *edx);
