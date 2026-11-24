/* SPDX-License-Identifier: MIT */
/* Map (root update 0x1003db80) and the mission events.
 *
 * Teams act through events (world.events, see docs/RE_NOTES.md). The map
 * sends a team on an area search: after its timer (logic frames, counted
 * only while the kingdom screen runs) the kingdom hands the finished
 * event to the event handler (0x10028538), which starts a battle against
 * a partydata.dat formation of the current area.
 *
 * Port status: the map shows its background and the search menu; the
 * area picture with its spots, Scout, Chat, Attack, Move and Info are not
 * reconstructed yet. */
#include "audio/pk_audio.h"
#include "core/pk_app.h"
#include "core/pk_fs.h"
#include "core/pk_log.h"
#include "game/pk_arena_game.h"
#include "game/pk_battle.h"
#include "game/pk_data.h"
#include "game/pk_help.h"
#include "game/pk_rules.h"
#include "game/pk_scenes.h"
#include "game/ui_dialog.h"
#include "gfx/pk_font.h"
#include "net/pk_online.h"
#include <SDL3/SDL.h>
#include <stdarg.h>

#define COL_GOLD_MAP 0xFFDC00u
#define TBL_MAP_BG   0x10096290u                                                    /* area -> background - 0x3a */
#define TBL_TRAVEL   0x10096344u                                                    /* area -> travel cost per rank (16 bytes per area) */
static const int k_event_time[12] = { 5, 11, 11, 11, 11, 5, 330, 5, 5, 660, 5, 5 }; /* 0x100aecf8 */

/* ------------------------------------------------------------------ */
/* events */

int pk_event_start(pk_game *g, int kind, int team) /* FUN_10070c78 */
{
    int i, *e;
    for (i = 0; i < PK_EVENTS && g->world.events[i][1] != 0; i++) {
    }
    if (i == PK_EVENTS) {
        return 0xff;
    }
    e = g->world.events[i];
    e[2] = kind;
    e[1] = 1;
    e[12] = 0;
    e[11] = 0;
    e[0] = kind < 5 ? k_event_time[kind] + pk_rng_range(&g->rng, 5) * 0x16 : k_event_time[kind];
    e[4] = ++g->event_seq;
    e[3] = team;
    return i;
}

void pk_event_clear(pk_game *g, int i) /* FUN_10070e28 */
{
    int *e;
    if (i < 0 || i >= PK_EVENTS) {
        return;
    }
    e = g->world.events[i];
    e[0] = e[1] = e[4] = e[5] = 0;
    e[2] = e[3] = 0xff;
}

void pk_events_tick(pk_game *g) /* FUN_10070b20, mode 1 */
{
    int i;
    for (i = 0; i < PK_EVENTS; i++) {
        int *e = g->world.events[i];
        if (e[1] == 1 && --e[0] < 1) {
            e[0] = 0;
            e[1] = 3; /* the original passes 2 (arrival message) on to 3 */
        }
    }
}

int pk_event_finished(pk_game *g) /* FUN_10070e6c */
{
    int i, best = 0xff;
    for (i = 0; i < PK_EVENTS; i++) {
        if (g->world.events[i][1] == 3 && (best == 0xff || g->world.events[i][4] < g->world.events[best][4])) {
            best = i;
        }
    }
    if (best != 0xff && g->world.events[best][2] == 0xb) {
        pk_help(g, 1, 0x25);
        pk_event_clear(g, best);
        best = 0xff;
    }
    return best;
}

/* mapinfo spots: kind and number of visits before the boss (0x100ac7d8) */
static int spot_kind(int area, int spot) { return (int)pk_app_i32(0x100ac7d8u + (Uint32)(area * 0x28 + spot * 8)); }
static int spot_count(int area, int spot) { return (int)pk_app_i32(0x100ac7dcu + (Uint32)(area * 0x28 + spot * 8)); }

/* FUN_1007154c */
static void invasion_reset(pk_game *g)
{
    g->invasion_timer = (pk_rng_range(&g->rng, 4) + 5) * 0x294;
}

/* FUN_10071588: enemy invasions while the kingdom runs */
void pk_invasion_tick(pk_game *g)
{
    int i;
    if (g->invasion_timer <= 0) {
        invasion_reset(g);
    }
    if (--g->invasion_timer > 0) {
        return;
    }
    invasion_reset(g);
    if (g->world.current_area <= 1) {
        return;
    }
    for (i = 0; i < PK_EVENTS; i++) {
        if (g->world.events[i][1] != 0 && g->world.events[i][2] == 0) {
            return;
        }
    }
    if (pk_event_start(g, 0, 0xff) != 0xff && g->world.h_dc != 0xff) {
        g->world.teams[g->world.h_dc & 7][12] = 1;
    }
}

/* ------------------------------------------------------------------ */
/* Reports */

/* the area search's results for the original's report screen
 * (0x10059514): new spots (move.dat names), enemy bases, materials */
static struct
{
    int valid, area, leader, nspot, nbase, nmat;
    int spot[12];
    char mat[12][48];
    int kind; /* 0 search, 1 scouted spot, 2 scouted base */
    int sc_known, sc_spot, sc_fights, sc_field, sc_level, sc_power, sc_hp;
    char sc_name[17]; /* a rival kingdom (FUN_100590c8 shows its name in place of 0x38) */
    int sc_gone;      /* port: the rival was not found */
} s_srch;

static void u16_to_utf8(const Uint16 *s, char *out, int cap)
{
    int n = 0;
    while (s && *s && n < cap - 4) {
        Uint16 c = *s++;
        if (c == 0x1b) {
            break;
        }
        if (c < 0x80) {
            out[n++] = (char)c;
        } else if (c < 0x800) {
            out[n++] = (char)(0xc0 | (c >> 6));
            out[n++] = (char)(0x80 | (c & 0x3f));
        } else {
            out[n++] = (char)(0xe0 | (c >> 12));
            out[n++] = (char)(0x80 | ((c >> 6) & 0x3f));
            out[n++] = (char)(0x80 | (c & 0x3f));
        }
    }
    out[n] = 0;
}

/* ---- the area search's report screen (0x10059514) ------------------ */
/* the magic circle (0x1005f080: image 0x4c, frame 0 over background 0x58,
 * then the beam: frame 1 with animations 0x100a2d18 / 0x100a2d50), the
 * party leader stepping out (0x1005f254: its unit sheet), and the window
 * rows (image 0x17, frames 0x40..0x42 of 0x1007ea68) */
static void srch_obj_update(pk_object *o, pk_game *g)
{
    Uint32 fr, idle;
    switch (o->state) {
    case 0: /* the circle */
        o->image = pk_game_load_image(g, 0x4c, 1);
        pk_object_set_frames(o, 0x100a2d90u);
        o->frame = 0;
        o->flags = 0x4e1;
        o->layer = 0;
        o->state = 9;
        break;
    case 1: /* the beam */
        o->image = pk_game_load_image(g, 0x4c, 1);
        pk_object_set_frames(o, 0x100a2d90u);
        o->flags = 0x4c1;
        pk_object_set_anim(o, 0x100a2d18u);
        o->layer = 1;
        o->counter = 6;
        o->state = 2;
        break;
    case 2:
        pk_object_animate(o);
        if (o->counter >= 0 && --o->counter < 1) {
            pk_object_set_anim(o, 0x100a2d50u);
            o->state = 3;
        }
        break;
    case 3:
        pk_object_animate(o);
        break;
    case 4: /* the leader */
        if (o->arg1 == 0xff) {
            pk_object_kill(o);
            break;
        }
        o->image = pk_game_load_image(g, o->arg1 * 2 + 0x102, 1);
        pk_battle_unit_look(o->arg1, &fr, &idle);
        pk_object_set_frames(o, fr);
        pk_object_set_anim(o, idle);
        o->flags |= 0x400 | PK_OBJ_SPRITE | PK_OBJ_SCREEN;
        o->layer = 1;
        o->state = 3;
        break;
    case 0x10: /* window rows */
        o->image = pk_game_load_image(g, 0x17, 1);
        o->flags = 0x4e1;
        pk_object_set_frames(o, 0x1007ea68u);
        o->frame = o->arg1 + 0x40;
        o->layer = 0;
        o->state = 9;
        break;
    default:
        break;
    }
}

/* mestext a + number + mestext b, centred (the pages' headings) */
static void srch_heading(pk_game *g, int a, int n, int b, int y)
{
    Uint16 buf[80];
    const Uint16 *t;
    char num[12];
    int k = 0, i;
    for (t = pk_data_text(PK_TBL_MESTEXT, a); t && *t && *t != 0x1b && k < 70; t++) {
        buf[k++] = *t;
    }
    SDL_snprintf(num, sizeof(num), "%d", n);
    for (i = 0; num[i] && k < 74; i++) {
        buf[k++] = (Uint16)num[i];
    }
    for (t = pk_data_text(PK_TBL_MESTEXT, b); t && *t && *t != 0x1b && k < 79; t++) {
        buf[k++] = *t;
    }
    buf[k] = 0;
    pk_drawq_text(&g->drawq, buf, 0x58, y, 0, PK_ALIGN_CENTER, 1, PK_FONT_DENSE);
}

