/* SPDX-License-Identifier: MIT */
#include "game/pk_data.h"
#include "core/pk_fs.h"
#include "core/pk_log.h"

#include <SDL3/SDL.h>

static const struct
{
    const char *file;
    size_t rec;
} k_tables[PK_TBL_COUNT] = {
    { "mestext.dat", 64 },
    { "area.dat", 48 },
    { "move.dat", 48 },
    { "opening.dat", 64 },
    { "ending.dat", 64 },
    { "staff.dat", 64 },
    { "battlemes.dat", 128 },
    { "pausemess.dat", 256 },
    { "questmes.dat", 320 },
};

static pk_text_table g_tables[PK_TBL_COUNT];
static Uint16 g_scratch[PK_TBL_COUNT][PK_TEXT_MAX];
static Uint16 g_version[64];

static void load_version(void)
{
    /* The data directory is \System\Apps\6RAH; game.id sits three levels
     * up at the card's root. It is plain ASCII. */
    static const char *const k_try[] = { "game.id", "../../../game.id", "../game.id" };
    size_t i, len = 0, k;
    char *s = NULL;
    for (i = 0; i < SDL_arraysize(k_try) && !s; i++) {
        char path[PK_PATH_MAX];
        if (pk_fs_resolve(k_try[i], path, sizeof(path))) {
            s = (char *)SDL_LoadFile(path, &len);
        }
    }
    g_version[0] = 0;
    if (!s) {
        return;
    }
    for (k = 0; k < len && k < SDL_arraysize(g_version) - 1 && s[k] >= 0x20; k++) {
        g_version[k] = (Uint8)s[k];
    }
    g_version[k] = 0;
    SDL_free(s);
}

bool pk_data_load(void)
{
    int i;
    bool ok = true;
    for (i = 0; i < PK_TBL_COUNT; i++) {
        if (!pk_text_table_load(&g_tables[i], k_tables[i].file, k_tables[i].rec, true)) {
            PK_WARN("data: could not load %s", k_tables[i].file);
            ok = false;
        }
    }
    load_version();
    return ok;
}

void pk_data_unload(void)
{
    int i;
    for (i = 0; i < PK_TBL_COUNT; i++) {
        pk_text_table_free(&g_tables[i]);
    }
}

const pk_text_table *pk_data_table(pk_table t)
{
    return t < PK_TBL_COUNT ? &g_tables[t] : NULL;
}

const Uint16 *pk_data_text(pk_table t, int i)
{
    if (t >= PK_TBL_COUNT) {
        return g_scratch[0];
    }
    pk_text_get16(&g_tables[t], i, g_scratch[t], PK_TEXT_MAX);
    return g_scratch[t];
}

const Uint16 *pk_data_version(void)
{
    return g_version;
}

int pk_data_kingdom_name_count(void)
{
    size_t len = 0;
    void *p = pk_fs_load("name.dat", &len);
    SDL_free(p);
    return len > 16 ? (int)((len - 16) / 24) : 0;
}

void pk_data_kingdom_name(int index, Uint16 *out, int cap)
{
    size_t len = 0, i;
    Uint8 *p;
    int n = 0;
    if (cap > 0) {
        out[0] = 0;
    }
    if (!pk_fs_data_dir()[0]) {
        return;
    }
    p = (Uint8 *)pk_fs_load("name.dat", &len);
    if (!p || index < 0 || 16 + (size_t)(index + 1) * 24 > len) {
        SDL_free(p);
        return;
    }
    for (i = 0; i < 24 && n < cap - 1; i++) {
        Uint8 c = p[16 + (size_t)index * 24 + i];
        if (!c) {
            break;
        }
        out[n++] = c; /* ISO 8859-1 maps 1:1 to Unicode */
    }
    out[n] = 0;
    SDL_free(p);
}
