/* SPDX-License-Identifier: MIT */
/* Party > Form (root update 0x100493c4): the team list (0x1004faec),
 * the team editor (0x1004c848) and the list of owned units
 * (0x1004a4a0). Up to eight teams of four units; a team's units may cost
 * six points in total (team f11 holds what is left). Team records are
 * world.teams[t]: f0..f3 unit per slot, f4..f7 targeting per slot, f8
 * order, f9 leader slot, f10 highest rank, f11 cost left, f12 on a
 * mission, f13 has units. A unit's f3 is team << 4 | slot, f4 = 1 while
 * it is in a team. */
#include "audio/pk_audio.h"
#include "core/pk_app.h"
#include "game/pk_rules.h"
#include "game/pk_scenes.h"
#include "game/ui_dialog.h"
#include "gfx/pk_font.h"
#include <SDL3/SDL.h>

#define IMG_UI           0x17
#define IMG_UNIT_ICON    0x19
#define IMG_FORM_BG      0xd
#define TBL_UI           0x1009870cu
#define TBL_UNIT_ICONS   0x1009999cu
#define TBL_PORTRAIT     0x1009a2ccu
#define TBL_SWAP_ANIM    0x1009a35cu
#define TBL_ORDER_LABEL  0x100986c4u
#define TBL_TARGET_LABEL 0x1008b85cu
#define COL_WHITE        0xFFFFFFu
#define COL_GOLD         0xFFDC00u
#define LINE             0xd

/* ------------------------------------------------------------------ */
/* helpers */

static int *team(pk_game *g, int t) { return g->world.teams[t & 7]; }
static int *unit(pk_game *g, int u) { return g->world.units[u]; }

/* FUN_10051a18: highest rank in a team */
void pk_team_update_rank(pk_game *g, int t)
{
    int i, best = 0, *tm = team(g, t);
    for (i = 0; i < 4; i++) {
        if (tm[i] != 0xff && unit(g, tm[i])[14] >= best) {
            best = unit(g, tm[i])[14];
        }
    }
    tm[10] = best;
}

static void unequip(pk_game *g, int u)
{
    int e = unit(g, u)[2];
    if (e != 0xff) {
        unit(g, u)[2] = 0xff;
        g->world.equip[e][10] = 0xff;
        g->world.equip[e][11] = 0;
    }
}

/* removes the unit in a slot of the edited team (Form states 0x12/0x13) */
static void slot_clear(pk_game *g, int t, int slot)
{
    int *tm = team(g, t), u = tm[slot];
    if (u != 0xff) {
        unit(g, u)[4] = 0;
        unit(g, u)[3] = 0xff;
        tm[11] += unit(g, u)[17];
        unequip(g, u);
    }
}

static void first_leader(pk_game *g, int t)
{
    int i, *tm = team(g, t);
    for (i = 0; i < 4; i++) {
        if (tm[i] != 0xff) {
            tm[9] = i;
            return;
        }
    }
}

static void shadow_mes(pk_game *g, int id, int x, int y, pk_color c, pk_align a)
{
    pk_game_mestext(g, id, x + 1, y + 1, 0, a, 1, PK_FONT_LEGEND);
    pk_game_mestext(g, id, x, y, c, a, 1, PK_FONT_LEGEND);
}

static void shadow_text(pk_game *g, const Uint16 *s, int x, int y, pk_color c, pk_align a)
{
    pk_drawq_text(&g->drawq, s, x + 1, y + 1, 0, a, 1, PK_FONT_LEGEND);
    pk_drawq_text(&g->drawq, s, x, y, c, a, 1, PK_FONT_LEGEND);
}

static void shadow_num(pk_game *g, const char *fmt, int a, int b, int x, int y, pk_color c, pk_align al)
{
    char buf[32];
    SDL_snprintf(buf, sizeof(buf), fmt, a, b);
    pk_drawq_text_utf8(&g->drawq, buf, x + 1, y + 1, 0, al, 1, PK_FONT_LEGEND);
    pk_drawq_text_utf8(&g->drawq, buf, x, y, c, al, 1, PK_FONT_LEGEND);
}

static void unit_name16(pk_game *g, int u, Uint16 *out, int cap)
{
    pk_unit_name(g, u, out, cap);
}

static void slide_child_common(pk_object *o, pk_game *g, pk_object_fn self, int next_wait, int next_out)
{
    (void)next_wait;
    if (pk_ui_root_left(g, self)) {
        o->counter = 0;
        o->state = next_out;
    }
}

static void icon_anim(pk_object *o)
{
    o->work[1] = (o->work[1] + 1) % 12;
    o->frame = o->work[2] + o->work[1] / 6;
}

static void portrait(pk_game *g, pk_object *o, int type, Uint32 frames)
{
    o->image = pk_game_load_image(g, PK_PORTRAIT_BASE + type * 2, 1);
    pk_object_set_frames(o, frames);
    o->flags = 0x4c1;
    o->frame = o->arg1;
}

/* ------------------------------------------------------------------ */
/* owned unit list (0x1004a4a0) */

static bool unit_list_caller_form(pk_game *g)
{
    return g->call_ret_fn == pk_form_update;
}

static int unit_row_frame(pk_game *g, int row)
{
    int i = g->list_top + row;
    if (g->world.unit_count == 0 || i >= g->world.unit_count) {
        return 0x10;
    }
    if (unit(g, i)[4] != 0) {
        return 0x12;
    }
    return g->cursor == row ? 0x11 : 0x10;
}

static void unit_list_info(pk_game *g)
{
    int i = g->list_top + g->cursor, y = 0xa5;
    if (i >= g->world.unit_count) {
        return;
    }
    shadow_mes(g, 0x1f, 0x2d, y, COL_WHITE, PK_ALIGN_RIGHT); /* Class : */
    shadow_text(g, pk_r.unit[g->cursor].name, 0x2d, y, COL_WHITE, PK_ALIGN_LEFT);
    y += LINE;
    shadow_mes(g, 0x20, 0x2d, y, COL_WHITE, PK_ALIGN_RIGHT); /* Race : */
    shadow_text(g, pk_r.unit[g->cursor].race, 0x2d, y, COL_WHITE, PK_ALIGN_LEFT);
    y += LINE;
    shadow_mes(g, 0x19, 0x2d, y, COL_WHITE, PK_ALIGN_RIGHT); /* Rank : */
    y += LINE;
    if (g->call_ret_fn == pk_medical_update) {
        shadow_mes(g, 0x2e, 0x2d, y, COL_WHITE, PK_ALIGN_RIGHT); /* HP : */
        shadow_num(g, "%d / %d", unit(g, i)[7], unit(g, i)[8], 0x2d, y, COL_WHITE, PK_ALIGN_LEFT);
        y += LINE;
        shadow_mes(g, 0x22, 0x2d, y, COL_WHITE, PK_ALIGN_RIGHT); /* Cost : */
        shadow_num(g, "%d", pk_heal_cost_unit(g, i), 0, 0x2d, y, COL_WHITE, PK_ALIGN_LEFT);
    } else if (unit_list_caller_form(g)) {
        shadow_mes(g, 0x30, 0x2d, y, COL_WHITE, PK_ALIGN_RIGHT); /* Slot : */
        shadow_num(g, "%d", unit(g, i)[17], 0, 0x2d, y, COL_WHITE, PK_ALIGN_LEFT);
        shadow_mes(g, 0x26, 0x6c, y, COL_WHITE, PK_ALIGN_RIGHT); /* Level : */
        shadow_num(g, "%d", unit(g, i)[1], 0, 0x6c, y, COL_WHITE, PK_ALIGN_LEFT);
    } else if (g->call_ret_fn == pk_shop_update) {
        shadow_mes(g, 0x18, 0x2d, y, COL_WHITE, PK_ALIGN_RIGHT);
        shadow_num(g, "%d", pk_r.unit[g->cursor].price, 0, 0x2d, y, COL_WHITE, PK_ALIGN_LEFT);
        shadow_num(g, "%d/%d", g->world.unit_count, PK_UNIT_MAX, 0xae, 0x8f, COL_GOLD, PK_ALIGN_RIGHT);
    }
}

static int unit_list_rank(pk_game *g)
{
    int i = g->list_top + g->cursor;
    return i < g->world.unit_count ? pk_r.unit[g->cursor].b[1] : 0;
}

void pk_unit_list_update(pk_object *o, pk_game *g);