/* FUN_10058edc (a spot) / FUN_100590c8 (a base) */
static void scout_page(pk_game *g)
{
    char num[16];
    if (s_srch.kind == 1) {
        if (s_srch.sc_spot >= 0 && s_srch.sc_spot < 5) {
            pk_drawq_text(&g->drawq, pk_data_text(PK_TBL_MOVE, s_srch.area * 5 + s_srch.sc_spot), 0x58, 0x78, 0,
                          PK_ALIGN_CENTER, 1, PK_FONT_DENSE);
        }
        if (!s_srch.sc_known) {
            pk_game_mestext(g, 0x35, 0x58, 0x8e, 0, PK_ALIGN_CENTER, 1, PK_FONT_DENSE);
            return;
        }
        pk_game_mestext(g, 0x36, 5, 0x8e, 0, PK_ALIGN_LEFT, 1, PK_FONT_DENSE);
        pk_game_mestext(g, 0x37, 5, 0x99, 0, PK_ALIGN_LEFT, 1, PK_FONT_DENSE);
        {
            Uint16 buf[64];
            const Uint16 *t;
            int k = 0, i;
            for (t = pk_data_text(PK_TBL_MESTEXT, 139); t && *t && *t != 0x1b && k < 50; t++) {
                buf[k++] = *t;
            }
            SDL_snprintf(num, sizeof(num), "%d", s_srch.sc_fights);
            for (i = 0; num[i] && k < 63; i++) {
                buf[k++] = (Uint16)num[i];
            }
            buf[k] = 0;
            pk_drawq_text(&g->drawq, buf, 5, 0xaf, 0, PK_ALIGN_LEFT, 1, PK_FONT_DENSE);
        }
        return;
    }
    if (s_srch.sc_name[0]) {
        pk_drawq_text_utf8(&g->drawq, s_srch.sc_name, 0x58, 0x78, 0, PK_ALIGN_CENTER, 1, PK_FONT_DENSE);
        if (s_srch.sc_gone) {
            pk_game_mestext(g, 0x35, 0x58, 0x8e, 0, PK_ALIGN_CENTER, 1, PK_FONT_DENSE);
            return;
        }
    } else {
        pk_game_mestext(g, 0x38, 0x58, 0x78, 0, PK_ALIGN_CENTER, 1, PK_FONT_DENSE);
    }
    pk_game_mestext(g, 0x39, 0x58, 0x8a, 0, PK_ALIGN_CENTER, 1, PK_FONT_DENSE);
    pk_game_mestext(g, 0x3a, 100, 0x9c, 0, PK_ALIGN_RIGHT, 1, PK_FONT_DENSE);
    if (s_srch.sc_known && s_srch.sc_field >= 0 && s_srch.sc_field < 11) {
        pk_game_mestext(g, (int)pk_app_i32(0x1009e1d8u + (Uint32)s_srch.sc_field * 4u), 100, 0x9c, 0, PK_ALIGN_LEFT, 1,
                        PK_FONT_DENSE);
    }
    pk_game_mestext(g, 0x106, 0x77, 0xae, 0, PK_ALIGN_RIGHT, 1, PK_FONT_DENSE);
    if (s_srch.sc_level >= 1) {
        SDL_snprintf(num, sizeof(num), "%d", s_srch.sc_power);
        pk_drawq_text_utf8(&g->drawq, num, 0x77, 0xae, 0, PK_ALIGN_LEFT, 1, PK_FONT_DENSE);
    } else {
        pk_game_mestext(g, 0x1e, 0x77, 0xae, 0, PK_ALIGN_LEFT, 1, PK_FONT_DENSE);
    }
    pk_game_mestext(g, 0x29, 0x77, 0xc0, 0, PK_ALIGN_RIGHT, 1, PK_FONT_DENSE);
    if (s_srch.sc_level >= 2) {
        SDL_snprintf(num, sizeof(num), "%d", s_srch.sc_hp);
        pk_drawq_text_utf8(&g->drawq, num, 0x77, 0xc0, 0, PK_ALIGN_LEFT, 1, PK_FONT_DENSE);
    } else {
        pk_game_mestext(g, 0x1e, 0x77, 0xc0, 0, PK_ALIGN_LEFT, 1, PK_FONT_DENSE);
    }
}

static void srch_page(pk_game *g, int page)
{
    int i, y = 0x8e;
    if (s_srch.kind != 0) {
        scout_page(g);
        return;
    }
    if (page == 0) { /* FUN_1005a400: new spots and enemy bases */
        int n = s_srch.nspot + s_srch.nbase;
        if (n == 0) {
            pk_game_mestext(g, 0x3b, 0x58, 0x78, 0, PK_ALIGN_CENTER, 1, PK_FONT_DENSE);
            return;
        }
        srch_heading(g, 140, n, 148, 0x78);
        for (i = 0; i < s_srch.nspot && y < 0xd0; i++, y += 0xb) {
            pk_drawq_text(&g->drawq, pk_data_text(PK_TBL_MOVE, s_srch.area * 5 + s_srch.spot[i]), 0x58, y, 0,
                          PK_ALIGN_CENTER, 1, PK_FONT_DENSE);
        }
        for (i = 0; i < s_srch.nbase && y < 0xd0; i++, y += 0xb) {
            pk_game_mestext(g, 0x38, 0x58, y, 0, PK_ALIGN_CENTER, 1, PK_FONT_DENSE);
        }
    } else { /* FUN_1005a7a4: the materials and the storage */
        char cnt[16];
        if (s_srch.nmat == 0) {
            pk_game_mestext(g, 0x3c, 0x58, 0x78, 0, PK_ALIGN_CENTER, 1, PK_FONT_DENSE);
        } else {
            srch_heading(g, 140, s_srch.nmat, 141, 0x78);
            for (i = 0; i < s_srch.nmat && y < 0xd0; i++, y += 0xb) {
                pk_drawq_text_utf8(&g->drawq, s_srch.mat[i], 0x58, y, 0, PK_ALIGN_CENTER, 1, PK_FONT_DENSE);
            }
        }
        SDL_snprintf(cnt, sizeof(cnt), "%d/%d", g->world.item_count, PK_ITEM_MAX);
        pk_drawq_text_utf8(&g->drawq, cnt, 0xaa, 0xcd, 0, PK_ALIGN_RIGHT, 1, PK_FONT_DENSE);
    }
}

static void srch_report_update(pk_object *o, pk_game *g)
{
    int i;
    switch (o->state) {
    case 0:
        if (g->curtain == PK_CURTAIN_NONE) {
            pk_game_set_curtain(g, PK_CURTAIN_CLOSE);
        }
        if (g->curtain != PK_CURTAIN_HELD) {
            break;
        }
        pk_object_kill_others(g, o);
        g->cam_x = g->cam_y = 0;
        pk_game_set_bg(g, 0, 0x58);
        g->bg_visible = 1;
        pk_audio_play_sfx(0x21);
        pk_object_spawn(g, srch_obj_update, 0, 0, 0, 0, 0x58, 0x44);
        pk_object_spawn(g, srch_obj_update, 0, 1, 0, 0, 0x58, 0x44);
        pk_object_spawn(g, srch_obj_update, 0, 4, 0, s_srch.leader, 0x58, 0x5f);
        pk_object_spawn(g, srch_obj_update, 0, 0x10, 0, 0, 0, 0x68);
        for (i = 0x6c; i < 0xcc; i += 4) {
            pk_object_spawn(g, srch_obj_update, 0, 0x10, 0, 1, 0, i);
        }
        pk_object_spawn(g, srch_obj_update, 0, 0x10, 0, 2, 0, 0xcc);
        pk_game_set_curtain(g, PK_CURTAIN_OPEN);
        o->counter = 0;
        o->work[2] = 0;
        o->state = 1;
        break;
    case 1: /* states 4 and 6: the pages, 5 turns them */
        srch_page(g, o->work[2]);
        if (g->curtain != PK_CURTAIN_NONE) {
            break;
        }
        if (++o->counter == 4 && o->work[2] == 0 && s_srch.kind == 0) {
            pk_help(g, 1, 2);
        }
        if (o->counter > 4 && !pk_help_active() && (g->key_code == PK_SC_5 || g->key_code == PK_SC_BACK)) {
            pk_audio_play_sfx(0);
            o->counter = 0;
            if (o->work[2] == 0 && s_srch.kind == 0) {
                o->work[2] = 1;
            } else {
                pk_game_set_curtain(g, PK_CURTAIN_CLOSE);
                o->state = 2;
            }
        }
        break;
    default:
        if (g->curtain == PK_CURTAIN_HELD) {
            pk_object_kill_others(g, o);
            g->bg_visible = 1;
            g->dlg_result = 0;
            o->counter = 0;
            s_srch.valid = 0;
            pk_object_return(g, o);
        }
        break;
    }
}

/* the report runs as a sub-routine of whichever screen handed the event */
static void report_open(pk_game *g)
{
    pk_object *root = pk_ui_root(g);
    pk_object_fn fn = srch_report_update;
    if (!root) {
        return;
    }
    if (root->update == pk_map_update) {
        pk_object_call(g, root, fn, 0, pk_map_update, 2);
    } else {
        pk_object_call(g, root, fn, 0, pk_kingdom_update, 1);
    }
    root->counter = 0;
}

/* ------------------------------------------------------------------ */
/* Area search results (FUN_10063f40 + 0x10059514 state 2). The party's
 * agility decides how much it finds; finds are materials from the
 * area's product.dat entry (weighted by rank: 70/60/50/40/30), new map
 * spots (one per two enemy bases destroyed in the area, up to the
 * area's spots) and enemy bases (formation slots 0..11, at most twelve
 * known places per area). Area words: 0 spots found, 1 rival bases,
 * 2 enemy bases, 3 bases destroyed, 4 base serial. */

static int area_spots(int area)
{
    int i, n = 0;
    for (i = 0; i < 5; i++) {
        if (spot_count(area, i) > 0 || spot_kind(area, i) != 0) {
            n = i + 1;
        }
    }
    return n;
}

static void search_resolve(pk_game *g, int ev)
{
    static const int k_rank_w[5] = { 70, 60, 50, 40, 30 }; /* 0x100ac30c */
    int *e = g->world.events[ev], team = e[3], area = e[6], *ar;
    int agi = 0, i, k, r, tier, nbase, nitem, kinds[5], ids[5], w[5], wsum = 0, found = 0;
    size_t n = 0;
    Uint8 *prod;
    SDL_zero(s_srch);
    s_srch.valid = 1;
    s_srch.area = area;
    s_srch.leader = 0xff;
    if (team >= 0 && team < PK_TEAMS) {
        int lead = g->world.teams[team][9];
        if (lead >= 0 && lead < 4 && g->world.teams[team][lead] != 0xff && g->world.teams[team][lead] < PK_UNIT_MAX) {
            s_srch.leader = g->world.units[g->world.teams[team][lead]][0];
        }
    }
    if (team >= 0 && team < PK_TEAMS) {
        g->world.teams[team][12] = 0;
    }
    pk_event_clear(g, ev);
    if (area != g->world.current_area || area < 0 || area >= PK_AREAS || team < 0 || team >= PK_TEAMS) {
        return;
    }
    ar = g->world.areas[area];
    for (i = 0; i < 4; i++) {
        int u = g->world.teams[team][i];
        if (u != 0xff && u < PK_UNIT_MAX) {
            agi += g->world.units[u][9];
        }
    }
    k = (int)pk_app_i32(0x100ac688u + (Uint32)area * 8u) / 100;
    r = pk_rng_range(&g->rng, k + 1) - k;
    tier = agi < 0x1f ? 0 : agi < 0x3d ? 1
                        : agi < 0x5b   ? 2
                        : agi < 0x79   ? 3
                                       : 4;
    nbase = SDL_max(r + tier, 0);
    nitem = SDL_max(r + tier + 1, 0);
    if (nitem + g->world.item_count > PK_ITEM_MAX) {
        nitem = PK_ITEM_MAX - g->world.item_count;
    }

    /* materials */
    prod = (Uint8 *)pk_fs_load("product.dat", &n);
    for (i = 0; i < 5; i++) {
        int rank = 1;
        kinds[i] = ids[i] = 0;
        if (prod && (size_t)(area * 0x18 + 0x10 + 10) <= n) {
            kinds[i] = prod[area * 0x18 + 0x10 + i * 2];
            ids[i] = prod[area * 0x18 + 0x10 + i * 2 + 1];
        }
        pk_item_preview(kinds[i], ids[i], i);
        rank = kinds[i] >= PK_KIND_EQUIP ? pk_r.equip[i].rec[5] : pk_r.item[i].rec[6];
        w[i] = k_rank_w[SDL_clamp(rank, 1, 5) - 1];
        wsum += w[i];
    }
    SDL_free(prod);
    for (i = 0; i < nitem && wsum > 0; i++) {
        int x = pk_rng_range(&g->rng, wsum), j = 0, acc = w[0], idx;
        while (j < 4 && x >= acc) {
            acc += w[++j];
        }
        idx = pk_item_add(g, kinds[j], ids[j]);
        if (idx >= 0) {
            char name[40];
            u16_to_utf8(kinds[j] >= PK_KIND_EQUIP ? pk_r.equip[j].name : pk_r.item[j].name, name, sizeof(name));
            if (s_srch.nmat < 12) {
                SDL_strlcpy(s_srch.mat[s_srch.nmat++], name, sizeof(s_srch.mat[0]));
            }
            found++;
        }
    }
    pk_items_sort(g);

    /* rival bases are forgotten; new spots and enemy bases */
    ar[1] = 0;
    if (ar[0] + ar[1] + ar[2] + nbase > 12) {
        nbase = SDL_max(12 - (ar[0] + ar[1] + ar[2]), 0);
    }
    if (nbase > 0) {
        int lim = ar[3] / 2, spots = area_spots(area), d = 0;
        if (ar[0] < spots && ar[0] < lim) {
            d = SDL_min(SDL_min(spots, lim) - ar[0], nbase);
            for (i = 0; i < d; i++) {
                if (s_srch.nspot < 12) {
                    s_srch.spot[s_srch.nspot++] = ar[0] + i;
                }
            }
            ar[0] += d;
            found += d;
        }
        for (i = d; i < nbase; i++) {
            int slot = -1, s2, level;
            for (s2 = 0; s2 < 0xc; s2++) {
                if (g->world.formations[s2][43] == 0xffff) {
                    slot = s2;
                    break;
                }
            }
            if (slot < 0) {
                break;
            }
            level = pk_rng_range(&g->rng, 6) + (g->world.h_43c >= 1 ? 2 : 0);
            pk_battle_load_formation(g, area, level, slot);
            g->world.formations[slot][43] = ar[4]++;
            g->world.formations[slot][45] = 0xff;
            g->world.formations[slot][46] = g->world.formations[slot][47] = 0;
            ar[2]++;
            s_srch.nbase++;
            found++;
        }
    }
    PK_DEBUG("area search: %d finds (agility %d, k %d, r %d)", found, agi, k, r);
}

