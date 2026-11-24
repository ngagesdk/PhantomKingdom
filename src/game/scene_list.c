/* SPDX-License-Identifier: MIT */
/* Party > List (0x1003d8a0): look through what the kingdom owns.
 *
 *   dialog 0xd  Item / Unit
 *   Item -> dialog 0xb (kind of thing) -> item list 0x10039b34 ->
 *           detail 0x1003c224 -> back to the list (0x1003bf3c)
 *   Unit -> unit list 0x1004a4a0 (info mode) -> detail (0x1005fc0c)
 *
 * Names and texts come from the user's data files. */
#include "audio/pk_audio.h"
#include "core/pk_app.h"
#include "core/pk_log.h"
#include "game/pk_data.h"
#include "game/pk_help.h"
#include "game/pk_rules.h"
#include "game/pk_scenes.h"
#include "game/pk_world.h"
#include "game/ui_dialog.h"
#include "gfx/pk_font.h"
#include <SDL3/SDL.h>

static void spawn_top_bar(pk_object *o, pk_game *g)
{
    pk_object_spawn(g, pk_kingdom_ui_update, o->dir, 0x80, 1, 2, 0x10, 3);
    pk_object_spawn(g, pk_kingdom_ui_update, o->dir, 0x80, 1, 3, 0x61, 3);
    pk_object_spawn(g, pk_kingdom_ui_update, o->dir, 0x80, 1, 4, 0xa1, 3);
    pk_object_spawn(g, pk_kingdom_ui_update, o->dir, 0x80, 0, 0x7a, 0, 0x92);
}

/* ---- the item's page (0x1003c224) ---------------------------------
 * a white window (rows 0x40..0x42 of image 0x17, frames 0x100930c8,
 * y 0xe..0x86), the kind's mark (frame 0x10093064[kind]) at 0x1e, 0x1d,
 * ten rank stars (0x3e lit / 0x3f) at 0x18 + 6 * i, 0x33 and the texts of
 * FUN_1003d280. */
static struct
{
    int kind, idx, unit;
} s_det;

static void det_mes(pk_game *g, int id, int x, int y, pk_align a)
{
    pk_game_mestext(g, id, x, y, 0, a, 1, PK_FONT_DENSE);
}

static void det_num(pk_game *g, const char *fmt, int v, int w, int x, int y)
{
    char buf[24];
    SDL_snprintf(buf, sizeof(buf), fmt, v, w);
    pk_drawq_text_utf8(&g->drawq, buf, x, y, 0, PK_ALIGN_LEFT, 1, PK_FONT_DENSE);
}

void pk_item_detail_update(pk_object *o, pk_game *g);

static void item_det_obj(pk_object *o, pk_game *g)
{
    pk_object *root = pk_ui_root(g);
    if (!root || root->update != pk_item_detail_update) {
        pk_object_kill(o);
        return;
    }
    o->image = pk_game_load_image(g, 0x17, 1);
    pk_object_set_frames(o, 0x100930c8u);
    o->flags = 0x4c1;
    o->layer = o->state == 0x10 ? 0 : 1;
    if (o->state == 0x10) {
        o->frame = o->arg1 + 0x40;
    } else if (o->state == 0x20) {
        o->frame = (int)pk_app_i32(0x10093064u + (Uint32)s_det.kind * 4u);
    } else { /* a star */
        int rank = s_det.kind < PK_KIND_EQUIP ? pk_r.item[10].rec[6] : pk_r.equip[10].rec[5];
        o->frame = rank < o->arg1 ? 0x3f : 0x3e;
    }
}

