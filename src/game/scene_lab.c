/* SPDX-License-Identifier: MIT */
/* Lab screens.
 *
 * Combine (hub 0x100329a0, panel 0x10032ee0, result 0x100342b8):
 * four material slots (game+0x17914): mold (kind 0), ore (kind 1) and
 * two extras (jewel or other, kinds 2/3). FUN_10062960 looks the
 * recipe up in itemdata.dat: the offset table at 0x100abee8 holds one
 * block per mold id * 7 + ore id; a block is a 16-byte header (count
 * in byte 0) and 16-byte entries {extra A kind, id, extra B kind, id,
 * result kind, result id, success %}; extras match in either order.
 * Without a match the first entry is the plain result. Combining costs
 * the result's rank * 200 loot (FUN_10063570); on success the
 * materials become the result, on failure the materials are lost and
 * nothing is paid.
 *
 * The panel is drawn with the original's plates (0x10032ee0). */
#include "audio/pk_audio.h"
#include "core/pk_app.h"
#include "core/pk_fs.h"
#include "core/pk_log.h"
#include "game/pk_data.h"
#include "game/pk_help.h"
#include "game/pk_rules.h"
#include "game/pk_scenes.h"
#include "game/pk_world.h"
#include "game/ui_dialog.h"
#include "gfx/pk_font.h"
#include <SDL3/SDL.h>

#define COL_GOLD 0xFFDC00u

static struct
{
    int slot[4];        /* owned item index or 0xff */
    int kind, id, rate; /* recipe result (0xff: none) */
    int cursor;
} s_comb;

static void spawn_top_bar(pk_object *o, pk_game *g)
{
    pk_object_spawn(g, pk_kingdom_ui_update, o->dir, 0x80, 1, 2, 0x10, 3);
    pk_object_spawn(g, pk_kingdom_ui_update, o->dir, 0x80, 1, 3, 0x61, 3);
    pk_object_spawn(g, pk_kingdom_ui_update, o->dir, 0x80, 1, 4, 0xa1, 3);
    pk_object_spawn(g, pk_kingdom_ui_update, o->dir, 0x80, 0, 0x7a, 0, 0x92);
}

static int result_rank(int kind, int id)
{
    pk_item_preview(kind, id, 9);
    return kind >= PK_KIND_EQUIP ? pk_r.equip[9].rec[5] : pk_r.item[9].rec[6];
}

/* FUN_10062960 */
static void recipe_lookup(pk_game *g)
{
    int *m, *o, i, n, a, b;
    size_t len = 0;
    Uint8 *d;
    Uint32 off;
    s_comb.kind = s_comb.id = 0xff;
    s_comb.rate = 0;
    if (s_comb.slot[0] == 0xff || s_comb.slot[1] == 0xff) {
        return;
    }
    m = g->world.items[s_comb.slot[0]];
    o = g->world.items[s_comb.slot[1]];
    if (m[1] < 0 || m[1] > 10 || o[1] < 0 || o[1] > 6) {
        return;
    }
    off = pk_app_i32(0x100abee8u + (Uint32)(m[1] * 7 + o[1]) * 4u);
    a = s_comb.slot[2] == 0xff ? 0xffff : (g->world.items[s_comb.slot[2]][0] << 8) | g->world.items[s_comb.slot[2]][1];
    b = s_comb.slot[3] == 0xff ? 0xffff : (g->world.items[s_comb.slot[3]][0] << 8) | g->world.items[s_comb.slot[3]][1];
    d = (Uint8 *)pk_fs_load_localized("itemdata.dat", &len);
    if (!d || off + 32 > len) {
        SDL_free(d);
        return;
    }
    n = d[off];
    for (i = 0; i < n && off + 16 + (Uint32)i * 16 + 16 <= len; i++) {
        const Uint8 *e = d + off + 16 + i * 16;
        int ka = (e[0] << 8) | e[1], kb = (e[2] << 8) | e[3];
        if ((ka == a && kb == b) || (ka == b && kb == a)) {
            s_comb.kind = e[4];
            s_comb.id = e[5];
            s_comb.rate = e[6];
            SDL_free(d);
            return;
        }
    }
    /* no match: the block's first entry */
    s_comb.kind = d[off + 16 + 4];
    s_comb.id = d[off + 16 + 5];
    s_comb.rate = d[off + 16 + 6];
    SDL_free(d);
}

static int combine_cost(void)
{
    if (s_comb.kind == 0xff) {
        return 0;
    }
    return result_rank(s_comb.kind, s_comb.id) * 200;
}

/* the panel's texts (0x10032ee0): item names in the plates at
 * (plate x + 0x18, y + 0xe) or the slot's empty label (mestext 0x7f mold,
 * 0x80 ore, 0x97 extra; grey), "Combine" (mestext 0xd) on the button and
 * the help line for the cursor (0xf6, 0xf5, 0xf7, 0xf8 + 0xf9) */
static void draw_panel(pk_game *g)
{
    static const int k_empty[4] = { 0x7f, 0x80, 0x97, 0x97 };
    int i;
    for (i = 0; i < 4; i++) {
        int y = 0x19 + i * 0x18;
        if (s_comb.slot[i] != 0xff) {
            const int *t = g->world.items[s_comb.slot[i]];
            pk_item_preview(t[0], t[1], 5 + i);
            pk_drawq_text(&g->drawq, pk_r.item[5 + i].name, 8 + 4 + 0x18, y + 1 + 0xe, 0, PK_ALIGN_LEFT, 1,
                          PK_FONT_DENSE);
        } else {
            pk_game_mestext(g, k_empty[i], 8 + 4 + 0x18, y + 1 + 0xe, 0x666666u, PK_ALIGN_LEFT, 1, PK_FONT_DENSE);
        }
    }
    pk_game_mestext(g, 0xd, 0x32 + 0x26, 0x79 + 0xc, 0, PK_ALIGN_CENTER, 1, PK_FONT_DENSE);
    switch (s_comb.cursor) {
    case 0:
        pk_game_mestext(g, 0xf6, 0x58, 0xb4, 0xFFFFFFu, PK_ALIGN_CENTER, 1, PK_FONT_DENSE);
        break;
    case 1:
        pk_game_mestext(g, 0xf5, 0x58, 0xb4, 0xFFFFFFu, PK_ALIGN_CENTER, 1, PK_FONT_DENSE);
        break;
    case 2:
    case 3:
        pk_game_mestext(g, 0xf7, 0x58, 0xb4, 0xFFFFFFu, PK_ALIGN_CENTER, 1, PK_FONT_DENSE);
        break;
    default:
        pk_game_mestext(g, 0xf8, 0x58, 0xad, 0xFFFFFFu, PK_ALIGN_CENTER, 1, PK_FONT_DENSE);
        pk_game_mestext(g, 0xf9, 0x58, 0xbe, 0xFFFFFFu, PK_ALIGN_CENTER, 1, PK_FONT_DENSE);
        break;
    }
}

/* FUN_10062d28: the result's properties from the extras' words 2 / 3 */
static void comb_attrs(pk_game *g, int *a0, int *a1, int *a2)
{
    int e1w2 = 0xff, e1w3 = 0xff, e2w2 = 0xff, e2w3 = 0xff;
    if (s_comb.slot[2] != 0xff) {
        e1w2 = g->world.items[s_comb.slot[2]][2];
        e1w3 = g->world.items[s_comb.slot[2]][3];
    }
    if (s_comb.slot[3] != 0xff) {
        e2w2 = g->world.items[s_comb.slot[3]][2];
        e2w3 = g->world.items[s_comb.slot[3]][3];
    }
    *a0 = e1w2 != 0xff ? e1w2 : e2w2;
    *a1 = *a2 = 0xff;
    if (s_comb.kind == 5 || s_comb.kind == 6) {
        *a1 = e1w3 != 0xff ? e1w3 : e2w3;
    } else if (s_comb.kind == 7) {
        *a1 = e1w3 != 0xff ? e1w3 : e2w3;
        *a2 = e1w3 != 0xff ? e2w3 : 0xff;
    } else {
        *a0 = 0xff;
    }
}