static void unit_list_child(pk_object *o, pk_game *g)
{
    pk_object *row;
    int i, t;
    switch (o->state) {
    case 8:
        pk_ui_setup_sprite(g, o, IMG_UI, TBL_UI, 0x4c1);
        o->frame = 0;
        o->layer = 1;
        pk_ui_cursor_anim(g, o);
        pk_object_clear_work(o);
        o->counter = 0;
        o->y = g->cursor * 0x14 + 0x22;
        pk_ui_start_offset(o);
        o->state = 9;
        break;
    case 9:
        pk_object_animate(o);
        pk_ui_slide_in(o);
        if (++o->counter > 5) {
            o->counter = 0;
            o->state = 10;
        }
        break;
    case 10:
        pk_object_animate(o);
        o->x = 8;
        o->y = g->cursor * 0x14 + 0x22;
        slide_child_common(o, g, pk_unit_list_update, 0, 0xb);
        break;
    case 0xb:
    case 0x13:
    case 0x33:
        pk_object_animate(o);
        if (pk_ui_slide_out(o, g)) {
            pk_object_kill(o);
        }
        break;
    case 0x10:
        pk_ui_setup_sprite(g, o, IMG_UI, TBL_UI, 0x4c1);
        o->frame = unit_row_frame(g, o->arg1);
        pk_object_clear_work(o);
        pk_ui_start_offset(o);
        o->state = 0x11;
        break;
    case 0x11:
        pk_ui_slide_in(o);
        if (++o->counter > 5) {
            o->counter = 0;
            o->state = 0x12;
        }
        break;
    case 0x12:
        o->frame = unit_row_frame(g, o->arg1);
        slide_child_common(o, g, pk_unit_list_update, 0, 0x13);
        break;
    case 0x20:
        pk_ui_setup_sprite(g, o, IMG_UNIT_ICON, TBL_UNIT_ICONS, 0x4c0);
        o->work[0] = -1;
        o->state = 0x21;
        /* fallthrough */
    case 0x21:
        row = pk_object_find(g, o->type, 0x40, o->arg1);
        if (!row || row->dead) {
            pk_object_kill(o);
            break;
        }
        i = pk_ui_root_left(g, pk_unit_list_update) ? o->work[0] : g->list_top + o->arg1;
        if (i >= 0 && i < g->world.unit_count) {
            Uint16 name[25];
            o->flags |= PK_OBJ_SPRITE;
            if (i != o->work[0]) {
                pk_unit_template(unit(g, i)[0], o->arg1);
                o->work[1] = 0;
                o->work[2] = pk_r.unit[o->arg1].b[0] * 2;
            }
            icon_anim(o);
            o->x = row->x + 4;
            o->y = row->y + 2;
            unit_name16(g, i, name, 25);
            pk_drawq_text(&g->drawq, name, o->x + 0x18, o->y + 0xd, 0, PK_ALIGN_LEFT, 0, PK_FONT_LEGEND);
        } else {
            o->flags &= ~(Uint32)PK_OBJ_SPRITE;
        }
        o->work[0] = i;
        break;
    case 0x30:
        pk_ui_setup_sprite(g, o, IMG_UI, TBL_UI, 0x4c0);
        o->frame = o->arg1 + 0x31;
        pk_ui_start_offset(o);
        o->state = 0x31;
        /* fallthrough */
    case 0x31:
    case 0x32:
        if (o->arg1 == 0 ? g->list_top != 0 : g->list_top + 5 < g->world.unit_count) {
            o->flags |= PK_OBJ_SPRITE;
        } else {
            o->flags &= ~(Uint32)PK_OBJ_SPRITE;
        }
        if (o->state == 0x31) {
            pk_ui_slide_in(o);
            if (++o->counter > 5) {
                o->counter = 0;
                o->state = 0x32;
            }
        } else {
            slide_child_common(o, g, pk_unit_list_update, 0, 0x33);
        }
        break;
    case 0x60: /* rank stars */
        pk_ui_setup_sprite(g, o, IMG_UI, TBL_UI, 0x4c1);
        o->frame = 0x3f;
        o->state = 0x61;
        /* fallthrough */
    case 0x61:
        o->frame = unit_list_rank(g) >= o->arg1 ? 0x3e : 0x3f;
        if (pk_ui_root_left(g, pk_unit_list_update)) {
            pk_object_kill(o);
        }
        break;
    case 0x80:
        pk_ui_setup_sprite(g, o, IMG_UI, TBL_UI, 0x4c1);
        o->frame = 0x93;
        o->state = 0x81;
        break;
    case 0x81:
        if (pk_ui_root_left(g, pk_unit_list_update)) {
            pk_object_kill(o);
        }
        break;
    case 0x90:
    case 0x91:
        i = g->list_top + g->cursor;
        t = i < g->world.unit_count ? unit(g, i)[0] : 0xff;
        if (t == 0xff) {
            o->flags &= ~(Uint32)PK_OBJ_SPRITE;
        } else if (o->state == 0x90 || t != o->work[0]) {
            portrait(g, o, t, 0x100a2c64u);
            o->layer = 1;
        }
        o->work[0] = t;
        o->state = 0x91;
        if (pk_ui_root_left(g, pk_unit_list_update)) {
            pk_object_kill(o);
        }
        break;
    default:
        break;
    }
}

void pk_unit_list_update(pk_object *o, pk_game *g)
{
    int i, n;
    if (o->type != 0xc2) {
        unit_list_child(o, g);
        return;
    }
    n = g->world.unit_count;
    switch (o->state) {
    case 0:
        pk_object_spawn(g, pk_unit_list_update, g->slide, 0x80, 0, 0, 0, 0x9a);
        pk_object_spawn(g, pk_unit_list_update, g->slide, 0x90, 0, 0, 0x98, 0xa3);
        pk_object_spawn(g, pk_unit_list_update, g->slide, 0x90, 0, 1, 0x98, 0xbb);
        for (i = 0; i < 5; i++) {
            pk_object_spawn(g, pk_unit_list_update, g->slide, 0x10, 0x40, i, 8, 0x1e + i * 0x14);
        }
        for (i = 0; i < 5; i++) {
            pk_object_spawn(g, pk_unit_list_update, g->slide, 0x20, 0x50, i, 8, 0x1f + i * 0x14);
        }
        pk_object_spawn(g, pk_unit_list_update, g->slide, 8, 0x60, 0, 8, 0x22);
        pk_object_spawn(g, pk_unit_list_update, g->slide, 0x30, 0x30, 0, 0x58, 0x19);
        pk_object_spawn(g, pk_unit_list_update, g->slide, 0x30, 0x30, 1, 0x58, 0x87);
        if (n > 0) {
            for (i = 1; i <= 10; i++) {
                pk_object_spawn(g, pk_unit_list_update, g->slide, 0x60, 0x60, i, 0x29 + i * 6, 0xbf);
            }
        }
        o->counter = 0;
        o->state = 1;
        break;
    case 1:
        if (++o->counter > 5) {
            o->counter = 0;
            o->state = 2;
        }
        unit_list_info(g);
        break;
    case 2:
        /* touch: rows, the arrows, scrolling */
        pk_tap_rows(g, 8, 0x1e, 0xa0, 0x14, SDL_min(5, n - g->list_top), &g->cursor);
        if (pk_tap(g, 0x40, 0x10, 0x30, 0xe) && g->list_top > 0) {
            g->list_top--;
            pk_audio_play_sfx(2);
        }
        if (pk_tap(g, 0x40, 0x84, 0x30, 0x12) && g->list_top + 5 < n) {
            g->list_top++;
            pk_audio_play_sfx(2);
        }
        pk_swipe_list(g, &g->list_top, &g->cursor, n, 5);
        if (g->key_code == PK_SC_UP) {
            pk_audio_play_sfx(2);
            if (g->cursor == 0) {
                if (g->list_top != 0) {
                    g->list_top--;
                }
            } else {
                g->cursor--;
            }
        } else if (g->key_code == PK_SC_DOWN) {
            pk_audio_play_sfx(2);
            if (g->list_top + g->cursor < n - 1) {
                if (g->cursor == 4) {
                    g->list_top++;
                } else {
                    g->cursor++;
                }
            }
        } else if (g->key_code == PK_SC_RIGHT) {
            pk_audio_play_sfx(2);
            if (g->list_top + g->cursor < n - 1 && n > 4) {
                g->list_top = g->list_top > n - 10 ? n - 5 : g->list_top + 5;
            }
        } else if (g->key_code == PK_SC_LEFT) {
            pk_audio_play_sfx(2);
            g->list_top = g->list_top < 5 ? 0 : g->list_top - 5;
        } else if (g->key_code == PK_SC_5) {
            int sel = g->list_top + g->cursor;
            bool ok = n > 0 && sel < n && (unit(g, sel)[4] == 0 || g->call_ret_fn == pk_list_update);
            pk_audio_play_sfx(0);
            if (g->call_ret_fn == pk_medical_update) {
                /* Medical: heal the unit in place when affordable */
                if (n > 0 && sel < n) {
                    int t = unit(g, sel)[3], cost;
                    if (t == 0xff || team(g, t >> 4)[12] == 0) {
                        cost = pk_heal_cost_unit(g, sel);
                        if (cost < g->loot) {
                            pk_heal_unit(g, sel);
                            pk_loot_sub(g, cost);
                        }
                    }
                }
                unit_list_info(g);
                break;
            }
            if (ok && unit_list_caller_form(g)) {
                /* the team's spare cost plus what the replaced unit frees */
                int *tm = team(g, g->team_sel);
                int cur = tm[g->sel];
                int room = tm[11] + (cur != 0xff ? unit(g, cur)[17] : 0);
                ok = room >= unit(g, sel)[17];
            }
            if (ok) {
                g->dlg_result = 0;
                pk_object_return(g, o);
                return;
            }
        } else if (g->key_code == PK_SC_BACK) {
            pk_audio_play_sfx(1);
            g->dlg_result = 1;
            pk_object_return(g, o);
            return;
        }
        unit_list_info(g);
        break;
    default:
        break;
    }
}

