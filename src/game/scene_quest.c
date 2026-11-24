/* SPDX-License-Identifier: MIT */
/* Quest spots (0x100525ac, talk 0x100535ec).
 *
 * A spot of kind 4 holds a quest. Offline the game deals quest 0 or 1
 * (FUN_100740e0: rand % 2), both trades: bring item A, receive item B.
 * A and B are entries of questitem.dat (16-byte header, kind/id pairs):
 * A = rand % 45 + tier * 15, B = rand % (tier == 4 ? 30 : 45) +
 * (tier + 1) * 15, tier = fame / 80 - 1 clamped to 0..4, A != B.
 * questmes.dat (localized; quest q at q * 0xa10 + 0x10: 16-byte header
 * with the kind in byte 0, then eight 0x140-byte pages of '\n'-separated
 * lines ending at 0x1b; a line with 7 / 8 gets item A / B's name).
 * Page by stage (FUN_10052ff8): new 0, declined 2, accepted 1, not
 * yet 4, done 5. State per spot in the area words: 5 + spot quest id
 * (0xff none), 10 + spot stage (3 = waiting), 15 + spot * 3 A, B.
 * Screen (0x100525ac state 2): background 0x59, a white window (rows
 * 0x40..0x42 of image 0x17, frames 0x1009adc8, y 0x68..0xcc), the party
 * leader's unit sheet at (0x28, 100), the quest giver (image 0x1d, frames
 * 0x1009c058, animation by area from 0x1009acf4: 0 / 1 / 2 / 4 ->
 * 0x1009ad40 / 58 / 70 / 88, 15 and 16 none) at (0x88, 100) and the
 * talk mark (frames 0x1009adc8, animation 0x1009ada0) at (0x88, 0x32).
 * FUN_10052ff8 draws the page's eight lines at (5, 0x78 + 0xb * i):
 * black, the line with item A in red (0x0000f0), item B in green. */
#include "audio/pk_audio.h"
#include "core/pk_app.h"
#include "core/pk_fs.h"
#include "core/pk_log.h"
#include "game/pk_battle.h"
#include "game/pk_help.h"
#include "game/pk_rules.h"
#include "game/pk_scenes.h"
#include "game/pk_world.h"
#include "game/ui_dialog.h"
#include "gfx/pk_font.h"
#include <SDL3/SDL.h>

static struct
{
    int area, spot, quest, a, b, leader;
    int stage; /* page shown last */
    int n;
    char line[8][96];
    int col[8];
} s_q;

static bool quest_item(int index, int *kind, int *id)
{
    size_t n = 0;
    Uint8 *d = (Uint8 *)pk_fs_load("questitem.dat", &n);
    bool ok = d && (size_t)index * 2 + 18 <= n;
    if (ok) {
        *kind = d[16 + index * 2];
        *id = d[17 + index * 2];
    }
    SDL_free(d);
    return ok;
}

static void item_name_utf8(int kind, int id, char *out, int cap)
{
    const Uint16 *s;
    int o = 0;
    pk_item_preview(kind, id, 9);
    s = kind >= PK_KIND_EQUIP ? pk_r.equip[9].name : pk_r.item[9].name;
    while (*s && *s != 0x1b && o < cap - 4) {
        Uint16 c = *s++;
        if (c < 0x80) {
            out[o++] = (char)c;
        } else if (c < 0x800) {
            out[o++] = (char)(0xc0 | (c >> 6));
            out[o++] = (char)(0x80 | (c & 0x3f));
        } else {
            out[o++] = (char)(0xe0 | (c >> 12));
            out[o++] = (char)(0x80 | ((c >> 6) & 0x3f));
            out[o++] = (char)(0x80 | (c & 0x3f));
        }
    }
    out[o] = 0;
}

/* the lines of a page, item names put in (FUN_10073a50) */
static void put_utf8(char *line, int *o, Uint16 c)
{
    if (*o >= 90) {
        return;
    }
    if (c < 0x80) {
        line[(*o)++] = (char)c;
    } else if (c < 0x800) {
        line[(*o)++] = (char)(0xc0 | (c >> 6));
        line[(*o)++] = (char)(0x80 | (c & 0x3f));
    } else {
        line[(*o)++] = (char)(0xe0 | (c >> 12));
        line[(*o)++] = (char)(0x80 | ((c >> 6) & 0x3f));
        line[(*o)++] = (char)(0x80 | (c & 0x3f));
    }
}

