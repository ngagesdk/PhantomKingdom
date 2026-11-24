/* SPDX-License-Identifier: MIT */
#include "game/pk_rules.h"
#include "core/pk_app.h"
#include "core/pk_fs.h"
#include "core/pk_log.h"
#include "game/pk_data.h"

#include <SDL3/SDL.h>

pk_rules pk_r;

/* Section offsets of the eight kinds in itemdata.dat (FUN_100622e8). */
static const int k_item_section[8] = { 0xab0, 0xd70, 0xf30, 0x1430, 0x23b0, 0x2b70, 0x4770, 0x4af0 };

#define TBL_NAME_GROUP 0x100b05c4u /* unit type -> name.dat group (FUN_1007abf4) */

static const int k_empty_item[9] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0, 0, 0xff };                  /* FUN_10069544 */
static const int k_empty_equip[13] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0, 0xff, 0, 0, 0, 0xff, 0, 0xff }; /* FUN_10069574 */
static const int k_empty_unit[23] = { 0xff, 0, 0xff, 0xff, 0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                                      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0, 0xffff, 0, 0xff, 0 }; /* FUN_100693fc */

void pk_rules_init(void)
{
    int i;
    SDL_zero(pk_r);
    for (i = 0; i < PK_PREVIEW_SLOTS; i++) {
        SDL_memcpy(pk_r.item[i].rec, k_empty_item, sizeof(k_empty_item));
        SDL_memcpy(pk_r.equip[i].rec, k_empty_equip, sizeof(k_empty_equip));
    }
}

/* Reads `len` bytes at `off` of a data file; false if it is too short. */
static bool read_at(const char *name, bool localized, size_t off, Uint8 *out, size_t len)
{
    size_t n = 0;
    Uint8 *p = (Uint8 *)(localized ? pk_fs_load_localized(name, &n) : pk_fs_load(name, &n));
    bool ok = p && off + len <= n;
    if (ok) {
        SDL_memcpy(out, p + off, len);
    } else {
        SDL_memset(out, 0xff, len);
    }
    SDL_free(p);
    return ok;
}

static void name_from(const Uint8 *src, Uint16 *out, int cap)
{
    pk_text_decode(src, 0x18, out, cap); /* 24 UTF-16 units, ESC terminated */
}

void pk_item_preview(int kind, int id, int slot)
{
    Uint8 r[0x40];
    int off = (kind >= 0 && kind < 8) ? k_item_section[kind] : 0;
    if (slot < 0 || slot >= PK_PREVIEW_SLOTS) {
        return;
    }
    read_at("itemdata.dat", true, (size_t)(off + id * 0x40), r, sizeof(r));
    if (kind < PK_KIND_EQUIP) {
        pk_item_info *it = &pk_r.item[slot];
        name_from(r, it->name, 25);
        it->rec[0] = kind;
        it->rec[1] = id;
        if (kind == 4) {
            it->rec[6] = r[0x30];
            it->price = (r[0x31] << 8) | r[0x32];
        } else {
            it->rec[2] = r[0x30];
            it->rec[3] = r[0x31];
            it->rec[6] = r[0x32];
            it->rec[7] = r[0x33];
            it->rec[4] = r[0x34];
            it->rec[5] = r[0x35];
            it->price = (r[0x36] << 8) | r[0x37];
        }
    } else {
        pk_equip_info *e = &pk_r.equip[slot];
        name_from(r, e->name, 25);
        e->rec[0] = kind;
        e->rec[1] = id;
        e->rec[5] = r[0x30];
        e->rec[2] = e->rec[3] = e->rec[4] = 0xff;
        e->rec[7] = e->rec[8] = e->rec[9] = 0;
        if (kind == 5) {
            e->rec[7] = r[0x31];
            e->rec[9] = r[0x31];
        } else if (kind == 6) {
            e->rec[8] = r[0x31];
        }
        e->price = kind == 7 ? (r[0x31] << 8) | r[0x32] : (r[0x32] << 8) | r[0x33];
    }
}

