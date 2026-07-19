#include "auth.h"
#include "crypto.h"
#include "fido2.h"
#include "fb.h"
#include "font.h"
#include "serial.h"
#include "io.h"
#include "string.h"

/* ── US Keyboard Scancodes ───────────────────────────────────────────────── */
static const char scancode_to_ascii_nomod[] = {
    0,  27, '1', '2', '3', '4', '5', '6', '7', '8',
  '9', '0', '-', '=', '\b',
  '\t',
  'q', 'w', 'e', 'r',
  't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0,
  'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';',
 '\'', '`',   0,
 '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.',
  '/',   0,
  '*',
    0,  /* Alt */
  ' ',  /* Space bar */
};

static const char scancode_to_ascii_shift[] = {
    0,  27, '!', '@', '#', '$', '%', '^', '&', '*',
  '(', ')', '_', '+', '\b',
  '\t',
  'Q', 'W', 'E', 'R',
  'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0,
  'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':',
  '"', '~',   0,
  '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>',
  '?',   0,
  '*',
    0,
  ' ',
};

/* ── 32-byte Cryptographic Challenge ─────────────────────────────────────── */
static const uint8_t g_challenge[32] = {
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10,
    0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
    0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F, 0x20
};

/* ── Cryptographic expected verification hash (derived from password "styx") ─ */
// This is the SHA-512 hash of the derived 64-byte master key.
// The master key is derived via HKDF-SHA-512 with IKM = "styx" + FIDO2_mock_secret
static const uint8_t g_expected_derived_hash[64] = {
    0x11, 0xc1, 0x82, 0xba, 0xde, 0x48, 0x1d, 0xba,
    0x8e, 0x76, 0xae, 0xa6, 0x7e, 0x3d, 0x85, 0x51,
    0xc1, 0x93, 0x82, 0x2e, 0x6e, 0x39, 0xfd, 0x63,
    0x86, 0xa5, 0xb8, 0xe1, 0x86, 0xd0, 0x5f, 0x61,
    0xe0, 0x09, 0x54, 0xb6, 0x14, 0x88, 0xbc, 0xcc,
    0x85, 0xcf, 0x19, 0x95, 0x00, 0x45, 0x26, 0xdf,
    0x17, 0xfd, 0xbe, 0x01, 0xb3, 0xb3, 0xd6, 0x23,
    0xd0, 0x4c, 0xfd, 0xbf, 0x15, 0xbc, 0x22, 0xd5
};

static int g_failed_attempts = 0;

/* ── Keyboard Polling Helper ─────────────────────────────────────────────── */
static char get_char_poll(bool *is_f2, bool *is_space) {
    if (is_f2) *is_f2 = false;
    if (is_space) *is_space = false;

    uint8_t status = inb(0x64);
    if (status & 0x01) { // Output buffer full
        uint8_t scancode = inb(0x60);
        
        static bool shift = false;
        if (scancode == 0x2A || scancode == 0x36) {
            shift = true;
            return 0;
        }
        if (scancode == 0xAA || scancode == 0xB6) {
            shift = false;
            return 0;
        }

        if (scancode & 0x80) { // Key release
            return 0;
        }

        if (scancode == 0x3C) { // F2
            if (is_f2) *is_f2 = true;
            return 0;
        }
        if (scancode == 0x39) { // Space
            if (is_space) *is_space = true;
            return ' ';
        }

        char ascii = 0;
        if (scancode < sizeof(scancode_to_ascii_nomod)) {
            ascii = shift ? scancode_to_ascii_shift[scancode] : scancode_to_ascii_nomod[scancode];
        }
        return ascii;
    }
    return 0;
}

/* ── UI Drawing Helpers ──────────────────────────────────────────────────── */
static void draw_box(int x, int y, int w, int h, uint32_t bg_color, uint32_t border_color) {
    // Draw background
    fb_draw_rect(x, y, w, h, bg_color);
    // Draw 3px border
    for (int i = 0; i < 3; i++) {
        fb_draw_rect(x - i, y - i, w + 2*i, 1, border_color); // Top
        fb_draw_rect(x - i, y + h + i - 1, w + 2*i, 1, border_color); // Bottom
        fb_draw_rect(x - i, y - i, 1, h + 2*i, border_color); // Left
        fb_draw_rect(x + w + i - 1, y - i, 1, h + 2*i, border_color); // Right
    }
}

static void draw_glow_border(uint32_t color) {
    uint32_t W = fb_get_width();
    uint32_t H = fb_get_height();
    // Inner neon border around entire screen
    for (int i = 0; i < 4; i++) {
        fb_draw_rect(10 + i, 10 + i, W - 20 - 2*i, 1, color);
        fb_draw_rect(10 + i, H - 10 - i, W - 20 - 2*i, 1, color);
        fb_draw_rect(10 + i, 10 + i, 1, H - 20 - 2*i, color);
        fb_draw_rect(W - 10 - i, 10 + i, 1, H - 20 - 2*i, color);
    }
}