/* Scout result (FUN_10064494 + 0x100583bc): the party's summed agility
 * gives a level (<41, <81, <121, <161, more). A base becomes partly
 * (level 1-2) or fully (3+) known (formation word 45 = 1 / 2); a spot
 * tells how many fights remain before its boss once level > 0. */
static void scout_resolve(pk_game *g, int ev)
{
    int *e = g->world.events[ev], team = e[3], type = e[7], idx = e[8], agi = 0, lv, i;
    SDL_zero(s_srch);
    s_srch.valid = 1;
    s_srch.kind = type == 0 ? 1 : 2;
    s_srch.area = g->world.current_area;
    s_srch.leader = 0xff;
    s_srch.sc_spot = idx;
    if (team >= 0 && team < PK_TEAMS) {
        int lead = g->world.teams[team][9];
        if (lead >= 0 && lead < 4 && g->world.teams[team][lead] != 0xff && g->world.teams[team][lead] < PK_UNIT_MAX) {
            s_srch.leader = g->world.units[g->world.teams[team][lead]][0];
        }
    }
    if (team >= 0 && team < PK_TEAMS) {
        g->world.teams[team][12] = 0;
    }
    pk_event_clear(g, ev);
    if (e[6] != g->world.current_area || team < 0 || team >= PK_TEAMS) {
        return;
    }
    for (i = 0; i < 4; i++) {
        int u = g->world.teams[team][i];
        if (u != 0xff && u < PK_UNIT_MAX) {
            agi += g->world.units[u][9];
        }
    }
    lv = agi < 0x29 ? 0 : agi < 0x51 ? 1
                      : agi < 0x79   ? 2
                      : agi < 0xa1   ? 3
                                     : 4;
    if (type == 0) {
        int area = g->world.current_area;
        if (lv == 0 || idx < 0 || idx >= 5) {
            return;
        }
        (void)area;
        s_srch.sc_known = 1;
        s_srch.sc_fights = spot_count(area, idx);
        return;
    }
    if (idx < 0 || idx >= 0xc || g->world.formations[idx][43] == 0xffff) {
        return;
    }
    {
        int *f = g->world.formations[idx], level = lv == 0 ? 0 : lv < 3 ? 1
                                                                        : 2,
            k;
        if (level > f[45] || f[45] == 0xff) {
            f[45] = level;
        }
        s_srch.sc_known = 1;
        s_srch.sc_field = f[0];
        s_srch.sc_level = f[45] == 0xff ? 0 : f[45];
        for (k = 0; k < 4; k++) {
            if (f[3 + k] == 0xff) {
                continue;
            }
            pk_unit_template(f[3 + k], 10);
            s_srch.sc_power += pk_r.unit[10].b[6]; /* template +0x198: file byte 6 */
            s_srch.sc_hp += pk_r.unit[10].b[5];    /* +0x194: byte 5 */
        }
    }
}

/* 0x10028538 state 2: the pop-up that announces a finished event, before
 * state 4 handles it. Kinds 1..4 and 8: trigger022 ("XXX" = the party,
 * named after its leader: game+0xec -> 0x1005b110); an enemy invasion:
 * trigger027; a rival's attack on the defense party (e[6] == 2):
 * trigger034 with the attacker's name. */
static int s_announced = -1;

static void event_announce(pk_game *g, int ev)
{
    int *e = g->world.events[ev], team = e[3], lead;
    switch (e[2]) {
    case 0:
        if (e[6] == 2) { /* a rival's attack: "Player XXX is attacking" */
            const Uint8 *am = pk_arena_attack_msg();
            Uint16 w[17];
            int k;
            for (k = 0; k < 16 && am && am[4 + k]; k++) {
                w[k] = am[4 + k];
            }
            w[am ? k : 0] = 0;
            pk_help_set_word(w);
            pk_help(g, 1, 0x22);
        } else {
            pk_help(g, 1, 0x1b);
        }
        break;
    case 1:
    case 2:
    case 3:
    case 4:
    case 8:
    {
        Uint16 w[25];
        w[0] = 0;
        if (team >= 0 && team < PK_TEAMS && (lead = g->world.teams[team][9]) >= 0 && lead < 4 &&
            g->world.teams[team][lead] != 0xff && g->world.teams[team][lead] < PK_UNIT_MAX) {
            pk_unit_name(g, g->world.teams[team][lead], w, 25);
        }
        pk_help_set_word(w);
        pk_help(g, 1, 0x16);
        break;
    }
    default:
        break;
    }
}

/* 0x10028538 for the kinds the port handles. True when it took over. */
bool pk_event_handle(pk_game *g, int ev)
{
    int *e = g->world.events[ev], team = e[3], level;
    if (pk_help_active()) {
        return true; /* the announcement is still open */
    }
    if (s_announced != ev) {
        s_announced = ev;
        event_announce(g, ev);
        if (pk_help_active()) {
            return true;
        }
    }
    s_announced = -1;
    switch (e[2]) {
    case 0: /* enemy invasion against the defense party; e[6] == 2: a rival's
             * attack (10 from the attacker), fought here against the units of
             * its message 9 (0x10028538 state 4) */
        g->boss_battle = 0;
        if (e[6] == 2 && pk_arena_attack_msg()) {
            const Uint8 *am = pk_arena_attack_msg();
            int *fm = g->world.formations[0xd], i;
            fm[0] = g->world.h_d4;
            fm[1] = am[3] < 8 ? am[3] : 0;
            fm[2] = 0xff;
            for (i = 0; i < 4; i++) {
                const Uint8 *u = am + 40 + i * 12;
                fm[3 + i] = u[0];
                fm[7 + i] = u[6];
                fm[11 + i] = u[1];
                fm[15 + i] = u[2];
                fm[19 + i] = u[3];
                fm[23 + i] = u[4];
                fm[27 + i] = u[5];
                fm[31 + i] = u[7];
                fm[35 + i] = fm[39 + i] = u[9] == 0xff ? 1 : u[9];
            }
            fm[43] = 0xffff;
            fm[45] = 0xff;
            fm[47] = 1;
            pk_arena_attack_done(g);
        } else {
            level = g->world.h_43c < 1 ? pk_rng_range(&g->rng, 6) : pk_rng_range(&g->rng, 8);
            pk_battle_load_formation(g, g->world.current_area, level, 0xd);
        }
        pk_event_clear(g, ev);
        if (g->world.h_dc == 0xff) {
            pk_battle_setup(g, 0xff, 0xd, 1, 1);
            pk_b.won = 0;
            pk_game_set_mode(g, PK_MODE_KINGDOM);
        } else {
            pk_battle_setup(g, g->world.h_dc, 0xd, 1, 1);
            pk_game_set_mode(g, PK_MODE_BATTLE);
        }
        return true;
    case 1:
    { /* attack on an enemy base found by a search */
        int slot = e[8];
        pk_event_clear(g, ev);
        if (slot < 0 || slot >= 0xc || g->world.formations[slot][43] == 0xffff || e[6] != g->world.current_area) {
            if (team >= 0 && team < PK_TEAMS) {
                g->world.teams[team][12] = 0;
            }
            if (slot >= 0 && slot < 0xc) {
                g->world.formations[slot][46] = 0;
            }
            return false;
        }
        g->boss_battle = 0;
        pk_battle_setup(g, team, slot, e[9], 0);
        pk_b.spot = -1;
        pk_game_set_mode(g, PK_MODE_BATTLE);
        return true;
    }
    case 3: /* scouting finished (0x100583bc) */
        scout_resolve(g, ev);
        report_open(g);
        return true;
    case 4: /* area search finished: the report (0x10059514) */
        search_resolve(g, ev);
        report_open(g);
        return true;
    case 2:
    { /* attack on a discovered spot: a battle, the area boss on the last visit */
        int area = g->world.current_area, spot = e[6], kind = spot_kind(area, spot), mode = 2, slot = 0xd;
        if (kind == 4) { /* quest spot (0x100525ac) */
            if (team >= 0 && team < PK_TEAMS) {
                g->world.teams[team][12] = 0;
            }
            pk_event_clear(g, ev);
            pk_quest_begin(g, area, spot, team);
            return true;
        }
        g->boss_battle = 0;
        level = g->world.h_43c < 1 ? pk_rng_range(&g->rng, 6) : pk_rng_range(&g->rng, 8);
        if (kind < 3) {
            if (g->world.areas[area][30 + spot] == spot_count(area, spot) - 1) {
                pk_battle_load_formation(g, area, 8, 0xc);
                g->boss_battle = 1;
                slot = 0xc;
            } else {
                pk_battle_load_formation(g, area, level, 0xd);
            }
            mode = 3;
        } else {
            pk_battle_load_formation(g, area, level, 0xd);
        }
        if (g->boss_battle) {
            mode = 3;
        }
        pk_battle_setup(g, team, slot, 0, mode);
        pk_b.spot = spot;
        pk_event_clear(g, ev);
        pk_game_set_mode(g, PK_MODE_BATTLE);
        return true;
    }
    default:
        PK_DEBUG("event kind %d not reconstructed yet", e[2]);
        if (team >= 0 && team < PK_TEAMS) {
            g->world.teams[team][12] = 0;
        }
        pk_event_clear(g, ev);
        return false;
    }
}