/* ------------------------------------------------------------------ */
/* team list (0x1004faec) */

static bool team_ok_for_caller(pk_game *g, int t)
{
    int *tm = team(g, t);
    if (g->list_124 != 0) {
        return g->list_120 == 0 || tm[13] != 0;
    }
    if (g->list_120 == 0) {
        return true;
    }
    /* sending a team out: it needs units, must be at home and must not
     * be the defense party */
    return tm[12] == 0 && tm[13] != 0 && g->world.h_dc != t;
}

static int team_row_frame(pk_game *g, int row)
{
    int t = g->list_top + row, *tm = team(g, t);
    if (tm[12] == 0 && g->world.h_dc != t) {
        return g->cursor == row ? 0xe : 0xd;
    }
    return 0xf;
}

static void team_list_info(pk_game *g)
{
    int t = g->list_top + g->cursor, *tm = team(g, t), i, fight = 0, search = 0;
    int x = g->language == 2 ? 100 : 0x7d;
    if (tm[13] == 0) {
        return;
    }
    if (tm[12] != 0) {
        shadow_mes(g, 0x28, 0x58, 0xb9, COL_WHITE, PK_ALIGN_CENTER); /* Movement */
        return;
    }
    for (i = 0; i < 4; i++) {
        if (tm[i] != 0xff) {
            fight += unit(g, tm[i])[5];
            search += unit(g, tm[i])[9];
        }
    }
    if (g->call_ret_fn == pk_medical_update) {
        shadow_mes(g, 0x22, 0x58, 0xb4, COL_WHITE, PK_ALIGN_RIGHT); /* Cost : */
        shadow_num(g, "%d", pk_heal_cost_team(g, t), 0, 0x58, 0xb4, COL_WHITE, PK_ALIGN_LEFT);
        return;
    }
    shadow_mes(g, 0x29, x, 0xa6, COL_WHITE, PK_ALIGN_RIGHT); /* Fighting Power : */
    shadow_num(g, "%d", fight, 0, x, 0xa6, COL_WHITE, PK_ALIGN_LEFT);
    shadow_mes(g, 0x2a, x, 0xb5, COL_WHITE, PK_ALIGN_RIGHT); /* Search Power : */
    shadow_num(g, "%d", search, 0, x, 0xb5, COL_WHITE, PK_ALIGN_LEFT);
    shadow_mes(g, 0x19, 100, 0xc4, COL_WHITE, PK_ALIGN_RIGHT); /* Rank : */
}

void pk_team_list_update(pk_object *o, pk_game *g);

static void team_list_child(pk_object *o, pk_game *g)
{
    pk_object *row;
    int t, u, slot;
    switch (o->state) {
    case 8:
        pk_ui_setup_sprite(g, o, IMG_UI, TBL_UI, 0x4c1);
        o->frame = 0;
        o->layer = 1;
        pk_ui_cursor_anim(g, o);
        pk_object_clear_work(o);
        o->counter = 0;
        o->y = g->cursor * 0x14 + 0x2c;
        pk_ui_start_offset(o);
        o->state = 9;
        break;
    case 9:
        pk_object_animate(o);
        pk_ui_slide_in(o);
        if (++o->counter > 5) {
            o->counter = 0;
            o->state = 10;
        }
        break;
    case 10:
        pk_object_animate(o);
        o->x = 8;
        o->y = g->cursor * 0x14 + 0x2c;
        slide_child_common(o, g, pk_team_list_update, 0, 0xb);
        break;
    case 0xb:
    case 0x13:
    case 0x33:
        if (o->state == 0xb) {
            pk_object_animate(o);
        }
        if (o->state == 0x13) {
            goto row_text;
        }
        if (pk_ui_slide_out(o, g)) {
            pk_object_kill(o);
        }
        break;
    case 0x10:
        pk_ui_setup_sprite(g, o, IMG_UI, TBL_UI, 0x4c1);
        o->frame = team_row_frame(g, o->arg1);
        pk_object_clear_work(o);
        pk_ui_start_offset(o);
        o->state = 0x11;
        goto row_text;
    case 0x11:
        pk_ui_slide_in(o);
        if (++o->counter > 5) {
            o->counter = 0;
            o->state = 0x12;
        }
        goto row_text;
    case 0x12:
        o->frame = team_row_frame(g, o->arg1);
        slide_child_common(o, g, pk_team_list_update, 0, 0x13);
    row_text:
        t = g->list_top + o->arg1;
        if (o->state == 0x13 && pk_ui_slide_out(o, g)) {
            pk_object_kill(o);
            break;
        }
        if (team(g, t)[13] == 0) {
            pk_game_mestext(g, 0xd2, o->x + 0x17, o->y + 0xe, 0, PK_ALIGN_LEFT, 0, PK_FONT_LEGEND); /* Empty */
        } else {
            int *tm = team(g, t);
            u = tm[9] != 0xff ? tm[tm[9] & 3] : 0xff;
            if (u != 0xff) {
                Uint16 name[25];
                unit_name16(g, u, name, 25);
                pk_drawq_text(&g->drawq, name, o->x + 0x17, o->y + 0xe, 0, PK_ALIGN_LEFT, 1, PK_FONT_LEGEND);
            }
        }
        break;
    case 0x20: /* unit icons: arg0 = row + 1, arg1 = slot */
        pk_ui_setup_sprite(g, o, IMG_UNIT_ICON, TBL_UNIT_ICONS, 0x4c0);
        o->work[0] = -1;
        o->state = 0x21;
        /* fallthrough */
    case 0x21:
        row = pk_object_find(g, o->type, 0x80, o->arg0 - 1);
        if (!row || row->dead) {
            pk_object_kill(o);
            break;
        }
        t = g->list_top + o->arg0 - 1;
        slot = o->arg1;
        u = team(g, t)[slot];
        if (u == 0xff) {
            o->flags &= ~(Uint32)PK_OBJ_SPRITE;
        } else {
            int key = u | (t << 8);
            o->flags |= PK_OBJ_SPRITE;
            if (key != o->work[0]) {
                pk_unit_template(unit(g, u)[0], PK_PREVIEW_SLOTS - 1);
                o->work[1] = 0;
                o->work[2] = pk_r.unit[PK_PREVIEW_SLOTS - 1].b[0] * 2;
                o->work[0] = key;
            }
            icon_anim(o);
        }
        o->x = slot * 0x11 + 0x6b + row->x;
        o->y = row->y + 1;
        break;
    case 0x30:
        pk_ui_setup_sprite(g, o, IMG_UI, TBL_UI, 0x4c0);
        o->frame = o->arg1 + 0x31;
        pk_ui_start_offset(o);
        o->state = 0x31;
        /* fallthrough */
    case 0x31:
    case 0x32:
        if (o->arg1 == 0 ? g->list_top != 0 : g->list_top + 4 < 8) {
            o->flags |= PK_OBJ_SPRITE;
        } else {
            o->flags &= ~(Uint32)PK_OBJ_SPRITE;
        }
        if (o->state == 0x31) {
            pk_ui_slide_in(o);
            if (++o->counter > 5) {
                o->counter = 0;
                o->state = 0x32;
            }
        } else {
            slide_child_common(o, g, pk_team_list_update, 0, 0x33);
        }
        break;
    case 0x50: /* team flag */
        pk_ui_setup_sprite(g, o, IMG_UI, TBL_UI, 0x4c0);
        o->state = 0x51;
        /* fallthrough */
    case 0x51:
        row = pk_object_find(g, o->type, 0x80, o->arg1);
        if (!row || row->dead) {
            pk_object_kill(o);
            break;
        }
        t = g->list_top + o->arg1;
        if (team(g, t)[13] == 0) {
            o->flags &= ~(Uint32)PK_OBJ_SPRITE;
        } else {
            o->flags |= PK_OBJ_SPRITE;
            o->frame = g->world.h_dc == t ? 0x2e : 0x2f;
        }
        o->x = row->x + 4;
        o->y = row->y + 1;
        break;
    case 0x60: /* rank stars of the team */
        pk_ui_setup_sprite(g, o, IMG_UI, TBL_UI, 0x4c0);
        o->frame = 0x3f;
        o->state = 0x61;
        /* fallthrough */
    case 0x61:
    {
        pk_object *root = pk_ui_root(g);
        int *tm = team(g, g->list_top + g->cursor);
        if (!root || root->update != pk_team_list_update || root->state > 2) {
            pk_object_kill(o);
            break;
        }
        if (tm[13] == 0 || tm[12] != 0 || tm[10] == 0) {
            o->flags &= ~(Uint32)PK_OBJ_SPRITE;
        } else if (root->state == 2) {
            o->frame = tm[10] < o->arg1 ? 0x3f : 0x3e;
            o->flags |= PK_OBJ_SPRITE;
        }
        break;
    }
    default:
        break;
    }
}

