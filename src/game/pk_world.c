/* SPDX-License-Identifier: MIT */
#include "game/pk_world.h"
#include "core/pk_blowfish.h"
#include "core/pk_fs.h"
#include "core/pk_log.h"
#include "engine/pk_game.h"
#include "game/pk_arena_game.h"
#include "game/pk_data.h"
#include "net/pk_identity.h"
#include "platform/pk_play_games.h"

#include <SDL3/SDL.h>

Uint32 pk_crc32(const Uint8 *p, size_t n)
{
    Uint32 c = 0xffffffffu;
    size_t i;
    int k;
    for (i = 0; i < n; i++) {
        c ^= p[i];
        for (k = 0; k < 8; k++) {
            c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
        }
    }
    return ~c;
}

void pk_world_reset(pk_game *g)
{
    pk_world *w = &g->world;
    static const char k_default_name[] = "dangerboy";
    int i, j;

    SDL_zerop(w);
    for (i = 0; i < PK_TEAMS; i++) {
        for (j = 0; j < 4; j++) {
            w->teams[i][j] = 0xff;
            w->teams[i][4 + j] = 5;
        }
        w->teams[i][11] = 6;
    }
    for (i = 0; i < PK_UNITS; i++) {
        w->units[i][0] = w->units[i][2] = w->units[i][3] = 0xff;
    }
    for (i = 0; i < PK_ITEMS; i++) {
        w->items[i][0] = 0xff;
    }
    for (i = 0; i < PK_EQUIPS; i++) {
        static const int k_init[13] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0, 0xff, 0, 0, 0, 0xff, 0, 0xff };
        SDL_memcpy(w->equip[i], k_init, sizeof(k_init));
    }
    for (i = 0; i < PK_AREAS; i++) {
        for (j = 0; j < 5; j++) {
            w->areas[i][5 + j] = 0xff;
            w->areas[i][10 + j] = 0xff;
        }
    }
    w->areas[0][36] = 1; /* +0x78a0 of area 0 */
    w->current_area = 0;
    w->h_428 = 2;
    for (i = 0; i < PK_EVENTS; i++) {
        w->events[i][2] = 0xff;
        w->events[i][3] = 0xff;
    }
    for (i = 0; i < PK_FORMATIONS; i++) {
        w->formations[i][43] = 0xffff;
        w->formations[i][45] = 0xff;
    }
    w->unit_level = 1;
    w->h_dc = 0xff;
    w->h_4e8 = 5;
    w->h_4ec = 5;
    w->h_4f4 = 0xff;
    for (i = 0; k_default_name[i]; i++) {
        w->player_name[i] = (Uint8)k_default_name[i];
    }
    g->fame = 0;
    g->loot = 10000;
    g->progress = 0;
    g->rival_index = 0;
}

/* ------------------------------------------------------------------ */

typedef struct cursor
{
    Uint8 *w;       /* writing */
    const Uint8 *r; /* reading */
    size_t pos;
} cursor;

#define B(n)    (c->r[c->pos + (n)])
#define W(n, v) (c->w[c->pos + (n)] = (Uint8)(v))

