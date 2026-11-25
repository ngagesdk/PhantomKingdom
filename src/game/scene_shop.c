/* SPDX-License-Identifier: MIT */
/* The shop (root update 0x1005b754) and its buy lists: items and
 * equipment from itemshop.dat (0x1005c7b4), units from unitshop.dat
 * (0x1005dac4). The kingdom root object (type 0xc2) switches to these
 * update functions; the generic dialog (ui_dialog.c) asks "Item / Unit"
 * and "Yes / No". Prices in the data are half of what the shop asks. */
#include "audio/pk_audio.h"
#include "core/pk_app.h"
#include "game/pk_arena_game.h"
#include "game/pk_rules.h"
#include "game/pk_scenes.h"
#include "game/ui_dialog.h"
#include "gfx/pk_font.h"

#define W_MENU   0 /* +0x18 kingdom menu shown by the interface children */
#define W_CURSOR 1 /* +0x1c */

#define IMG_UI          0x17
#define IMG_UNIT_ICON   0x19
#define IMG_SHOP_BG     0xc
#define TBL_LIST_FRAMES 0x100a10a4u
#define TBL_KIND_ICON   0x100a1050u /* item kind -> icon frame */
#define TBL_KIND_LABEL  0x100a1070u /* item kind -> mestext */
#define TBL_UNIT_ICONS  0x100a2334u /* frames of bitmap 0x19 */
#define TBL_PORTRAIT    0x100a2c64u /* two halves of a pokeport.mbm portrait */
#define PORTRAIT_BASE   0x1e8       /* image ids of pokeport.mbm bitmaps */

#define COL_WHITE 0xFFFFFFu
#define COL_GOLD  0xFFDC00u /* TRgb 0xdcff */

static void kill_screen_children(pk_game *g, pk_object *root)
{
    pk_object_kill_others(g, root); /* FUN_100252d0 */
}

/* FUN_10024604: the top bar (fame, loot, VP) */
static void spawn_top_bar(pk_object *o, pk_game *g)
{
    pk_object_spawn(g, pk_kingdom_ui_update, o->dir, 0x80, 1, 2, 0x10, 3);
    pk_object_spawn(g, pk_kingdom_ui_update, o->dir, 0x80, 1, 3, 0x61, 3);
    pk_object_spawn(g, pk_kingdom_ui_update, o->dir, 0x80, 1, 4, 0xa1, 3);
    pk_object_spawn(g, pk_kingdom_ui_update, o->dir, 0x80, 0, 0x7a, 0, 0x92);
}

static void call(pk_object *o, pk_game *g, pk_object_fn fn, int ret_state)
{
    pk_object_call(g, o, fn, 0, pk_shop_update, ret_state);
}

static void save_pos(pk_game *g)
{
    g->list_f8 = g->list_top;
    g->list_fc = g->cursor;
} /* FUN_10029bfc */
static void load_pos(pk_game *g)
{
    g->list_top = g->list_f8;
    g->cursor = g->list_fc;
} /* FUN_10029c10 */
static void clear_pos(pk_game *g) { g->list_f8 = g->list_fc = 0; } /* FUN_10029bec */

/* FUN_100284ec: shows entry idx of n with `page` rows visible */
static void list_position(pk_game *g, int idx, int n, int page)
{
    if (idx < page) {
        g->list_top = 0;
        g->cursor = idx;
    } else if (idx < n - page) {
        g->list_top = idx;
        g->cursor = 0;
    } else {
        g->list_top = n - page;
        g->cursor = idx - (n - page);
    }
    if (g->cursor < 0) {
        g->cursor = 0;
    }
}

static void draw_number(pk_game *g, int v, int x, int y, pk_color c, pk_align a)
{
    char buf[16];
    SDL_snprintf(buf, sizeof(buf), "%d", v);
    pk_drawq_text_utf8(&g->drawq, buf, x + 1, y + 1, 0, a, 1, PK_FONT_LEGEND);
    pk_drawq_text_utf8(&g->drawq, buf, x, y, c, a, 1, PK_FONT_LEGEND);
}

static void shadow_mes(pk_game *g, int id, int x, int y, pk_color c, pk_align a)
{
    pk_game_mestext(g, id, x + 1, y + 1, 0, a, 1, PK_FONT_LEGEND); /* FUN_10065578 */
    pk_game_mestext(g, id, x, y, c, a, 1, PK_FONT_LEGEND);
}

