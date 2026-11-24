/* SPDX-License-Identifier: MIT */
/* The list of owned items or equipment of one kind (update 0x10039b34),
 * used to sell, to equip units and in the laboratory. g->dlg_type holds
 * the kind (0..4 materials, 5..7 equipment); the chosen entry is the
 * (list_top + cursor)-th owned thing of that kind (FUN_10062240). */
#include "audio/pk_audio.h"
#include "core/pk_app.h"
#include "game/pk_rules.h"
#include "game/pk_scenes.h"
#include "game/ui_dialog.h"
#include "gfx/pk_font.h"

#define IMG_UI         0x17
#define TBL_FRAMES     0x10091250u
#define TBL_KIND_ICON  0x100911fcu
#define TBL_KIND_LABEL 0x1009121cu
#define COL_WHITE      0xFFFFFFu
#define COL_GOLD       0xFFDC00u

static void shadow_mes(pk_game *g, int id, int x, int y, pk_color c, pk_align a)
{
    pk_game_mestext(g, id, x + 1, y + 1, 0, a, 1, PK_FONT_LEGEND);
    pk_game_mestext(g, id, x, y, c, a, 1, PK_FONT_LEGEND);
}

static void shadow_num(pk_game *g, const char *fmt, int a, int b, int x, int y, pk_color c, pk_align al)
{
    char buf[32];
    SDL_snprintf(buf, sizeof(buf), fmt, a, b);
    pk_drawq_text_utf8(&g->drawq, buf, x + 1, y + 1, 0, al, 1, PK_FONT_LEGEND);
    pk_drawq_text_utf8(&g->drawq, buf, x, y, c, al, 1, PK_FONT_LEGEND);
}

static int owned(pk_game *g, int row)
{
    return pk_item_find(g, g->dlg_type, g->list_top + row);
}

static bool equipped(pk_game *g, int idx)
{
    return g->dlg_type >= PK_KIND_EQUIP && idx != 0xff && g->world.equip[idx][11] != 0;
}

static int row_frame(pk_game *g, int row)
{
    int idx = owned(g, row);
    if (idx == 0xff) {
        return 0x10;
    }
    if (equipped(g, idx)) {
        return 0x12;
    }
    return g->cursor == row ? 0x11 : 0x10;
}

static int rank_of(pk_game *g, int row)
{
    int idx = owned(g, row);
    if (idx == 0xff) {
        return 0;
    }
    return g->dlg_type < PK_KIND_EQUIP ? g->world.items[idx][6] : g->world.equip[idx][5];
}

static void load_preview(pk_game *g, int row)
{
    int idx = owned(g, row);
    if (idx == 0xff) {
        return;
    }
    if (g->dlg_type < PK_KIND_EQUIP) {
        pk_item_preview(g->dlg_type, g->world.items[idx][1], row);
    } else {
        pk_item_preview(g->dlg_type, g->world.equip[idx][1], row);
    }
}

static void info(pk_game *g, int count)
{
    int idx = owned(g, g->cursor), y, step;
    bool shop = g->call_ret_fn == pk_shop_update;
    if (g->list_top + g->cursor >= count) {
        return;
    }
    if (g->dlg_type < PK_KIND_EQUIP) {
        y = shop ? 0xa6 : 0xab;
        step = shop ? 0xf : 0x12;
    } else {
        y = 0xa6;
        step = 0xd;
    }
    shadow_mes(g, 0x17, 0x33, y, COL_WHITE, PK_ALIGN_RIGHT); /* Kind : */
    shadow_mes(g, pk_app_i32(TBL_KIND_LABEL + (Uint32)g->dlg_type * 4u), 0x37, y, COL_WHITE, PK_ALIGN_LEFT);
    y += step;
    shadow_mes(g, 0x11, 0x33, y, COL_WHITE, PK_ALIGN_RIGHT); /* Rarity : */
    y += step;
    if (shop) {
        shadow_mes(g, 0x18, 0x33, y, COL_WHITE, PK_ALIGN_RIGHT); /* Price : */
        if (idx != 0xff) {
            int p = g->dlg_type < PK_KIND_EQUIP ? pk_r.item[g->cursor].price : pk_r.equip[g->cursor].price;
            shadow_num(g, "%d", p, 0, 0x37, y, COL_WHITE, PK_ALIGN_LEFT);
        }
        if (g->dlg_type < PK_KIND_EQUIP) {
            shadow_num(g, "%d/%d", g->world.item_count, PK_ITEM_MAX, 0xae, 0x8f, COL_GOLD, PK_ALIGN_RIGHT);
        } else {
            shadow_num(g, "%d/%d", g->world.equip_count, PK_EQUIP_MAX, 0xae, 0x8f, COL_GOLD, PK_ALIGN_RIGHT);
        }
    }
}