/* FUN_1003dafc: highest rank in the chosen team times the area's rate */
static int travel_cost(pk_game *g, int team)
{
    int i, best = 0;
    for (i = 0; i < 4; i++) {
        int u = g->world.teams[team][i];
        if (u != 0xff && g->world.units[u][14] >= best) {
            best = g->world.units[u][14];
        }
    }
    return (int)pk_app_i32(TBL_TRAVEL + (Uint32)g->world.current_area * 0x10u) * best;
}

/* ------------------------------------------------------------------ */
/* map root */

static void spawn_top_bar(pk_object *o, pk_game *g)
{
    pk_object_spawn(g, pk_kingdom_ui_update, o->dir, 0x80, 1, 2, 0x10, 3);
    pk_object_spawn(g, pk_kingdom_ui_update, o->dir, 0x80, 1, 3, 0x61, 3);
    pk_object_spawn(g, pk_kingdom_ui_update, o->dir, 0x80, 1, 4, 0xa1, 3);
    pk_object_spawn(g, pk_kingdom_ui_update, o->dir, 0x80, 0, 0x7a, 0, 0x92);
}

static void call(pk_object *o, pk_game *g, pk_object_fn fn, int ret_state)
{
    pk_object_call(g, o, fn, 0, pk_map_update, ret_state);
}

static void map_hint(pk_game *g)
{
    (void)g;
}

/* ------------------------------------------------------------------ */
/* Map > Move (0x1004017c). The world map is nine 176x208 pages (bitmaps
 * page * 3 + 0x1f); every area has a page, a position and a travel cost
 * (0x10096338, 16 bytes per area) and up to four neighbours (0x10096738,
 * 0x24 bytes per area). Only unlocked areas (areas f36) can be chosen. */

#define TBL_AREA_POS   0x10096338u
#define TBL_AREA_NEXT  0x10096738u
#define TBL_AREA_LINKS 0x100965d8u
#define IMG_UI         0x17
#define TBL_UI_FRAMES  0x1008bc68u

static int area_info(int a, int k) { return (int)pk_app_i32(TBL_AREA_POS + (Uint32)a * 16u + (Uint32)k * 4u); }

/* FUN_1004143c: next unlocked neighbour of the current area */
static int next_area(pk_game *g, int dir)
{
    int cur = g->world.current_area;
    int n = (int)pk_app_i32(TBL_AREA_NEXT + (Uint32)cur * 0x24u), i, k = g->cursor;
    for (i = 0; i < n; i++) {
        int a;
        if (dir == 0) {
            if (--k < 0) {
                k = n - 1;
            }
        } else if (++k == n) {
            k = 0;
        }
        a = (int)pk_app_i32(TBL_AREA_NEXT + (Uint32)cur * 0x24u + (Uint32)(k * 8 + 4));
        if (a >= 0 && a < PK_AREAS && g->world.areas[a][36] != 0) {
            g->cursor = k;
            return a;
        }
    }
    return cur;
}

/* the page and the vertical scroll (FUN_100413e4): pages are taller
 * than the screen and scroll with the kingdom, -0x10 .. 0x38 */
static int move_cam_y(int page, int y)
{
    int top = (page / 3) * 0xd0, d = y - 0x50 - top;
    if (d < -0xf) {
        d = -0x10;
    } else if (d > 0x37) {
        d = 0x38;
    }
    return d + top;
}

#define TBL_MOVE_FRAMES   0x100943e8u /* image 0x17 */
#define TBL_ARROW_OFF     0x10096d20u /* by direction */
#define TBL_CASTLE_FRAMES 0x10095db0u
#define TBL_AREA_THUMBS   0x100959c0u /* image 0x44 */
/* the root (+0x24 moving, +0x28 area on entry, +0x30 chosen area,
 * +0x34 0xff on entry, 1 after a page change) */
#define MV_MOVING 3
#define MV_START  4
#define MV_SEL    6
#define MV_PAGE   7

/* the children (0x1004017c states 0x10..0x61); all go when the curtain
 * is closed (0x100413c4) */
static void move_child(pk_object *o, pk_game *g)
{
    pk_object *root = pk_object_find(g, 0xc2, 0, 0);
    if (!root || root->update != pk_move_update) {
        pk_object_kill(o);
        return;
    }
    switch (o->state) {
    case 0x10: /* an area */
        o->image = pk_game_load_image(g, IMG_UI, 1);
        pk_object_set_frames(o, TBL_MOVE_FRAMES);
        o->frame = 0x30;
        o->flags = 0x481;
        pk_object_clear_work(o);
        o->counter = 0;
        o->state = 0x11;
        break;
    case 0x20: /* a road: its shadow, then the road */
    case 0x21:
    {
        int sh = o->state == 0x20;
        o->image = pk_game_load_image(g, IMG_UI, 1);
        o->flags = 0x581;
        pk_object_clear_work(o);
        o->x = area_info(o->arg0, 1);
        o->y = area_info(o->arg0, 2) + sh;
        o->x2 = area_info(o->arg1, 1);
        o->y2 = area_info(o->arg1, 2) + sh;
        o->work[0] = o->work[1] = sh ? 0x80 : 0xff;
        o->work[2] = 0;
        o->counter = 0;
        o->state = 0x22;
        break;
    }
    case 0x30:
    { /* the kingdom (four parts of its look), follows the root */
        int fr = -1;
        o->image = pk_game_load_image(g, 0x45, 1);
        pk_object_set_frames(o, TBL_CASTLE_FRAMES);
        o->flags = 0x481;
        if (o->arg0 == 0) {
            fr = g->world.h_4e8 == 5 ? -1 : g->world.h_4e8 * 2 + 3;
        } else if (o->arg0 == 1) {
            fr = g->world.h_4e4 + 0x12;
        } else if (o->arg0 == 2) {
            fr = g->world.h_4e8 == 5 ? -1 : g->world.h_4e8 * 2 + 2;
        } else {
            fr = g->world.h_4ec == 5 ? -1 : g->world.h_4ec + 0xc;
        }
        if (fr < 0) {
            o->flags &= ~1u;
        } else {
            o->frame = fr;
        }
        pk_object_clear_work(o);
        o->counter = 0;
        o->state = 0x31;
    }
        /* fallthrough */
    case 0x31:
        o->x = root->x;
        o->y = root->y;
        break;
    case 0x40: /* the chosen area's picture */
        o->image = pk_game_load_image(g, 0x44, 1);
        pk_object_set_frames(o, TBL_AREA_THUMBS);
        o->flags = 0xe1;
        o->frame = g->world.current_area;
        pk_object_clear_work(o);
        o->layer = 1;
        o->counter = 0;
        o->state = 0x41;
        break;
    case 0x41:
        o->frame = root->work[MV_SEL];
        if (root->work[MV_MOVING]) {
            o->flags &= ~1u;
        } else {
            o->flags |= 1;
        }
        break;
    case 0x50: /* the arrow to the chosen neighbour */
        o->image = pk_game_load_image(g, IMG_UI, 1);
        pk_object_set_frames(o, TBL_MOVE_FRAMES);
        o->flags = 0x480;
        o->counter = 0;
        pk_object_clear_work(o);
        o->state = 0x51;
        break;
    case 0x51:
    {
        int cur = g->world.current_area;
        int d = (int)pk_app_i32(TBL_AREA_NEXT + (Uint32)cur * 0x24u + (Uint32)g->cursor * 8u + 8u);
        o->frame = d + 0x6b;
        o->x = root->x + (int)pk_app_i32(TBL_ARROW_OFF + (Uint32)d * 8u);
        o->y = root->y + (int)pk_app_i32(TBL_ARROW_OFF + (Uint32)d * 8u + 4u);
        if (!root->work[MV_MOVING] && root->work[MV_SEL] != cur) {
            o->flags |= 1;
        } else {
            o->flags &= ~1u;
        }
        break;
    }
    case 0x60: /* the top bar's ground */
        o->image = pk_game_load_image(g, IMG_UI, 1);
        pk_object_set_frames(o, TBL_MOVE_FRAMES);
        o->flags = 0x4e1;
        o->frame = 0x92;
        pk_object_clear_work(o);
        o->layer = 0;
        o->counter = 0;
        o->state = 0x61;
        break;
    default:
        break;
    }
    if (g->curtain == PK_CURTAIN_HELD) {
        pk_object_kill(o);
    }
}

/* 0x1004017c state 1: the page around the kingdom */
static void move_build(pk_object *o, pk_game *g)
{
    int a, i, page = g->map_page;
    g->cam_x = (page % 3) * 0xb0;
    g->cam_y = (page / 3) * 0xd0;
    for (a = 0; a < 0x2a; a++) {
        if (area_info(a, 0) == page && g->world.areas[a][36]) {
            pk_object_spawn(g, pk_move_update, 0, 0x10, 0x10, a, area_info(a, 1), area_info(a, 2));
        }
    }
    for (i = 0; i < 0x2c; i++) {
        int p = (int)pk_app_i32(TBL_AREA_LINKS + (Uint32)i * 8u), q = (int)pk_app_i32(TBL_AREA_LINKS + (Uint32)i * 8u + 4u);
        if ((area_info(p, 0) == page || area_info(q, 0) == page) && g->world.areas[p][36] && g->world.areas[q][36]) {
            pk_object_spawn(g, pk_move_update, 0, 0x20, p, q, 0, 0);
            pk_object_spawn(g, pk_move_update, 0, 0x21, p, q, 0, 0);
        }
    }
    for (i = 0; i < 4; i++) {
        pk_object_spawn(g, pk_move_update, 0, 0x30, i, g->world.current_area, o->x, o->y);
    }
    pk_object_spawn(g, pk_move_update, 0, 0x50, 0, 0, o->x, o->y);
    pk_object_spawn(g, pk_move_update, 0, 0x60, 0, 0, 0, 0);
    spawn_top_bar(o, g);
    pk_object_spawn(g, pk_move_update, 0, 0x40, 0, 0, 0x1a, 0xb1);
}

/* the page's three pictures (FUN_10070a10), shown in turn (FUN_1006f1a8) */
static void move_set_page(pk_game *g, int page)
{
    g->map_page = page;
    pk_game_set_bg(g, 0, page * 3 + 0x1f);
    pk_game_set_bg(g, 1, page * 3 + 0x20);
    pk_game_set_bg(g, 2, page * 3 + 0x21);
    g->bg_visible = 2;
}