static void item_det_texts(pk_game *g)
{
    static const int k_kind[5] = { 127, 128, 125, 126, 44 };
    int x = 0x1e;
    if (s_det.kind < PK_KIND_EQUIP) {
        pk_drawq_text(&g->drawq, pk_r.item[10].name, x + 0x14, 0x28, 0, PK_ALIGN_LEFT, 1, PK_FONT_DENSE);
        det_mes(g, 0x11, x + 0x12, 0x3c, PK_ALIGN_RIGHT);
        det_mes(g, 0x17, x + 0x12, 0x4d, PK_ALIGN_RIGHT);
        det_mes(g, k_kind[s_det.kind], x + 0x15, 0x4d, PK_ALIGN_LEFT);
        det_mes(g, 0x18, x + 0x12, 0x5c, PK_ALIGN_RIGHT);
        det_num(g, "%d", pk_r.item[10].price, 0, x + 0x15, 0x5c);
        return;
    }
    {
        int *e = g->world.equip[s_det.idx];
        Uint16 t[25];
        pk_drawq_text(&g->drawq, pk_r.equip[10].name, x + 0x14, 0x28, 0, PK_ALIGN_LEFT, 1, PK_FONT_DENSE);
        det_mes(g, 0x11, x + 0x12, 0x3c, PK_ALIGN_RIGHT);
        det_mes(g, 0x1a, x + 0x25, 0x49, PK_ALIGN_RIGHT);
        det_num(g, "%d", pk_r.equip[10].rec[9], 0, x + 0x2a, 0x49);
        if (s_det.kind == 5) {
            det_mes(g, 0x1b, x + 0x25, 0x54, PK_ALIGN_RIGHT);
            det_num(g, "%d", pk_r.equip[10].rec[7], 0, x + 0x2a, 0x54);
        } else if (s_det.kind == 6) {
            det_mes(g, 0x1c, x + 0x25, 0x54, PK_ALIGN_RIGHT);
            det_num(g, "%d", pk_r.equip[10].rec[8], 0, x + 0x2a, 0x54);
        }
        det_mes(g, 0x15, x + 0x25, 0x5f, PK_ALIGN_RIGHT);
        pk_item_attr_name(1, e[3], t, 25);
        pk_drawq_text(&g->drawq, t, x + 0x2a, 0x5f, 0, PK_ALIGN_LEFT, 1, PK_FONT_DENSE);
        pk_item_attr_name(1, e[4], t, 25);
        pk_drawq_text(&g->drawq, t, x + 0x2a, 0x6a, 0, PK_ALIGN_LEFT, 1, PK_FONT_DENSE);
        det_mes(g, 0x16, x + 0x25, 0x75, PK_ALIGN_RIGHT);
        pk_item_attr_name(0, e[2], t, 25);
        pk_drawq_text(&g->drawq, t, x + 0x2a, 0x75, 0, PK_ALIGN_LEFT, 1, PK_FONT_DENSE);
        det_mes(g, 0x18, x + 0x25, 0x80, PK_ALIGN_RIGHT);
        det_num(g, "%d", pk_r.equip[10].price, 0, x + 0x2a, 0x80);
    }
}

void pk_item_detail_update(pk_object *o, pk_game *g)
{
    int i;
    switch (o->state) {
    case 0:
        if (s_det.kind < PK_KIND_EQUIP) {
            pk_item_preview(s_det.kind, g->world.items[s_det.idx][1], 10);
        } else {
            pk_item_preview(s_det.kind, g->world.equip[s_det.idx][1], 10);
        }
        pk_object_spawn(g, item_det_obj, 0, 0x20, 0x20, 0, 0x1e, 0x1d);
        pk_object_spawn(g, item_det_obj, 0, 0x10, 0x10, 0, 0, 0xe);
        for (i = 0x12; i < 0x86; i += 4) {
            pk_object_spawn(g, item_det_obj, 0, 0x10, 0x10, 1, 0, i);
        }
        pk_object_spawn(g, item_det_obj, 0, 0x10, 0x10, 2, 0, 0x86);
        for (i = 1; i <= 10; i++) {
            pk_object_spawn(g, item_det_obj, 0, 0x30, 0x30, i, 0x1e + 0x12 + 6 * i, 0x33);
        }
        o->counter = 0;
        o->state = 1;
        break;
    case 1:
        item_det_texts(g);
        if (++o->counter < 6) {
            break;
        }
        if (g->key_code == PK_SC_5 || g->key_code == PK_SC_BACK) {
            pk_audio_play_sfx(g->key_code == PK_SC_5 ? 0 : 1);
            g->dlg_result = g->key_code == PK_SC_5 ? 0 : 1;
            pk_object_return(g, o);
        }
        break;
    default:
        break;
    }
}

/* ---- the unit's page (0x1005fe98) -----------------------------------
 * the picture 0xd1 + template byte 0 over the background at 0, 0xe
 * (FUN_10070ad8), the face (image 0x19, frames 0x100ab310, byte 0 * 2,
 * blinking) at 0x9b, 0x11, ten rank stars (byte 1) at 0x33 + 6 * i, 0x36,
 * the window 0xa9 at 0x51, 0x97 with the unit's words (battlemes.dat
 * group 0, byte 14) at 6, 0xa2 + 0xd * i, the portrait (frames 0x100abc40)
 * at 0x98, 0x9c / 0xb4 and the texts of FUN_10060980. */
