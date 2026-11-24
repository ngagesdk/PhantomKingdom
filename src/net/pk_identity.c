/* SPDX-License-Identifier: MIT */
#include "net/pk_identity.h"
#include "core/pk_fs.h"
#include "core/pk_log.h"
#include "core/pk_rand.h"

#include <SDL3/SDL.h>

#define IDENTITY_FILE "identity.cfg"
#define TEST_PREFIX   "00"

static char g_imei[PK_IMEI_LEN + 1];

char pk_luhn_digit(const char *d)
{
    /* Luhn over 14 digits: double every second digit from the right of
     * the payload, i.e. positions 1,3,5,... counting from 0 at the left. */
    int sum = 0, i;
    for (i = 0; i < 14; i++) {
        int v = d[i] - '0';
        if (i & 1) {
            v *= 2;
            if (v > 9) {
                v -= 9;
            }
        }
        sum += v;
    }
    return (char)('0' + (10 - sum % 10) % 10);
}

bool pk_imei_is_valid(const char *s)
{
    int i;
    if (!s || SDL_strlen(s) != PK_IMEI_LEN) {
        return false;
    }
    for (i = 0; i < PK_IMEI_LEN; i++) {
        if (s[i] < '0' || s[i] > '9') {
            return false;
        }
    }
    return pk_luhn_digit(s) == s[14];
}

static void generate(char *out)
{
    Uint8 rnd[32];
    int n = 2, i = 0;
    if (!pk_secure_random(rnd, sizeof(rnd))) {
        PK_WARN("identity: no OS random source, using a weaker fallback");
    }
    SDL_memcpy(out, TEST_PREFIX, 2);
    /* rejection sampling keeps the digits uniform */
    while (n < 14) {
        if (i == (int)sizeof(rnd)) {
            pk_secure_random(rnd, sizeof(rnd));
            i = 0;
        }
        if (rnd[i] < 250) {
            out[n++] = (char)('0' + rnd[i] % 10);
        }
        i++;
    }
    out[14] = pk_luhn_digit(out);
    out[15] = '\0';
}

static bool parse_file(const char *text, size_t len, char *out)
{
    /* format: lines "key=value"; the key we need is imei */
    const char *p = text, *end = text + len;
    while (p < end) {
        const char *eol = p;
        while (eol < end && *eol != '\n' && *eol != '\r') {
            eol++;
        }
        if (eol - p == 5 + PK_IMEI_LEN && SDL_strncmp(p, "imei=", 5) == 0) {
            SDL_memcpy(out, p + 5, PK_IMEI_LEN);
            out[PK_IMEI_LEN] = '\0';
            return pk_imei_is_valid(out);
        }
        p = eol + 1;
    }
    return false;
}

bool pk_identity_init(const char *override_imei)
{
    size_t len = 0;
    char *text;
    char buf[512];

    if (override_imei) {
        if (!pk_imei_is_valid(override_imei)) {
            PK_ERROR("identity: --imei needs 15 digits with a valid check digit");
            return false;
        }
        SDL_strlcpy(g_imei, override_imei, sizeof(g_imei));
        return true;
    }
    text = (char *)pk_fs_load_pref(IDENTITY_FILE, &len);
    if (text && parse_file(text, len, g_imei)) {
        SDL_free(text);
        return true;
    }
    SDL_free(text);
    generate(g_imei);
    SDL_snprintf(buf, sizeof(buf),
                 "# Pocket Kingdom port: synthetic N-Gage identity for N-Gage Arena / PhantomLink.\n"
                 "# Your online account is bound to this value. Keep this file to keep your account.\n"
                 "imei=%s\n",
                 g_imei);
    if (!pk_fs_save_pref(IDENTITY_FILE, buf, SDL_strlen(buf))) {
        PK_WARN("identity: could not save %s; the IMEI will change next start", IDENTITY_FILE);
    } else {
        PK_INFO("identity: created a new synthetic IMEI");
    }
    return true;
}

const char *pk_identity_imei(void)
{
    return g_imei;
}