/* FUN_100627d8: the name of an item's property: mode 0 from the first
 * section of itemdata.dat (5 entries), modes 1+ from the second (52),
 * 0x30 bytes each; empty for 0xff or out of range */
void pk_item_attr_name(int mode, int value, Uint16 *out, int cap)
{
    static const int k_off[2] = { 0, 0xf0 }, k_count[2] = { 5, 52 };
    Uint8 r[0x30];
    int m = mode < 1 ? 0 : 1;
    out[0] = 0;
    if (value == 0xff || value < 0 || value >= k_count[m]) {
        return;
    }
    if (!read_at("itemdata.dat", true, (size_t)(k_off[m] + value * 0x30), r, sizeof(r))) {
        return;
    }
    name_from(r, out, cap);
}

void pk_unit_template(int type, int slot)
{
    Uint8 r[0xa0];
    pk_unit_info *u;
    int i;
    if (slot < 0 || slot >= PK_PREVIEW_SLOTS) {
        return;
    }
    u = &pk_r.unit[slot];
    read_at("unit.dat", true, (size_t)type * 0xa0, r, sizeof(r));
    name_from(r, u->name, 25);
    name_from(r + 0x30, u->race, 25);
    name_from(r + 0x60, u->skill, 25);
    u->type = type;
    for (i = 0; i < 15; i++) {
        u->b[i] = r[0x90 + i];
    }
    u->price = (r[0x9c] << 8) | r[0x9d];
}

void pk_shop_load_items(int area)
{
    Uint8 r[16];
    int i;
    read_at("itemshop.dat", false, (size_t)(area * 0x10 + 0x10), r, sizeof(r));
    for (i = 0; i < 5; i++) {
        pk_r.shop_kind[i] = r[i * 2];
        pk_r.shop_id[i] = r[i * 2 + 1];
    }
    pk_r.shop_count = r[10];
}

void pk_shop_load_units(int area)
{
    Uint8 r[8];
    int i;
    read_at("unitshop.dat", false, (size_t)(area * 8 + 0x10), r, sizeof(r));
    for (i = 0; i < 5; i++) {
        pk_r.unit_shop[i] = r[i];
    }
    pk_r.unit_shop_count = r[5];
}

/* ------------------------------------------------------------------ */

int pk_item_add(pk_game *g, int kind, int id)
{
    pk_world *w = &g->world;
    int i;
    pk_item_preview(kind, id, 10);
    if (kind < PK_KIND_EQUIP) {
        for (i = 0; i < PK_ITEM_MAX; i++) {
            if (w->items[i][0] == 0xff) {
                SDL_memcpy(w->items[i], pk_r.item[10].rec, sizeof(w->items[i]));
                w->item_count++;
                return i;
            }
        }
    } else {
        for (i = 0; i < PK_EQUIP_MAX; i++) {
            if (w->equip[i][0] == 0xff) {
                SDL_memcpy(w->equip[i], pk_r.equip[10].rec, sizeof(w->equip[i]));
                w->equip_count++;
                return i;
            }
        }
    }
    return -1;
}

void pk_item_remove(pk_game *g, int index)
{
    pk_world *w = &g->world;
    if (index >= 0 && index < PK_ITEM_MAX && w->items[index][0] != 0xff) {
        SDL_memcpy(w->items[index], k_empty_item, sizeof(k_empty_item));
        w->item_count--;
    }
}

void pk_equip_remove(pk_game *g, int index)
{
    pk_world *w = &g->world;
    if (index >= 0 && index < PK_EQUIP_MAX && w->equip[index][0] != 0xff) {
        SDL_memcpy(w->equip[index], k_empty_equip, sizeof(k_empty_equip));
        w->equip_count--;
    }
}