static void shadow_text(pk_game *g, const Uint16 *s, int x, int y, pk_color c, pk_align a)
{
    pk_drawq_text(&g->drawq, s, x + 1, y + 1, 0, a, 1, PK_FONT_LEGEND); /* FUN_100655f4 */
    pk_drawq_text(&g->drawq, s, x, y, c, a, 1, PK_FONT_LEGEND);
}

static int item_price(int slot)
{
    return pk_r.shop_kind[slot] < PK_KIND_EQUIP ? pk_r.item[slot].price : pk_r.equip[slot].price;
}

/* Lines under the dialog while the shop calls it (cases of 0x1002637c
 * for caller 0x1005b754). */
static void shop_hint(pk_game *g)
{
    switch (g->call_ret_state) {
    case 0x0a:
    case 0x12:
        shadow_mes(g, 0x9a, 0x55, 0xb2, COL_WHITE, PK_ALIGN_CENTER); /* Select Item or Unit. */
        break;
    case 0x0b:
        shadow_mes(g, 0x9b, 0x50, 0xb2, COL_WHITE, PK_ALIGN_CENTER); /* Select Item. */
        break;
    case 0x0d:
    case 0x14:
        shadow_mes(g, 0x18, 0x58, 0xb4, COL_WHITE, PK_ALIGN_RIGHT); /* Price : */
        if (g->call_ret_state == 0x14) {
            draw_number(g, item_price(g->sel) * 2, 0x58, 0xb4, COL_WHITE, PK_ALIGN_LEFT);
        } else { /* selling: the preview of the chosen row, single price */
            int p = g->sel < PK_KIND_EQUIP ? pk_r.item[g->list_fc].price : pk_r.equip[g->list_fc].price;
            draw_number(g, p, 0x58, 0xb4, COL_WHITE, PK_ALIGN_LEFT);
        }
        break;
    case 0x0f:
    case 0x16:
        shadow_mes(g, 0x18, 0x58, 0xb4, COL_WHITE, PK_ALIGN_RIGHT);
        if (g->sel != 0xff) {
            int p = pk_r.unit[g->sel].price;
            draw_number(g, g->call_ret_state == 0x16 ? p * 2 : p, 0x58, 0xb4, COL_WHITE, PK_ALIGN_LEFT);
        }
        break;
    default:
        break;
    }
}

/* ------------------------------------------------------------------ */
/* item and equipment buy list (0x1005c7b4) */

static void item_info(pk_game *g)
{
    int idx = g->list_top + g->cursor;
    int kind = idx < 5 ? pk_r.shop_kind[idx] : 0xff;
    char buf[24];
    shadow_mes(g, 0x17, 0x33, 0xa6, COL_WHITE, PK_ALIGN_RIGHT); /* Kind : */
    if (kind != 0xff) {
        shadow_mes(g, pk_app_i32(TBL_KIND_LABEL + (Uint32)kind * 4u), 0x37, 0xa6, COL_WHITE, PK_ALIGN_LEFT);
    }
    shadow_mes(g, 0x11, 0x33, 0xb5, COL_WHITE, PK_ALIGN_RIGHT); /* Rarity : */
    shadow_mes(g, 0x18, 0x33, 0xc4, COL_WHITE, PK_ALIGN_RIGHT); /* Price : */
    if (kind != 0xff) {
        int p = kind < PK_KIND_EQUIP ? pk_r.item[g->cursor].price : pk_r.equip[g->cursor].price;
        draw_number(g, p * 2, 0x37, 0xc4, COL_WHITE, PK_ALIGN_LEFT);
    }
    if (kind < PK_KIND_EQUIP || kind == 0xff) {
        SDL_snprintf(buf, sizeof(buf), "%d/%d", g->world.item_count, PK_ITEM_MAX);
    } else {
        SDL_snprintf(buf, sizeof(buf), "%d/%d", g->world.equip_count, PK_EQUIP_MAX);
    }
    pk_drawq_text_utf8(&g->drawq, buf, 0xaf, 0x90, 0, PK_ALIGN_RIGHT, 1, PK_FONT_LEGEND);
    pk_drawq_text_utf8(&g->drawq, buf, 0xae, 0x8f, COL_GOLD, PK_ALIGN_RIGHT, 1, PK_FONT_LEGEND);
}