static void show_page(int page)
{
    size_t n = 0;
    Uint8 *d = (Uint8 *)pk_fs_load_localized("questmes.dat", &n);
    int i, o = 0;
    size_t base = (size_t)s_q.quest * 0xa10 + 0x10 + 0x10 + (size_t)page * 0x140;
    s_q.stage = page;
    s_q.n = 0;
    SDL_zeroa(s_q.col);
    if (!d || base + 0x140 > n) {
        SDL_free(d);
        return;
    }
    s_q.line[0][0] = 0;
    for (i = 0; i + 1 < 0x140 && s_q.n < 8; i += 2) {
        Uint16 c = (Uint16)(d[base + i] | (d[base + i + 1] << 8));
        if (c == 0x1b) {
            break;
        }
        if (c == '\r') {
            continue;
        }
        if (c == '\n') {
            s_q.line[s_q.n][o] = 0;
            if (++s_q.n < 8) {
                s_q.line[s_q.n][0] = 0;
            }
            o = 0;
            continue;
        }
        if (c == 7 || c == 8) {
            char nm[48];
            int kind, id;
            s_q.col[s_q.n] = c == 7 ? 0xF00000 : 0x00F000;
            if (quest_item(c == 7 ? s_q.a : s_q.b, &kind, &id)) {
                item_name_utf8(kind, id, nm, sizeof(nm));
                o += SDL_snprintf(s_q.line[s_q.n] + o, (size_t)(96 - o), "%s", nm);
                if (o > 90) {
                    o = 90;
                }
            }
            continue;
        }
        put_utf8(s_q.line[s_q.n], &o, c);
    }
    if (o > 0 && s_q.n < 8) {
        s_q.line[s_q.n][o] = 0;
        s_q.n++;
    }
    SDL_free(d);
}

static void draw_page(pk_game *g)
{
    int i;
    for (i = 0; i < s_q.n; i++) {
        pk_drawq_text_utf8(&g->drawq, s_q.line[i], 5, 0x78 + 0xb * i, (pk_color)s_q.col[i], PK_ALIGN_LEFT, 1,
                           PK_FONT_DENSE);
    }
}

/* window rows, the leader, the quest giver and the talk mark */
static void quest_obj_update(pk_object *o, pk_game *g)
{
    Uint32 fr, idle;
    switch (o->state) {
    case 0x10:
        o->image = pk_game_load_image(g, 0x17, 1);
        pk_object_set_frames(o, 0x1009adc8u);
        o->frame = o->arg1 + 0x40;
        o->flags = 0x4e1;
        o->layer = 0;
        o->state = 0x11;
        break;
    case 1: /* the leader (0x10020f3c outside a battle) */
        if (o->arg1 == 0xff) {
            pk_object_kill(o);
            break;
        }
        o->image = pk_game_load_image(g, o->arg1 * 2 + 0x102, 1);
        pk_battle_unit_look(o->arg1, &fr, &idle);
        pk_object_set_frames(o, fr);
        pk_object_set_anim(o, idle);
        o->flags = 0x400 | PK_OBJ_SPRITE | PK_OBJ_SCREEN;
        o->layer = 1;
        o->state = 0x22;
        break;
    case 0x20:
    { /* the quest giver */
        static const Uint32 k_anim[5] = { 0x1009ad40u, 0x1009ad58u, 0x1009ad70u, 0, 0x1009ad88u };
        int k = o->arg1;
        if (k < 0 || k > 4 || !k_anim[k]) {
            pk_object_kill(o);
            break;
        }
        o->image = pk_game_load_image(g, 0x1d, 1);
        pk_object_set_frames(o, 0x1009c058u);
        pk_object_set_anim(o, k_anim[k]);
        o->flags = 0x441;
        o->layer = 1;
        o->state = 0x22;
        break;
    }
    case 0x21: /* the talk mark */
        o->image = pk_game_load_image(g, 0x17, 1);
        pk_object_set_frames(o, 0x1009adc8u);
        pk_object_set_anim(o, 0x1009ada0u);
        o->flags = 0x4c1;
        o->layer = 1;
        o->state = 0x22;
        break;
    case 0x22:
        pk_object_animate(o);
        break;
    default:
        break;
    }
}