/* ---- the Combine confirmation (0x100342b8) ---------------------------
 * a white window (rows 0x40..0x42 of frames 0x1008fe40, y 0x14..0x6c),
 * the result's kind mark (0x1008fde4[kind]) at 0x10, 0x19 with its name
 * at 0x28, 0x26, rank stars at 0x4a + 6 * i, 0x2d, the success rate
 * (mestext 0x12), its values (FUN_10035dac), the buttons 0xb / 0xc at
 * 8, 0x78 (mestext 0xd) and 0x5c, 0x78 (0x10), the cursor at
 * 8 + 0x54 * choice, 0x78 and the cost and storage lines in the panel
 * (mestext 0xe gold, 0x22 + " %d loot", 4 / 0x12e + " %d / %d"; no room:
 * 300 / 0x12d). A success sets the word to the result and shows help 0x1a,
 * a failure help 0x17. */
void pk_combine_confirm_update(pk_object *o, pk_game *g);
static int s_cc_choice, s_cc_full;

static void cc_obj(pk_object *o, pk_game *g)
{
    pk_object *root = pk_ui_root(g);
    if (!root || root->update != pk_combine_confirm_update) {
        pk_object_kill(o);
        return;
    }
    o->image = pk_game_load_image(g, 0x17, 1);
    pk_object_set_frames(o, 0x1008fe40u);
    o->flags = 0x4c1;
    o->layer = o->state == 0x10 ? 0 : 1;
    switch (o->state) {
    case 0x10:
        o->frame = o->arg1 + 0x40;
        o->flags = 0x4e1;
        break;
    case 0x20:
        o->frame = s_cc_choice == o->arg1 ? 0xc : 0xb;
        break;
    case 0x30:
        if (s_comb.kind == 0xff) {
            o->flags = 0x4c0;
            break;
        }
        if (o->arg1 == 0) {
            o->frame = (int)pk_app_i32(0x1008fde4u + (Uint32)(s_comb.kind & 7) * 4u);
        } else {
            o->frame = result_rank(s_comb.kind, s_comb.id) < o->arg1 ? 0x3f : 0x3e;
        }
        break;
    case 8:
        if (!o->work[0]) {
            pk_ui_cursor_anim(g, o);
            o->work[0] = 1;
        }
        o->flags = 0x4c3;
        pk_object_animate(o);
        o->x = s_cc_choice * 0x54 + 8;
        break;
    default:
        break;
    }
}

static void cc_text(pk_game *g, const Uint16 *t, int x, int y, pk_align a)
{
    pk_drawq_text(&g->drawq, t, x, y, 0, a, 1, PK_FONT_DENSE);
}

static void cc_draw(pk_game *g)
{
    char num[24];
    Uint16 t[25];
    int a0, a1, a2;
    pk_game_mestext(g, 0xd, 8 + 0x26, 0x78 + 0xc, 0, PK_ALIGN_CENTER, 1, PK_FONT_DENSE);
    pk_game_mestext(g, 0x10, 0x5c + 0x26, 0x78 + 0xc, 0, PK_ALIGN_CENTER, 1, PK_FONT_DENSE);
    if (s_comb.kind == 0xff) {
        pk_game_mestext(g, 0xf, 0x58, 0x3c, 0, PK_ALIGN_CENTER, 1, PK_FONT_DENSE);
        pk_game_mestext(g, 0x86, 0x58, 0xb5, 0xFFFFFFu, PK_ALIGN_CENTER, 1, PK_FONT_LEGEND);
        return;
    }
    pk_item_preview(s_comb.kind, s_comb.id, 0);
    cc_text(g, s_comb.kind < PK_KIND_EQUIP ? pk_r.item[0].name : pk_r.equip[0].name, 0x28, 0x26, PK_ALIGN_LEFT);
    pk_game_mestext(g, 0x11, 0x19, 0x37, 0, PK_ALIGN_LEFT, 1, PK_FONT_DENSE);
    pk_game_mestext(g, 0x12, 0x78, 0x44, 0, PK_ALIGN_RIGHT, 1, PK_FONT_DENSE);
    SDL_snprintf(num, sizeof(num), "%d%%", s_comb.rate);
    pk_drawq_text_utf8(&g->drawq, num, 0x78, 0x44, 0, PK_ALIGN_LEFT, 1, PK_FONT_DENSE);
    comb_attrs(g, &a0, &a1, &a2);
    if (s_comb.kind == 7) {
        pk_game_mestext(g, 0x15, 0x37, 0x51, 0, PK_ALIGN_RIGHT, 1, PK_FONT_DENSE);
        pk_item_attr_name(1, a1, t, 25);
        cc_text(g, t, 0x37, 0x51, PK_ALIGN_LEFT);
        pk_item_attr_name(1, a2, t, 25);
        cc_text(g, t, 0x37, 0x5e, PK_ALIGN_LEFT);
    } else {
        if (s_comb.kind == 5 || s_comb.kind == 6) {
            pk_game_mestext(g, s_comb.kind == 5 ? 0x13 : 0x14, s_comb.kind == 5 ? 0x37 : 0x41, 0x51, 0, PK_ALIGN_RIGHT, 1,
                            PK_FONT_DENSE);
            SDL_snprintf(num, sizeof(num), "%d", s_comb.kind == 5 ? pk_r.equip[0].rec[7] : pk_r.equip[0].rec[8]);
            pk_drawq_text_utf8(&g->drawq, num, s_comb.kind == 5 ? 0x37 : 0x41, 0x51, 0, PK_ALIGN_LEFT, 1, PK_FONT_DENSE);
        }
        pk_game_mestext(g, 0x15, 0x37, 0x5e, 0, PK_ALIGN_RIGHT, 1, PK_FONT_DENSE);
        pk_item_attr_name(1, a1, t, 25);
        cc_text(g, t, 0x37, 0x5e, PK_ALIGN_LEFT);
    }
    pk_game_mestext(g, 0x16, 0x46, 0x6b, 0, PK_ALIGN_RIGHT, 1, PK_FONT_DENSE);
    pk_item_attr_name(0, a0, t, 25);
    cc_text(g, t, 0x46, 0x6b, PK_ALIGN_LEFT);
    /* the panel */
    if (s_cc_full) {
        pk_game_mestext(g, s_comb.kind < PK_KIND_EQUIP ? 300 : 0x12d, 0x58, 0xb5, 0xFFFFFFu, PK_ALIGN_CENTER, 1,
                        PK_FONT_LEGEND);
        return;
    }
    pk_game_mestext(g, 0xe, 11, 0xa7, 0, PK_ALIGN_LEFT, 1, PK_FONT_LEGEND);
    pk_game_mestext(g, 0xe, 10, 0xa6, 0xFFDC00u, PK_ALIGN_LEFT, 1, PK_FONT_LEGEND);
    {
        Uint16 line[64];
        const Uint16 *w;
        int k = 0, i;
        for (w = pk_data_text(PK_TBL_MESTEXT, 0x22); w && *w && *w != 0x1b && k < 40; w++) {
            line[k++] = *w;
        }
        SDL_snprintf(num, sizeof(num), " %d loot", combine_cost());
        for (i = 0; num[i] && k < 62; i++) {
            line[k++] = (Uint16)num[i];
        }
        line[k] = 0;
        pk_drawq_text(&g->drawq, line, 11, 0xb6, 0, PK_ALIGN_LEFT, 1, PK_FONT_LEGEND);
        pk_drawq_text(&g->drawq, line, 10, 0xb5, 0xFFFFFFu, PK_ALIGN_LEFT, 1, PK_FONT_LEGEND);
        k = 0;
        for (w = pk_data_text(PK_TBL_MESTEXT, s_comb.kind < PK_KIND_EQUIP ? 4 : 0x12e); w && *w && *w != 0x1b && k < 40; w++) {
            line[k++] = *w;
        }
        if (s_comb.kind < PK_KIND_EQUIP) {
            SDL_snprintf(num, sizeof(num), " %d / %d", g->world.item_count, 0xfa);
        } else {
            SDL_snprintf(num, sizeof(num), " %d / %d", g->world.equip_count, 100);
        }
        for (i = 0; num[i] && k < 62; i++) {
            line[k++] = (Uint16)num[i];
        }
        line[k] = 0;
        pk_drawq_text(&g->drawq, line, 11, 0xc5, 0, PK_ALIGN_LEFT, 1, PK_FONT_LEGEND);
        pk_drawq_text(&g->drawq, line, 10, 0xc4, 0xFFFFFFu, PK_ALIGN_LEFT, 1, PK_FONT_LEGEND);
    }
}