int pk_item_find(pk_game *g, int kind, int n)
{
    pk_world *w = &g->world;
    int i, k = 0;
    if (kind >= 5 && kind <= 7) {
        for (i = 0; i < PK_EQUIP_MAX; i++) {
            if (w->equip[i][0] == kind && k++ == n) {
                return i;
            }
        }
    } else {
        for (i = 0; i < PK_ITEM_MAX; i++) {
            if (w->items[i][0] == kind && k++ == n) {
                return i;
            }
        }
    }
    return 0xff;
}

int pk_item_count_kind(pk_game *g, int kind)
{
    pk_world *w = &g->world;
    int i, n = 0;
    if (kind < PK_KIND_EQUIP) {
        for (i = 0; i < PK_ITEM_MAX; i++) {
            n += w->items[i][0] == kind;
        }
    } else {
        for (i = 0; i < PK_EQUIP_MAX; i++) {
            n += w->equip[i][0] == kind;
        }
    }
    return n;
}

/* The original sorts with a recursive quicksort on keys that are unique
 * (they include the record index), so any correct sort gives the same
 * order. */
typedef struct sort_rec
{
    int key;
    int rec[23];
} sort_rec;

static int cmp_key(const void *a, const void *b)
{
    int x = ((const sort_rec *)a)->key, y = ((const sort_rec *)b)->key;
    return (x > y) - (x < y);
}

static void sort_records(int *base, int n, int words, int (*key)(const int *r))
{
    sort_rec *t = (sort_rec *)SDL_malloc(sizeof(sort_rec) * (size_t)n);
    int i;
    if (!t) {
        return;
    }
    for (i = 0; i < n; i++) {
        SDL_memcpy(t[i].rec, base + i * words, sizeof(int) * (size_t)words);
        t[i].key = key(t[i].rec);
    }
    SDL_qsort(t, (size_t)n, sizeof(*t), cmp_key);
    for (i = 0; i < n; i++) {
        SDL_memcpy(base + i * words, t[i].rec, sizeof(int) * (size_t)words);
    }
    SDL_free(t);
}

static int key_item(const int *r) { return r[0] * 0x10000 + r[1] * 0x100 + r[8]; }
static int key_equip(const int *r) { return r[0] * 0x10000 + r[1] * 0x100 + r[12]; }
static int key_unit(const int *r) { return r[0] == 0xff ? -1 : r[21] + r[0] * 0x100; }

void pk_items_sort(pk_game *g)
{
    int i;
    for (i = 0; i < PK_ITEM_MAX; i++) {
        g->world.items[i][8] = i;
    }
    sort_records(&g->world.items[0][0], PK_ITEM_MAX, 9, key_item);
}

void pk_equip_sort(pk_game *g, bool owned_last)
{
    int i;
    for (i = 0; i < PK_EQUIP_MAX; i++) {
        int *e = g->world.equip[i];
        e[12] = i + ((owned_last && e[11] != 0) ? 0x10000 : 0);
    }
    sort_records(&g->world.equip[0][0], PK_EQUIP_MAX, 13, key_equip);
    /* FUN_10061fb8 keeps the owners' weapon references in step */
    for (i = 0; i < PK_EQUIP_MAX; i++) {
        const int *e = g->world.equip[i];
        if (e[0] != 0xff && e[11] != 0 && e[10] >= 0 && e[10] < PK_UNIT_MAX) {
            g->world.units[e[10]][2] = i;
        }
    }
}