static void quest_store(pk_game *g, int stage)
{
    int *ar = g->world.areas[s_q.area];
    ar[5 + s_q.spot] = s_q.quest;
    ar[10 + s_q.spot] = stage;
    ar[15 + s_q.spot * 3] = s_q.a;
    ar[16 + s_q.spot * 3] = s_q.b;
    ar[17 + s_q.spot * 3] = 0;
    PK_DEBUG("quest %d accepted at area %d spot %d (items %d -> %d)", s_q.quest, s_q.area, s_q.spot, s_q.a, s_q.b);
}

static void quest_clear(pk_game *g) /* FUN_10074628 */
{
    int *ar = g->world.areas[s_q.area];
    ar[5 + s_q.spot] = 0xff;
    ar[10 + s_q.spot] = 0xff;
    ar[15 + s_q.spot * 3] = ar[16 + s_q.spot * 3] = ar[17 + s_q.spot * 3] = 0;
}

static void quest_deal(pk_game *g) /* FUN_100740e0 for a new quest */
{
    int tier = g->fame / 80 - 1, ka, ia, kb, ib, tries = 0;
    if (tier < 0) {
        tier = 0;
    }
    if (tier > 4) {
        tier = 4;
    }
    s_q.quest = pk_rng_range(&g->rng, 2);
    do {
        s_q.a = pk_rng_range(&g->rng, 45) + tier * 15;
        s_q.b = pk_rng_range(&g->rng, tier == 4 ? 30 : 45) + (tier + 1) * 15;
        if (!quest_item(s_q.a, &ka, &ia) || !quest_item(s_q.b, &kb, &ib)) {
            break;
        }
    } while (ka == kb && ia == ib && ++tries < 20);
}

/* the owned item that matches kind/id (FUN_10063940), or -1 */
static int find_owned(pk_game *g, int kind, int id)
{
    int i;
    if (kind >= PK_KIND_EQUIP) {
        for (i = 0; i < PK_EQUIP_MAX; i++) {
            if (g->world.equip[i][0] == kind && g->world.equip[i][1] == id && g->world.equip[i][10] == 0xff) {
                return i;
            }
        }
        return -1;
    }
    for (i = 0; i < PK_ITEM_MAX; i++) {
        if (g->world.items[i][0] == kind && g->world.items[i][1] == id) {
            return i;
        }
    }
    return -1;
}