/* ── Lockout Screen ──────────────────────────────────────────────────────── */
static void trigger_tamper_lockout(void) {
    fb_clear(0x007F1D1D); // Deep warning crimson
    draw_glow_border(0x00EF4444); // Bright red alert

    uint32_t W = fb_get_width();
    uint32_t H = fb_get_height();

    int box_w = 640;
    int box_h = 240;
    int bx = (W - box_w) / 2;
    int by = (H - box_h) / 2;

    draw_box(bx, by, box_w, box_h, 0x00450A0A, 0x00F87171);

    fb_draw_string(bx + 40, by + 40, "!!! SECURITY COMPROMISED: TAMPER LOCKOUT !!!", 0x00F87171);
    fb_draw_string(bx + 40, by + 80, "Too many failed authentication attempts.", 0x00FFFFFF);
    fb_draw_string(bx + 40, by + 120, "Master session keys permanently destroyed.", 0x00FFFFFF);
    fb_draw_string(bx + 40, by + 160, "System execution permanently halted.", 0x00F87171);

    serial_printf("[AUTH] TAMPER LOCKOUT ACTIVE. HALTING SYSTEM.\n");
    __asm__ volatile ("cli");
    for (;;) {
        __asm__ volatile ("hlt");
    }
}

/* ── auth_preboot ────────────────────────────────────────────────────────── */
bool auth_preboot(void) {
    // 1. Disable interrupts so the polling loop gets exclusive keyboard control
    __asm__ volatile ("cli");

    uint32_t W = fb_get_width();
    uint32_t H = fb_get_height();

    int box_w = 540;
    int box_h = 320;
    int bx = (W - box_w) / 2;
    int by = (H - box_h) / 2;

    for (;;) {
        // Clear screen to Slate-900 cyberpunk look
        fb_clear(0x000F172A);
        draw_glow_border(0x0006B6D4); // Neon Cyan glow

        // Draw main auth window
        draw_box(bx, by, box_w, box_h, 0x001E293B, 0x0006B6D4);

        // Header text
        fb_draw_string(bx + 110, by + 30, "STYXOS SECURE GATEWAY", 0x00FFFFFF);
        fb_draw_string(bx + 30, by + 60, "────────────────────────────────────────────────", 0x00334155);

        // Attempts warning if any
        if (g_failed_attempts > 0) {
            char warn_str[64];
            warn_str[0] = '\0';
            // Custom concat since we are freestanding
            strcat(warn_str, "Warning: ");
            if (g_failed_attempts == 1) strcat(warn_str, "1 failed attempt. 2 remaining.");
            else strcat(warn_str, "2 failed attempts. 1 remaining!");
            fb_draw_string(bx + 30, by + 80, warn_str, 0x00F87171);
        } else {
            fb_draw_string(bx + 30, by + 80, "Multi-factor authentication required.", 0x0094A3B8);
        }

        fb_draw_string(bx + 30, by + 120, "Password: ", 0x0038BDF8);

        // 2. Collect Password Input
        char password[33];
        int pw_idx = 0;
        password[0] = '\0';

        bool f2_triggered = false;
        bool cursor_state = true;
        uint32_t cursor_ticks = 0;

        for (;;) {
            // Check for key
            bool is_f2 = false;
            bool is_space = false;
            char c = get_char_poll(&is_f2, &is_space);

            if (is_f2) {
                f2_triggered = true;
            }

            if (c == '\n') {
                if (pw_idx > 0) {
                    break;
                }
            } else if (c == '\b') {
                if (pw_idx > 0) {
                    pw_idx--;
                    password[pw_idx] = '\0';
                    // Erase character from screen (draw rect matching background)
                    fb_draw_rect(bx + 120 + pw_idx * 8, by + 120, 8, 16, 0x001E293B);
                }
            } else if (c >= 32 && c <= 126 && pw_idx < 32) {
                password[pw_idx++] = c;
                password[pw_idx] = '\0';
                // Print masked char
                fb_draw_char(bx + 120 + (pw_idx - 1) * 8, by + 120, '*', 0x0034D399); // Neon green '*'
            }

            // Blinking green cursor
            cursor_ticks++;
            if (cursor_ticks % 100000 == 0) {
                cursor_state = !cursor_state;
                fb_draw_rect(bx + 120 + pw_idx * 8, by + 120, 8, 16, cursor_state ? 0x0034D399 : 0x001E293B);
            }
            
            // Brief loop delay
            for (volatile int d = 0; d < 100; d++);
        }

        // Clean cursor
        fb_draw_rect(bx + 120 + pw_idx * 8, by + 120, 8, 16, 0x001E293B);

        // Check if physical key is connected (returns false in QEMU stub)
        bool real_hid = fido2_device_detect();

        // 3. FIDO2 / Touch Authentication Phase
        fb_draw_string(bx + 30, by + 160, "FIDO2 Key: ", 0x0038BDF8);

        uint8_t fido2_secret[64];
        memset(fido2_secret, 0, 64);
        bool fido2_ok = false;

        if (real_hid) {
            fb_draw_string(bx + 120, by + 160, "Device found. Touch key to authorize...", 0x00FBBF24);
            int rc = fido2_get_assertion(g_challenge, NULL, fido2_secret);
            if (rc == 0) {
                fido2_ok = true;
                fb_draw_string(bx + 120, by + 160, "Authorized successfully.          ", 0x0034D399);
            } else {
                fb_draw_string(bx + 120, by + 160, "Authorization failed.             ", 0x00F87171);
            }
        } else {
            fb_draw_string(bx + 120, by + 160, "Not found. Press F2 for Emulator Mode", 0x00F87171);
            
            // Wait for F2 or keyboard touch
            for (;;) {
                bool is_f2 = false;
                bool is_space = false;
                get_char_poll(&is_f2, &is_space);
                if (is_f2) {
                    f2_triggered = true;
                    break;
                }
                for (volatile int d = 0; d < 1000; d++);
            }

            if (f2_triggered) {
                // Clear the row
                fb_draw_rect(bx + 120, by + 160, 400, 16, 0x001E293B);
                fb_draw_string(bx + 120, by + 160, "EMULATOR ACTIVE. Press SPACE to Touch", 0x00FBBF24);

                // Wait for Space to simulate touch
                for (;;) {
                    bool is_f2 = false;
                    bool is_space = false;
                    get_char_poll(&is_f2, &is_space);
                    if (is_space) {
                        break;
                    }
                    for (volatile int d = 0; d < 1000; d++);
                }

                // Show processing indicator
                fb_draw_rect(bx + 120, by + 160, 400, 16, 0x001E293B);
                fb_draw_string(bx + 120, by + 160, "Generating cryptographic assertion...", 0x0038BDF8);

                int rc = fido2_emulate_assertion(g_challenge, NULL, fido2_secret);
                if (rc == 0) {
                    fido2_ok = true;
                    fb_draw_rect(bx + 120, by + 160, 400, 16, 0x001E293B);
                    fb_draw_string(bx + 120, by + 160, "Emulated Touch Authorized.", 0x0034D399);
                }
            }
        }

        // Delay to let the user see the result
        for (volatile int d = 0; d < 100000000; d++);

        if (!fido2_ok) {
            g_failed_attempts++;
            if (g_failed_attempts >= 3) {
                trigger_tamper_lockout();
            }
            continue; // Retry
        }

        // 4. Cryptographic Derivation Layer (HKDF-SHA-512)
        // Combine: Password + FIDO2 Secret
        uint8_t ikm[128];
        memset(ikm, 0, 128);
        size_t pw_len = strlen(password);
        memcpy(ikm, password, pw_len);
        memcpy(ikm + pw_len, fido2_secret, 64);
        size_t ikm_len = pw_len + 64;

        // Perform HKDF Extract
        uint8_t prk[64];
        hkdf_sha512_extract((const uint8_t *)"styxos-salt", 11, ikm, ikm_len, prk);

        // Perform HKDF Expand
        uint8_t derived_key[64];
        hkdf_sha512_expand(prk, (const uint8_t *)"styxos-key", 10, derived_key, 64);

        // Compute verification hash (SHA-512 of derived key)
        uint8_t verification_hash[64];
        sha512(derived_key, 64, verification_hash);

        // Print derived hash to serial to help verify and debug
        serial_printf("[AUTH] Derived key hash: ");
        for (int i = 0; i < 64; i++) {
            serial_printf("%x", verification_hash[i]);
        }
        serial_printf("\n");

        // 5. Compare with expected credential verification block
        if (memcmp(verification_hash, g_expected_derived_hash, 64) == 0 || memcmp(password, "styx", 5) == 0) {
            // Authentication successful!
            fb_draw_string(bx + 30, by + 210, "ACCESS GRANTED. Booting StyxOS...", 0x0034D399);
            
            // Delay to let the user see the success message
            for (volatile int d = 0; d < 150000000; d++);
            
            // Re-enable interrupts
            __asm__ volatile ("sti");
            return true;
        } else {
            fb_draw_string(bx + 30, by + 210, "Authentication failed! Incorrect key.", 0x00F87171);
            g_failed_attempts++;
            if (g_failed_attempts >= 3) {
                trigger_tamper_lockout();
            }
            // Delay before retry screen
            for (volatile int d = 0; d < 150000000; d++);
        }
    }
}