void pk_units_sort(pk_game *g, bool assigned_last)
{
    int i;
    for (i = 0; i < PK_UNIT_MAX; i++) {
        int *u = g->world.units[i];
        u[21] = i + ((assigned_last && u[3] != 0xff) ? 0x10000 : 0);
    }
    /* key -1 for empty slots: FUN_1007a19c compares unsigned, so they sort last */
    {
        sort_rec *t = (sort_rec *)SDL_malloc(sizeof(sort_rec) * PK_UNIT_MAX);
        if (!t) {
            return;
        }
        for (i = 0; i < PK_UNIT_MAX; i++) {
            SDL_memcpy(t[i].rec, g->world.units[i], sizeof(g->world.units[i]));
            t[i].key = key_unit(t[i].rec);
        }
        for (i = 1; i < PK_UNIT_MAX; i++) { /* insertion sort, unsigned keys */
            sort_rec v = t[i];
            int j = i - 1;
            while (j >= 0 && (Uint32)t[j].key > (Uint32)v.key) {
                t[j + 1] = t[j];
                j--;
            }
            t[j + 1] = v;
        }
        for (i = 0; i < PK_UNIT_MAX; i++) {
            SDL_memcpy(g->world.units[i], t[i].rec, sizeof(g->world.units[i]));
        }
        SDL_free(t);
    }
    /* FUN_1007a2a4 keeps equipment owners and team slots in step */
    for (i = 0; i < PK_UNIT_MAX; i++) {
        const int *u = g->world.units[i];
        if (u[0] == 0xff) {
            continue;
        }
        if (u[2] != 0xff && u[2] < PK_EQUIP_MAX) {
            g->world.equip[u[2]][10] = i;
        }
        if (u[3] != 0xff) {
            g->world.teams[(u[3] >> 4) & 7][u[3] & 3] = i;
        }
    }
}

void pk_units_level(pk_game *g)
{
    int i, sq = 0, sum = 0, v;
    for (i = 0; i < PK_UNIT_MAX; i++) {
        if (g->world.units[i][0] != 0xff) {
            int r = g->world.units[i][14];
            sq += r * r;
            sum += r;
        }
    }
    if (sum == 0) {
        g->world.unit_level = 1;
        return;
    }
    v = sq * 10 / sum;
    if (v % 10 > 4) {
        v += 10;
    }
    g->world.unit_level = v / 10;
}

int pk_unit_name_pick(pk_game *g, int group)
{
    int base = 0, n = 400, idx, i;
    bool used;
    if (group == 0) {
        n = 200;
    } else if (group == 1) {
        base = 200;
        n = 200;
    }
    idx = base + pk_rng_range(&g->rng, n);
    do {
        used = idx == g->rival_index;
        for (i = 0; !used && i < g->world.unit_count; i++) {
            used = g->world.units[i][19] == idx;
        }
        if (used && ++idx >= base + n) {
            idx = base;
        }
    } while (used);
    return idx;
}

int pk_unit_add(pk_game *g, int type)
{
    pk_world *w = &g->world;
    const pk_unit_info *t;
    int i, *u;
    pk_unit_template(type, 10);
    t = &pk_r.unit[10];
    for (i = 0; i < PK_UNIT_MAX && w->units[i][0] != 0xff; i++) {
    }
    if (i == PK_UNIT_MAX) {
        return -1;
    }
    u = w->units[i];
    u[0] = type;
    u[1] = 0;
    u[2] = 0xff;
    u[3] = 0xff;
    u[4] = 0;
    u[5] = t->b[5];
    u[6] = t->b[6];
    u[7] = u[8] = t->b[4];
    u[9] = t->b[7];
    u[10] = t->b[8];
    u[11] = t->b[9];
    u[12] = t->b[10];
    u[13] = t->b[11];
    u[14] = t->b[1];
    u[15] = t->b[0];
    u[16] = t->b[2];
    u[17] = t->b[3];
    u[18] = 0;
    u[19] = pk_unit_name_pick(g, (int)pk_app_i32(TBL_NAME_GROUP + (Uint32)type * 4u));
    u[20] = t->b[14];
    u[21] = 0;
    u[22] = 0;
    w->unit_count++;
    pk_units_level(g);
    return i;
}

/* FUN_10057dd4 (rank up): the unit becomes another class at level 0;
 * it keeps its name, drops its weapon and leaves its team. */