static int shop_kind_at(pk_game *g, int row)
{
    int i = g->list_top + row;
    return i < 5 ? pk_r.shop_kind[i] : 0xff;
}

static const Uint16 *list_name(int slot, int kind)
{
    return kind < PK_KIND_EQUIP ? pk_r.item[slot].name : pk_r.equip[slot].name;
}

/* rows 0x10, icons and names 0x20, cursor 8, rarity stars 0x50 */
static void list_child(pk_object *o, pk_game *g, pk_object_fn self, int (*kind_at)(pk_game *, int),
                       int (*rank_at)(pk_game *), int count, bool units)
{
    pk_object *row;
    int k;
    switch (o->state) {
    case 8:
        pk_ui_setup_sprite(g, o, IMG_UI, TBL_LIST_FRAMES, 0x4c1);
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
        if (pk_ui_root_left(g, self)) {
            o->counter = 0;
            o->state = 0xb;
        }
        break;
    case 0xb:
        pk_object_animate(o);
        if (pk_ui_slide_out(o, g)) {
            pk_object_kill(o);
        }
        break;
    case 0x10:
    case 0x12:
        if (o->state == 0x10) {
            pk_ui_setup_sprite(g, o, IMG_UI, TBL_LIST_FRAMES, 0x4c1);
        }
        k = kind_at(g, o->arg1);
        if (units && k == 0xff) {
            o->frame = 0x12;
        } else {
            o->frame = (k == 0xff || g->cursor != o->arg1) ? 0x10 : 0x11;
        }
        if (o->state == 0x10) {
            pk_object_clear_work(o);
            o->counter = 0;
            pk_ui_start_offset(o);
            o->state = 0x11;
        } else if (pk_ui_root_left(g, self)) {
            o->counter = 0;
            o->state = 0x13;
        }
        break;
    case 0x11:
        pk_ui_slide_in(o);
        if (++o->counter > 5) {
            o->counter = 0;
            o->state = 0x12;
        }
        break;
    case 0x13:
        if (pk_ui_slide_out(o, g)) {
            pk_object_kill(o);
        }
        break;
    case 0x20: /* icon + name, follows its row */
        row = pk_object_find(g, o->type, 0x40, o->arg1);
        k = kind_at(g, o->arg1);
        if (units) {
            pk_ui_setup_sprite(g, o, IMG_UNIT_ICON, TBL_UNIT_ICONS, 0x4c0);
        } else {
            pk_ui_setup_sprite(g, o, IMG_UI, TBL_LIST_FRAMES, 0x4c0);
        }
        o->frame = 0;
        if (k != 0xff) {
            o->flags |= PK_OBJ_SPRITE;
            o->frame = units ? pk_r.unit[o->arg1].b[0] * 2 : pk_app_i32(TBL_KIND_ICON + (Uint32)k * 4u);
        }
        o->work[0] = k;
        o->work[1] = 0;
        o->work[2] = o->frame;
        o->state = 0x21;
        /* fallthrough */
    case 0x21:
        row = pk_object_find(g, o->type, 0x40, o->arg1);
        if (!row || row->dead) {
            pk_object_kill(o);
            break;
        }
        if (units) { /* two-frame idle animation, 6 logic frames each */
            o->work[1] = (o->work[1] + 1) % 12;
            o->frame = o->work[2] + o->work[1] / 6;
        }
        o->x = row->x + 4;
        o->y = row->y + (units ? 2 : 1);
        if (o->flags & PK_OBJ_SPRITE) {
            const Uint16 *name = units ? pk_r.unit[o->arg1].name : list_name(o->arg1, kind_at(g, o->arg1));
            pk_drawq_text(&g->drawq, name, o->x + 0x18, o->y + (units ? 0xd : 0xe), 0, PK_ALIGN_LEFT, 0,
                          PK_FONT_LEGEND);
        }
        break;
    case 0x50: /* rarity star k = arg1 */
        pk_ui_setup_sprite(g, o, IMG_UI, TBL_LIST_FRAMES, 0x4c1);
        o->frame = 0x3f;
        pk_object_clear_work(o);
        o->counter = 0;
        o->state = 0x51;
        break;
    case 0x51:
        if (g->curtain == PK_CURTAIN_NONE) {
            int idx = g->list_top + g->cursor;
            o->frame = (idx >= count || kind_at(g, g->cursor) == 0xff || rank_at(g) < o->arg1) ? 0x3f : 0x3e;
        }
        if (pk_ui_root_left(g, self)) {
            pk_object_kill(o);
        }
        break;
    default:
        break;
    }
}