void pk_unit_detail_update(pk_object *o, pk_game *g);

static void unit_det_obj(pk_object *o, pk_game *g)
{
    pk_object *root = pk_ui_root(g);
    int *b = pk_r.unit[10].b;
    if (!root || root->update != pk_unit_detail_update) {
        pk_object_kill(o);
        return;
    }
    o->layer = 1;
    switch (o->state) {
    case 0x30: /* the face */
        o->image = pk_game_load_image(g, 0x19, 1);
        pk_object_set_frames(o, 0x100ab310u);
        o->flags = 0x4c1;
        o->counter = (o->counter + 1) % 12;
        o->frame = b[0] * 2 + o->counter / 6;
        break;
    case 0x20: /* a star */
        o->image = pk_game_load_image(g, 0x17, 1);
        pk_object_set_frames(o, 0x100aa080u);
        o->flags = 0x4c1;
        o->frame = b[1] < o->arg1 ? 0x3f : 0x3e;
        break;
    case 0xa0: /* the words' window */
        o->image = pk_game_load_image(g, 0x17, 1);
        pk_object_set_frames(o, 0x100aa080u);
        o->flags = 0x4c1;
        o->frame = 0xa9;
        o->layer = 0;
        break;
    case 0x90: /* the portrait: plate (arg1 0) and face (1) */
        o->image = pk_game_load_image(g, PK_PORTRAIT_BASE + g->world.units[s_det.unit][0] * 2, 1);
        pk_object_set_frames(o, 0x100abc40u);
        o->flags = 0x4c1;
        o->frame = o->arg1;
        break;
    default:
        break;
    }
}