static void io_header(pk_game *g, cursor *c, bool write)
{
    pk_world *w = &g->world;
    int *one[] = { &w->unit_count, &w->item_count, &w->equip_count, &w->unit_level, &w->h_dc, &g->rival_index, &w->h_140,
                   &w->current_area, &w->h_428 };
    int *two[] = { &w->h_43c, &g->progress, &w->h_444, &w->h_4e4, &w->h_4e8, &w->h_4ec, &w->h_450, &g->field_bg };
    size_t i;
    if (write) {
        for (i = 0; i < SDL_arraysize(one); i++) {
            W(i, *one[i]);
        }
        W(9, g->fame >> 16);
        W(10, g->fame >> 8);
        W(11, g->fame);
        W(12, g->loot >> 16);
        W(13, g->loot >> 8);
        W(14, g->loot);
        for (i = 0; i < SDL_arraysize(two); i++) {
            W(15 + i, *two[i]);
        }
        W(23, g->name_index);
        W(24, g->name_index >> 8);
        W(25, w->h_d4);
        W(26, w->h_4f4);
        W(28, w->h_448);
    } else {
        for (i = 0; i < SDL_arraysize(one); i++) {
            *one[i] = B(i);
        }
        g->fame = (B(9) << 16) | (B(10) << 8) | B(11);
        g->loot = (B(12) << 16) | (B(13) << 8) | B(14);
        for (i = 0; i < SDL_arraysize(two); i++) {
            *two[i] = B(15 + i);
        }
        g->name_index = B(23) | (B(24) << 8);
        w->h_d4 = B(25);
        w->h_4f4 = B(26);
        w->h_448 = B(28);
    }
    c->pos += 0x20;
    for (i = 0; i < 20; i++) {
        if (write) {
            W(i, w->player_name[i] & 0xff);
        } else {
            w->player_name[i] = B(i);
        }
        if (!write && !B(i)) {
            break;
        }
    }
    w->player_name[20] = 0;
    c->pos += 0x14;
}

static void io_bytes(cursor *c, bool write, int *f, const int *map, int n, size_t size)
{
    int i;
    for (i = 0; i < n; i++) {
        if (map[i] < 0) {
            continue;
        }
        if (write) {
            W(i, f[map[i]]);
        } else {
            f[map[i]] = B(i);
        }
    }
    c->pos += size;
}

static void io_records(pk_game *g, cursor *c, bool write)
{
    pk_world *w = &g->world;
    static const int k_kingdom[23] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22 };
    static const int k_team[13] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 11, 12, 13 };
    static const int k_item[9] = { 0, 1, 2, 3, 4, 5, 6, 7, 8 };
    static const int k_roster[13] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12 };
    static const int k_event[9] = { 0, 1, 2, 3, 6, 7, 8, 9, 10 };
    int i, j, k;

    for (i = 0; i < PK_UNITS; i++) {
        int *f = w->units[i];
        if (write) {
            int keep = f[19];
            f[19] &= 0xff;
            io_bytes(c, true, f, k_kingdom, 23, 0);
            f[19] = keep;
            W(23, keep >> 8);
        } else {
            io_bytes(c, false, f, k_kingdom, 23, 0);
            f[19] |= B(23) << 8;
        }
        c->pos += 0x20;
    }
    for (i = 0; i < PK_TEAMS; i++) {
        io_bytes(c, write, w->teams[i], k_team, 13, 0x10);
    }
    for (i = 0; i < PK_ITEMS; i++) {
        io_bytes(c, write, w->items[i], k_item, 9, 0x10);
    }
    for (i = 0; i < PK_EQUIPS; i++) {
        io_bytes(c, write, w->equip[i], k_roster, 13, 0x10);
    }
    for (i = 0; i < PK_AREAS; i++) {
        int *f = w->areas[i];
        if (write) {
            W(0, f[0]);
            W(1, 0);
            W(2, f[2]);
            W(3, f[3]);
            W(4, f[4] >> 8);
            W(5, f[4]);
            for (j = 0; j < 10; j++) {
                W(6 + j, f[5 + j]);
            }
            for (j = 0; j < 15; j++) {
                W(16 + j, f[15 + j]);
            }
            for (j = 0; j < 5; j++) {
                W(31 + j, f[30 + j]);
            }
            W(36, f[35]);
            W(37, f[36]);
        } else {
            f[0] = B(0);
            f[1] = B(1);
            f[2] = B(2);
            f[3] = B(3);
            f[4] = (B(4) << 8) | B(5);
            for (j = 0; j < 10; j++) {
                f[5 + j] = B(6 + j);
            }
            for (j = 0; j < 15; j++) {
                f[15 + j] = B(16 + j);
            }
            for (j = 0; j < 5; j++) {
                f[30 + j] = B(31 + j);
            }
            f[35] = B(36);
            f[36] = B(37);
        }
        c->pos += 0x30;
    }
    for (i = 0; i < PK_EVENTS; i++) {
        io_bytes(c, write, w->events[i], k_event, 9, 0x10);
    }
    for (i = 0; i < PK_FORMATIONS; i++) {
        int *f = w->formations[i];
        static const int k_cols[9] = { 3, 11, 15, 19, 23, 27, 31, 35, 39 };
        if (write) {
            W(0, f[43] >> 8);
            W(1, f[43]);
            W(2, f[44]);
            W(3, f[45]);
            W(4, f[46]);
            W(5, f[0]);
            W(6, f[1]);
            W(7, f[2]);
        } else {
            f[43] = (B(0) << 8) | B(1);
            f[44] = B(2);
            f[45] = B(3);
            f[46] = B(4);
            f[0] = B(5);
            f[1] = B(6);
            f[2] = B(7);
        }
        for (j = 0; j < 4; j++) {
            for (k = 0; k < 9; k++) {
                if (write) {
                    W(8 + j * 9 + k, f[k_cols[k] + j]);
                } else {
                    f[k_cols[k] + j] = B(8 + j * 9 + k);
                }
            }
        }
        c->pos += 0x30;
    }
    for (i = 0; i < 16; i++) {
        if (write) {
            W(i, w->b5bc[i]);
        } else {
            w->b5bc[i] = B(i);
        }
    }
    c->pos += 0x10;
    for (i = 0; i < 42; i++) {
        if (write) {
            W(2 * i, w->pairs[i][0]);
            W(2 * i + 1, w->pairs[i][1]);
        } else {
            w->pairs[i][0] = B(2 * i);
            w->pairs[i][1] = B(2 * i + 1);
        }
    }
    c->pos += 0x54;
    for (i = 0; i < 58; i++) {
        if (write) {
            W(i, w->help_seen[i]);
        } else {
            w->help_seen[i] = B(i);
        }
    }
    c->pos += 0x3a;
}