static int item_rank(pk_game *g)
{
    int idx = g->list_top + g->cursor;
    if (idx >= 5 || pk_r.shop_kind[idx] == 0xff) {
        return 0;
    }
    return pk_r.shop_kind[idx] < PK_KIND_EQUIP ? pk_r.item[idx].rec[6] : pk_r.equip[idx].rec[5];
}

static void buy_items_update(pk_object *o, pk_game *g);

static void buy_items_child(pk_object *o, pk_game *g)
{
    list_child(o, g, buy_items_update, shop_kind_at, item_rank, pk_r.shop_count, false);
}

static void buy_items_update(pk_object *o, pk_game *g)
{
    int i, idx, kind;
    if (o->type != 0xc2) {
        buy_items_child(o, g);
        return;
    }
    switch (o->state) {
    case 0:
        pk_shop_load_items(g->world.current_area);
        for (i = 0; i < 5; i++) {
            pk_object_spawn(g, buy_items_update, g->slide, 0x10, 0x40, i, 8, 0x1e + i * 0x14);
        }
        for (i = 0; i < 5; i++) {
            pk_object_spawn(g, buy_items_update, g->slide, 0x20, 0x50, i, 8, 0x1e + i * 0x14);
        }
        pk_object_spawn(g, buy_items_update, g->slide, 8, 0x60, 0, 8, 0x22);
        for (i = 1; i <= 10; i++) {
            pk_object_spawn(g, buy_items_update, g->slide, 0x50, 0x50, i, 0x39 + (i - 1) * 6, 0xac);
        }
        for (i = 0; i < 5; i++) {
            if (pk_r.shop_kind[i] != 0xff) {
                pk_item_preview(pk_r.shop_kind[i], pk_r.shop_id[i], i);
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
        item_info(g);
        break;
    case 2:
        pk_tap_rows(g, 8, 0x1e, 0xa0, 0x14, SDL_min(5, pk_r.shop_count - g->list_top), &g->cursor); /* touch */
        pk_swipe_list(g, &g->list_top, &g->cursor, pk_r.shop_count, 5);
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
            if (g->list_top + g->cursor < pk_r.shop_count - 1) {
                if (g->cursor == 4) {
                    g->list_top++;
                } else {
                    g->cursor++;
                }
            }
        } else if (g->key_code == PK_SC_5) {
            bool room = true;
            int price;
            idx = g->list_top + g->cursor;
            kind = idx < 5 ? pk_r.shop_kind[idx] : 0xff;
            if (kind < PK_KIND_EQUIP) {
                price = pk_r.item[idx].price;
                if (g->world.item_count > PK_ITEM_MAX - 1) {
                    room = false;
                }
            } else {
                price = idx < 5 ? pk_r.equip[idx].price : 0;
                if (g->world.equip_count > PK_EQUIP_MAX - 1) {
                    room = false;
                }
            }
            if (!room) {
                pk_help(g, 1, 0x39);
            }
            if (kind != 0xff && room && price * 2 <= g->loot) {
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
        item_info(g);
        break;
    default:
        break;
    }
}

/* ------------------------------------------------------------------ */
/* unit buy list (0x1005dac4) */

static void unit_info(pk_game *g)
{
    int idx = g->list_top + g->cursor;
    bool have = idx < 5 && pk_r.unit_shop[idx] != 0xff;
    char buf[24];
    shadow_mes(g, 0x1f, 0x2d, 0xa5, COL_WHITE, PK_ALIGN_RIGHT); /* Class : */
    if (have) {
        shadow_text(g, pk_r.unit[g->cursor].name, 0x30, 0xa5, COL_WHITE, PK_ALIGN_LEFT);
    }
    shadow_mes(g, 0x20, 0x2d, 0xb2, COL_WHITE, PK_ALIGN_RIGHT); /* Race : */
    if (have) {
        shadow_text(g, pk_r.unit[g->cursor].race, 0x30, 0xb2, COL_WHITE, PK_ALIGN_LEFT);
    }
    shadow_mes(g, 0x19, 0x2d, 0xbf, COL_WHITE, PK_ALIGN_RIGHT); /* Rank : */
    shadow_mes(g, 0x18, 0x2d, 0xcc, COL_WHITE, PK_ALIGN_RIGHT); /* Price : */
    draw_number(g, pk_r.unit[g->cursor].price * 2, 0x30, 0xcc, COL_WHITE, PK_ALIGN_LEFT);
    SDL_snprintf(buf, sizeof(buf), "%d/%d", g->world.unit_count, PK_UNIT_MAX);
    pk_drawq_text_utf8(&g->drawq, buf, 0xaf, 0x90, 0, PK_ALIGN_RIGHT, 1, PK_FONT_LEGEND);
    pk_drawq_text_utf8(&g->drawq, buf, 0xae, 0x8f, COL_GOLD, PK_ALIGN_RIGHT, 1, PK_FONT_LEGEND);
}

static int unit_at(pk_game *g, int row)
{
    int i = g->list_top + row;
    return i < 5 ? pk_r.unit_shop[i] : 0xff;
}

static int unit_rank(pk_game *g)
{
    int idx = g->list_top + g->cursor;
    return idx < 5 ? pk_r.unit[idx].b[1] : 0;
}

static void buy_units_update(pk_object *o, pk_game *g);

static void portrait_load(pk_game *g, pk_object *o, int type)
{
    int id = PORTRAIT_BASE + type * 2;
    o->image = pk_game_load_image(g, id, 1);
    pk_object_set_frames(o, TBL_PORTRAIT);
    o->flags = 0x4c1;
    o->frame = o->arg1;
}

static void buy_units_child(pk_object *o, pk_game *g)
{
    int t;
    switch (o->state) {
    case 0x80: /* frame of the portrait */
        pk_ui_setup_sprite(g, o, IMG_UI, TBL_LIST_FRAMES, 0x4c1);
        o->frame = 0x93;
        o->layer = 0;
        pk_object_clear_work(o);
        o->state = 0x81;
        break;
    case 0x81:
        if (pk_ui_root_left(g, buy_units_update)) {
            pk_object_kill(o);
        }
        break;
    case 0x90: /* portrait halves (pokeport.mbm) */
        t = unit_at(g, g->cursor);
        if (t == 0xff) {
            pk_object_kill(o);
            break;
        }
        o->work[0] = t;
        portrait_load(g, o, t);
        o->layer = 1;
        o->state = 0x91;
        break;
    case 0x91:
        t = unit_at(g, g->cursor);
        if (t != 0xff && t != o->work[0]) {
            o->work[0] = t;
            portrait_load(g, o, t);
        }
        o->flags = t == 0xff ? (o->flags & ~(Uint32)PK_OBJ_SPRITE) : (o->flags | PK_OBJ_SPRITE);
        if (pk_ui_root_left(g, buy_units_update)) {
            pk_object_kill(o);
        }
        break;
    default:
        list_child(o, g, buy_units_update, unit_at, unit_rank, pk_r.unit_shop_count, true);
        break;
    }
}

static void buy_units_update(pk_object *o, pk_game *g)
{
    int i, price;
    if (o->type != 0xc2) {
        buy_units_child(o, g);
        return;
    }
    switch (o->state) {
    case 0:
        pk_shop_load_units(g->world.current_area);
        pk_object_spawn(g, buy_units_update, g->slide, 0x80, 0, 0, 0, 0x9a);
        pk_object_spawn(g, buy_units_update, g->slide, 0x90, 0, 0, 0x98, 0xa3);
        pk_object_spawn(g, buy_units_update, g->slide, 0x90, 0, 1, 0x98, 0xbb);
        for (i = 0; i < 5; i++) {
            pk_object_spawn(g, buy_units_update, g->slide, 0x10, 0x40, i, 8, 0x1e + i * 0x14);
        }
        for (i = 0; i < 5; i++) {
            pk_object_spawn(g, buy_units_update, g->slide, 0x20, 0x50, i, 8, 0x1e + i * 0x14);
        }
        pk_object_spawn(g, buy_units_update, g->slide, 8, 0x60, 0, 8, 0x22);
        for (i = 1; i <= 10; i++) {
            pk_object_spawn(g, buy_units_update, g->slide, 0x50, 0x50, i, 0x32 + (i - 1) * 6, 0xb6);
        }
        for (i = 0; i < 5; i++) {
            if (pk_r.unit_shop[i] != 0xff) {
                pk_unit_template(pk_r.unit_shop[i], i);
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
        unit_info(g);
        break;
    case 2:
        pk_tap_rows(g, 8, 0x1e, 0xa0, 0x14, SDL_min(5, pk_r.unit_shop_count - g->list_top), &g->cursor); /* touch */
        pk_swipe_list(g, &g->list_top, &g->cursor, pk_r.unit_shop_count, 5);
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
            if (g->list_top + g->cursor < pk_r.unit_shop_count - 1) {
                if (g->cursor == 4) {
                    g->list_top++;
                } else {
                    g->cursor++;
                }
            }
        } else if (g->key_code == PK_SC_5) {
            bool room = g->world.unit_count < PK_UNIT_MAX;
            pk_audio_play_sfx(0);
            price = pk_r.unit[g->list_top + g->cursor].price;
            if (!room) {
                pk_help(g, 1, 0x38);
            }
            if (price * 2 <= g->loot && room) {
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
        unit_info(g);
        break;
    default:
        break;
    }
}

/* ------------------------------------------------------------------ */
/* shop root */

void pk_shop_update(pk_object *o, pk_game *g)
{
    int i, kind, idx;
    g->dlg_hint = shop_hint;
    switch (o->state) {
    case 2: /* enter */
        if (g->curtain != PK_CURTAIN_HELD || ++o->counter < 4) {
            break;
        }
        pk_game_set_curtain(g, PK_CURTAIN_OPEN);
        o->work[W_MENU] = 2;
        o->work[W_CURSOR] = 0;
        pk_object_spawn(g, pk_kingdom_ui_update, o->dir, 0xd0, 0, 0, 0, 0xc2);
        spawn_top_bar(o, g);
        pk_units_sort(g, false);
        g->field_bg = IMG_SHOP_BG;
        pk_game_set_bg(g, 0, IMG_SHOP_BG);
        g->bg_visible = 1;
        o->state = 7;
        break;
    case 7: /* buttons */
        if (g->curtain != PK_CURTAIN_NONE) {
            break;
        }
        o->counter = 0;
        for (i = 0; i < 4; i++) {
            pk_object_spawn(g, pk_kingdom_ui_update, o->dir, 0x90, i, 6, i < 2 ? -0x54 : 0xb0, (i & 1) ? 0xb2 : 0x9a);
        }
        for (i = 0; i < 4; i++) {
            pk_object_spawn(g, pk_kingdom_ui_update, o->dir, 0x90, i, 7, i < 2 ? -0x50 : 0xb4, (i & 1) ? 0xb3 : 0x9b);
        }
        pk_object_spawn(g, pk_kingdom_ui_update, o->dir, 0xa0, 0, 1, -0x1f, 0x9a);
        o->state = 3;
        break;
    case 3:
        if (++o->counter >= 5) {
            o->counter = 0;
            o->state = 4;
        }
        break;
    case 4:
        if (g->loot < 1) {
            pk_help(g, 1, 0x10); /* "no money" tip */
        }
        /* touch: Buy, Sell and (online) the wide Auction button */
        if (pk_tap(g, 4, 0x9a, 0x54, 0x18)) {
            o->work[W_CURSOR] = 0;
            g->key_code = PK_SC_5;
        }
        if (pk_tap(g, 0x58, 0x9a, 0x54, 0x18)) {
            o->work[W_CURSOR] = 2;
            g->key_code = PK_SC_5;
        }
        if (pk_tap(g, 4, 0xb2, 0xa8, 0x18) && g->arena_online) {
            o->work[W_CURSOR] = 1;
            g->key_code = PK_SC_5;
        }
        if (g->key_code == PK_SC_LEFT || g->key_code == PK_SC_RIGHT) {
            pk_audio_play_sfx(2);
            if (o->work[W_CURSOR] == 0 || o->work[W_CURSOR] == 2) {
                o->work[W_CURSOR] = (o->work[W_CURSOR] + 2) % 4;
            }
        } else if ((g->key_code == PK_SC_UP || g->key_code == PK_SC_DOWN) && g->arena_online) {
            pk_audio_play_sfx(2);
            o->work[W_CURSOR] = (o->work[W_CURSOR] == 1) ? 0 : 1;
        }
        if (g->key_code == PK_SC_5) {
            pk_audio_play_sfx(0);
            if (o->work[W_CURSOR] == 0) {
                pk_game_set_curtain(g, PK_CURTAIN_CLOSE);
                o->counter = 0;
                o->state = 0x10; /* Buy */
            } else if (o->work[W_CURSOR] == 2) {
                pk_game_set_curtain(g, PK_CURTAIN_CLOSE);
                o->counter = 0;
                o->state = 8; /* Sell */
            } else if (o->work[W_CURSOR] == 1) {
                o->counter = 0; /* 0x1002a544 */
                pk_object_call(g, o, pk_auction_update, 0, pk_shop_update, 2);
            }
        } else if (g->key_code == PK_SC_BACK) {
            pk_audio_play_sfx(1);
            pk_game_set_curtain(g, PK_CURTAIN_CLOSE);
            o->counter = 0;
            o->dir = 2;
            o->state = 0x17;
        }
        break;
    case 6: /* back from a list: rebuild the shop */
        if (g->curtain == PK_CURTAIN_NONE) {
            pk_game_set_curtain(g, PK_CURTAIN_CLOSE);
        }
        if (g->curtain == PK_CURTAIN_HELD) {
            g->cam_x = g->cam_y = 0;
            kill_screen_children(g, o);
            o->counter = 0;
            o->state = 2;
        }
        break;

    /* --- Sell --- */
    case 8:
    case 0x10:
        if (g->curtain != PK_CURTAIN_HELD || ++o->counter < 4) {
            break;
        }
        pk_game_set_curtain(g, PK_CURTAIN_OPEN);
        spawn_top_bar(o, g);
        g->slide = 0;
        o->state = o->state == 8 ? 9 : 0x11;
        break;
    case 9:
    case 0x11:
        if (g->curtain != PK_CURTAIN_NONE) {
            break;
        }
        o->counter = 0;
        g->dlg_type = 0xd; /* Item / Unit */
        g->list_top = g->cursor = 0;
        call(o, g, pk_dialog_update, o->state == 9 ? 0xa : 0x12);
        break;
    case 0xa:
        if (g->dlg_result == 0) {
            o->counter = 0;
            g->slide = 0;
            clear_pos(g);
            if (g->cursor == 0) {
                g->dlg_type = 0xb; /* material kinds */
                g->list_top = 0;
                call(o, g, pk_dialog_update, 0xb);
            } else {
                g->list_top = g->cursor = 0;
                g->list_120 = g->list_124 = 0;
                call(o, g, pk_unit_list_update, 0xe);
            }
        } else {
            g->slide = 2;
            pk_game_set_curtain(g, PK_CURTAIN_CLOSE);
            o->counter = 0;
            o->state = 6;
        }
        break;
    case 0xb: /* kind chosen */
        if (g->dlg_result == 0) {
            g->dlg_type = g->list_top + g->cursor;
            load_pos(g);
            g->slide = 0;
            call(o, g, pk_item_list_update, 0xc);
        } else {
            g->slide = 1;
            o->state = 9;
        }
        break;
    case 0xc: /* item chosen */
        if (g->dlg_result == 0) {
            g->sel = g->dlg_type;
            g->sel2 = g->list_top + g->cursor;
            g->slide = 0;
            g->dlg_type = 0xc;
            save_pos(g);
            g->list_top = g->cursor = 0;
            call(o, g, pk_dialog_update, 0xd);
        } else {
            o->counter = 0;
            g->slide = 1;
            g->dlg_type = 0xb;
            g->list_top = 0;
            save_pos(g);
            call(o, g, pk_dialog_update, 0xb);
        }
        break;
    case 0xd:
        if (g->dlg_result == 0 && g->cursor == 0) {
            int n;
            idx = pk_item_find(g, g->sel, g->sel2);
            if (idx != 0xff) {
                if (g->sel < PK_KIND_EQUIP) {
                    pk_item_preview(g->sel, g->world.items[idx][1], 10);
                    pk_loot_add(g, pk_r.item[10].price);
                    pk_item_remove(g, idx);
                    pk_items_sort(g);
                } else {
                    pk_item_preview(g->sel, g->world.equip[idx][1], 10);
                    pk_loot_add(g, pk_r.equip[10].price);
                    pk_equip_remove(g, idx);
                    pk_equip_sort(g, false);
                }
            }
            g->dlg_type = g->sel;
            load_pos(g);
            n = pk_item_count_kind(g, g->dlg_type);
            idx = g->cursor + g->list_top;
            if (idx == n) {
                idx--;
            }
            list_position(g, idx, n, 5);
        } else {
            g->dlg_type = g->sel;
            load_pos(g);
        }
        g->slide = 1;
        call(o, g, pk_item_list_update, 0xc);
        break;
    case 0xe: /* unit chosen for selling */
        if (g->dlg_result == 0) {
            g->sel = g->cursor;
            g->slide = 0;
            g->dlg_type = 0xc;
            save_pos(g);
            g->list_top = g->cursor = 0;
            call(o, g, pk_dialog_update, 0xf);
        } else {
            g->slide = 1;
            o->state = 9;
        }
        break;
    case 0xf:
        if (g->dlg_result == 0 && g->cursor == 0) {
            int u = g->sel + g->list_f8;
            pk_loot_add(g, pk_r.unit[g->sel].price);
            if (u < PK_UNIT_MAX && g->world.units[u][2] != 0xff) {
                int e = g->world.units[u][2];
                g->world.units[u][2] = 0xff;
                g->world.equip[e][10] = 0xff;
                g->world.equip[e][11] = 0;
            }
            pk_unit_remove(g, u);
            pk_units_sort(g, false);
            load_pos(g);
            if (g->cursor == 0) {
                if (g->list_top != 0) {
                    g->list_top--;
                }
            } else {
                g->cursor--;
            }
        } else {
            load_pos(g);
        }
        g->slide = 1;
        g->list_120 = g->list_124 = 0;
        call(o, g, pk_unit_list_update, 0xe);
        break;

    /* --- Buy --- */
    case 0x12:
        if (g->dlg_result == 0) {
            o->counter = 0;
            g->slide = 0;
            g->list_top = 0;
            clear_pos(g);
            if (g->cursor == 0) {
                call(o, g, buy_items_update, 0x13);
            } else {
                g->cursor = 0;
                call(o, g, buy_units_update, 0x15);
            }
        } else {
            g->slide = 2;
            pk_game_set_curtain(g, PK_CURTAIN_CLOSE);
            o->counter = 0;
            o->state = 6;
        }
        break;
    case 0x13: /* back from the item list */
        if (g->dlg_result == 0) {
            g->sel = g->list_top + g->cursor;
            g->slide = 0;
            g->dlg_type = 0xc; /* Yes / No */
            save_pos(g);
            g->list_top = g->cursor = 0;
            call(o, g, pk_dialog_update, 0x14);
        } else {
            g->slide = 1;
            o->state = 0x11;
        }
        break;
    case 0x14:
        if (g->dlg_result == 0 && g->cursor == 0) {
            idx = g->sel;
            kind = pk_r.shop_kind[idx];
            pk_item_add(g, kind, pk_r.shop_id[idx]);
            if (kind < PK_KIND_EQUIP) {
                pk_items_sort(g);
            } else {
                pk_equip_sort(g, false);
            }
            pk_loot_sub(g, item_price(idx) * 2);
        }
        o->counter = 0;
        g->slide = 1;
        load_pos(g);
        call(o, g, buy_items_update, 0x13);
        break;
    case 0x15: /* back from the unit list */
        if (g->dlg_result == 0) {
            g->sel = g->list_top + g->cursor;
            g->slide = 0;
            g->dlg_type = 0xc;
            save_pos(g);
            g->list_top = g->cursor = 0;
            call(o, g, pk_dialog_update, 0x16);
        } else {
            g->slide = 1;
            o->state = 0x11;
        }
        break;
    case 0x16:
        if (g->dlg_result == 0 && g->cursor == 0) {
            idx = g->sel;
            pk_unit_add(g, pk_r.unit_shop[idx]);
            pk_units_sort(g, false);
            pk_loot_sub(g, pk_r.unit[idx].price * 2);
        }
        o->counter = 0;
        g->slide = 1;
        load_pos(g);
        call(o, g, buy_units_update, 0x15);
        break;

    case 0x17: /* back to the kingdom screen */
        if (g->curtain == PK_CURTAIN_HELD) {
            kill_screen_children(g, o);
            g->field_bg = g->fame > 300 ? 0xe : g->fame < 0x97 ? 0xf
                                                               : 9;
            pk_game_set_bg(g, 0, g->field_bg);
            o->work[W_MENU] = 0;
            o->work[W_CURSOR] = 0;
            o->state = 1;
            g->dlg_hint = NULL;
            pk_object_set_update(o, pk_kingdom_update);
        }
        break;
    default:
        break;
    }
}