void pk_combine_confirm_update(pk_object *o, pk_game *g)
{
    int i;
    switch (o->state) {
    case 0:
        pk_object_spawn(g, cc_obj, 0, 0x10, 0x10, 0, 0, 0x14);
        for (i = 0x18; i < 0x6c; i += 4) {
            pk_object_spawn(g, cc_obj, 0, 0x10, 0x10, 1, 0, i);
        }
        pk_object_spawn(g, cc_obj, 0, 0x10, 0x10, 2, 0, 0x6c);
        pk_object_spawn(g, cc_obj, 0, 0x20, 0x20, 0, 8, 0x78);
        pk_object_spawn(g, cc_obj, 0, 0x20, 0x20, 1, 0x5c, 0x78);
        pk_object_spawn(g, cc_obj, 0, 0x30, 0x30, 0, 0x10, 0x19);
        for (i = 1; i <= 10; i++) {
            pk_object_spawn(g, cc_obj, 0, 0x30, 0x30, i, 0x44 + 6 * i, 0x2d);
        }
        pk_object_spawn(g, cc_obj, 0, 8, 0, 1, 8, 0x78);
        s_cc_choice = s_comb.kind == 0xff ? 1 : 0;
        s_cc_full = 0;
        o->counter = 0;
        o->state = 2;
        break;
    case 2:
        cc_draw(g);
        if (++o->counter < 6 || pk_help_active()) {
            break;
        }
        /* touch: the two buttons (Combine / Cancel) */
        if (pk_tap(g, 8, 0x78, 0x4c, 0x16) && s_comb.kind != 0xff) {
            s_cc_choice = 0;
            g->key_code = PK_SC_5;
        }
        if (pk_tap(g, 0x5c, 0x78, 0x4c, 0x16)) {
            s_cc_choice = 1;
            g->key_code = PK_SC_5;
        }
        if (g->key_code == PK_SC_LEFT || g->key_code == PK_SC_RIGHT) {
            pk_audio_play_sfx(2);
            s_cc_choice = s_comb.kind == 0xff ? 1 : (s_cc_choice + 1) % 2;
        } else if (g->key_code == PK_SC_BACK || (g->key_code == PK_SC_5 && s_cc_choice == 1)) {
            pk_audio_play_sfx(1);
            g->dlg_result = 1;
            pk_object_return(g, o);
        } else if (g->key_code == PK_SC_5) {
            int full = s_comb.kind < PK_KIND_EQUIP ? (g->world.item_count > 0xf9 && s_comb.kind != 4)
                                                   : g->world.equip_count > 99;
            s_cc_full = full;
            if (full || g->loot < combine_cost()) {
                pk_audio_play_sfx(4);
                break;
            }
            if (pk_rng_range(&g->rng, 100) < s_comb.rate) { /* FUN_10063570 */
                int a0, a1, a2, idx;
                comb_attrs(g, &a0, &a1, &a2);
                pk_audio_play_sfx(3);
                pk_loot_sub(g, combine_cost());
                for (i = 0; i < 4; i++) {
                    if (s_comb.slot[i] != 0xff) {
                        pk_item_remove(g, s_comb.slot[i]);
                    }
                }
                idx = pk_item_add(g, s_comb.kind, s_comb.id);
                if (idx >= 0 && s_comb.kind >= PK_KIND_EQUIP) {
                    g->world.equip[idx][2] = a0;
                    g->world.equip[idx][3] = a1;
                    g->world.equip[idx][4] = a2;
                }
                pk_item_preview(s_comb.kind, s_comb.id, 0);
                pk_help_set_word(s_comb.kind < PK_KIND_EQUIP ? pk_r.item[0].name : pk_r.equip[0].name);
                o->state = 3;
            } else { /* the materials are lost */
                for (i = 0; i < 4; i++) {
                    if (s_comb.slot[i] != 0xff) {
                        pk_item_remove(g, s_comb.slot[i]);
                    }
                }
                o->state = 4;
            }
            pk_items_sort(g);
            pk_equip_sort(g, false);
            pk_world_save(g);
        }
        break;
    case 3:
    case 4:
        pk_help(g, 1, o->state == 3 ? 0x1a : 0x17);
        pk_audio_play_sfx(0);
        for (i = 0; i < 4; i++) {
            s_comb.slot[i] = 0xff;
        }
        recipe_lookup(g);
        g->dlg_result = 0;
        pk_object_return(g, o);
        break;
    default:
        break;
    }
}

/* 0x10032ee0 children: the plates (state 0x10: image 0x17, frames
 * 0x1008fe40; 160 x 20 at x 8, y 0x19 + 0x18 * slot: 0x10, selected 0x11,
 * an empty extra 0x12; the button 0xb / selected 0xc at 0x32,0x79), the
 * slot icons (0x20: mold 0x35, ore 0x34, an extra by its kind from the
 * table 0x1008fde4) and the cursor (8: frames 0..1 at x 8 / 0x32) */
static void panel_update(pk_object *o, pk_game *g)
{
    pk_object *root = pk_ui_root(g);
    int sl = o->arg1;
    if (!root || root->update != pk_combine_update || root->state != 0xd) {
        pk_object_kill(o);
        return;
    }
    o->image = pk_game_load_image(g, 0x17, 1);
    pk_object_set_frames(o, 0x1008fe40u);
    o->layer = 1;
    o->flags = 0x4c1;
    if (o->state == 0x10) {
        if (sl == 4) {
            o->frame = s_comb.cursor == 4 ? 0xc : 0xb;
        } else if (s_comb.cursor == sl) {
            o->frame = 0x11;
        } else if (sl >= 2 && s_comb.slot[sl] == 0xff) {
            o->frame = 0x12;
        } else {
            o->frame = 0x10;
        }
    } else if (o->state == 0x20) {
        if (sl == 0) {
            o->frame = 0x35;
        } else if (sl == 1) {
            o->frame = 0x34;
        } else if (s_comb.slot[sl] != 0xff) {
            int kind = g->world.items[s_comb.slot[sl]][0];
            o->frame = (int)pk_app_i32(0x1008fde4u + (Uint32)(kind & 7) * 4u);
        } else {
            o->flags &= ~(Uint32)PK_OBJ_SPRITE;
        }
    } else { /* the cursor */
        o->frame = (g->timer / 4) & 1;
        o->x = s_comb.cursor == 4 ? 0x32 : 8;
        o->y = 0x1d + s_comb.cursor * 0x18;
    }
}