static void unit_det_texts(pk_game *g)
{
    static const int k_kind[3] = { 142, 143, 144 };
    int *f = g->world.units[s_det.unit], x = 0, y = 0xe, i, k, n;
    Uint16 nm[25], line[100];
    const Uint16 *w;
    pk_unit_name(g, s_det.unit, nm, 25);
    det_mes(g, 0x25, x + 0x31, y + 0xe, PK_ALIGN_RIGHT);
    pk_drawq_text(&g->drawq, nm, x + 0x31, y + 0xe, 0, PK_ALIGN_LEFT, 1, PK_FONT_DENSE);
    det_mes(g, 0x1f, x + 0x31, y + 0x1a, PK_ALIGN_RIGHT);
    pk_drawq_text(&g->drawq, pk_r.unit[10].name, x + 0x31, y + 0x1a, 0, PK_ALIGN_LEFT, 1, PK_FONT_DENSE);
    det_mes(g, 0x20, x + 0x31, y + 0x26, PK_ALIGN_RIGHT);
    pk_drawq_text(&g->drawq, pk_r.unit[10].race, x + 0x31, y + 0x26, 0, PK_ALIGN_LEFT, 1, PK_FONT_DENSE);
    det_mes(g, 0x19, x + 0x31, y + 0x32, PK_ALIGN_RIGHT);
    det_mes(g, 0x3d, x + 0x31, y + 0x3e, PK_ALIGN_RIGHT);
    if (f[16] >= 0 && f[16] < 3) {
        det_mes(g, k_kind[f[16]], x + 0x31, y + 0x3e, PK_ALIGN_LEFT);
    }
    det_mes(g, 0x30, x + 0x9d, y + 0x3e, PK_ALIGN_RIGHT);
    det_num(g, "%d", f[17], 0, x + 0x9d, y + 0x3e);
    det_mes(g, 0x2e, x + 0x2a, y + 0x4a, PK_ALIGN_RIGHT);
    det_num(g, "%d/%d", f[7], f[8], x + 0x2a, y + 0x4a);
    det_mes(g, 0x26, x + 0x9d, y + 0x4a, PK_ALIGN_RIGHT);
    det_num(g, "%d", f[1], 0, x + 0x9d, y + 0x4a);
    det_mes(g, 0x1b, x + 0x2a, y + 0x56, PK_ALIGN_RIGHT);
    det_num(g, "%d", f[5], 0, x + 0x2a, y + 0x56);
    det_mes(g, 0x1c, x + 0x5f, y + 0x56, PK_ALIGN_RIGHT);
    det_num(g, "%d", f[6], 0, x + 0x5f, y + 0x56);
    det_mes(g, 0x2f, x + 0x96, y + 0x56, PK_ALIGN_RIGHT);
    det_num(g, "%d", f[9], 0, x + 0x96, y + 0x56);
    det_mes(g, 0x31, x + 0x2a, y + 0x62, PK_ALIGN_RIGHT);
    det_num(g, "%d", f[10], 0, x + 0x2a, y + 0x62);
    det_mes(g, 0x33, x + 0x5f, y + 0x62, PK_ALIGN_RIGHT);
    det_num(g, "%d", f[11], 0, x + 0x5f, y + 0x62);
    det_mes(g, 0x32, x + 0x96, y + 0x62, PK_ALIGN_RIGHT);
    det_num(g, "%d", f[12], 0, x + 0x96, y + 0x62);
    /* game+0xbd04 (mestext 0x34) + " " + the ability; mestext 0x91 + " %d %" */
    k = 0;
    for (w = pk_data_text(PK_TBL_MESTEXT, 0x34); w && *w && *w != 0x1b && k < 60; w++) {
        line[k++] = *w;
    }
    line[k++] = ' ';
    for (w = pk_r.unit[10].skill; *w && *w != 0x1b && k < 98; w++) {
        line[k++] = *w;
    }
    line[k] = 0;
    pk_drawq_text(&g->drawq, line, x + 5, y + 0x6e, 0, PK_ALIGN_LEFT, 1, PK_FONT_DENSE);
    {
        char num[16];
        k = 0;
        for (w = pk_data_text(PK_TBL_MESTEXT, 0x91); w && *w && *w != 0x1b && k < 60; w++) {
            line[k++] = *w;
        }
        SDL_snprintf(num, sizeof(num), " %d %%", f[13]);
        for (i = 0; num[i] && k < 98; i++) {
            line[k++] = (Uint16)num[i];
        }
        line[k] = 0;
        pk_drawq_text(&g->drawq, line, x + 5, y + 0x7a, 0, PK_ALIGN_LEFT, 1, PK_FONT_DENSE);
    }
    /* the unit's words (FUN_1000ea00: battlemes.dat group 0, '\n' lines) */
    w = pk_data_text(PK_TBL_BATTLEMES, pk_r.unit[10].b[14]);
    n = 0;
    k = 0;
    while (w && n < 3) {
        if (*w == '\n' || *w == 0 || *w == 0x1b || k >= 98) {
            line[k] = 0;
            if (k) {
                pk_drawq_text(&g->drawq, line, 6, 0xa2 + 0xd * n, 0, PK_ALIGN_LEFT, 1, PK_FONT_DENSE);
            }
            n++;
            k = 0;
            if (*w != '\n') {
                break;
            }
            w++;
            continue;
        }
        if (*w != '\r') {
            line[k++] = *w;
        }
        w++;
    }
}

void pk_unit_detail_update(pk_object *o, pk_game *g)
{
    int i;
    switch (o->state) {
    case 0:
        pk_unit_template(g->world.units[s_det.unit][0], 10);
        g->overlay_id = 0xd1 + pk_r.unit[10].b[0];
        g->overlay_x = 0;
        g->overlay_y = 0xe;
        pk_object_spawn(g, unit_det_obj, 0, 0x30, 0x30, 0, 0x9b, 0x11);
        for (i = 1; i <= 10; i++) {
            pk_object_spawn(g, unit_det_obj, 0, 0x20, 0x20, i, 0x2d + 6 * i, 0x36);
        }
        pk_object_spawn(g, unit_det_obj, 0, 0xa0, 0, 0, 0x51, 0x97);
        pk_object_spawn(g, unit_det_obj, 0, 0x90, 0, 0, 0x98, 0x9c);
        pk_object_spawn(g, unit_det_obj, 0, 0x90, 0, 1, 0x98, 0xb4);
        o->counter = 0;
        o->state = 1;
        break;
    case 1:
        unit_det_texts(g);
        if (++o->counter < 6) {
            break;
        }
        if (g->key_code == PK_SC_5 || g->key_code == PK_SC_BACK) {
            pk_audio_play_sfx(g->key_code == PK_SC_5 ? 0 : 1);
            g->overlay_id = -1;
            g->dlg_result = g->key_code == PK_SC_5 ? 0 : 1;
            pk_object_return(g, o);
        }
        break;
    default:
        break;
    }
}