/* leaving an area forgets what was found there (state 3, back key) */
static void area_forget(pk_game *g, int a)
{
    int i;
    g->world.areas[a][1] = g->world.areas[a][2] = g->world.areas[a][4] = 0;
    for (i = 0; i < 5; i++) {
        g->world.areas[a][30 + i] = 0;
    }
}

static void area_changed(pk_game *g, int old)
{
    int i;
    area_forget(g, old);
    area_forget(g, g->world.current_area);
    for (i = 0; i < PK_FORMATIONS; i++) {
        g->world.formations[i][43] = 0xffff;
        g->world.formations[i][47] = 0;
        g->world.formations[i][45] = 0xff;
        g->world.formations[i][46] = 0;
    }
    g->world.h_450 = 0;
    for (i = 0; i < PK_EVENTS; i++) {
        if (g->world.events[i][1] != 0 && g->world.events[i][2] < 5) {
            pk_event_clear(g, i);
        }
    }
    for (i = 0; i < PK_TEAMS; i++) {
        g->world.teams[i][12] = 0;
    }
}

/* state 3: the current area (gold, right) and the chosen one with its
 * cost: mestext 0x22, " %d ", mestext 303. Area names (FUN_10063e3c)
 * are every fifth 48-byte record of the language's area.dat. */
static void move_text(pk_object *o, pk_game *g)
{
    const Uint16 *name;
    Uint16 line[96];
    char num[16];
    int target = o->work[MV_SEL], n = 0, i;
    name = pk_data_text(PK_TBL_AREA, g->world.current_area * 5);
    pk_drawq_text(&g->drawq, name, 0xaf, 0xa7, 0, PK_ALIGN_RIGHT, 1, PK_FONT_LEGEND);
    pk_drawq_text(&g->drawq, name, 0xae, 0xa6, COL_GOLD_MAP, PK_ALIGN_RIGHT, 1, PK_FONT_LEGEND);
    if (target == g->world.current_area) {
        return;
    }
    name = pk_data_text(PK_TBL_AREA, target * 5);
    pk_drawq_text(&g->drawq, name, 0x33, 0xb7, 0, PK_ALIGN_LEFT, 1, PK_FONT_LEGEND);
    pk_drawq_text(&g->drawq, name, 0x32, 0xb6, 0xFFFFFFu, PK_ALIGN_LEFT, 1, PK_FONT_LEGEND);
    for (name = pk_data_text(PK_TBL_MESTEXT, 0x22); *name && n < 40; name++) {
        line[n++] = *name;
    }
    SDL_snprintf(num, sizeof(num), " %d ", area_info(target, 3));
    for (i = 0; num[i] && n < 60; i++) {
        line[n++] = (Uint16)num[i];
    }
    for (name = pk_data_text(PK_TBL_MESTEXT, 303); *name && n < 95; name++) {
        line[n++] = *name;
    }
    line[n] = 0;
    pk_drawq_text(&g->drawq, line, 0x33, 0xc7, 0, PK_ALIGN_LEFT, 1, PK_FONT_LEGEND);
    pk_drawq_text(&g->drawq, line, 0x32, 0xc6, 0xFFFFFFu, PK_ALIGN_LEFT, 1, PK_FONT_LEGEND);
}

/* Touch (port): a tap near a neighbouring area on this page chooses it,
 * a tap on the arrow chooses the next neighbour, a tap on the chosen
 * area or on the panel below travels there; a swipe acts as the arrow
 * keys and Back (Android back) returns to the map. */
static void move_touch(pk_object *o, pk_game *g)
{
    int cur = g->world.current_area, n = (int)pk_app_i32(TBL_AREA_NEXT + (Uint32)cur * 0x24u), k;
    for (k = 0; k < n; k++) {
        int a = (int)pk_app_i32(TBL_AREA_NEXT + (Uint32)cur * 0x24u + (Uint32)(k * 8 + 4));
        int x, y;
        if (a < 0 || a >= PK_AREAS || !g->world.areas[a][36] || area_info(a, 0) != g->map_page) {
            continue;
        }
        x = area_info(a, 1) - g->cam_x;
        y = area_info(a, 2) - g->cam_y;
        if (!pk_tap(g, x - 0xc, y - 0xc, 0x18, 0x18)) {
            continue;
        }
        if (a == o->work[MV_SEL]) {
            g->key_code = PK_SC_5;
        } else {
            pk_audio_play_sfx(2);
            g->cursor = k;
            o->work[MV_SEL] = a;
        }
    }
    if (o->work[MV_SEL] != cur) { /* the arrow: the next neighbour (one on another page too) */
        int d = (int)pk_app_i32(TBL_AREA_NEXT + (Uint32)cur * 0x24u + (Uint32)g->cursor * 8u + 8u);
        int x = o->x + (int)pk_app_i32(TBL_ARROW_OFF + (Uint32)d * 8u) - g->cam_x;
        int y = o->y + (int)pk_app_i32(TBL_ARROW_OFF + (Uint32)d * 8u + 4u) - g->cam_y;
        if (pk_tap(g, x - 0xa, y - 0xa, 0x14, 0x14)) {
            g->key_code = PK_SC_RIGHT;
        }
    }
    if (pk_tap(g, 0, 0xa2, PK_SCREEN_W, PK_SCREEN_H - 0xa2)) {
        g->key_code = PK_SC_5;
    }
}

void pk_move_update(pk_object *o, pk_game *g)
{
    int a;
    if (o->type != 0xc2) {
        move_child(o, g);
        return;
    }
    switch (o->state) {
    case 0: /* the map closed the curtain; it sets the pictures (0x1003db80 state 0xf) */
        if (g->curtain != PK_CURTAIN_HELD) {
            break;
        }
        pk_object_kill_others(g, o);
        move_set_page(g, g->world.h_428);
        o->work[MV_SEL] = g->world.current_area;
        o->work[MV_PAGE] = 0xff;
        o->work[MV_MOVING] = 0;
        o->x = area_info(g->world.current_area, 1);
        o->y = area_info(g->world.current_area, 2);
        o->counter = 0;
        o->state = 1;
        /* fallthrough */
    case 1:
        if (g->curtain != PK_CURTAIN_HELD) {
            break;
        }
        move_build(o, g);
        pk_game_set_curtain(g, PK_CURTAIN_OPEN);
        if (o->work[MV_PAGE] == 0xff) {
            o->work[MV_START] = g->world.current_area;
            g->cam_y = move_cam_y(g->map_page, area_info(g->world.current_area, 2));
            g->cursor = 0;
            o->work[MV_SEL] = next_area(g, 1);
            o->state = 2;
        } else {
            g->cam_y = move_cam_y(g->map_page, o->y);
            o->state = 6;
        }
        break;
    case 2:
        if (g->curtain == PK_CURTAIN_NONE) {
            o->state = 3;
        }
        break;
    case 3:
        o->work[MV_MOVING] = 0;
        move_touch(o, g);
        if (g->key_code == PK_SC_LEFT || g->key_code == PK_SC_UP || g->key_code == PK_SC_RIGHT ||
            g->key_code == PK_SC_DOWN) {
            int dir = (g->key_code == PK_SC_LEFT || g->key_code == PK_SC_UP) ? 0 : 1;
            pk_audio_play_sfx(2);
            a = next_area(g, dir);
            if (a != o->work[MV_SEL]) {
                o->work[MV_SEL] = a;
            } else if (a == g->world.current_area) {
                pk_help(g, 1, a == 0 ? 1 : 10);
            }
        } else if (g->key_code == PK_SC_5) {
            int cost = area_info(o->work[MV_SEL], 3);
            if (o->work[MV_SEL] == g->world.current_area || g->loot <= cost) {
                pk_audio_play_sfx(1);
            } else {
                pk_audio_play_sfx(0);
                pk_loot_sub(g, cost);
                o->counter = 0;
                o->work[MV_MOVING] = 1;
                o->state = 6; /* the page is the kingdom's: no curtain */
            }
        } else if (g->key_code == PK_SC_BACK) {
            pk_audio_play_sfx(1);
            if (o->work[MV_START] != g->world.current_area) {
                area_changed(g, o->work[MV_START]);
            }
            g->cam_x = g->cam_y = 0;
            g->bg_visible = 1;
            pk_game_set_bg(g, 1, PK_IMAGE_NONE);
            pk_game_set_bg(g, 2, PK_IMAGE_NONE);
            g->dlg_result = 1;
            pk_object_kill_others(g, o);
            pk_object_return(g, o);
            return;
        }
        move_text(o, g);
        break;
    case 6:
        if (g->curtain == PK_CURTAIN_NONE) {
            o->state = 7;
        }
        break;
    case 7:
    { /* on the way: 0x16 steps, a page change on the way closes the curtain */
        int page = g->map_page, px = (page % 3) * 0xb0, py = (page / 3) * 0xd0, sel = o->work[MV_SEL];
        int t = 0x16 - o->counter;
        if (t < 1) {
            t = 1;
        }
        o->x += (area_info(sel, 1) - o->x) / t;
        o->y += (area_info(sel, 2) - o->y) / t;
        g->cam_y = move_cam_y(page, o->y);
        if ((px < o->x && o->x < px + 0xb0 && py < o->y && o->y < py + 0xd0) || area_info(sel, 0) == page) {
            if (++o->counter > 0x15) {
                o->counter = 0;
                o->state = 9;
            }
        } else {
            pk_game_set_curtain(g, PK_CURTAIN_CLOSE);
            o->state = 8;
        }
        pk_game_mestext(g, 0x1d, 0x33, 0xb9, 0, PK_ALIGN_LEFT, 1, PK_FONT_LEGEND);
        pk_game_mestext(g, 0x1d, 0x32, 0xb8, 0xFFFFFFu, PK_ALIGN_LEFT, 1, PK_FONT_LEGEND);
        break;
    }
    case 8:
        if (g->curtain != PK_CURTAIN_HELD) {
            break;
        }
        a = area_info(o->work[MV_SEL], 0);
        g->world.h_428 = a;
        move_set_page(g, a);
        g->cursor = 0;
        o->work[MV_PAGE] = 1;
        o->state = 1;
        break;
    case 9: /* arrived */
        g->world.current_area = o->work[MV_SEL];
        g->world.h_428 = g->map_page;
        g->cursor = 0;
        o->x = area_info(g->world.current_area, 1);
        o->y = area_info(g->world.current_area, 2);
        o->state = 3;
        pk_arena_send_moved(g); /* online (FUN_10065f28, FUN_100723e0) */
        g_rival_count = 0;
        o->work[MV_SEL] = next_area(g, 1);
        o->work[MV_PAGE] = 0xff;
        break;
    default:
        break;
    }
}

