#pragma once

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    char word1[16];
    char word2[16];
    char word3[16];
    uint32_t color1;
    uint32_t color2;
    uint32_t color3;
    uint32_t color4;
} tpm_visual_seal_t;

void tpm_visual_derive_seal(tpm_visual_seal_t *seal);
void tpm_visual_render_seal(void);
