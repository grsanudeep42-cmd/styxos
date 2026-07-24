#include "tpm_visual.h"
#include "tpm.h"
#include "serial.h"
#include "string.h"

static const char *g_wordlist[64] = {
    "AEGIS", "ARGON", "AURORA", "BEACON", "CIPHER", "COBALT", "CRIMSON", "CRYPTO",
    "CYBER", "DAWN", "ECHO", "ECLIPSE", "EMERALD", "FALCON", "FORGE", "FROST",
    "GENESIS", "GLACIER", "GRID", "HALO", "HELIOS", "HYDRA", "IGNEOUS", "INFINITY",
    "IRIS", "JADE", "KINETIC", "LEGION", "LUNAR", "MATRIX", "NEBULA", "NEXUS",
    "NOVA", "OBSIDIAN", "ODYSSEY", "OMEGA", "ONYX", "ORION", "PHANTOM", "PHOENIX",
    "PULSE", "QUANTUM", "QUARTZ", "RADIAN", "RUBY", "SAPPHIRE", "SENTINEL", "SHADOW",
    "SHIELD", "SPECTRE", "STEALTH", "STORM", "STYX", "TITAN", "TORRENT", "VALKYRIE",
    "VECTOR", "VERITAS", "VIPER", "VORTEX", "ZENITH", "ZEPHYR", "ZERO", "ZODIAC"
};

void tpm_visual_derive_seal(tpm_visual_seal_t *seal) {
    if (!seal) return;
    memset(seal, 0, sizeof(*seal));

    uint8_t pcr0[SHA256_DIGEST_SIZE];
    uint8_t pcr1[SHA256_DIGEST_SIZE];
    tpm2_get_pcr(0, pcr0);
    tpm2_get_pcr(1, pcr1);

    uint8_t idx1 = pcr0[0] % 64;
    uint8_t idx2 = pcr0[1] % 64;
    uint8_t idx3 = pcr0[2] % 64;

    memcpy(seal->word1, g_wordlist[idx1], strlen(g_wordlist[idx1]) + 1);
    memcpy(seal->word2, g_wordlist[idx2], strlen(g_wordlist[idx2]) + 1);
    memcpy(seal->word3, g_wordlist[idx3], strlen(g_wordlist[idx3]) + 1);

    seal->color1 = 0xFF000000 | (pcr1[0] << 16) | (pcr1[1] << 8) | pcr1[2];
    seal->color2 = 0xFF000000 | (pcr1[3] << 16) | (pcr1[4] << 8) | pcr1[5];
    seal->color3 = 0xFF000000 | (pcr1[6] << 16) | (pcr1[7] << 8) | pcr1[8];
    seal->color4 = 0xFF000000 | (pcr1[9] << 16) | (pcr1[10] << 8) | pcr1[11];
}

void tpm_visual_render_seal(void) {
    tpm_visual_seal_t seal;
    tpm_visual_derive_seal(&seal);

    serial_printf("\n====================================================\n");
    serial_printf("      ANTI-EVIL-MAID VISUAL VERIFICATION SEAL       \n");
    serial_printf("====================================================\n");
    serial_printf("  Passphrase Seal : [ %s - %s - %s ]\n", seal.word1, seal.word2, seal.word3);
    serial_printf("  Color Matrix    : [ #%x | #%x | #%x | #%x ]\n",
                  seal.color1 & 0xFFFFFF, seal.color2 & 0xFFFFFF,
                  seal.color3 & 0xFFFFFF, seal.color4 & 0xFFFFFF);
    serial_printf("====================================================\n");
    serial_printf("Verify that the 3-word passphrase and color block match\n");
    serial_printf("your memorized secret seal BEFORE entering credentials!\n\n");
}