/* ------------------------------------------------------------------ */
/* Rival kingdoms (N-Gage Arena). Scouting sends 7 {from @4, to @20};
 * the defender's game answers 135 ([2] battlefield, [3] order, units
 * @36: type, HP, cost). An attack sends 9 {[3] party, from @4, to @20,
 * 4 x 12-byte units @40}; the defender answers 137 ([2] battlefield or
 * 0xff/253/254/255 = no battle, [3] order, [4] trap, its units @40)
 * and the battle is fought here against those units; 10 tells the
 * other side it starts. */

static int s_rival_team = 0xff;
static char s_rival_name[17];

const char *pk_map_rival_name(void) { return s_rival_name; }
static int s_rival_scout;

static void rival_put_units(pk_game *g, Uint8 *at, int team)
{
    int i;
    for (i = 0; i < 4; i++) {
        Uint8 *e = at + i * 12;
        int u = g->world.teams[team][i];
        SDL_memset(e, 0xff, 6);
        e[6] = 0;
        e[7] = 5;
        e[8] = 0;
        e[9] = 0xff;
        e[10] = e[11] = 0;
        if (u == 0xff || u >= PK_UNIT_MAX) {
            continue;
        }
        e[0] = (Uint8)g->world.units[u][0];
        if (g->world.units[u][2] != 0xff && g->world.units[u][2] < PK_EQUIP_MAX) {
            int *w = g->world.equip[g->world.units[u][2]];
            e[1] = (Uint8)w[0];
            e[2] = (Uint8)w[1];
            e[3] = (Uint8)w[3];
            e[4] = (Uint8)w[4];
            e[5] = (Uint8)w[2];
        }
        e[6] = (Uint8)g->world.units[u][1];
        e[7] = (Uint8)g->world.teams[team][4 + i];
        e[9] = (Uint8)g->world.units[u][7];
    }
}

static void rival_mission(pk_game *g, int team, int rival, int scout)
{
    Uint8 m[88];
    SDL_zeroa(m);
    if (rival < 0 || rival >= g_rival_count) {
        return;
    }
    s_rival_team = team;
    s_rival_scout = scout;
    SDL_strlcpy(s_rival_name, g_rivals[rival].name, sizeof(s_rival_name));
    m[0] = scout ? 7 : 9;
    m[1] = (Uint8)(SDL_GetTicks() & 0xff);
    m[3] = (Uint8)team;
    SDL_strlcpy((char *)m + 4, pk_online_user(), 16);
    SDL_strlcpy((char *)m + 20, s_rival_name, 16);
    if (!scout) {
        rival_put_units(g, m + 40, team);
    }
    pk_online_send(m, scout ? 36 : 88);
    g->world.teams[team][12] = 1;
    PK_INFO("arena: %s %s with party %d", scout ? "scouting" : "attacking", s_rival_name, team + 1);
}

/* a rival kingdom's scouting (135) or a failed attack (137) on the
 * original's scout report page */
static void rival_report(pk_game *g, int team, const Uint8 *units, int field, bool gone)
{
    int i;
    SDL_zero(s_srch);
    s_srch.valid = 1;
    s_srch.kind = 2;
    s_srch.area = g->world.current_area;
    s_srch.leader = 0xff;
    if (team >= 0 && team < PK_TEAMS) {
        int lead = g->world.teams[team][9];
        if (lead >= 0 && lead < 4 && g->world.teams[team][lead] != 0xff && g->world.teams[team][lead] < PK_UNIT_MAX) {
            s_srch.leader = g->world.units[g->world.teams[team][lead]][0];
        }
    }
    SDL_strlcpy(s_srch.sc_name, s_rival_name, sizeof(s_srch.sc_name));
    s_srch.sc_gone = gone;
    s_srch.sc_known = !gone;
    s_srch.sc_field = field;
    s_srch.sc_level = gone ? 0 : 2;
    for (i = 0; units && i < 4; i++) {
        const Uint8 *e = units + i * 4;
        if (e[0] == 0xff) {
            continue;
        }
        pk_unit_template(e[0], 10);
        s_srch.sc_power += pk_r.unit[10].b[6];
        s_srch.sc_hp += pk_r.unit[10].b[5];
    }
}

void pk_arena_game_message(pk_game *g, const Uint8 *m, int n)
{
    if (s_rival_team == 0xff) {
        return;
    }
    if (m[0] == 135 && n >= 52) {
        int i, team = s_rival_team;
        g->world.teams[s_rival_team][12] = 0;
        s_rival_team = 0xff;
        if (m[2] > 252) {
            rival_report(g, team, NULL, 0, true);
        } else {
            rival_report(g, team, m + 36, m[2], false);
            for (i = 0; i < g_rival_count; i++) {
                if (!SDL_strcmp(g_rivals[i].name, s_rival_name)) {
                    g_rivals[i].scouted = 1;
                }
            }
        }
        report_open(g);
    } else if (m[0] == 137 && n >= 88) {
        int team = s_rival_team, i, *fm = g->world.formations[0xd];
        s_rival_team = 0xff;
        if (m[2] > 252) {
            g->world.teams[team][12] = 0;
            rival_report(g, team, NULL, 0, true);
            report_open(g);
            return;
        }
        SDL_memset(fm, 0, sizeof(g->world.formations[0]));
        fm[0] = m[2];
        fm[1] = m[3];
        fm[2] = m[4];
        for (i = 0; i < 4; i++) {
            const Uint8 *e = m + 40 + i * 12;
            fm[3 + i] = e[0];
            fm[7 + i] = e[6];
            fm[11 + i] = e[1];
            fm[15 + i] = e[2];
            fm[19 + i] = e[3];
            fm[23 + i] = e[4];
            fm[27 + i] = e[5];
            fm[31 + i] = e[7];
            fm[35 + i] = fm[39 + i] = e[9] == 0xff ? 1 : e[9];
        }
        fm[43] = 0xffff;
        fm[45] = 0xff;
        fm[47] = 1; /* rival */
        {
            Uint8 go[36];
            SDL_zeroa(go);
            go[0] = 10;
            go[1] = m[1];
            SDL_strlcpy((char *)go + 4, pk_online_user(), 16);
            SDL_strlcpy((char *)go + 20, s_rival_name, 16);
            pk_online_send(go, 36);
        }
        g->world.teams[team][12] = 0;
        g->boss_battle = 0;
        pk_battle_setup(g, team, 0xd, 0, 0);
        pk_b.spot = -1;
        pk_game_set_mode(g, PK_MODE_BATTLE);
    }
}

/* ------------------------------------------------------------------ */
/* Map > Attack (0x10043060): the targets an area search has found in
 * the current area: discovered spots (type 0) and enemy bases (type 1).
 * The choice goes to g+0x128 / g+0x12c in the original; here to
 * s_target_type / s_target_idx. */

static int s_target_type, s_target_idx, s_scout;

static int attack_targets(pk_game *g, int *type, int *idx, int cap)
{
    /* original order (0x10043060): spots, rival kingdoms, enemy bases */
    int n = 0, i, area = g->world.current_area;
    for (i = 0; i < g->world.areas[area][0] && i < 5 && n < cap; i++) {
        type[n] = 0;
        idx[n++] = i;
    }
    if (g->arena_online && g_rival_area == area) { /* rival kingdoms (134) */
        for (i = 0; i < g_rival_count && n < cap; i++) {
            type[n] = 2;
            idx[n++] = i;
        }
    }
    for (i = 0; i < 0xc && n < cap; i++) {
        if (g->world.formations[i][43] != 0xffff) {
            type[n] = 1;
            idx[n++] = i;
        }
    }
    return n;
}
static bool target_busy(pk_game *g, int type, int idx)
{
    int i;
    if (type == 2) {
        return false;
    }
    if (type == 1) {
        return g->world.formations[idx][46] != 0;
    }
    for (i = 0; i < PK_EVENTS; i++) {
        if (g->world.events[i][1] == 1 && g->world.events[i][2] == 2 && g->world.events[i][6] == idx) {
            return true;
        }
    }
    return false;
}

/* a cell of the area's 4 x 3 grid, shared by the map (0x1003db80 state
 * 0xc0) and the target screen (0x10043060): arg0 the layer (0x10 base,
 * 0x11..0x13 the rest of a rival's castle), work[0] the kind (0 spot,
 * 1 enemy base, 2 rival kingdom), work[1] its index. Rival castles are
 * image 0x45 (frames 0x10095db0) built from their look; spots and enemy
 * bases image 0x1b (frames 0x10095678): a spot by its kind, a base by the
 * area's travel cost / 100 + 0x12. */
static void cell_setup(pk_game *g, pk_object *o)
{
    int t = o->work[0], i = o->work[1], fr = -1, img = 0x1b;
    Uint32 frames = 0x10095678u;
    if (t == 0) {
        fr = o->arg0 == 0x10 ? spot_kind(g->world.current_area, i) + 0x1b : -1;
    } else if (t == 1) {
        fr = o->arg0 == 0x10 ? (int)pk_app_i32(0x10096344u + (Uint32)g->world.current_area * 0x10u) / 100 + 0x12 : -1;
    } else if (t == 2 && i < g_rival_count) {
        const Uint8 *lk = g_rivals[i].look; /* size, transporter, mark, colour */
        img = 0x45;
        frames = 0x10095db0u;
        if (o->arg0 == 0x10) {
            fr = lk[1] == 5 ? -1 : lk[1] * 2 + 3;
        } else if (o->arg0 == 0x11) {
            fr = lk[0] + lk[3] * 4 + 0x12;
        } else if (o->arg0 == 0x12) {
            fr = lk[1] == 5 ? -1 : lk[1] * 2 + 2;
        } else {
            fr = lk[2] == 5 ? -1 : lk[2] + 0xc;
        }
    }
    pk_ui_setup_sprite(g, o, img, frames, fr >= 0 ? 0x4c1 : 0x4c0);
    if (fr >= 0) {
        o->frame = fr;
    }
    o->layer = 1;
}

/* the cells on the map itself, until the screen closes */
static void map_cell_update(pk_object *o, pk_game *g)
{
    if (o->state == 0x10) {
        cell_setup(g, o);
        o->state = 0x11;
    } else if (g->curtain == PK_CURTAIN_HELD) {
        pk_object_kill(o);
    }
}

static void spawn_cells(pk_game *g, pk_object_fn fn, int state)
{
    int type[12], idx[12], n = attack_targets(g, type, idx, 12), i, l;
    for (i = 0; i < n; i++) {
        for (l = 0; l < (type[i] == 2 ? 4 : 1); l++) {
            pk_object *c = pk_object_spawn(g, fn, 0, state, 0x10 + l, i, 0x1c + 0x28 * (i & 3), 0x2d + 0x28 * (i >> 2));
            if (c) {
                c->work[0] = type[i];
                c->work[1] = idx[i];
            }
        }
    }
}