void pk_team_list_update(pk_object *o, pk_game *g)
{
    int i, j;
    if (o->type != 0xc2) {
        team_list_child(o, g);
        return;
    }
    switch (o->state) {
    case 0:
        for (i = 0; i < 4; i++) {
            pk_object_spawn(g, pk_team_list_update, g->slide, 0x10, 0x80, i, 0, 0x28 + i * 0x14);
        }
        for (i = 0; i < 4; i++) {
            pk_object_spawn(g, pk_team_list_update, g->slide, 0x50, 0x50, i, 0, 0x28 + i * 0x14);
        }
        for (i = 1; i <= 4; i++) {
            for (j = 0; j < 4; j++) {
                pk_object_spawn(g, pk_team_list_update, g->slide, 0x20, i, j, 0, 0x14 + i * 0x14);
            }
        }
        pk_object_spawn(g, pk_team_list_update, g->slide, 8, 0x40, 0, 8, 0x2c);
        pk_object_spawn(g, pk_team_list_update, g->slide, 0x30, 0x40, 0, 0x58, 0x23);
        pk_object_spawn(g, pk_team_list_update, g->slide, 0x30, 0x40, 1, 0x58, 0x7d);
        for (i = 1; i <= 10; i++) {
            pk_object_spawn(g, pk_team_list_update, g->slide, 0x60, 0x60, i, 0x5f + i * 6, 0xbb);
        }
        for (i = 0; i < 8; i++) {
            pk_team_update_rank(g, i);
        }
        o->counter = 0;
        o->state = 1;
        break;
    case 1:
        if (++o->counter > 5) {
            o->counter = 0;
            o->state = 2;
        }
        break;
    case 2:
        /* touch: the four team rows, the arrows, scrolling */
        pk_tap_rows(g, 0, 0x28, 0xb0, 0x14, 4, &g->cursor);
        if (pk_tap(g, 0x40, 0x16, 0x30, 0x12) && g->list_top > 0) {
            g->list_top--;
            pk_audio_play_sfx(2);
        }
        if (pk_tap(g, 0x40, 0x78, 0x30, 0x12) && g->list_top < 4) {
            g->list_top++;
            pk_audio_play_sfx(2);
        }
        pk_swipe_list(g, &g->list_top, &g->cursor, 8, 4);
        if (g->key_code == PK_SC_UP) {
            pk_audio_play_sfx(2);
            if (g->cursor == 0) {
                if (g->list_top == 0) {
                    g->list_top = 4;
                    g->cursor = 3;
                } else {
                    g->list_top--;
                }
            } else {
                g->cursor--;
            }
        } else if (g->key_code == PK_SC_DOWN) {
            pk_audio_play_sfx(2);
            if (g->list_top + g->cursor < 7) {
                if (g->cursor == 3) {
                    g->list_top++;
                } else {
                    g->cursor++;
                }
            } else {
                g->list_top = g->cursor = 0;
            }
        } else if (g->key_code == PK_SC_5 && g->call_ret_fn == pk_medical_update) {
            int t = g->list_top + g->cursor, cost = pk_heal_cost_team(g, t), i2;
            pk_audio_play_sfx(0);
            if (team(g, t)[12] == 0 && team(g, t)[13] != 0 && cost < g->loot) {
                for (i2 = 0; i2 < 4; i2++) {
                    pk_heal_unit(g, team(g, t)[i2]);
                }
                pk_loot_sub(g, cost);
            }
        } else if (g->key_code == PK_SC_5) {
            if (team_ok_for_caller(g, g->list_top + g->cursor)) {
                pk_audio_play_sfx(0);
                g->dlg_result = 0;
                pk_object_return(g, o);
                return;
            }
            pk_audio_play_sfx(1);
        } else if (g->key_code == PK_SC_BACK) {
            pk_audio_play_sfx(1);
            g->dlg_result = 1;
            pk_object_return(g, o);
            return;
        }
        team_list_info(g);
        break;
    default:
        break;
    }
}

/* ------------------------------------------------------------------ */
/* team editor (0x1004c848). g->sel = row (0..3 slots, 4 order row,
 * 5 bottom row), g->sel2 = column (0 unit, 1 equipment, 2 targeting;
 * 3..6 slot position, 7 order; 8 Clear, 9 Done). g->team_sel = team. */

static void team_edit_update(pk_object *o, pk_game *g);

static int edit_frame(pk_game *g, int id)
{
    int col = g->sel2;
    switch (id) {
    case 6:
        return col == 3 ? 0x5a : 0x56;
    case 7:
        return col == 4 ? 0x5b : 0x57;
    case 8:
        return col == 5 ? 0x5c : 0x58;
    case 9:
        return col == 6 ? 0x5d : 0x59;
    case 10:
        return col == 7 ? 0x66 : 0x65;
    case 11:
        return col == 8 ? 0x63 : 0x62;
    case 12:
        return col == 9 ? 0x63 : 0x62;
    default:
        break;
    }
    if (id < 4) {
        return 0x4e;
    }
    if (id == 4) {
        return 0x4d;
    }
    if (id == 5) {
        return 0x4f;
    }
    if (id < 0x20) {
        return 0x73;
    }
    if (id < 0x30) {
        return 0x74;
    }
    if (id < 0x40) {
        return 0x51;
    }
    if (id < 0x50) {
        return 0x53;
    }
    return 0x55;
}

static bool edit_visible(pk_game *g, int id)
{
    int *tm = team(g, g->team_sel);
    if (id >= 0x10 && id < 0x20) { /* cost pips per slot */
        int r = (id - 0x10) / 4, c = (id - 0x10) % 4, u = tm[r];
        return c < (u != 0xff ? unit(g, u)[17] : 0);
    }
    if (id >= 0x20 && id < 0x30) {
        return id - 0x20 < 6 - tm[11];
    }
    if (id >= 0x30) { /* selection highlight of the current cell */
        return g->sel < 4 && g->sel + g->sel2 * 0x10 + 0x30 == id;
    }
    return true;
}

static void edit_cursor_pos(pk_game *g, int *x, int *y)
{
    if (g->sel < 4) {
        *x = g->sel2 * 0x12 + 8;
        *y = g->sel * 0x12 + 0x1c;
    } else if (g->sel == 4) {
        *x = g->sel2 == 7 ? 0x62 : (g->sel2 - 3) * 0x13 + 10;
        *y = 100;
    } else {
        *x = g->sel2 == 8 ? 0x12 : 0x66;
        *y = 0x78;
    }
}

/* Touch (port): the editor's cells as drawn; a tap picks the cell and
 * selects it. While a slot is being swapped, a tap picks the other one. */