static void child(pk_object *o, pk_game *g)
{
    pk_object *row, *root = pk_ui_root(g);
    int idx;
    switch (o->state) {
    case 8:
        pk_ui_setup_sprite(g, o, IMG_UI, TBL_FRAMES, 0x4c1);
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
        if (pk_ui_root_left(g, pk_item_list_update)) {
            o->counter = 0;
            o->state = 0xb;
        }
        break;
    case 0xb:
    case 0x13:
    case 0x33:
        if (pk_ui_slide_out(o, g)) {
            pk_object_kill(o);
        }
        break;
    case 0x10:
        pk_ui_setup_sprite(g, o, IMG_UI, TBL_FRAMES, 0x4c1);
        o->frame = row_frame(g, o->arg1);
        pk_object_clear_work(o);
        o->counter = 0;
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
        o->frame = row_frame(g, o->arg1);
        if (pk_ui_root_left(g, pk_item_list_update)) {
            o->counter = 0;
            o->state = 0x13;
        }
        break;
    case 0x20:
        pk_ui_setup_sprite(g, o, IMG_UI, TBL_FRAMES, 0x4c0);
        o->work[0] = -1;
        o->state = 0x21;
        /* fallthrough */
    case 0x21:
        row = pk_object_find(g, o->type, 0x40, o->arg1);
        if (!row || row->dead) {
            pk_object_kill(o);
            break;
        }
        o->x = row->x + 4;
        o->y = row->y + 1;
        if (root && root->update != pk_item_list_update) {
            idx = o->work[0];
        } else {
            idx = owned(g, o->arg1);
            if (idx != o->work[0]) {
                load_preview(g, o->arg1);
            }
        }
        o->work[0] = idx;
        if (idx == 0xff || idx < 0) {
            o->flags &= ~(Uint32)PK_OBJ_SPRITE;
        } else {
            const Uint16 *name = g->dlg_type < PK_KIND_EQUIP ? pk_r.item[o->arg1].name : pk_r.equip[o->arg1].name;
            o->flags |= PK_OBJ_SPRITE;
            o->frame = pk_app_i32(TBL_KIND_ICON + (Uint32)g->dlg_type * 4u);
            pk_drawq_text(&g->drawq, name, o->x + 0x18, o->y + 0xe, 0, PK_ALIGN_LEFT, 0, PK_FONT_LEGEND);
        }
        break;
    case 0x30:
        pk_ui_setup_sprite(g, o, IMG_UI, TBL_FRAMES, 0x4c0);
        o->frame = o->arg1 + 0x31;
        pk_ui_start_offset(o);
        o->state = 0x31;
        /* fallthrough */
    case 0x31:
    case 0x32:
        if (o->arg1 == 0 ? g->list_top != 0 : root && g->list_top + 5 < root->work[6]) {
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
        } else if (pk_ui_root_left(g, pk_item_list_update)) {
            o->counter = 0;
            o->state = 0x33;
        }
        break;
    case 0x50:
        pk_ui_setup_sprite(g, o, IMG_UI, TBL_FRAMES, 0x4c1);
        o->frame = 0x3f;
        o->state = 0x51;
        /* fallthrough */
    case 0x51:
        if (g->curtain == PK_CURTAIN_NONE) {
            o->frame = rank_of(g, g->cursor) >= o->arg1 ? 0x3e : 0x3f;
        }
        if (pk_ui_root_left(g, pk_item_list_update)) {
            pk_object_kill(o);
        }
        break;
    case 0x80:
        pk_ui_setup_sprite(g, o, IMG_UI, TBL_FRAMES, 0x4c0);
        o->frame = 0x93;
        o->state = 0x81;
        break;
    case 0x81:
        if (pk_ui_root_left(g, pk_item_list_update)) {
            pk_object_kill(o);
        }
        break;
    default:
        break;
    }
}

void pk_item_list_update(pk_object *o, pk_game *g)
{
    int i, n, y;
    if (o->type != 0xc2) {
        child(o, g);
        return;
    }
    n = o->work[6]; /* +0x30: entries of the kind */
    switch (o->state) {
    case 0:
        if (g->dlg_type > 4) {
            pk_object_spawn(g, pk_item_list_update, g->slide, 0x80, 0, 0, 0, 0x9a);
        }
        for (i = 0; i < 5; i++) {
            pk_object_spawn(g, pk_item_list_update, g->slide, 0x10, 0x40, i, 8, 0x1e + i * 0x14);
        }
        for (i = 0; i < 5; i++) {
            pk_object_spawn(g, pk_item_list_update, g->slide, 0x20, 0x50, i, 8, 0x1e + i * 0x14);
        }
        pk_object_spawn(g, pk_item_list_update, g->slide, 8, 0x60, 0, 8, 0x22);
        pk_object_spawn(g, pk_item_list_update, g->slide, 0x30, 0x30, 0, 0x58, 0x19);
        pk_object_spawn(g, pk_item_list_update, g->slide, 0x30, 0x30, 1, 0x58, 0x87);
        o->work[6] = n = pk_item_count_kind(g, g->dlg_type);
        if (n != 0) {
            if (g->dlg_type < PK_KIND_EQUIP) {
                y = g->call_ret_fn == pk_shop_update ? 0xac : 0xb4;
            } else {
                y = 0xaa;
            }
            for (i = 1; i <= 10; i++) {
                pk_object_spawn(g, pk_item_list_update, g->slide, 0x50, 0x50, i, 0x39 + (i - 1) * 6, y);
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
            int idx = owned(g, g->cursor);
            if (n > 0) {
                if (idx == 0xff || (equipped(g, idx) && g->call_ret_fn != pk_list_update)) {
                    pk_audio_play_sfx(1);
                } else {
                    pk_audio_play_sfx(0);
                    g->dlg_result = 0;
                    pk_object_return(g, o);
                    return;
                }
            }
        } else if (g->key_code == PK_SC_BACK) {
            pk_audio_play_sfx(1);
            g->dlg_result = 1;
            pk_object_return(g, o);
            return;
        }
        info(g, n);
        break;
    default:
        break;
    }
}