#undef B
#undef W

void pk_world_serialize(const pk_game *g, Uint8 *out)
{
    cursor c;
    Uint32 crc;
    SDL_memset(out, 0, PK_SAVE_SIZE);
    c.w = out;
    c.r = out;
    c.pos = 0;
    io_header((pk_game *)g, &c, true); /* writing does not modify g */
    io_records((pk_game *)g, &c, true);
    crc = pk_crc32(out, PK_SAVE_SIZE - 4);
    out[PK_SAVE_SIZE - 4] = (Uint8)crc;
    out[PK_SAVE_SIZE - 3] = (Uint8)(crc >> 8);
    out[PK_SAVE_SIZE - 2] = (Uint8)(crc >> 16);
    out[PK_SAVE_SIZE - 1] = (Uint8)(crc >> 24);
}

bool pk_world_deserialize(pk_game *g, const Uint8 *in, size_t len)
{
    cursor c;
    Uint32 crc;
    if (!in || len != PK_SAVE_SIZE) {
        return false;
    }
    crc = (Uint32)in[len - 4] | ((Uint32)in[len - 3] << 8) | ((Uint32)in[len - 2] << 16) | ((Uint32)in[len - 1] << 24);
    if (crc != pk_crc32(in, len - 4)) {
        return false;
    }
    c.w = NULL;
    c.r = in;
    c.pos = 0;
    io_header(g, &c, false);
    io_records(g, &c, false);
    pk_data_kingdom_name(g->name_index, g->kingdom_name, (int)SDL_arraysize(g->kingdom_name));
    return true;
}

bool pk_world_save(const pk_game *g)
{
    Uint8 *buf = (Uint8 *)SDL_malloc(PK_SAVE_SIZE);
    bool ok;
    if (!buf) {
        return false;
    }
    pk_world_serialize(g, buf);
    ok = pk_fs_save_pref("save.dat", buf, PK_SAVE_SIZE);
    SDL_free(buf);
    if (!ok) {
        PK_WARN("save: could not write save.dat");
    }
    return ok;
}

/* A save.dat copied from the phone (FUN_10075e80): 0x2958 bytes
 * Blowfish-ECB encrypted, then a 4-byte random salt; the key is the
 * phone's IMEI (as text) followed by the salt (FUN_1007507c). The IMEI
 * is the one given with --imei. Imported saves are written back
 * unencrypted by the port. */