static void edit_touch(pk_game *g)
{
    int r, c;
    if (g->list_124) {
        for (c = 0; c < 4; c++) {
            if (pk_tap(g, 7 + c * 0x13, 0x66, 0x13, 0x12)) {
                g->list_fc = c;
                g->key_code = PK_SC_5;
            }
        }
        return;
    }
    for (r = 0; r < 4; r++) {
        int y = 0x1c + r * 0x12;
        if (pk_tap(g, 4, y, 0x13, 0x12)) {
            g->sel = r;
            g->sel2 = 0;
            g->key_code = PK_SC_5;
        }
        if (pk_tap(g, 0x17, y, 0x13, 0x12)) {
            g->sel = r;
            g->sel2 = 1;
            g->key_code = PK_SC_5;
        }
        if (pk_tap(g, 0x2c, y, 0x66, 0x12)) {
            g->sel = r;
            g->sel2 = 2;
            g->key_code = PK_SC_5;
        }
    }
    for (c = 3; c <= 6; c++) {
        if (pk_tap(g, 7 + (c - 3) * 0x13, 0x66, 0x13, 0x12)) {
            g->sel = 4;
            g->sel2 = c;
            g->key_code = PK_SC_5;
        }
    }
    if (pk_tap(g, 0x5d, 0x66, 0x48, 0x12)) {
        g->sel = 4;
        g->sel2 = 7;
        g->key_code = PK_SC_5;
    }
    if (pk_tap(g, 0xa, 0x7c, 0x46, 0x12)) {
        g->sel = 5;
        g->sel2 = 8;
        g->key_code = PK_SC_5;
    }
    if (pk_tap(g, 0x5e, 0x7c, 0x48, 0x12)) {
        g->sel = 5;
        g->sel2 = 9;
        g->key_code = PK_SC_5;
    }
}

static void edit_cell_text(pk_object *o, pk_game *g)
{
    int *tm = team(g, g->team_sel);
    if (o->arg1 == 10 && tm[8] != 0xff) {
        pk_game_mestext(g, pk_app_i32(TBL_ORDER_LABEL + (Uint32)tm[8] * 4u), o->x, o->y + 4, 0, PK_ALIGN_CENTER, 0,
                        PK_FONT_LEGEND);
    } else if (o->arg1 == 11) {
        pk_game_mestext(g, 0x24, o->x, o->y + 4, 0, PK_ALIGN_CENTER, 0, PK_FONT_LEGEND); /* Clear */
    } else if (o->arg1 == 12) {
        pk_game_mestext(g, 0x96, o->x, o->y + 4, 0, PK_ALIGN_CENTER, 0, PK_FONT_LEGEND); /* Done */
    }
}

static void team_edit_info(pk_game *g)
{
    int *tm = team(g, g->team_sel), u;
    Uint16 name[25];
    if (g->sel >= 4) {
        return;
    }
    u = tm[g->sel];
    if (u == 0xff) {
        return;
    }
    if (g->sel2 == 0) {
        shadow_mes(g, 0x1f, 0x30, 0xa6, COL_WHITE, PK_ALIGN_RIGHT);
        shadow_text(g, pk_r.unit[g->sel].name, 0x32, 0xa6, COL_WHITE, PK_ALIGN_LEFT);
        shadow_mes(g, 0x25, 0x30, 0xb3, COL_WHITE, PK_ALIGN_RIGHT);
        unit_name16(g, u, name, 25);
        shadow_text(g, name, 0x32, 0xb3, COL_WHITE, PK_ALIGN_LEFT);
        shadow_mes(g, 0x26, 0x30, 0xc0, COL_WHITE, PK_ALIGN_RIGHT);
        shadow_num(g, "%d", unit(g, u)[1], 0, 0x32, 0xc0, COL_WHITE, PK_ALIGN_LEFT);
        shadow_mes(g, 0x30, 0x30, 0xcd, COL_WHITE, PK_ALIGN_RIGHT);
        shadow_num(g, "%d", unit(g, u)[17], 0, 0x32, 0xcd, COL_WHITE, PK_ALIGN_LEFT);
    } else if (g->sel2 == 1 && unit(g, u)[2] != 0xff) {
        int e = unit(g, u)[2], x;
        switch (g->language) {
        case 1:
            x = 0x23;
            break;
        case 2:
        case 3:
            x = 0x2a;
            break;
        case 4:
            x = 0x2d;
            break;
        default:
            x = 0x2f;
            break;
        }
        pk_item_preview(g->world.equip[e][0], g->world.equip[e][1], 9);
        shadow_mes(g, 0x25, x, 0xa6, COL_WHITE, PK_ALIGN_RIGHT);
        shadow_text(g, pk_r.equip[9].name, x, 0xa6, COL_WHITE, PK_ALIGN_LEFT);
        if (g->world.equip[e][0] == 5) {
            shadow_mes(g, 0x27, 0x36, 0xcd, COL_WHITE, PK_ALIGN_RIGHT);
            shadow_num(g, "ATK %d UP", pk_r.equip[9].rec[7], 0, 0x36, 0xcd, COL_WHITE, PK_ALIGN_LEFT);
        } else if (g->world.equip[e][0] == 6) {
            shadow_mes(g, 0x27, 0x36, 0xcd, COL_WHITE, PK_ALIGN_RIGHT);
            shadow_num(g, "DEF %d UP", pk_r.equip[9].rec[8], 0, 0x36, 0xcd, COL_WHITE, PK_ALIGN_LEFT);
        }
    }
}