/* the target screen's parts (0x10043060): the panel (state 0x80, frame
 * 0x93 of 0x100943e8 at 0, 0x9a), the cells of a 4 x 3 grid at
 * (0x1c + 0x28 * col, 0x2d + 0x28 * row) (state 0x10: a spot is frame
 * kind + 0x1b of image 0x1b, frames 0x10095678; a rival kingdom its castle
 * in four layers and an enemy base a castle of image 0x45, frames
 * 0x10095db0, frame 0x10096344[area * 4] / 100 + 0x12) and the cursor
 * (state 8) at the cell - (0xd, 0xa). */
static void atk_obj_update(pk_object *o, pk_game *g)
{
    pk_object *root = pk_ui_root(g);
    if (!root || root->update != pk_attack_list_update) {
        pk_object_kill(o);
        return;
    }
    switch (o->state) {
    case 0x80:
        pk_ui_setup_sprite(g, o, 0x17, 0x100943e8u, 0x4c1);
        o->frame = 0x93;
        o->layer = 0;
        o->state = 0x81;
        break;
    case 8:
        pk_ui_setup_sprite(g, o, 0x17, 0x100943e8u, 0x4c1);
        pk_ui_cursor_anim(g, o);
        o->layer = 1;
        o->state = 9;
        /* fall through */
    case 9:
        pk_object_animate(o);
        o->x = (g->cursor & 3) * 0x28 + 0xf;
        o->y = (g->cursor >> 2) * 0x28 + 0x23;
        break;
    case 0x10:
        cell_setup(g, o);
        break;
    default:
        break;
    }
}

static void atk_mes(pk_game *g, int id, int x, int y, pk_color c, pk_align a)
{
    pk_game_mestext(g, id, x + 1, y + 1, 0, a, 1, PK_FONT_LEGEND);
    pk_game_mestext(g, id, x, y, c, a, 1, PK_FONT_LEGEND);
}

static void atk_text(pk_game *g, const char *t, int x, int y, pk_align a)
{
    pk_drawq_text_utf8(&g->drawq, t, x + 1, y + 1, 0, a, 1, PK_FONT_LEGEND);
    pk_drawq_text_utf8(&g->drawq, t, x, y, 0xFFFFFFu, a, 1, PK_FONT_LEGEND);
}

static void atk_text16(pk_game *g, const Uint16 *t, int x, int y, pk_align a)
{
    pk_drawq_text(&g->drawq, t, x + 1, y + 1, 0, a, 1, PK_FONT_LEGEND);
    pk_drawq_text(&g->drawq, t, x, y, 0xFFFFFFu, a, 1, PK_FONT_LEGEND);
}

/* FUN_10044b90: what the panel says about the target under the cursor */
static void atk_info(pk_game *g, int type, int idx)
{
    static const int k_level[11] = { 100, 101, 108, 103, 99, 102, 109, 104, 107, 105, 106 };
    char num[16];
    int area = g->world.current_area;
    if (type == 0) {
        int kind = spot_kind(area, idx), ev;
        for (ev = 0; ev < PK_EVENTS; ev++) {
            if (g->world.events[ev][1] != 0 && g->world.events[ev][2] == 2 && g->world.events[ev][6] == idx) {
                break;
            }
        }
        if (ev == PK_EVENTS) {
            atk_text16(g, pk_data_text(PK_TBL_MOVE, area * 5 + idx), 0x58, 0xaa, PK_ALIGN_CENTER);
            if (kind != 4) {
                int left = spot_count(area, idx) - g->world.areas[area][30 + idx], k = 0, j;
                const Uint16 *lab = pk_data_text(PK_TBL_MESTEXT, 139); /* game+0xeb3c */
                Uint16 line[64];
                while (lab && lab[k] && lab[k] != 0x1b && k < 48) {
                    line[k] = lab[k];
                    k++;
                }
                SDL_snprintf(num, sizeof(num), "%d", left < 0 ? 0 : left);
                for (j = 0; num[j] && k < 62; j++) {
                    line[k++] = (Uint16)num[j];
                }
                line[k] = 0;
                atk_text16(g, line, 0x20, 0xb8, PK_ALIGN_LEFT);
            } else {
                atk_mes(g, 0x148, 0x58, 0xc6, 0xFFFFFFu, PK_ALIGN_CENTER);
            }
        } else {
            atk_mes(g, kind == 4 ? 0x148 : 0x11e, 0x58, 0xb4, 0xFFFFFFu, PK_ALIGN_CENTER);
        }
        return;
    }
    if (type == 1 || type == 2) {
        int ev;
        for (ev = 0; ev < PK_EVENTS; ev++) {
            int *e = g->world.events[ev];
            if (e[1] == 0 || (e[2] != 1 && e[2] != 3)) {
                continue;
            }
            if (e[7] == type && e[8] == idx) {
                break;
            }
        }
        if (ev != PK_EVENTS) {
            atk_mes(g, g->world.events[ev][2] == 1 ? 0x11e : 0x11f, 0x58, 0xb4, 0xFFFFFFu, PK_ALIGN_CENTER);
            return;
        }
    }
    if (type == 2) { /* a rival kingdom: its name */
        atk_text(g, g_rivals[idx].name, 0x58, g_rivals[idx].scouted ? 0xa5 : 0xc6, PK_ALIGN_CENTER);
        if (!g_rivals[idx].scouted) {
            atk_mes(g, 0x35, 0x58, 0xaa, 0xFFFFFFu, PK_ALIGN_CENTER);
        }
        return;
    }
    {
        int *f = g->world.formations[idx], y = 0xa7, step = 0x10, k, hp = 0, atk = 0;
        if (f[45] == 0xff || f[45] == 0) { /* not scouted */
            atk_mes(g, 0x35, 0x58, 0xaa, 0xFFFFFFu, PK_ALIGN_CENTER);
            if (s_scout) {
                atk_mes(g, 0x153, 0x58, 0xc6, 0xFFDC00u, PK_ALIGN_CENTER);
            } else {
                atk_mes(g, 0x38, 0x58, 0xc6, 0xFFFFFFu, PK_ALIGN_CENTER);
            }
            return;
        }
        if (s_scout) {
            atk_mes(g, 0x153, 0x58, 0xa5, 0xFFDC00u, PK_ALIGN_CENTER);
            y = 0xb2;
            step = 0xd;
        }
        atk_mes(g, 0x3a, 100, y, 0xFFFFFFu, PK_ALIGN_RIGHT);
        if (f[0] >= 0 && f[0] < 11) {
            atk_mes(g, k_level[f[0]], 100, y, 0xFFFFFFu, PK_ALIGN_LEFT);
        }
        for (k = 0; k < 4; k++) {
            if (f[3 + k] == 0xff) {
                continue;
            }
            pk_unit_template(f[3 + k], 9);
            atk += pk_r.unit[9].b[5]; /* template +0x194 (FUN_1007a614: byte 5) */
            hp += pk_r.unit[9].b[6];  /* +0x198: byte 6 */
        }
        y += step;
        atk_mes(g, 0x106, 0x7d, y, 0xFFFFFFu, PK_ALIGN_RIGHT);
        if (f[45] < 1) {
            atk_mes(g, 0x1e, 0x7d, y, 0xFFFFFFu, PK_ALIGN_LEFT);
        } else {
            SDL_snprintf(num, sizeof(num), "%d", hp);
            atk_text(g, num, 0x7d, y, PK_ALIGN_LEFT);
        }
        y += step;
        atk_mes(g, 0x29, 0x7d, y, 0xFFFFFFu, PK_ALIGN_RIGHT);
        if (f[45] > 1) {
            SDL_snprintf(num, sizeof(num), "%d", atk);
            atk_text(g, num, 0x7d, y, PK_ALIGN_LEFT);
        } else {
            atk_mes(g, 0x1e, 0x7d, y, 0xFFFFFFu, PK_ALIGN_LEFT);
        }
    }
}

void pk_attack_list_update(pk_object *o, pk_game *g)
{
    int type[12], idx[12], n, i;
    n = attack_targets(g, type, idx, 12);
    switch (o->state) {
    case 0:
        if (g->curtain == PK_CURTAIN_NONE) {
            pk_game_set_curtain(g, PK_CURTAIN_CLOSE);
        }
        if (g->curtain == PK_CURTAIN_HELD) {
            pk_object_kill_others(g, o);
            g->bg_visible = 1; /* over the map */
            pk_object_spawn(g, atk_obj_update, 0, 0x80, 0, 0, 0, 0x9a);
            spawn_cells(g, atk_obj_update, 0x10);
            pk_object_spawn(g, atk_obj_update, 0, 8, 0, 0, 0xe, 0x22);
            pk_game_set_curtain(g, PK_CURTAIN_OPEN);
            g->cursor = 0;
            o->counter = 0;
            o->state = 1;
        }
        break;
    case 1:
        if (g->cursor < n) {
            atk_info(g, type[g->cursor], idx[g->cursor]);
        }
        if (g->curtain != PK_CURTAIN_NONE || ++o->counter < 6) {
            break;
        }
        for (i = 0; i < 12; i++) { /* touch: a cell (an empty one cancels) */
            if (pk_tap(g, 0x1c + 0x28 * (i & 3) - 0x14, 0x2d + 0x28 * (i >> 2) - 0x14, 0x28, 0x28)) {
                g->cursor = i;
                g->key_code = PK_SC_5;
            }
        }
        if (g->key_code == PK_SC_LEFT) {
            pk_audio_play_sfx(2);
            g->cursor = (g->cursor & 3) == 0 ? g->cursor + 3 : g->cursor - 1;
        } else if (g->key_code == PK_SC_RIGHT) {
            pk_audio_play_sfx(2);
            g->cursor = (g->cursor & 3) == 3 ? g->cursor - 3 : g->cursor + 1;
        } else if (g->key_code == PK_SC_UP) {
            pk_audio_play_sfx(2);
            g->cursor = g->cursor < 4 ? g->cursor + 8 : g->cursor - 4;
        } else if (g->key_code == PK_SC_DOWN) {
            pk_audio_play_sfx(2);
            g->cursor = g->cursor < 8 ? g->cursor + 4 : g->cursor - 8;
        } else if (g->key_code == PK_SC_5 && g->cursor < n) {
            if (!s_scout && target_busy(g, type[g->cursor], idx[g->cursor])) {
                pk_audio_play_sfx(4);
            } else {
                pk_audio_play_sfx(0);
                s_target_type = type[g->cursor];
                s_target_idx = idx[g->cursor];
                g->dlg_result = 0;
                pk_game_set_curtain(g, PK_CURTAIN_CLOSE);
                o->state = 2;
            }
        } else if (g->key_code == PK_SC_BACK || g->key_code == PK_SC_5) { /* an empty cell cancels */
            pk_audio_play_sfx(1);
            g->dlg_result = 1;
            pk_game_set_curtain(g, PK_CURTAIN_CLOSE);
            o->state = 2;
        }
        break;
    default:
        if (g->curtain == PK_CURTAIN_HELD) {
            pk_object_kill_others(g, o);
            g->bg_visible = 1;
            o->counter = 0;
            pk_object_return(g, o);
        }
        break;
    }
}