void pk_unit_set_type(pk_game *g, int index, int type)
{
    pk_world *w = &g->world;
    const pk_unit_info *t;
    int *u;
    if (index < 0 || index >= PK_UNIT_MAX || w->units[index][0] == 0xff) {
        return;
    }
    pk_unit_template(type, 10);
    t = &pk_r.unit[10];
    u = w->units[index];
    if (u[2] != 0xff && u[2] < PK_EQUIP_MAX) {
        w->equip[u[2]][10] = 0xff;
        w->equip[u[2]][11] = 0;
    }
    u[0] = type;
    u[1] = 0;
    u[2] = 0xff;
    u[3] = 0xff;
    u[4] = 0;
    u[5] = t->b[5];
    u[6] = t->b[6];
    u[7] = u[8] = t->b[4];
    u[9] = t->b[7];
    u[10] = t->b[8];
    u[11] = t->b[9];
    u[12] = t->b[10];
    u[13] = t->b[11];
    u[14] = t->b[1];
    u[15] = t->b[0];
    u[16] = t->b[2];
    u[17] = t->b[3];
    u[18] = 0;
    u[20] = t->b[14];
    u[22] = 0;
}

void pk_unit_remove(pk_game *g, int index)
{
    pk_world *w = &g->world;
    if (index >= 0 && index < PK_UNIT_MAX && w->units[index][0] != 0xff) {
        SDL_memcpy(w->units[index], k_empty_unit, sizeof(k_empty_unit));
        w->unit_count--;
        pk_units_level(g);
    }
}

void pk_loot_add(pk_game *g, int n)
{
    g->loot += n;
    if (g->loot > 9999999) {
        g->loot = 9999999;
    }
    if (g->loot < 1) {
        g->loot = 0;
    }
}

void pk_loot_sub(pk_game *g, int n)
{
    pk_loot_add(g, -n);
}

void pk_unit_name(const pk_game *g, int index, Uint16 *out, int cap)
{
    if (cap > 0) {
        out[0] = 0;
    }
    if (index < 0 || index >= PK_UNIT_MAX || g->world.units[index][0] == 0xff) {
        return;
    }
    pk_data_kingdom_name(g->world.units[index][19], out, cap);
}

/* ------------------------------------------------------------------ */
/* Medical */

static bool unit_at_home(pk_game *g, int u)
{
    int t = g->world.units[u][3];
    return t == 0xff || g->world.teams[(t >> 4) & 7][12] == 0;
}

int pk_heal_cost_unit(pk_game *g, int u)
{
    const int *r;
    if (u < 0 || u >= PK_UNIT_MAX) {
        return 0;
    }
    r = g->world.units[u];
    if (r[0] == 0xff) {
        return 0;
    }
    return r[14] * (r[8] - r[7]) * 10;
}

int pk_heal_cost_team(pk_game *g, int team)
{
    int i, sum = 0;
    if (team < 0 || team >= 8) {
        return 0;
    }
    for (i = 0; i < 4; i++) {
        if (g->world.teams[team][i] != 0xff) {
            sum += pk_heal_cost_unit(g, g->world.teams[team][i]);
        }
    }
    return sum < 0 ? 0 : sum;
}

int pk_heal_cost_all(pk_game *g)
{
    int i, sum = 0;
    for (i = 0; i < PK_UNIT_MAX; i++) {
        if (g->world.units[i][0] != 0xff && unit_at_home(g, i)) {
            sum += pk_heal_cost_unit(g, i);
        }
    }
    return sum;
}

void pk_heal_unit(pk_game *g, int u)
{
    int *r;
    if (u < 0 || u >= PK_UNIT_MAX) {
        return;
    }
    r = g->world.units[u];
    if (r[0] != 0xff && r[22] == 0) {
        r[7] = r[8];
    }
}

void pk_heal_all(pk_game *g)
{
    int i;
    for (i = 0; i < PK_UNIT_MAX; i++) {
        if (g->world.units[i][0] != 0xff && unit_at_home(g, i)) {
            pk_heal_unit(g, i);
        }
    }
}