static void team_edit_child(pk_object *o, pk_game *g)
{
    int *tm = team(g, g->team_sel), slot, u, x, y;
    pk_object *ref;
    switch (o->state) {
    case 3: /* grid cell, arg1 = id */
        pk_ui_setup_sprite(g, o, IMG_UI, TBL_UI, 0x4c1);
        o->frame = edit_frame(g, o->arg1);
        pk_object_clear_work(o);
        pk_ui_start_offset(o);
        o->state = 4;
        /* fallthrough */
    case 4:
    case 5:
    case 6:
        o->frame = edit_frame(g, o->arg1);
        if (edit_visible(g, o->arg1)) {
            o->flags |= PK_OBJ_SPRITE;
        } else {
            o->flags &= ~(Uint32)PK_OBJ_SPRITE;
        }
        if (o->state == 4) {
            pk_ui_slide_in(o);
            if (++o->counter > 5) {
                o->counter = 0;
                o->state = 5;
            }
        } else if (o->state == 5) {
            slide_child_common(o, g, team_edit_update, 0, 6);
        } else if (pk_ui_slide_out(o, g)) {
            pk_object_kill(o);
            break;
        }
        edit_cell_text(o, g);
        break;
    case 7: /* per-slot icons: arg0 = slot + 1 (or 7..10 in the order row), arg1 = kind */
        o->state = 8;
        if (o->arg1 == 1) {
            pk_ui_setup_sprite(g, o, IMG_UI, TBL_UI, 0x4c0);
        } else if (o->arg1 == 0) {
            pk_ui_setup_sprite(g, o, IMG_UNIT_ICON, TBL_UNIT_ICONS, 0x4c0);
        } else {
            o->flags = 0x440;
        }
        o->work[0] = -1;
        /* fallthrough */
    case 8:
        ref = pk_object_find(g, o->type, 0xb0, o->arg0 - 1);
        if (!ref || ref->dead) {
            pk_object_kill(o);
            break;
        }
        slot = o->arg0 < 7 ? o->arg0 - 1 : o->arg0 - 7;
        u = tm[slot & 3];
        if (o->arg1 == 0) {
            if (u == 0xff) {
                o->flags &= ~(Uint32)PK_OBJ_SPRITE;
                o->work[0] = -1;
            } else {
                o->flags |= PK_OBJ_SPRITE;
                if (u != o->work[0]) {
                    pk_unit_template(unit(g, u)[0], slot & 3);
                    o->work[1] = 0;
                    o->work[2] = pk_r.unit[slot & 3].b[0] * 2;
                    o->work[0] = u;
                }
                icon_anim(o);
            }
            if (o->arg0 < 7) {
                o->x = ref->x - 0x54;
                o->y = ref->y - 10;
            } else {
                o->x = ref->x - 8;
                o->y = ref->y - 9;
            }
        } else if (o->arg1 == 1) {
            int e = u != 0xff ? unit(g, u)[2] : 0xff;
            if (e == 0xff) {
                o->flags &= ~(Uint32)PK_OBJ_SPRITE;
            } else {
                int k = g->world.equip[e][0];
                o->flags |= PK_OBJ_SPRITE;
                o->frame = k == 5 ? 0x37 : k == 6 ? 0x38
                                                  : 0x39;
            }
            o->x = ref->x - 0x41;
            o->y = ref->y - 10;
        } else {
            o->x = ref->x - 0x28;
            o->y = ref->y + 4;
            if (u != 0xff && tm[4 + (slot & 3)] != 0xff) {
                pk_game_mestext(g, pk_app_i32(TBL_TARGET_LABEL + (Uint32)tm[4 + (slot & 3)] * 4u), o->x, o->y, 0,
                                PK_ALIGN_LEFT, 0, PK_FONT_LEGEND);
            }
        }
        break;
    case 9: /* cursor */
        pk_ui_setup_sprite(g, o, IMG_UI, TBL_UI, 0x4c1);
        o->frame = 0;
        o->layer = 1;
        pk_ui_cursor_anim(g, o);
        pk_object_clear_work(o);
        edit_cursor_pos(g, &x, &y);
        o->x = x + (o->type == 0 ? 0xc0 : -0xc0);
        o->y = y;
        o->counter = 0;
        o->state = 10;
        break;
    case 10:
        pk_object_animate(o);
        pk_ui_slide_in(o);
        if (++o->counter > 5) {
            o->counter = 0;
            o->state = 0xb;
        }
        break;
    case 0xb:
        pk_object_animate(o);
        edit_cursor_pos(g, &o->x, &o->y);
        slide_child_common(o, g, team_edit_update, 0, 0xc);
        break;
    case 0xc:
        pk_object_animate(o);
        if (pk_ui_slide_out(o, g)) {
            pk_object_kill(o);
        }
        break;
    case 0xd: /* target of a position swap */
        pk_ui_setup_sprite(g, o, IMG_UI, TBL_UI, 0x4c1);
        o->frame = 0;
        o->layer = 1;
        pk_object_set_anim(o, TBL_SWAP_ANIM);
        o->frame = pk_app_i32(o->anim);
        o->state = 0xe;
        /* fallthrough */
    case 0xe:
        pk_object_animate(o);
        o->x = g->list_fc * 0x13 + 0xe;
        o->y = 100;
        if (!g->list_124) {
            pk_object_kill(o);
        }
        break;
    case 0x80:
        pk_ui_setup_sprite(g, o, IMG_UI, TBL_UI, 0x4c0);
        o->frame = 0x93;
        o->state = 0x81;
        /* fallthrough */
    case 0x81:
    {
        pk_object *root = pk_ui_root(g);
        if (root && root->update != team_edit_update) {
            pk_object_kill(o);
            break;
        }
        if (g->sel < 4 && g->sel2 < 2 && root && root->state == 2) {
            o->flags |= PK_OBJ_SPRITE;
        } else {
            o->flags &= ~(Uint32)PK_OBJ_SPRITE;
        }
        break;
    }
    case 0x90:
    case 0x91:
    {
        pk_object *root = pk_ui_root(g);
        int t;
        if (root && root->update != team_edit_update) {
            pk_object_kill(o);
            break;
        }
        u = g->sel < 4 ? tm[g->sel] : 0xff;
        t = u != 0xff ? unit(g, u)[0] : 0xff;
        if (t == 0xff || !(g->sel2 < 2 && root && root->state == 2)) {
            o->flags &= ~(Uint32)PK_OBJ_SPRITE;
        } else if (o->state == 0x90 || t != o->work[0]) {
            portrait(g, o, t, TBL_PORTRAIT);
            o->layer = 1;
        } else {
            o->flags |= PK_OBJ_SPRITE;
        }
        o->work[0] = t;
        o->state = 0x91;
        break;
    }
    default:
        break;
    }
}

static void team_edit_update(pk_object *o, pk_game *g)
{
    static const struct
    {
        int id, x, y;
    } cells[] = {
        { 0, 0x58, 0x25 },
        { 1, 0x58, 0x37 },
        { 2, 0x58, 0x49 },
        { 3, 0x58, 0x5b },
        { 6, 0x10, 0x71 },
        { 7, 0x23, 0x71 },
        { 8, 0x36, 0x71 },
        { 9, 0x49, 0x71 },
        { 10, 0x80, 0x71 },
        { 11, 0x2e, 0x85 },
        { 12, 0x82, 0x85 },
        { 0x30, 0xd, 0x25 },
        { 0x31, 0xd, 0x37 },
        { 0x32, 0xd, 0x49 },
        { 0x33, 0xd, 0x5b },
        { 0x40, 0x21, 0x25 },
        { 0x41, 0x21, 0x37 },
        { 0x42, 0x21, 0x49 },
        { 0x43, 0x21, 0x5b },
        { 0x50, 0x5f, 0x25 },
        { 0x51, 0x5f, 0x37 },
        { 0x52, 0x5f, 0x49 },
        { 0x53, 0x5f, 0x5b },
        { 4, 0x58, 0x16 },
        { 5, 0x58, 0x65 },
    };
    int i, j, *tm = team(g, g->team_sel);
    if (o->type != 0xc2) {
        team_edit_child(o, g);
        return;
    }
    switch (o->state) {
    case 0:
        pk_object_spawn(g, team_edit_update, g->slide, 0x80, 0, 0, 0, 0x9a);
        pk_object_spawn(g, team_edit_update, g->slide, 0x90, 0, 0, 0x98, 0xa3);
        pk_object_spawn(g, team_edit_update, g->slide, 0x90, 0, 1, 0x98, 0xbb);
        for (i = 0; i < (int)SDL_arraysize(cells); i++) {
            pk_object_spawn(g, team_edit_update, g->slide, 3, 0xb0, cells[i].id, cells[i].x, cells[i].y);
        }
        for (i = 0; i < 16; i++) {
            pk_object_spawn(g, team_edit_update, g->slide, 3, 0xb0, 0x10 + i, 0x96 + (i % 4) * 6, 0x1e + (i / 4) * 0x12);
        }
        for (i = 0; i < 6; i++) {
            pk_object_spawn(g, team_edit_update, g->slide, 3, 0xb0, 0x20 + i, 0x89 + i * 6, 0x12);
        }
        for (i = 1; i <= 4; i++) {
            for (j = 0; j < 3; j++) {
                pk_object_spawn(g, team_edit_update, g->slide, 7, i, j, 0xe, 0x25 + (i - 1) * 0x12);
            }
        }
        for (i = 7; i <= 10; i++) {
            pk_object_spawn(g, team_edit_update, g->slide, 7, i, 0, 0x10 + (i - 7) * 0x13, 0x71);
        }
        pk_object_spawn(g, team_edit_update, g->slide, 9, 0, 1, 0xe, 0x14);
        o->counter = 0;
        o->state = 1;
        break;
    case 1:
        if (++o->counter > 5) {
            o->counter = 0;
            o->state = 2;
        }
        break;
    case 2:
        edit_touch(g);
        if (g->list_124) { /* choosing the slot to swap with (unit list state 0x70) */
            if (g->key_code == PK_SC_LEFT || g->key_code == PK_SC_RIGHT) {
                pk_audio_play_sfx(2);
                g->list_fc = (g->list_fc + (g->key_code == PK_SC_LEFT ? 3 : 1)) % 4;
            } else if (g->key_code == PK_SC_5 || g->key_code == PK_SC_BACK) {
                int a = g->sel2 - 3, b = g->list_fc;
                pk_audio_play_sfx(g->key_code == PK_SC_5 ? 0 : 1);
                if (g->key_code == PK_SC_5 && a != b) { /* Form state 0x19 */
                    int ua = tm[a], ub = tm[b], ta = tm[4 + a];
                    tm[a] = ub;
                    tm[4 + a] = tm[4 + b];
                    tm[b] = ua;
                    tm[4 + b] = ta;
                    if (ub != 0xff) {
                        unit(g, ub)[3] = a | (g->team_sel << 4);
                    }
                    if (ua != 0xff) {
                        unit(g, ua)[3] = b | (g->team_sel << 4);
                    }
                    first_leader(g, g->team_sel);
                }
                g->list_124 = 0;
            }
            break;
        }
        if (g->key_code == PK_SC_LEFT || g->key_code == PK_SC_RIGHT) {
            bool left = g->key_code == PK_SC_LEFT;
            pk_audio_play_sfx(2);
            if (g->sel < 4) {
                g->sel2 = left ? (g->sel2 == 0 ? 2 : g->sel2 - 1) : (g->sel2 + 1) % 3;
            } else if (g->sel == 4) {
                g->sel2 = left ? (g->sel2 == 3 ? 7 : g->sel2 - 1) : (g->sel2 == 7 ? 3 : g->sel2 + 1);
            } else {
                g->sel2 = g->sel2 == 8 ? 9 : 8;
            }
        } else if (g->key_code == PK_SC_UP || g->key_code == PK_SC_DOWN) {
            bool up = g->key_code == PK_SC_UP;
            pk_audio_play_sfx(2);
            g->sel = up ? (g->sel == 0 ? 5 : g->sel - 1) : (g->sel == 5 ? 0 : g->sel + 1);
            if (g->sel > 3) {
                g->sel2 = g->sel == 4 ? 3 : 9;
            } else if ((up && g->sel == 3) || (!up && g->sel == 0)) {
                g->sel2 = 0;
            }
        } else if (g->key_code == PK_SC_5) {
            bool ok = true;
            if (g->sel < 4) {
                if (g->sel2 == 1 && tm[g->sel] == 0xff) {
                    ok = false;
                    pk_audio_play_sfx(1);
                } else if (g->sel2 == 2 && tm[g->sel] == 0xff) {
                    ok = false;
                    pk_audio_play_sfx(1);
                }
            } else if (g->sel2 >= 3 && g->sel2 <= 6) {
                pk_audio_play_sfx(0);
                g->list_fc = g->sel2 - 3 == 0 ? 1 : 0;
                g->list_124 = 1;
                pk_object_spawn(g, team_edit_update, g->slide, 0xd, 0, 2, 0xe, 0x14);
                ok = false;
            } else if (g->sel2 == 8) { /* Clear the team */
                pk_audio_play_sfx(0);
                for (i = 0; i < 4; i++) {
                    if (tm[i] != 0xff) {
                        unequip(g, tm[i]);
                        unit(g, tm[i])[4] = 0;
                        unit(g, tm[i])[3] = 0xff;
                    }
                    tm[i] = 0xff;
                    tm[4 + i] = 5;
                }
                tm[11] = 6;
                tm[8] = 0;
                tm[13] = 0;
                pk_team_update_rank(g, g->team_sel);
                if (g->team_sel == g->world.h_dc) {
                    g->world.h_dc = 0xff;
                }
                ok = false;
            }
            if (ok) {
                pk_audio_play_sfx(0);
                g->dlg_result = 0;
                pk_object_return(g, o);
                return;
            }
        } else if (g->key_code == PK_SC_BACK) {
            pk_audio_play_sfx(1);
            g->dlg_result = 1;
            pk_object_return(g, o);
            return;
        }
        team_edit_info(g);
        break;
    default:
        break;
    }
}