static void spawn_buttons(pk_object *o, pk_game *g)
{
    int i;
    for (i = 0; i < 4; i++) {
        pk_object_spawn(g, pk_kingdom_ui_update, o->dir, 0x90, i, 6, i < 2 ? -0x54 : 0xb0, (i & 1) ? 0xb2 : 0x9a);
    }
    for (i = 0; i < 4; i++) {
        pk_object_spawn(g, pk_kingdom_ui_update, o->dir, 0x90, i, 7, i < 2 ? -0x50 : 0xb4, (i & 1) ? 0xb3 : 0x9b);
    }
    pk_object_spawn(g, pk_kingdom_ui_update, o->dir, 0xa0, 0, 1, -0x1f, 0x9a);
}

void pk_map_update(pk_object *o, pk_game *g)
{
    int ev;
    g->dlg_hint = map_hint;
    switch (o->state) {
    case 2: /* enter */
        if (g->curtain != PK_CURTAIN_HELD || ++o->counter < 4) {
            break;
        }
        pk_game_set_curtain(g, PK_CURTAIN_OPEN);
        o->work[0] = 3; /* menu: Search, Attack, Move, Info */
        o->work[1] = 0;
        spawn_top_bar(o, g);
        g->field_bg = (int)pk_app_i32(TBL_MAP_BG + (Uint32)g->world.current_area * 4u) + 0x3a;
        pk_game_set_bg(g, 0, g->field_bg);
        g->bg_visible = 1;
        o->counter = 0;
        o->state = 3;
        break;
    case 3:
        if (g->curtain != PK_CURTAIN_NONE) {
            break;
        }
        spawn_buttons(o, g);
        spawn_cells(g, map_cell_update, 0x10); /* spots, rival kingdoms, enemy bases */
        o->counter = 0;
        o->state = 9;
        break;
    case 9: /* the area's pop-up after the buttons (map state 9) */
        if (++o->counter >= 8) {
            o->counter = 0;
            o->state = 4;
            pk_help(g, 0, g->world.current_area);
        }
        break;
    case 4: /* input; finished missions are handled here too */
        if ((ev = pk_event_finished(g)) != 0xff) {
            pk_event_handle(g, ev);
            break;
        }
        if (pk_chat_pending() && g->curtain == PK_CURTAIN_NONE && !pk_help_active()) {
            /* a letter arrived: the map hands it to 0x10028538 too (map
             * state 4, return flag +0x28 = 2), which opens 0x10032114 */
            o->counter = 0;
            pk_object_call(g, o, pk_chat_update, 0, pk_map_update, 2);
            break;
        }
        { /* touch: Search, Attack, Move, Info as on the kingdom screen */
            int c;
            for (c = 0; c < 4; c++) {
                if (pk_tap(g, (c & 2) ? 0x58 : 4, (c & 1) ? 0xb2 : 0x9a, 0x54, 0x18)) {
                    o->work[1] = c;
                    g->key_code = PK_SC_5;
                }
            }
        }
        if (g->key_code == PK_SC_LEFT || g->key_code == PK_SC_RIGHT) {
            pk_audio_play_sfx(2);
            o->work[1] ^= 2;
        } else if (g->key_code == PK_SC_UP || g->key_code == PK_SC_DOWN) {
            pk_audio_play_sfx(2);
            o->work[1] ^= 1;
        } else if (g->key_code == PK_SC_5) {
            int cur = o->work[1]; /* 0 Search, 1 Attack, 2 Move, 3 Info */
            pk_audio_play_sfx(0);
            if (cur == 0) {
                pk_game_set_curtain(g, PK_CURTAIN_CLOSE);
                o->counter = 0;
                o->state = 0x14;
            } else if (cur == 1) {
                o->counter = 0;
                s_scout = 0;
                pk_object_call(g, o, pk_attack_list_update, 0, pk_map_update, 0x1f); /* 0x10043060 */
            } else if (cur == 2) {
                pk_game_set_curtain(g, PK_CURTAIN_CLOSE); /* 0x1004017c */
                o->counter = 0;
                pk_object_call(g, o, pk_move_update, 0, pk_map_update, 0x30);
            } else {
                pk_help(g, 2, g->world.current_area); /* FUN_10071c74(g, 2, area) */
            }
        } else if (g->key_code == PK_SC_BACK) {
            pk_audio_play_sfx(1);
            pk_game_set_curtain(g, PK_CURTAIN_CLOSE);
            o->counter = 0;
            o->dir = 2;
            o->state = 0x31;
        }
        break;
    case 0x14: /* Search */
        if (g->curtain != PK_CURTAIN_HELD || ++o->counter < 4) {
            break;
        }
        pk_object_kill_others(g, o);
        pk_game_set_curtain(g, PK_CURTAIN_OPEN);
        spawn_top_bar(o, g);
        o->state = 0x15;
        break;
    case 0x15:
        if (g->curtain != PK_CURTAIN_NONE) {
            break;
        }
        g->slide = 0;
        g->dlg_type = 8; /* Area Search / Scout / Chat */
        g->list_top = g->cursor = 0;
        call(o, g, pk_dialog_update, 0x16);
        break;
    case 0x16:
        if (g->dlg_result == 0 && g->list_top + g->cursor == 0) {
            g->slide = 0;
            g->list_top = g->cursor = 0;
            g->list_120 = 1;
            g->list_124 = 0;
            call(o, g, pk_team_list_update, 0x17);
        } else if (g->dlg_result == 0 && g->list_top + g->cursor == 2) {
            s_scout = 2; /* Chat (online): a base first (0x100414e8), then the
                          * messenger party (0x1004faec), then the letter
                          * (0x10031e9c); map states 0x21..0x23 */
            o->counter = 0;
            pk_object_call(g, o, pk_attack_list_update, 0, pk_map_update, 0x1f);
        } else if (g->dlg_result == 0 && g->list_top + g->cursor == 1) {
            s_scout = 1; /* Scout: a target first (map states 0x19/0x1a) */
            o->counter = 0;
            pk_object_call(g, o, pk_attack_list_update, 0, pk_map_update, 0x1f);
        } else {
            pk_game_set_curtain(g, PK_CURTAIN_CLOSE);
            g->slide = 2;
            o->counter = 0;
            o->state = 0x30;
        }
        break;
    case 0x17:
        if (g->dlg_result == 0) {
            int team = g->list_top + g->cursor, cost = travel_cost(g, team);
            if (cost <= g->loot) {
                pk_loot_sub(g, cost);
                int ev2 = pk_event_start(g, 4, team); /* area search: a report, no battle */
                pk_arena_send_search(g);              /* online: rival kingdoms (6 -> 134) */
                if (ev2 != 0xff) {
                    g->world.teams[team][12] = 1;
                    g->world.events[ev2][6] = g->world.current_area;
                }
            }
        }
        g->list_120 = 0;
        pk_game_set_curtain(g, PK_CURTAIN_CLOSE);
        g->slide = 2;
        o->counter = 0;
        o->state = 0x30;
        break;
    case 0x1f: /* Attack: target chosen -> the team list */
        if (g->dlg_result != 0) {
            o->counter = 0;
            o->state = 2; /* curtain is held: rebuild */
            break;
        }
        if (++o->counter < 2) {
            break;
        }
        pk_game_set_bg(g, 0, g->field_bg);
        pk_game_set_curtain(g, PK_CURTAIN_OPEN);
        spawn_top_bar(o, g);
        o->state = 0x20;
        break;
    case 0x20:
        if (g->curtain != PK_CURTAIN_NONE) {
            break;
        }
        g->slide = 0;
        g->list_top = g->cursor = 0;
        g->list_120 = 1;
        g->list_124 = 0;
        call(o, g, pk_team_list_update, 0x21);
        break;
    case 0x21:              /* map state 0x20 of the original: the mission starts */
        if (s_scout == 2) { /* Chat: the party carries the letter, no cost */
            g->list_120 = 0;
            s_scout = 0;
            if (g->dlg_result == 0) {
                pk_chat_compose(s_target_type == 2 ? s_target_idx : -1, g->list_top + g->cursor);
                o->counter = 0;
                pk_object_call(g, o, pk_chat_update, 0, pk_map_update, 0x30);
                break;
            }
            pk_game_set_curtain(g, PK_CURTAIN_CLOSE);
            g->slide = 2;
            o->counter = 0;
            o->state = 0x30;
            break;
        }
        if (g->dlg_result == 0) {
            int team = g->list_top + g->cursor, cost = travel_cost(g, team), ev2;
            if (cost <= g->loot) {
                pk_loot_sub(g, cost);
                if (s_target_type == 2) {
                    rival_mission(g, team, s_target_idx, s_scout);
                    ev2 = 0xff;
                } else if (s_scout) { /* map state 0x1a: kind 3 */
                    ev2 = pk_event_start(g, 3, team);
                    if (ev2 != 0xff) {
                        g->world.events[ev2][6] = g->world.current_area;
                        g->world.events[ev2][7] = s_target_type;
                        g->world.events[ev2][8] = s_target_idx;
                    }
                } else if (s_target_type == 0) {
                    ev2 = pk_event_start(g, 2, team); /* FUN_100740e0 */
                    if (ev2 != 0xff) {
                        g->world.events[ev2][6] = s_target_idx;
                    }
                } else {
                    g->world.formations[s_target_idx][46] = 1;
                    ev2 = pk_event_start(g, 1, team);
                    if (ev2 != 0xff) {
                        g->world.events[ev2][6] = g->world.current_area;
                        g->world.events[ev2][7] = s_target_type;
                        g->world.events[ev2][8] = s_target_idx;
                        g->world.events[ev2][9] = 0;
                    } else {
                        g->world.formations[s_target_idx][46] = 0;
                    }
                }
                if (ev2 != 0xff) {
                    g->world.teams[team][12] = 1;
                }
            }
        }
        g->list_120 = 0;
        s_scout = 0;
        pk_game_set_curtain(g, PK_CURTAIN_CLOSE);
        g->slide = 2;
        o->counter = 0;
        o->state = 0x30;
        break;
    case 0x30: /* rebuild the map screen */
        if (g->curtain == PK_CURTAIN_NONE) {
            pk_game_set_curtain(g, PK_CURTAIN_CLOSE);
        }
        if (g->curtain == PK_CURTAIN_HELD) {
            pk_object_kill_others(g, o);
            o->counter = 0;
            o->state = 2;
        }
        break;
    case 0x31: /* back to the kingdom */
        if (g->curtain == PK_CURTAIN_HELD) {
            g->cam_x = g->cam_y = 0;
            pk_object_kill_others(g, o);
            g->field_bg = g->fame > 300 ? 0xe : g->fame < 0x97 ? 0xf
                                                               : 9;
            pk_game_set_bg(g, 0, g->field_bg);
            o->work[0] = 0;
            o->work[1] = 0;
            o->state = 1;
            g->dlg_hint = NULL;
            pk_object_set_update(o, pk_kingdom_update);
        }
        break;
    default:
        break;
    }
}