static void open_list(pk_object *o, pk_game *g, int kind, int ret)
{
    g->dlg_type = kind;
    g->list_top = g->cursor = 0;
    g->slide = 0;
    pk_object_call(g, o, pk_item_list_update, 0, pk_combine_update, ret);
}

void pk_combine_update(pk_object *o, pk_game *g)
{
    int i;
    switch (o->state) {
    case 0xc: /* enter */
        if (g->curtain != PK_CURTAIN_HELD || ++o->counter < 4) {
            break;
        }
        pk_game_set_curtain(g, PK_CURTAIN_OPEN);
        pk_object_spawn(g, pk_kingdom_ui_update, o->dir, 0xd0, 0, 0, 0, 0xc2);
        spawn_top_bar(o, g);
        g->field_bg = 10;
        pk_game_set_bg(g, 0, 10);
        g->bg_visible = 1;
        for (i = 0; i < 4; i++) {
            s_comb.slot[i] = 0xff;
        }
        s_comb.cursor = 0;
        o->work[7] = 0;
        recipe_lookup(g);
        o->state = 0xd;
        break;
    case 0xd: /* the panel */
        if (g->curtain != PK_CURTAIN_NONE) {
            break;
        }
        if (o->work[7] == 0) {
            for (i = 0; i < 4; i++) {
                pk_object_spawn(g, panel_update, 0, 0x10, 0x50, i, 8, 0x19 + i * 0x18);
                pk_object_spawn(g, panel_update, 0, 0x20, 0x60, i, 8 + 4, 0x19 + i * 0x18 + 1);
            }
            pk_object_spawn(g, panel_update, 0, 0x10, 0x10, 4, 0x32, 0x79);
            o->work[7] = 1;
        }
        draw_panel(g);
        if (pk_help_active()) {
            break;
        }
        { /* touch: the four plates and the button */
            int sl = s_comb.cursor;
            if (pk_tap_rows(g, 8, 0x19, 0xa0, 0x18, 4, &sl)) {
                s_comb.cursor = sl;
            }
            if (pk_tap(g, 0x32, 0x79, 0x48, 0x16)) {
                s_comb.cursor = 4;
                g->key_code = PK_SC_5;
            }
        }
        if (g->key_code == PK_SC_UP || g->key_code == PK_SC_DOWN) {
            pk_audio_play_sfx(2);
            s_comb.cursor = (s_comb.cursor + (g->key_code == PK_SC_UP ? 4 : 1)) % 5;
        } else if (g->key_code == PK_SC_5) {
            pk_audio_play_sfx(0);
            o->work[7] = 0;
            if (s_comb.cursor < 2) {
                open_list(o, g, s_comb.cursor, 0x10);
            } else if (s_comb.cursor < 4) {
                g->dlg_type = 10; /* Jewel / Other / Clear */
                g->list_top = g->cursor = 0;
                g->slide = 0;
                pk_object_call(g, o, pk_dialog_update, 0, pk_combine_update, 0x11);
            } else { /* the confirmation (0x100342b8) */
                pk_object_call(g, o, pk_combine_confirm_update, 0, pk_combine_update, 0x12);
            }
        } else if (g->key_code == PK_SC_BACK) {
            pk_audio_play_sfx(1);
            pk_game_set_curtain(g, PK_CURTAIN_CLOSE);
            o->work[7] = 0;
            o->state = 0x20;
        }
        break;
    case 0x10: /* item chosen for the current slot */
        if (g->dlg_result == 0) {
            int idx = pk_item_find(g, g->dlg_type, g->list_top + g->cursor), k, dup = 0;
            for (k = 0; k < 4; k++) {
                if (k != s_comb.cursor && s_comb.slot[k] == idx) {
                    dup = 1;
                }
            }
            if (idx >= 0 && idx != 0xff && !dup) {
                s_comb.slot[s_comb.cursor] = idx;
            }
            recipe_lookup(g);
        }
        o->state = 0xd;
        break;
    case 0x11: /* extra: jewel, other or clear */
        if (g->dlg_result != 0) {
            o->state = 0xd;
        } else if (g->list_top + g->cursor == 2) {
            s_comb.slot[s_comb.cursor] = 0xff;
            recipe_lookup(g);
            o->state = 0xd;
        } else {
            open_list(o, g, 2 + g->list_top + g->cursor, 0x10);
        }
        break;
    case 0x12: /* back from the confirmation */
        o->state = 0xd;
        break;
    case 0x20: /* back to the kingdom */
        if (g->curtain != PK_CURTAIN_HELD) {
            break;
        }
        pk_object_kill_others(g, o);
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

/* ------------------------------------------------------------------ */
/* Vault (0x1003b9a8): the treasures the kingdom holds. Background
 * 0x47; nine pictures (image 0x48, frames 0x10092e34) at fixed spots,
 * shown when their bit (0x10092ffc[i]) is set in game+0x444; the
 * ninth once the story is under way (+0x43c > 0 or +0x440 > 2). */

static const int k_vault_pos[9][2] = {
    { 0x3d, 0x52 },
    { 0x62, 0x5f },
    { 0x24, 0x48 },
    { 0x1d, 0x68 },
    { 0x4a, 0x2a },
    { 0x77, 0x42 },
    { 0x0e, 0x4d },
    { 0x79, 0x4a },
    { 0x1e, 0x2d },
};

static void vault_child(pk_object *o, pk_game *g)
{
    pk_object *root = pk_ui_root(g);
    if (!root || root->update != pk_vault_update) {
        pk_object_kill(o);
        return;
    }
    if (o->state == 0x10) {
        int i = o->arg1, shown = 0;
        if (i < 8) {
            shown = (g->world.h_444 & (int)pk_app_i32(0x10092ffcu + (Uint32)i * 4u)) != 0;
        } else {
            shown = g->world.h_43c > 0 || g->progress > 2;
        }
        pk_ui_setup_sprite(g, o, 0x48, 0x10092e34u, shown ? 0x4c1 : 0x441);
        o->frame = i < 8 ? i : 0x12;
        if (!shown) {
            o->flags &= ~(Uint32)PK_OBJ_SPRITE;
        }
        o->state = 0x11;
    }
}

void pk_vault_update(pk_object *o, pk_game *g)
{
    int i;
    if (o->type != 0xc2) {
        vault_child(o, g);
        return;
    }
    switch (o->state) {
    case 0xc:
        if (g->curtain != PK_CURTAIN_HELD || ++o->counter < 4) {
            break;
        }
        pk_game_set_curtain(g, PK_CURTAIN_OPEN);
        g->field_bg = 0x47;
        pk_game_set_bg(g, 0, 0x47);
        g->bg_visible = 1;
        spawn_top_bar(o, g);
        for (i = 0; i < 9; i++) {
            pk_object_spawn(g, pk_vault_update, 0, 0x10, 0, i, k_vault_pos[i][0], k_vault_pos[i][1]);
        }
        o->counter = 0;
        o->state = 0xd;
        break;
    case 0xd:
        if (g->curtain != PK_CURTAIN_NONE || ++o->counter < 2) {
            break;
        }
        o->counter = 0;
        pk_help(g, 1, 0x20);
        o->state = 0xe;
        break;
    case 0xe:
        if (pk_help_active()) {
            break;
        }
        if (g->key_code == PK_SC_5 || g->key_code == PK_SC_BACK) {
            pk_audio_play_sfx(1);
            pk_game_set_curtain(g, PK_CURTAIN_CLOSE);
            o->state = 0x20;
        }
        break;
    case 0x20:
        if (g->curtain != PK_CURTAIN_HELD) {
            break;
        }
        pk_object_kill_others(g, o);
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

/* ------------------------------------------------------------------ */
/* Rank Up (hub 0x10053a70, panel 0x10053e80, confirm 0x10054f8c,
 * chart 0x10056738). A unit that is not in a party and a rank-up
 * emblem (item kind 4) give a new class when rankup.dat has an entry
 * (FUN_1007aa30): 16-byte header (count in byte 0), 8-byte entries
 * {new type, unit type, emblem id, minimum level, fame needed (16-bit
 * big-endian)}. Fame counts as 10 when it is 0 and the story is past
 * its start. It costs the new class's rank * 200 loot (FUN_10057dd4);
 * the emblem is used up. */

static struct
{
    int unit, emblem, result, cursor;
} s_rank;
static Uint16 s_rank_name[25];

static void rank_lookup(pk_game *g)
{
    size_t len = 0;
    Uint8 *d;
    int n, i, fame = g->fame;
    s_rank.result = 0xff;
    if (s_rank.unit == 0xff || s_rank.emblem == 0xff) {
        return;
    }
    if (fame == 0 && g->progress > 1) {
        fame = 10;
    }
    d = (Uint8 *)pk_fs_load("rankup.dat", &len);
    if (!d || len < 16) {
        SDL_free(d);
        return;
    }
    n = d[0];
    for (i = 0; i < n && 16 + (size_t)i * 8 + 8 <= len; i++) {
        const Uint8 *e = d + 16 + i * 8;
        int need = (e[4] << 8) | e[5];
        if (need > fame) {
            continue;
        }
        if (g->world.units[s_rank.unit][0] == e[1] && g->world.items[s_rank.emblem][1] == e[2] &&
            e[3] <= g->world.units[s_rank.unit][1]) {
            s_rank.result = e[0];
            break;
        }
    }
    SDL_free(d);
}

static int rank_cost(void)
{
    if (s_rank.result == 0xff) {
        return 0;
    }
    pk_unit_template(s_rank.result, 9);
    return pk_r.unit[9].b[1] * 200;
}

/* the Rank Up panel (0x10053e80): plates (image 0x17, frames 0x1009c18c)
 * for the unit (8,0x23) and the emblem (8,0x41), buttons at 0x32,0x5f
 * (mestext 0x2c) and 0x32,0x73 (0x146, back); in the plates the unit's
 * face (image 0x19, frames 0x1009d41c: template byte 0 (+0x1bc) * 2, blinking)
 * with its class name, or mestext 0x62, and the emblem (frame 0x3a) with
 * its name, or 0x81; the help line 0xfd / 0xfe / 0xff / 0x147 at
 * 0x58,0xb5 */
static void rank_panel_update(pk_object *o, pk_game *g)
{
    pk_object *root = pk_ui_root(g);
    int sl = o->arg1;
    if (!root || root->update != pk_rankup_update || root->state != 0xd) {
        pk_object_kill(o);
        return;
    }
    o->layer = 1;
    o->flags = 0x4c1;
    if (o->state == 0x10) {
        o->image = pk_game_load_image(g, 0x17, 1);
        pk_object_set_frames(o, 0x1009c18cu);
        if (sl < 2) {
            o->frame = s_rank.cursor == sl ? 0x11 : 0x10;
        } else {
            o->frame = s_rank.cursor == sl ? 0xc : 0xb;
        }
    } else if (o->state == 0x20) { /* the unit's face */
        if (s_rank.unit == 0xff) {
            o->flags &= ~(Uint32)PK_OBJ_SPRITE;
            return;
        }
        o->image = pk_game_load_image(g, 0x19, 1);
        pk_object_set_frames(o, 0x1009d41cu);
        pk_unit_template(g->world.units[s_rank.unit][0], 5);
        o->frame = pk_r.unit[5].b[0] * 2 + ((g->timer / 6) & 1); /* template +0x1bc */
    } else {                                                     /* the emblem */
        if (s_rank.emblem == 0xff) {
            o->flags &= ~(Uint32)PK_OBJ_SPRITE;
            return;
        }
        o->image = pk_game_load_image(g, 0x17, 1);
        pk_object_set_frames(o, 0x1009c18cu);
        o->frame = 0x3a;
    }
}

static void rank_draw(pk_game *g)
{
    static const int k_help[4] = { 0xfd, 0xfe, 0xff, 0x147 };
    if (s_rank.unit != 0xff) {
        pk_unit_template(g->world.units[s_rank.unit][0], 5);
        pk_drawq_text(&g->drawq, pk_r.unit[5].name, 8 + 4 + 0x18, 0x23 + 2 + 0xd, 0, PK_ALIGN_LEFT, 1, PK_FONT_DENSE);
    } else {
        pk_game_mestext(g, 0x62, 8 + 4 + 0x18, 0x23 + 2 + 0xd, 0x666666u, PK_ALIGN_LEFT, 1, PK_FONT_DENSE);
    }
    if (s_rank.emblem != 0xff) {
        const int *t = g->world.items[s_rank.emblem];
        pk_item_preview(t[0], t[1], 5);
        pk_drawq_text(&g->drawq, pk_r.item[5].name, 8 + 4 + 0x18, 0x41 + 1 + 0xd, 0, PK_ALIGN_LEFT, 1, PK_FONT_DENSE);
    } else {
        pk_game_mestext(g, 0x81, 8 + 4 + 0x18, 0x41 + 1 + 0xd, 0x666666u, PK_ALIGN_LEFT, 1, PK_FONT_DENSE);
    }
    pk_game_mestext(g, 0x2c, 0x32 + 0x26, 0x5f + 0xc, 0, PK_ALIGN_CENTER, 1, PK_FONT_DENSE);
    pk_game_mestext(g, 0x146, 0x32 + 0x26, 0x73 + 0xc, 0, PK_ALIGN_CENTER, 1, PK_FONT_DENSE);
    pk_game_mestext(g, k_help[s_rank.cursor & 3], 0x58, 0xb5, 0xFFFFFFu, PK_ALIGN_CENTER, 1, PK_FONT_DENSE);
}

/* ------------------------------------------------------------------ */
/* the Rank Up dictionary (0x10056738): the 70 rank-ups of the table
 * 0x1009dd4c, five rows (plates at 8, 0x1e + 0x14 * row), each named
 * (FUN_100627d8 kind 6: itemdata.dat at 0x100abeb0[6], 0x40 bytes a
 * record) once game+0x5bc knows it; up / down move, left / right page by
 * five, 5 or back return */
static struct
{
    int top, cur;
} s_dict;

static void dict_name(int k, Uint16 *out, int cap)
{
    size_t n = 0;
    Uint8 *d = (Uint8 *)pk_fs_load("itemdata.dat", &n);
    int idx = (int)pk_app_i32(0x1009dd4cu + (Uint32)k * 16u + 12u), i = 0;
    size_t off = (size_t)pk_app_i32(0x100abeb0u + 6u * 4u) + (size_t)idx * 0x40u;
    out[0] = 0;
    if (d && idx >= 0 && idx < (int)pk_app_i32(0x100abeccu + 6u * 4u) && off + 0x40 <= n) {
        for (i = 0; i < 0x20 && i < cap - 1; i++) {
            Uint16 c = (Uint16)(d[off + (size_t)i * 2] | (d[off + (size_t)i * 2 + 1] << 8));
            if (c == 0x1b || c == 0) {
                break;
            }
            out[i] = c;
        }
        out[i] = 0;
    }
    SDL_free(d);
}

static void dict_row_update(pk_object *o, pk_game *g)
{
    pk_object *root = pk_ui_root(g);
    if (!root || root->update != pk_rankup_update || root->state != 0x30) {
        pk_object_kill(o);
        return;
    }
    o->image = pk_game_load_image(g, 0x17, 1);
    pk_object_set_frames(o, 0x1009c18cu);
    o->layer = 1;
    o->flags = 0x4c1;
    o->frame = s_dict.cur == o->arg1 ? 0x11 : 0x10;
}

static void dict_draw(pk_game *g)
{
    int i;
    char num[8];
    for (i = 0; i < 5; i++) {
        int k = s_dict.top + i;
        SDL_snprintf(num, sizeof(num), "%d", k + 1);
        pk_drawq_text_utf8(&g->drawq, num, 8 + 0x14, 0x1e + 0x14 * i + 0xe, 0x666666u, PK_ALIGN_RIGHT, 1, PK_FONT_DENSE);
        if (g->world.b5bc[k >> 3] & (1 << (k & 7))) {
            Uint16 nm[33];
            dict_name(k, nm, 33);
            pk_drawq_text(&g->drawq, nm, 8 + 0x1c, 0x1e + 0x14 * i + 0xf, 0, PK_ALIGN_LEFT, 1, PK_FONT_DENSE);
        }
    }
}

static void do_rankup(pk_game *g)
{
    int cost = rank_cost();
    if (g->loot < cost) {
        pk_audio_play_sfx(4);
        pk_help(g, 1, 0x18);
        return;
    }
    pk_unit_template(s_rank.result, 9);
    SDL_memcpy(s_rank_name, pk_r.unit[9].name, sizeof(s_rank_name));
    pk_loot_sub(g, cost);
    { /* FUN_1005826c: the dictionary entry (table 0x1009dd4c, 70 x
       * {emblem, unit type, result, name}) becomes known (game+0x5bc) */
        int k, best = -1, em = g->world.items[s_rank.emblem][1], ut = g->world.units[s_rank.unit][0];
        for (k = 0; k < 70; k++) {
            Uint32 a = 0x1009dd4cu + (Uint32)k * 16u;
            int m = ((int)pk_app_i32(a) == em) + ((int)pk_app_i32(a + 4) == ut) + ((int)pk_app_i32(a + 8) == s_rank.result);
            if (m == 3) {
                best = k;
                break;
            }
        }
        if (best >= 0) {
            g->world.b5bc[best >> 3] |= (Uint8)(1 << (best & 7));
        }
    }
    pk_unit_set_type(g, s_rank.unit, s_rank.result);
    pk_item_remove(g, s_rank.emblem);
    pk_items_sort(g);
    pk_units_level(g);
    s_rank.unit = s_rank.emblem = s_rank.result = 0xff;
    pk_audio_play_sfx(3);
    pk_help_set_word(s_rank_name);
    pk_help(g, 1, 0x1a);
}

void pk_rankup_update(pk_object *o, pk_game *g)
{
    switch (o->state) {
    case 0xc:
        if (g->curtain != PK_CURTAIN_HELD || ++o->counter < 4) {
            break;
        }
        pk_game_set_curtain(g, PK_CURTAIN_OPEN);
        pk_object_spawn(g, pk_kingdom_ui_update, o->dir, 0xd0, 0, 0, 0, 0xc2);
        spawn_top_bar(o, g);
        g->field_bg = 10;
        pk_game_set_bg(g, 0, 10);
        g->bg_visible = 1;
        s_rank.unit = s_rank.emblem = s_rank.result = 0xff;
        s_rank.cursor = 0;
        o->work[7] = 0;
        o->state = 0xd;
        break;
    case 0xd:
        if (g->curtain != PK_CURTAIN_NONE) {
            break;
        }
        if (o->work[7] == 0) {
            static const int k_pos[4][2] = { { 8, 0x23 }, { 8, 0x41 }, { 0x32, 0x5f }, { 0x32, 0x73 } };
            int k;
            for (k = 0; k < 4; k++) {
                pk_object_spawn(g, rank_panel_update, 0, 0x10, 0x10, k, k_pos[k][0], k_pos[k][1]);
            }
            pk_object_spawn(g, rank_panel_update, 0, 0x20, 0x20, 0, 8 + 4, 0x23 + 2);
            pk_object_spawn(g, rank_panel_update, 0, 0x30, 0x30, 1, 8 + 4, 0x41 + 1);
            o->work[7] = 1;
        }
        rank_draw(g);
        if (pk_help_active()) {
            break;
        }
        { /* touch: the two plates and the two buttons */
            static const int k_hit[4][4] = { { 8, 0x23, 0xa0, 0x1c }, { 8, 0x41, 0xa0, 0x1c }, { 0x32, 0x5f, 0x48, 0x13 }, { 0x32, 0x73, 0x48, 0x13 } };
            int k;
            for (k = 0; k < 4; k++) {
                if (pk_tap(g, k_hit[k][0], k_hit[k][1], k_hit[k][2], k_hit[k][3])) {
                    s_rank.cursor = k;
                    g->key_code = PK_SC_5;
                }
            }
        }
        if (g->key_code == PK_SC_UP || g->key_code == PK_SC_DOWN) {
            pk_audio_play_sfx(2);
            s_rank.cursor = (s_rank.cursor + (g->key_code == PK_SC_UP ? 3 : 1)) % 4;
        } else if (g->key_code == PK_SC_5) {
            pk_audio_play_sfx(0);
            g->list_top = g->cursor = 0;
            g->slide = 0;
            if (s_rank.cursor == 0) {
                o->work[7] = 0;
                g->list_120 = g->list_124 = 0;
                pk_units_sort(g, true);
                s_rank.unit = 0xff; /* indices change with the sort */
                pk_object_call(g, o, pk_unit_list_update, 0, pk_rankup_update, 0x10);
            } else if (s_rank.cursor == 1) {
                o->work[7] = 0;
                g->dlg_type = 4;
                pk_object_call(g, o, pk_item_list_update, 0, pk_rankup_update, 0x11);
            } else if (s_rank.cursor == 3) { /* the dictionary (mestext 0x146) */
                int k;
                o->work[7] = 0;
                s_dict.top = s_dict.cur = 0;
                o->state = 0x30;
                for (k = 0; k < 5; k++) {
                    pk_object_spawn(g, dict_row_update, 0, 0x10, 0x40, k, 8, 0x1e + 0x14 * k);
                }
            } else if (s_rank.result != 0xff) {
                o->work[7] = 0;
                g->dlg_type = 0xc;
                pk_object_call(g, o, pk_dialog_update, 0, pk_rankup_update, 0x12);
            } else if (s_rank.unit != 0xff && s_rank.emblem != 0xff) {
                pk_help(g, 1, 0x19); /* these cannot rank up together */
            }
        } else if (g->key_code == PK_SC_BACK) {
            pk_audio_play_sfx(1);
            pk_game_set_curtain(g, PK_CURTAIN_CLOSE);
            o->work[7] = 0;
            o->state = 0x20;
        }
        break;
    case 0x30: /* the dictionary */
        dict_draw(g);
        { /* touch: a row is marked, scrolling moves the list, a tap below it closes it */
            int k;
            for (k = 0; k < 5; k++) {
                if (pk_tap(g, 8, 0x1e + 0x14 * k, 0xa0, 0x14)) {
                    s_dict.cur = k;
                    pk_audio_play_sfx(2);
                }
            }
            if (pk_tap(g, 0, 0x84, PK_SCREEN_W, PK_SCREEN_H - 0x84)) {
                g->key_code = PK_SC_BACK;
            }
            pk_swipe_list(g, &s_dict.top, &s_dict.cur, 0x46, 5);
        }
        if (g->key_code == PK_SC_UP) {
            pk_audio_play_sfx(2);
            if (s_dict.cur > 0) {
                s_dict.cur--;
            } else if (s_dict.top > 0) {
                s_dict.top--;
            }
        } else if (g->key_code == PK_SC_DOWN) {
            pk_audio_play_sfx(2);
            if (s_dict.top + s_dict.cur < 0x45) {
                if (s_dict.cur < 4) {
                    s_dict.cur++;
                } else {
                    s_dict.top++;
                }
            }
        } else if (g->key_code == PK_SC_RIGHT) {
            pk_audio_play_sfx(2);
            s_dict.top = s_dict.top < 0x3e ? s_dict.top + 5 : 0x41;
        } else if (g->key_code == PK_SC_LEFT) {
            pk_audio_play_sfx(2);
            s_dict.top = s_dict.top < 5 ? 0 : s_dict.top - 5;
        } else if (g->key_code == PK_SC_5 || g->key_code == PK_SC_BACK) {
            pk_audio_play_sfx(1);
            o->work[7] = 0;
            o->state = 0xd;
        }
        break;
    case 0x10:
        if (g->dlg_result == 0) {
            s_rank.unit = g->list_top + g->cursor;
        }
        rank_lookup(g);
        o->state = 0xd;
        break;
    case 0x11:
        if (g->dlg_result == 0) {
            int idx = pk_item_find(g, 4, g->list_top + g->cursor);
            if (idx >= 0 && idx != 0xff) {
                s_rank.emblem = idx;
            }
        }
        rank_lookup(g);
        o->state = 0xd;
        break;
    case 0x12:
        if (g->dlg_result == 0 && g->list_top + g->cursor == 0) {
            do_rankup(g);
        }
        o->state = 0xd;
        break;
    case 0x20:
        if (g->curtain != PK_CURTAIN_HELD) {
            break;
        }
        pk_object_kill_others(g, o);
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

/* ------------------------------------------------------------------ */
/* Upgrade (0x10045dc8): improvements to the kingdom, shown on the
 * world map (castle sprite frames, 0x1004017c state 0x30).
 *   category 0  castle tier   game+0x4e4 & 3, 4 tiers, cost 0x1009863c
 *   category 1  game+0x4e8    5 choices (5 = none), cost 0x1009864c
 *   category 2  game+0x4ec    5 choices (5 = none), cost 0x10098664
 *   category 3  castle style  game+0x4e4 >> 2, 4 choices, 2000 loot
 * Names are mestext ids: categories 0xa3 + c; choices 0xab + 2n,
 * 0xb9 + n, 0xc4 + n, 0x107 + n. Owned choices cannot be bought again.
 * The port shows the screens as a text panel. */

static struct
{
    int level; /* 0 categories, 1 choices */
    int cat, cursor;
} s_upg;

static int upg_count(int cat) { return cat == 1 || cat == 2 ? 5 : 4; }

static int upg_cost(int cat, int n)
{
    if (cat == 0) {
        return (int)pk_app_i32(0x1009863cu + (Uint32)n * 4u);
    }
    if (cat == 1) {
        return (int)pk_app_i32(0x1009864cu + (Uint32)n * 4u);
    }
    if (cat == 2) {
        return (int)pk_app_i32(0x10098664u + (Uint32)n * 4u);
    }
    return 2000;
}

static void upg_confirm_hint(pk_game *g)
{
    char buf[24];
    SDL_snprintf(buf, sizeof(buf), "%d", upg_cost(s_upg.cat, s_upg.cursor));
    pk_game_mestext(g, 0x18, 0x59, 0xb5, 0, PK_ALIGN_RIGHT, 1, PK_FONT_LEGEND);
    pk_game_mestext(g, 0x18, 0x58, 0xb4, 0xFFFFFFu, PK_ALIGN_RIGHT, 1, PK_FONT_LEGEND);
    pk_drawq_text_utf8(&g->drawq, buf, 0x59, 0xb5, 0, PK_ALIGN_LEFT, 1, PK_FONT_LEGEND);
    pk_drawq_text_utf8(&g->drawq, buf, 0x58, 0xb4, 0xFFFFFFu, PK_ALIGN_LEFT, 1, PK_FONT_LEGEND);
}

static int upg_label(int cat, int n)
{
    if (cat == 0) {
        return 0xab + n * 2;
    }
    if (cat == 1) {
        return 0xb9 + n;
    }
    if (cat == 2) {
        return 0xc4 + n;
    }
    return 0x107 + n;
}

static int upg_owned(pk_game *g, int cat)
{
    if (cat == 0) {
        return g->world.h_4e4 & 3;
    }
    if (cat == 1) {
        return g->world.h_4e8;
    }
    if (cat == 2) {
        return g->world.h_4ec;
    }
    return g->world.h_4e4 >> 2;
}

/* the sub-screens' preview window and pictures (0x10045dc8 states 0x10
 * window rows at y 0xe..0x32 and 0x2a..0x2c): image 0x45, frames
 * 0x1009815c, 32 x 32. Layers: 0 transporter back (+0x4e8 * 2 + 3),
 * 1 castle (+0x4e4 + 0x12: size + colour * 4), 2 transporter front
 * (+0x4e8 * 2 + 2), 3 mark (+0x4ec + 0xc); 5 means none. The owned look is
 * at 0x2d, 0x24, the one under the cursor (0x10 + layer) at 0x83, 0x24,
 * with the arrow (frame 0x11) between them at 0x58. */
static void upg_preview_update(pk_object *o, pk_game *g)
{
    pk_object *root = pk_ui_root(g);
    int cs, cc, tr, mk, layer = o->arg1 & 0xf, fr = -1;
    if (!root || root->update != pk_upgrade_update || root->state != 0xd) {
        pk_object_kill(o);
        return;
    }
    o->layer = 1;
    if (o->state == 0x10) { /* window rows */
        o->image = pk_game_load_image(g, 0x17, 1);
        pk_object_set_frames(o, 0x1007ea68u);
        o->frame = o->arg0 + 0x40;
        o->layer = 0;
        o->flags = s_upg.level == 1 ? 0x4e1 : 0x4e0;
        return;
    }
    o->image = pk_game_load_image(g, 0x45, 1);
    pk_object_set_frames(o, 0x1009815cu);
    cs = g->world.h_4e4 & 3;
    cc = g->world.h_4e4 >> 2;
    tr = g->world.h_4e8;
    mk = g->world.h_4ec;
    if (o->arg1 >= 0x10 && o->arg1 < 0x20) {
        if (s_upg.cat == 0) {
            cs = s_upg.cursor;
        } else if (s_upg.cat == 1) {
            tr = s_upg.cursor;
        } else if (s_upg.cat == 2) {
            mk = s_upg.cursor;
        } else {
            cc = s_upg.cursor;
        }
    }
    if (o->arg1 == 0x20) {
        fr = 0x11;
    } else if (layer == 0) {
        fr = tr == 5 ? -1 : tr * 2 + 3;
    } else if (layer == 1) {
        fr = cs + cc * 4 + 0x12;
    } else if (layer == 2) {
        fr = tr == 5 ? -1 : tr * 2 + 2;
    } else if (layer == 3) {
        fr = mk == 5 ? -1 : mk + 0xc;
    }
    o->flags = s_upg.level == 1 && fr >= 0 ? 0x4c1 : 0x4c0;
    if (fr >= 0) {
        o->frame = fr;
    }
}

/* the Upgrade plates (0x10045dc8 state 0x26: image 0x17, frames
 * 0x10096ecc, 132 x 14, 0x3c / selected 0x3d) at 0x16, 0x28 + 0x14 * row;
 * their labels (FUN_1004932c) come from the table 0x10096e0c: group 5 the
 * four choices, groups 0..3 the items of each */
static void upg_panel_update(pk_object *o, pk_game *g)
{
    pk_object *root = pk_ui_root(g);
    int n = s_upg.level == 0 ? 4 : upg_count(s_upg.cat);
    if (!root || root->update != pk_upgrade_update || root->state != 0xd) {
        pk_object_kill(o);
        return;
    }
    o->image = pk_game_load_image(g, 0x17, 1);
    pk_object_set_frames(o, 0x10096eccu);
    o->layer = 1;
    o->flags = o->arg1 < n ? 0x4c1 : 0x4c0;
    o->frame = s_upg.cursor == o->arg1 ? 0x3d : 0x3c;
    o->y = s_upg.level == 0 ? 0x28 + 0x14 * o->arg1 : 0x41 + 0x10 * o->arg1; /* states 0xe / 0x12 */
}

static void upg_draw(pk_game *g)
{
    char buf[48];
    int i, n, grp = s_upg.level == 0 ? 5 : s_upg.cat;
    n = s_upg.level == 0 ? 4 : upg_count(s_upg.cat);
    for (i = 0; i < n; i++) {
        int id = (int)pk_app_i32(0x10096e0cu + (Uint32)(grp * 8 + i) * 4u);
        int owned = s_upg.level == 1 && upg_owned(g, s_upg.cat) == i;
        int py = s_upg.level == 0 ? 0x28 + 0x14 * i : 0x41 + 0x10 * i;
        if (id) {
            pk_game_mestext(g, id, 0x16 + 0x42, py + 0xc, owned ? 0x2266DDu : 0, PK_ALIGN_CENTER, 1,
                            PK_FONT_DENSE);
        }
    }
    if (s_upg.level == 0) { /* state 0xf: the help line 0xa3 + cursor */
        pk_game_mestext(g, 0xa3 + s_upg.cursor, 0x58, 0xb4, 0xFFFFFFu, PK_ALIGN_CENTER, 1, PK_FONT_DENSE);
        return;
    }
    if (s_upg.cat == 0) { /* state 0x13: two lines 0xab / 0xac + 2 * cursor */
        pk_game_mestext(g, 0xab + s_upg.cursor * 2, 0x58, 0xa7, 0xFFFFFFu, PK_ALIGN_CENTER, 1, PK_FONT_DENSE);
        pk_game_mestext(g, 0xac + s_upg.cursor * 2, 0x58, 0xb6, 0xFFFFFFu, PK_ALIGN_CENTER, 1, PK_FONT_DENSE);
    } else if (s_upg.cat == 1) { /* state 0x17: 0xb9 + cursor */
        pk_game_mestext(g, 0xb9 + s_upg.cursor, 0x58, 0xae, 0xFFFFFFu, PK_ALIGN_CENTER, 1, PK_FONT_DENSE);
    } else {
        pk_game_mestext(g, upg_label(s_upg.cat, s_upg.cursor), 0x58, 0xae, 0xFFFFFFu, PK_ALIGN_CENTER, 1,
                        PK_FONT_DENSE);
    }
    if (upg_owned(g, s_upg.cat) != s_upg.cursor) {
        SDL_snprintf(buf, sizeof(buf), "%d", upg_cost(s_upg.cat, s_upg.cursor));
        pk_drawq_text_utf8(&g->drawq, buf, 0xaa, 0xc6, 0xFFDC00u, PK_ALIGN_RIGHT, 1, PK_FONT_DENSE);
    }
}

static void upg_buy(pk_game *g)
{
    int cat = s_upg.cat, n = s_upg.cursor, cost = upg_cost(cat, n);
    if (upg_owned(g, cat) == n || g->loot <= cost) {
        pk_audio_play_sfx(1);
        return;
    }
    pk_loot_sub(g, cost);
    if (cat == 0) {
        g->world.h_4e4 = (g->world.h_4e4 & ~3) | n;
    } else if (cat == 1) {
        g->world.h_4e8 = n;
    } else if (cat == 2) {
        g->world.h_4ec = n;
    } else {
        g->world.h_4e4 = (g->world.h_4e4 & 3) | (n << 2);
    }
    pk_audio_play_sfx(3);
    PK_DEBUG("upgrade %d -> %d (%d loot)", cat, n, cost);
}

void pk_upgrade_update(pk_object *o, pk_game *g)
{
    switch (o->state) {
    case 0xc:
        if (g->curtain != PK_CURTAIN_HELD || ++o->counter < 4) {
            break;
        }
        pk_game_set_curtain(g, PK_CURTAIN_OPEN);
        pk_object_spawn(g, pk_kingdom_ui_update, o->dir, 0xd0, 0, 0, 0, 0xc2);
        spawn_top_bar(o, g);
        g->field_bg = 0xc;
        pk_game_set_bg(g, 0, 0xc);
        g->bg_visible = 1;
        s_upg.level = s_upg.cat = s_upg.cursor = 0;
        o->work[7] = 0;
        o->state = 0xd;
        break;
    case 0xd:
        if (g->curtain != PK_CURTAIN_NONE) {
            break;
        }
        if (o->work[7] != 1) {
            int k;
            for (k = 0; k < 5; k++) {
                pk_object_spawn(g, upg_panel_update, 0, 0x26, 5, k, 0x16, 0x28 + 0x14 * k);
            }
            for (k = 0; k < 10; k++) {
                pk_object_spawn(g, upg_preview_update, 0, 0x10, k == 0 ? 0 : k == 9 ? 2
                                                                                    : 1,
                                0, 0, 0xe + 4 * k);
            }
            for (k = 0; k < 4; k++) {
                pk_object_spawn(g, upg_preview_update, 0, 0x2a, 0, k, 0x2d, 0x24);
                pk_object_spawn(g, upg_preview_update, 0, 0x2a, 0, 0x10 + k, 0x83, 0x24);
            }
            pk_object_spawn(g, upg_preview_update, 0, 0x2a, 0, 0x20, 0x58, 0x24);
            if (o->work[7] == 0) {
                pk_help(g, 1, 0x21);
            }
            o->work[7] = 1;
        }
        upg_draw(g);
        if (pk_help_active()) {
            break;
        }
        {
            int c = s_upg.cursor;
            int y = s_upg.level == 0 ? 0x28 : 0x41;
            int h = s_upg.level == 0 ? 0x14 : 0x10;
            if (pk_tap_rows(g, 0x16, y, 0x84, h, s_upg.level == 0 ? 4 : upg_count(s_upg.cat), &c)) {
                s_upg.cursor = c;
                if (s_upg.level == 1) {
                    g->dlg_type = 0xc;
                    g->list_top = g->cursor = 0;
                    g->slide = 0;
                    g->dlg_hint = upg_confirm_hint;
                    pk_object_call(g, o, pk_dialog_update, 0, pk_upgrade_update, 0x14);
                    break;
                }
            }
        }
        if (g->key_code == PK_SC_UP || g->key_code == PK_SC_DOWN) {
            int n = s_upg.level == 0 ? 4 : upg_count(s_upg.cat);
            pk_audio_play_sfx(2);
            s_upg.cursor = (s_upg.cursor + (g->key_code == PK_SC_UP ? n - 1 : 1)) % n;
        } else if (g->key_code == PK_SC_5) {
            if (s_upg.level == 0) {
                pk_audio_play_sfx(0);
                s_upg.cat = s_upg.cursor;
                s_upg.level = 1;
                s_upg.cursor = 0;
            } else {
                upg_buy(g);
            }
        } else if (g->key_code == PK_SC_BACK) {
            pk_audio_play_sfx(1);
            if (s_upg.level == 1) {
                s_upg.level = 0;
                s_upg.cursor = s_upg.cat;
            } else {
                pk_game_set_curtain(g, PK_CURTAIN_CLOSE);
                o->work[7] = 0;
                o->state = 0x20;
            }
        }
        break;
    case 0x14:
        if (g->dlg_result == 0 && g->list_top + g->cursor == 0) {
            upg_buy(g);
        }
        g->dlg_hint = NULL;
        g->slide = 1;
        o->work[7] = 2;
        o->state = 0xd;
        break;
    case 0x20:
        if (g->curtain != PK_CURTAIN_HELD) {
            break;
        }
        pk_object_kill_others(g, o);
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
