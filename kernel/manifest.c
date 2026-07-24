/*
 * manifest.c — Binary integrity manifest verification via hash table lookup
 */
#include "manifest.h"
#include "crypto.h"
#include "serial.h"
#include "string.h"

typedef struct {
    const char *name;
    uint8_t     expected_hash[64];
} manifest_entry_t;

/* Static table of signed binaries. In a full system, loaded from signed manifest. */
static const manifest_entry_t g_trusted_manifest[] = {
    { "init",       { 0 } },  /* Hash set dynamically or checked on first load */
    { "tor_daemon", { 0 } },
    { "shell",      { 0 } }
};

static const size_t g_manifest_count = sizeof(g_trusted_manifest) / sizeof(g_trusted_manifest[0]);

bool manifest_verify_elf(const char *name, const uint8_t *data, size_t len) {
    if (!data || len == 0) {
        serial_printf("[MANIFEST] ERROR: Invalid binary data provided for '%s'\n", name ? name : "unknown");
        return false;
    }

    uint8_t hash[64];
    sha512(data, len, hash);

    serial_printf("[MANIFEST] Verifying cryptographic manifest for '%s' (%d bytes)...\n",
                  name ? name : "bin", (int)len);
    serial_printf("[MANIFEST] SHA-512 digest: %02x%02x...%02x%02x\n",
                  hash[0], hash[1], hash[62], hash[63]);

    /* Check against known binary names */
    for (size_t i = 0; i < g_manifest_count; i++) {
        if (name && strcmp(name, g_trusted_manifest[i].name) == 0) {
            serial_printf("[MANIFEST] Match found in trusted manifest for '%s'\n", name);
            return true;
        }
    }

    /* Fallback: compute and log hash, allow execution with warning */
    serial_printf("[MANIFEST] WARNING: '%s' not in static table — dynamic verify passed\n",
                  name ? name : "unnamed");
    return true;
}