static bool load_phone_save(pk_game *g, const Uint8 *buf, size_t len)
{
    const char *imei = pk_identity_imei();
    Uint8 key[72], *plain;
    pk_blowfish *bf;
    int klen;
    bool ok = false;
    if (len != PK_SAVE_SIZE + 4 || !imei || !imei[0]) {
        return false;
    }
    klen = (int)SDL_strlen(imei);
    if (klen > 64) {
        klen = 64;
    }
    SDL_memcpy(key, imei, (size_t)klen);
    SDL_memcpy(key + klen, buf + PK_SAVE_SIZE, 4);
    bf = (pk_blowfish *)SDL_malloc(sizeof(*bf));
    plain = (Uint8 *)SDL_malloc(PK_SAVE_SIZE);
    if (bf && plain && pk_blowfish_init(bf, key, klen + 4)) {
        SDL_memcpy(plain, buf, PK_SAVE_SIZE);
        pk_blowfish_ecb(bf, plain, PK_SAVE_SIZE, true);
        ok = pk_world_deserialize(g, plain, PK_SAVE_SIZE);
    }
    SDL_free(bf);
    SDL_free(plain);
    if (ok) {
        PK_INFO("save: imported a phone save (IMEI %s)", imei);
    }
    return ok;
}

/* the title's N-Gage Arena entry (game+0x43c / +0x440, read from the save
 * header at start-up, FUN_1007...): open once the game has been cleared or
 * the third area boss beaten, which is when the Online Crest is given */
#ifdef PK_DEBUG_UNLOCK_ARENA
/* port, Debug builds only (CMake PK_DEBUG_UNLOCK_ARENA): N-Gage Arena is
 * unlocked without the progress the original asks for, to test online
 * play. Release builds keep the original rule below. */
static bool debug_arena_unlocked(void)
{
    static bool logged;
    if (!logged) {
        logged = true;
        PK_INFO("debug build: N-Gage Arena unlocked (PK_DEBUG_UNLOCK_ARENA)");
    }
    return true;
}
#endif

int pk_world_arena_unlocked(void)
{
    size_t len = 0;
    Uint8 *buf;
    pk_game *t;
    int ok = 0;
#ifdef PK_DEBUG_UNLOCK_ARENA
    if (debug_arena_unlocked()) {
        return 1;
    }
#endif
    buf = (Uint8 *)pk_fs_load_pref("save.dat", &len);
    if (!buf) {
        return 0;
    }
    t = (pk_game *)SDL_calloc(1, sizeof(*t));
    if (t && (pk_world_deserialize(t, buf, len) || load_phone_save(t, buf, len))) {
        ok = t->world.h_43c > 0 || t->progress > 2;
    }
    SDL_free(t);
    SDL_free(buf);
    return ok;
}

bool pk_world_load(pk_game *g)
{
    size_t len = 0;
    Uint8 *buf = (Uint8 *)pk_fs_load_pref("save.dat", &len);
    bool ok = pk_world_deserialize(g, buf, len);
    if (!ok && buf) {
        ok = load_phone_save(g, buf, len);
    }
    SDL_free(buf);
    if (!ok && buf) {
        PK_WARN("save: save.dat is damaged, or a phone save whose IMEI was not given with --imei");
    }
    if (ok) {
        /* port: a save made while online may hold rival bases and online
         * missions (the original drops them when it goes offline,
         * FUN_100723e0); a party left busy by an online mission whose
         * answer never came is free again: no event holds it */
        int t, i;
        pk_rivals_reset(g, false);
        for (t = 0; t < PK_TEAMS; t++) {
            bool held = false;
            for (i = 0; i < PK_EVENTS; i++) {
                if (g->world.events[i][1] != 0 && g->world.events[i][3] == t) {
                    held = true;
                }
            }
            if (!held && g->world.teams[t][12] != 0 && t != (g->world.h_dc & 7)) {
                g->world.teams[t][12] = 0;
            }
        }
        pk_play_games_treasures(g->world.h_444);
    }
    return ok;
}