void pk_list_update(pk_object *o, pk_game *g)
{
    switch (o->state) {
    case 0xc: /* enter */
        if (g->curtain != PK_CURTAIN_HELD || ++o->counter < 4) {
            break;
        }
        pk_game_set_curtain(g, PK_CURTAIN_OPEN);
        pk_object_spawn(g, pk_kingdom_ui_update, o->dir, 0xd0, 0, 0, 0, 0xc2);
        spawn_top_bar(o, g);
        g->field_bg = 0xc;
        pk_game_set_bg(g, 0, 0xc);
        g->bg_visible = 1;
        g->slide = 0;
        o->state = 0xd;
        break;
    case 0xd: /* Item / Unit */
        if (g->curtain != PK_CURTAIN_NONE) {
            break;
        }
        g->dlg_type = 0xd;
        g->list_top = g->cursor = 0;
        pk_object_call(g, o, pk_dialog_update, 0, pk_list_update, 0xe);
        break;
    case 0xe:
        if (g->dlg_result != 0) {
            pk_game_set_curtain(g, PK_CURTAIN_CLOSE);
            g->slide = 2;
            o->state = 0x20;
        } else if (g->list_top + g->cursor == 0) {
            g->slide = 0;
            g->dlg_type = 0xb;
            g->list_top = g->cursor = 0;
            pk_object_call(g, o, pk_dialog_update, 0, pk_list_update, 0xf);
        } else {
            g->slide = 0;
            g->list_top = g->cursor = 0;
            g->list_120 = 1;
            g->list_124 = 1;
            pk_units_sort(g, false);
            pk_object_call(g, o, pk_unit_list_update, 0, pk_list_update, 0x14);
        }
        break;
    case 0xf: /* kind chosen */
        if (g->dlg_result != 0) {
            g->slide = 1;
            o->state = 0xd;
            break;
        }
        o->work[2] = g->list_top + g->cursor; /* kind */
        g->list_f8 = g->list_fc = 0;
        /* fallthrough */
    case 0x11: /* the item list (again) */
        g->dlg_type = o->work[2];
        g->list_top = g->list_f8;
        g->cursor = g->list_fc;
        g->slide = 0;
        pk_object_call(g, o, pk_item_list_update, 0, pk_list_update, 0x10);
        break;
    case 0x10:
        if (g->dlg_result != 0) {
            g->slide = 1;
            g->dlg_type = 0xb;
            g->list_top = 0;
            g->cursor = o->work[2];
            pk_object_call(g, o, pk_dialog_update, 0, pk_list_update, 0xf);
            break;
        }
        g->list_f8 = g->list_top;
        g->list_fc = g->cursor;
        s_det.kind = o->work[2];
        s_det.idx = pk_item_find(g, o->work[2], g->list_top + g->cursor);
        if (s_det.idx < 0 || s_det.idx == 0xff) {
            o->state = 0x11;
            break;
        }
        pk_object_call(g, o, pk_item_detail_update, 0, pk_list_update, 0x11);
        break;
    case 0x14: /* unit chosen */
        if (g->dlg_result != 0) {
            g->list_120 = g->list_124 = 0;
            g->slide = 1;
            o->state = 0xd;
            break;
        }
        g->list_f8 = g->list_top;
        g->list_fc = g->cursor;
        s_det.unit = g->list_top + g->cursor;
        if (s_det.unit < 0 || s_det.unit >= g->world.unit_count) {
            o->state = 0x15;
            break;
        }
        pk_object_call(g, o, pk_unit_detail_update, 0, pk_list_update, 0x15);
        break;
    case 0x15:
        g->list_top = g->list_f8;
        g->cursor = g->list_fc;
        g->slide = 0;
        pk_object_call(g, o, pk_unit_list_update, 0, pk_list_update, 0x14);
        break;
    case 0x20: /* back to the kingdom */
        if (g->curtain != PK_CURTAIN_HELD) {
            break;
        }
        pk_object_kill_others(g, o);
        g->list_120 = g->list_124 = 0;
        g->field_bg = g->fame > 300 ? 0xe : g->fame < 0x97 ? 0xf
                                                           : 9;
        pk_game_set_bg(g, 0, g->field_bg);
        o->state = 1;
        o->work[1] = 0;
        g->dlg_hint = NULL;
        pk_object_set_update(o, pk_kingdom_update);
        break;
    default:
        break;
    }
}