void pk_quest_update(pk_object *o, pk_game *g)
{
    switch (o->state) {
    case 0: /* black screen */
        if (g->curtain == PK_CURTAIN_NONE) {
            pk_game_set_curtain(g, PK_CURTAIN_CLOSE);
        }
        if (g->curtain != PK_CURTAIN_HELD) {
            break;
        }
        pk_object_kill_others(g, o);
        g->cam_x = g->cam_y = 0;
        pk_game_set_bg(g, 0, 0x59);
        g->bg_visible = 1;
        {
            int y, giver = (int)pk_app_i32(0x1009acf4u + (Uint32)s_q.area * 4u);
            pk_object_spawn(g, quest_obj_update, 0, 0x10, 0x10, 0, 0, 0x68);
            for (y = 0x6c; y < 0xcc; y += 4) {
                pk_object_spawn(g, quest_obj_update, 0, 0x10, 0x10, 1, 0, y);
            }
            pk_object_spawn(g, quest_obj_update, 0, 0x10, 0x10, 2, 0, 0xcc);
            pk_object_spawn(g, quest_obj_update, 0, 1, 0, s_q.leader, 0x28, 100);
            if (giver != 15 && giver != 16) {
                pk_object_spawn(g, quest_obj_update, 0, 0x20, 0x20, giver, 0x88, 100);
            }
        }
        pk_game_set_curtain(g, PK_CURTAIN_OPEN);
        s_q.n = 0;
        o->counter = 0;
        o->state = 0x20;
        break;
    case 0x20: /* talk object state 0: the mark, then help 0x14 */
        if (g->curtain != PK_CURTAIN_NONE) {
            break;
        }
        pk_object_spawn(g, quest_obj_update, 0, 0x21, 0x21, 0, 0x88, 0x32);
        pk_help(g, 1, 0x14);
        o->state = 1;
        break;
    case 1:
    { /* first page */
        int *ar = g->world.areas[s_q.area];
        if (g->curtain != PK_CURTAIN_NONE || pk_help_active()) {
            break;
        }
        o->counter = 0;
        if (ar[5 + s_q.spot] == 0xff) {
            quest_deal(g);
            show_page(0);
            o->state = 2;
        } else {
            int kind, id, idx = -1;
            s_q.quest = ar[5 + s_q.spot];
            s_q.a = ar[15 + s_q.spot * 3];
            s_q.b = ar[16 + s_q.spot * 3];
            if (quest_item(s_q.a, &kind, &id)) {
                idx = find_owned(g, kind, id);
            }
            if (idx < 0) {
                show_page(4); /* not yet */
                o->state = 9;
            } else {
                show_page(5); /* the trade */
                if (kind >= PK_KIND_EQUIP) {
                    pk_equip_remove(g, idx);
                } else {
                    pk_item_remove(g, idx);
                }
                if (quest_item(s_q.b, &kind, &id)) {
                    pk_item_add(g, kind, id);
                }
                pk_items_sort(g);
                pk_equip_sort(g, false);
                quest_clear(g);
                PK_DEBUG("quest %d done", s_q.quest);
                o->state = 9;
            }
        }
        break;
    }
    case 2: /* accept? (talk state 1: after a key, the dialog 0xc) */
        draw_page(g);
        if (++o->counter < 6 || (g->key_code != PK_SC_5 && g->key_code != PK_SC_BACK)) {
            break;
        }
        pk_audio_play_sfx(0);
        g->dlg_type = 0xc;
        g->list_top = g->cursor = 0;
        g->slide = 0;
        pk_object_call(g, o, pk_dialog_update, 0, pk_quest_update, 3);
        break;
    case 3:
        if (g->dlg_result == 0 && g->list_top + g->cursor == 0) {
            show_page(1);
            quest_store(g, 3); /* FUN_10074544 */
        } else {
            show_page(2);
            quest_clear(g);
        }
        o->counter = 0;
        o->state = 9;
        break;
    case 9: /* the last page until a key (talk states 3 / 5 / 9 / 10) */
        draw_page(g);
        if (pk_help_active() || ++o->counter < 6 || (g->key_code != PK_SC_5 && g->key_code != PK_SC_BACK)) {
            break;
        }
        pk_audio_play_sfx(0);
        pk_game_set_curtain(g, PK_CURTAIN_CLOSE);
        o->state = 10;
        break;
    default:
        if (g->curtain != PK_CURTAIN_HELD) {
            break;
        }
        pk_object_kill_others(g, o);
        g->bg_visible = 1;
        o->counter = 0;
        pk_object_return(g, o);
        break;
    }
}

void pk_quest_begin(pk_game *g, int area, int spot, int team)
{
    pk_object *root = pk_ui_root(g);
    s_q.area = area;
    s_q.spot = spot;
    s_q.leader = 0xff;
    if (team >= 0 && team < PK_TEAMS) {
        int lead = g->world.teams[team][9];
        if (lead >= 0 && lead < 4 && g->world.teams[team][lead] != 0xff && g->world.teams[team][lead] < PK_UNIT_MAX) {
            s_q.leader = g->world.units[g->world.teams[team][lead]][0];
        }
    }
    if (!root) {
        return;
    }
    if (root->update == pk_map_update) {
        pk_object_call(g, root, pk_quest_update, 0, pk_map_update, 2);
    } else {
        pk_object_call(g, root, pk_quest_update, 0, pk_kingdom_update, 1);
    }
    root->counter = 0;
}