/* ------------------------------------------------------------------ */
/* Form root (0x100493c4) */

static void form_hint(pk_game *g)
{
    (void)g;
}

static void call(pk_object *o, pk_game *g, pk_object_fn fn, int ret_state)
{
    pk_object_call(g, o, fn, 0, pk_form_update, ret_state);
}

static void spawn_top_bar(pk_object *o, pk_game *g)
{
    pk_object_spawn(g, pk_kingdom_ui_update, o->dir, 0x80, 1, 2, 0x10, 3);
    pk_object_spawn(g, pk_kingdom_ui_update, o->dir, 0x80, 1, 3, 0x61, 3);
    pk_object_spawn(g, pk_kingdom_ui_update, o->dir, 0x80, 1, 4, 0xa1, 3);
    pk_object_spawn(g, pk_kingdom_ui_update, o->dir, 0x80, 0, 0x7a, 0, 0x92);
}

static void to_edit(pk_object *o)
{
    o->counter = 0;
    o->state = 0xd;
}

void pk_form_update(pk_object *o, pk_game *g)
{
    int *tm = team(g, g->team_sel), u, i;
    g->dlg_hint = form_hint;
    switch (o->state) {
    case 0xc: /* enter */
        if (g->curtain != PK_CURTAIN_HELD || ++o->counter < 4) {
            break;
        }
        pk_game_set_curtain(g, PK_CURTAIN_OPEN);
        pk_object_spawn(g, pk_kingdom_ui_update, o->dir, 0xd0, 0, 0, 0, 0xc2);
        spawn_top_bar(o, g);
        g->field_bg = IMG_FORM_BG;
        pk_game_set_bg(g, 0, IMG_FORM_BG);
        g->bg_visible = 1;
        g->dlg_type = 0;
        g->slide = 0;
        g->team_sel = 0;
        g->list_124 = 0;
        g->list_top = g->cursor = 0;
        pk_units_sort(g, true);
        o->counter = 0;
        o->state = 0x10;
        break;
    case 0x10: /* team list */
        if (g->curtain != PK_CURTAIN_NONE) {
            break;
        }
        g->list_120 = g->list_124 = 0;
        call(o, g, pk_team_list_update, 0x11);
        break;
    case 0x11:
        if (g->dlg_result == 0) {
            g->team_sel = g->list_top + g->cursor;
            g->sel = g->sel2 = 0;
            g->slide = 0;
            to_edit(o);
        } else {
            pk_game_set_curtain(g, PK_CURTAIN_CLOSE);
            o->counter = 0;
            g->slide = 2;
            o->state = 0xf;
        }
        break;
    case 0xd:
        o->counter = 0;
        g->list_124 = 0;
        call(o, g, team_edit_update, 0xe);
        break;
    case 0xe:
        if (g->dlg_result != 0 || (g->sel == 5 && g->sel2 == 9)) {
            g->slide = 1;
            pk_team_update_rank(g, g->team_sel);
            g->list_top = g->team_sel < 4 ? 0 : 4;
            g->cursor = g->team_sel - g->list_top;
            o->state = 0x10;
            break;
        }
        g->list_top = g->cursor = 0;
        g->slide = 0;
        if (g->sel < 4) {
            if (g->sel2 == 0) {
                g->dlg_type = 2;
                call(o, g, pk_dialog_update, 0x12);
            } /* Select / Clear */
            else if (g->sel2 == 1) {
                g->dlg_type = 5;
                call(o, g, pk_dialog_update, 0x14);
            } /* equipment */
            else {
                g->dlg_type = 0;
                call(o, g, pk_dialog_update, 0x17);
            } /* targeting */
        } else if (g->sel == 4 && g->sel2 == 7) {
            g->dlg_type = 1;
            call(o, g, pk_dialog_update, 0x18); /* order */
        } else {
            to_edit(o);
        }
        break;
    case 0x12: /* Select / Clear unit */
        if (g->dlg_result != 0) {
            g->slide = 1;
            to_edit(o);
            break;
        }
        g->slide = 0;
        if (g->cursor == 0) {
            u = tm[g->sel];
            g->list_top = g->cursor = 0;
            if (u != 0xff) {
                /* FUN_100284ec: start the list at the unit in the slot */
                int n = g->world.unit_count;
                if (u < 5) {
                    g->list_top = 0;
                    g->cursor = u;
                } else if (u < n - 5) {
                    g->list_top = u;
                    g->cursor = 0;
                } else {
                    g->list_top = n - 5;
                    g->cursor = u - (n - 5);
                }
                if (g->cursor < 0) {
                    g->cursor = 0;
                }
            }
            g->list_120 = g->list_124 = 0;
            pk_units_sort(g, true);
            call(o, g, pk_unit_list_update, 0x13);
        } else {
            slot_clear(g, g->team_sel, g->sel);
            tm[g->sel] = 0xff;
            tm[4 + g->sel] = 5;
            pk_team_update_rank(g, g->team_sel);
            g->slide = 1;
            for (i = 0; i < 4 && tm[i] == 0xff; i++) {
            }
            if (i < 4) {
                if (tm[9] == g->sel) {
                    tm[9] = i;
                }
            } else {
                tm[13] = 0;
                if (g->team_sel == g->world.h_dc) {
                    g->world.h_dc = 0xff;
                }
            }
            to_edit(o);
        }
        break;
    case 0x13: /* unit chosen */
        if (g->dlg_result == 0) {
            int nu = g->list_top + g->cursor;
            slot_clear(g, g->team_sel, g->sel);
            tm[13] = 1;
            tm[g->sel] = nu;
            tm[11] -= unit(g, nu)[17];
            unit(g, nu)[4] = 1;
            unit(g, nu)[3] = g->sel | (g->team_sel << 4);
            pk_team_update_rank(g, g->team_sel);
            pk_units_sort(g, true);
            first_leader(g, g->team_sel);
        }
        g->slide = 1;
        to_edit(o);
        break;
    case 0x14: /* equipment Select / Clear */
        if (g->dlg_result != 0) {
            g->slide = 1;
            to_edit(o);
            break;
        }
        if (g->cursor == 0) {
            g->dlg_type = 9;
            g->list_top = 0;
            call(o, g, pk_dialog_update, 0x15);
        } else {
            if (tm[g->sel] != 0xff) {
                unequip(g, tm[g->sel]);
            }
            g->slide = 1;
            to_edit(o);
        }
        break;
    case 0x15: /* Weapons / Armor / Accessories */
        if (g->dlg_result != 0) {
            g->slide = 1;
            to_edit(o, g);
            break;
        }
        g->dlg_type = 5 + g->cursor;
        g->team_sel |= 0; /* team stays in list_f8 */
        g->slide = 0;
        g->list_top = g->cursor = 0;
        pk_equip_sort(g, true);
        call(o, g, pk_item_list_update, 0x16);
        break;
    case 0x16: /* equipment chosen */
        if (g->dlg_result == 0) {
            int nu = tm[g->sel];
            int e = pk_item_find(g, g->dlg_type, g->list_top + g->cursor);
            if (nu != 0xff && e != 0xff) {
                unequip(g, nu);
                unit(g, nu)[2] = e;
                g->world.equip[e][10] = nu;
                g->world.equip[e][11] = 1;
                pk_equip_sort(g, true);
            }
        }
        g->slide = 1;
        to_edit(o);
        break;
    case 0x17: /* targeting */
        if (g->dlg_result == 0) {
            tm[4 + g->sel] = g->cursor;
        }
        g->slide = 1;
        to_edit(o);
        break;
    case 0x18: /* order */
        if (g->dlg_result == 0) {
            tm[8] = g->cursor;
        }
        g->slide = 1;
        to_edit(o);
        break;
    case 0xf: /* back to the kingdom screen */
        if (g->curtain == PK_CURTAIN_HELD) {
            g->cam_x = g->cam_y = 0;
            pk_object_kill_others(g, o);
            g->field_bg = g->fame > 300 ? 0xe : g->fame < 0x97 ? 0xf
                                                               : 9;
            pk_game_set_bg(g, 0, g->field_bg);
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

/* ------------------------------------------------------------------ */
/* Party > Medical (0x10045630) */

static void medical_hint(pk_game *g)
{
    if (g->call_ret_state == 4) {                                   /* Yes / No for healing everyone */
        shadow_mes(g, 0x22, 0x58, 0xb4, COL_WHITE, PK_ALIGN_RIGHT); /* Cost : */
        shadow_num(g, "%d", pk_heal_cost_all(g), 0, 0x58, 0xb4, COL_WHITE, PK_ALIGN_LEFT);
    }
}

void pk_medical_update(pk_object *o, pk_game *g)
{
    g->dlg_hint = medical_hint;
    switch (o->state) {
    case 0x20: /* enter (state 0 of the original) */
        if (g->curtain != PK_CURTAIN_HELD || ++o->counter < 4) {
            break;
        }
        pk_game_set_curtain(g, PK_CURTAIN_OPEN);
        pk_object_spawn(g, pk_kingdom_ui_update, o->dir, 0xd0, 0, 0, 0, 0xc2);
        spawn_top_bar(o, g);
        g->field_bg = 0xb;
        pk_game_set_bg(g, 0, 0xb);
        g->bg_visible = 1;
        g->slide = 0;
        o->state = 1;
        break;
    case 1:
        if (g->curtain != PK_CURTAIN_NONE) {
            break;
        }
        o->counter = 0;
        g->dlg_type = 4; /* All / Party / Unit */
        g->list_top = g->cursor = 0;
        pk_object_call(g, o, pk_dialog_update, 0, pk_medical_update, 2);
        break;
    case 2:
        if (g->dlg_result == 0) {
            o->counter = 0;
            g->slide = 0;
            if (g->cursor == 0) {
                g->dlg_type = 0xc;
                g->list_top = 0;
                pk_object_call(g, o, pk_dialog_update, 0, pk_medical_update, 4);
            } else {
                int which = g->cursor;
                g->list_top = g->cursor = 0;
                g->list_120 = 1;
                g->list_124 = 0;
                if (which == 1) {
                    pk_object_call(g, o, pk_team_list_update, 0, pk_medical_update, 5);
                } else {
                    pk_object_call(g, o, pk_unit_list_update, 0, pk_medical_update, 6);
                }
            }
        } else {
            pk_game_set_curtain(g, PK_CURTAIN_CLOSE);
            g->slide = 2;
            o->counter = 0;
            o->state = 3;
        }
        break;
    case 3: /* back to the kingdom */
        if (g->curtain == PK_CURTAIN_HELD) {
            pk_object_kill_others(g, o);
            g->field_bg = g->fame > 300 ? 0xe : g->fame < 0x97 ? 0xf
                                                               : 9;
            pk_game_set_bg(g, 0, g->field_bg);
            o->state = 1;
            o->work[1] = 0;
            g->dlg_hint = NULL;
            pk_object_set_update(o, pk_kingdom_update);
        }
        break;
    case 4:
        if (g->dlg_result == 0 && g->cursor == 0) {
            int cost = pk_heal_cost_all(g);
            if (cost < g->loot) {
                pk_loot_sub(g, cost);
                pk_heal_all(g);
            }
            pk_game_set_curtain(g, PK_CURTAIN_CLOSE);
            g->slide = 2;
            o->counter = 0;
            o->state = 3;
        } else {
            if (g->dlg_result == 1 || g->cursor == 1) {
                g->slide = 1;
            }
            o->state = 1;
        }
        break;
    case 5:
    case 6:
        g->list_120 = 0;
        g->slide = 1;
        o->state = 1;
        break;
    default:
        break;
    }
}

/* ------------------------------------------------------------------ */
/* Party > Defense (0x10036b8c): defense party (+0xdc), strategy / trap
 * (+0x4f4) and battlefield (+0xd4) for enemy invasions. */

void pk_defense_update(pk_object *o, pk_game *g)
{
    g->dlg_hint = NULL;
    switch (o->state) {
    case 0xc: /* enter */
        if (g->curtain != PK_CURTAIN_HELD || ++o->counter < 4) {
            break;
        }
        pk_game_set_curtain(g, PK_CURTAIN_OPEN);
        spawn_top_bar(o, g);
        g->list_f8 = g->list_fc = 0;
        o->state = 0xd;
        break;
    case 0xd:
        if (g->curtain != PK_CURTAIN_NONE) {
            break;
        }
        g->dlg_type = 3; /* Defense Party / Strategy / Battlefield */
        g->list_top = g->list_f8;
        g->cursor = g->list_fc;
        o->counter = 0;
        pk_object_call(g, o, pk_dialog_update, 0, pk_defense_update, 0xf);
        break;
    case 0xf:
        if (g->dlg_result == 0) {
            int sel = g->cursor;
            g->list_f8 = g->list_top;
            g->list_fc = g->cursor;
            g->list_top = g->cursor = 0;
            g->slide = 0;
            o->counter = 0;
            if (sel == 0) {
                g->list_120 = 1;
                g->list_124 = 0;
                pk_object_call(g, o, pk_team_list_update, 0, pk_defense_update, 0x11);
            } else if (sel == 1) {
                g->dlg_type = 7;
                pk_object_call(g, o, pk_dialog_update, 0, pk_defense_update, 0x13);
            } else {
                g->dlg_type = 6;
                pk_object_call(g, o, pk_dialog_update, 0, pk_defense_update, 0x12);
            }
        } else {
            pk_game_set_curtain(g, PK_CURTAIN_CLOSE);
            o->counter = 0;
            g->slide = 2;
            o->state = 0x10;
        }
        break;
    case 0x10:
        if (g->curtain == PK_CURTAIN_HELD) {
            pk_object_kill_others(g, o);
            o->work[1] = 0;
            o->state = 1;
            pk_object_set_update(o, pk_kingdom_update);
        }
        break;
    case 0x11:
        if (g->dlg_result == 0) {
            g->world.h_dc = g->list_top + g->cursor;
        }
        g->list_120 = 0;
        g->slide = 1;
        o->state = 0xd;
        break;
    case 0x12:
        if (g->dlg_result == 0) {
            g->world.h_d4 = g->list_top + g->cursor;
        }
        g->slide = 1;
        o->state = 0xd;
        break;
    case 0x13:
        if (g->dlg_result == 0) {
            g->world.h_4f4 = g->list_top + g->cursor;
        }
        g->slide = 1;
        o->state = 0xd;
        break;
    default:
        break;
    }
}
