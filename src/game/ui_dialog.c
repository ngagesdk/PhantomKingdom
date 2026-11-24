/* SPDX-License-Identifier: MIT */
/* The generic selection dialog (update function 0x1002637c).
 *
 * A screen object calls it as a sub-routine (FUN_1006ad98) after setting
 * game+0x100 to the dialog type; the dialog builds a column of rows
 * labelled from the table at 0x1008b93c (11 mestext ids per type, row
 * counts at 0x1008b87c, top y at 0x1008b8c0), lets the player pick one
 * and returns to the caller with game+0x104 = 0 (picked, index in
 * game+0xf0 + game+0xf4) or 1 (cancelled). More than seven rows scroll
 * with six visible and two arrows. Rows, cursor and arrows are child
 * objects running the same update function in their own states; they
 * slide in from the side given by game+0x110 and out again when the
 * caller takes the root object back. */
#include "game/ui_dialog.h"
#include "game/pk_arena_game.h"
#include "audio/pk_audio.h"
#include "core/pk_app.h"
#include "game/pk_scenes.h"
#include "gfx/pk_font.h"
#include "input/pk_haptics.h"

#define TBL_COUNT       0x1008b87cu
#define TBL_Y           0x1008b8c0u
#define TBL_LABELS      0x1008b93cu
#define TBL_FRAMES      0x1008bc68u /* interface frames */
#define TBL_CURSOR_ANIM 0x100aed70u /* five cursor animations, 0x18 apart (FUN_10071f3c) */
#define IMG_UI          0x17
#define OBJ_ROOT        0xc2

#define ST_ROW    0x10
#define ST_CURSOR 0x18
#define ST_ARROW  0x30

pk_object *pk_ui_root(pk_game *g)
{
    return pk_object_find(g, OBJ_ROOT, 0, 0);
}

void pk_ui_cursor_anim(pk_game *g, pk_object *o)
{
    if (g->rival_index >= 0 && g->rival_index < 5) {
        pk_object_set_anim(o, TBL_CURSOR_ANIM + (Uint32)g->rival_index * 0x18u);
    }
    o->frame = pk_app_i32(o->anim);
}

void pk_ui_setup_sprite(pk_game *g, pk_object *o, int image, Uint32 frames, Uint32 flags)
{
    o->image = pk_game_image_slot(g, image);
    if (o->image == PK_IMAGE_NONE) {
        o->image = pk_game_load_image(g, image, 1);
    }
    o->flags = flags;
    pk_object_set_frames(o, frames);
}

/* Slide of a child in from (state 0x11 ...) / out to the side. */
void pk_ui_slide_in(pk_object *o)
{
    o->x += o->type == 0 ? -0x20 : 0x20;
}

bool pk_ui_slide_out(pk_object *o, pk_game *g)
{
    if (g->slide == 0) {
        o->x -= 0x20;
    } else if (g->slide == 1) {
        o->x += 0x20;
    } else {
        return false;
    }
    return ++o->counter >= 6;
}

void pk_ui_start_offset(pk_object *o)
{
    o->x += o->type == 0 ? 0xc0 : -0xc0;
}

bool pk_ui_root_left(pk_game *g, pk_object_fn fn)
{
    pk_object *root = pk_ui_root(g);
    return root && root->update != fn;
}

static int label_id(int type, int idx)
{
    return pk_app_i32(TBL_LABELS + (Uint32)(type * 11 + idx) * 4u);
}

static void draw_row_label(pk_object *o, pk_game *g)
{
    pk_game_mestext(g, label_id(o->work[0], o->work[4] + o->arg1), o->x + 0x42, o->y + 0xc, 0, PK_ALIGN_CENTER,
                    0, PK_FONT_LEGEND);
}

/* FUN_10024714: keeps the cursor inside `visible` rows. */
static void clamp_list(pk_game *g, int visible)
{
    int over = g->list_top - visible + g->cursor + 1;
    if (over > 0) {
        g->list_top += over;
        g->cursor -= over;
    }
}

/* Touch (port): a tap on a row picks it, the arrows and scrolling move
 * a long list (six rows shown), a short one's cursor. */
static void dialog_touch(pk_game *g, int n)
{
    int vis = n >= 7 ? 6 : n, d;
    pk_tap_rows(g, 0x16, g->dlg_y, 0x84, 0x10, vis, &g->cursor);
    if (n >= 7) {
        if (pk_tap(g, 0x40, 0x10, 0x30, 0x12) && g->list_top > 0) {
            g->list_top--;
            pk_audio_play_sfx(2);
        }
        if (pk_tap(g, 0x40, 0x80, 0x30, 0x12) && g->list_top + 6 < n) {
            g->list_top++;
            pk_audio_play_sfx(2);
        }
        pk_swipe_list(g, &g->list_top, &g->cursor, n, 6);
    } else if ((d = pk_swipe_y(g)) != 0 && g->key_code == PK_SC_NONE) {
        g->key_code = d > 0 ? PK_SC_DOWN : PK_SC_UP;
        pk_haptics_play(PK_HAPTIC_STEP);
    }
}

static void root_update(pk_object *o, pk_game *g)
{
    int i, n, y;
    switch (o->state) {
    case 0:
        g->dlg_count = pk_app_i32(TBL_COUNT + (Uint32)g->dlg_type * 4u);
        g->dlg_y = pk_app_i32(TBL_Y + (Uint32)g->dlg_type * 4u);
        if (g->dlg_type == 3) {
            if (g->progress < 3) {
                g->dlg_count -= 2;
            } else if (g->progress <= 5) {
                g->dlg_count -= 1;
            }
        }
        if (g->dlg_type == 8) {
            const int *a = g->world.areas[g->world.current_area];
            /* port: the players the server reported for this area (area
             * search, message 134) count as rival kingdoms here; the
             * area word 1 alone stays 0 in the port, so Chat was never
             * offered */
            int rivals = g->arena_online && g_rival_area == g->world.current_area ? g_rival_count : 0;
            /* Chat needs the Arena and a player in the area; Scout
             * needs someone there at all */
            if (!g->arena_online || (a[1] == 0 && rivals == 0)) {
                g->dlg_count--;
            }
            if (a[1] + a[2] + a[0] + rivals == 0) {
                g->dlg_count--;
            }
        }
        n = g->dlg_count;
        if (n >= 7) {
            n = 6;
            pk_object_spawn(g, pk_dialog_update, g->slide, ST_ARROW, ST_ARROW, 0, 0x58, 0x19);
            pk_object_spawn(g, pk_dialog_update, g->slide, ST_ARROW, ST_ARROW, 1, 0x58, 0x87);
        }
        clamp_list(g, n);
        y = g->dlg_y;
        for (i = 0; i < n; i++, y += 0x10) {
            pk_object_spawn(g, pk_dialog_update, g->slide, ST_ROW, ST_ROW, i, 0x16, y);
        }
        pk_object_spawn(g, pk_dialog_update, g->slide, ST_CURSOR, ST_CURSOR, 0, 0x16, g->dlg_y + 4);
        o->counter = 0;
        o->state = 1;
        break;
    case 1:
        if (++o->counter > 8) {
            o->counter = 0;
            o->state = 2;
        }
        break;
    case 2:
        n = g->dlg_count;
        dialog_touch(g, n);
        if (g->key_code == PK_SC_UP) {
            pk_audio_play_sfx(2);
            if (g->cursor != 0) {
                g->cursor--;
            } else if (g->list_top != 0) {
                g->list_top--;
            } else {
                g->list_top = n - 1 < 7 ? 0 : n - 6;
                g->cursor = n - g->list_top - 1;
            }
        } else if (g->key_code == PK_SC_DOWN) {
            pk_audio_play_sfx(2);
            if (g->list_top + g->cursor < n - 1) {
                if (g->cursor == 5) {
                    g->list_top++;
                } else {
                    g->cursor++;
                }
            } else {
                g->list_top = g->cursor = 0;
            }
        } else if (g->key_code == PK_SC_5) {
            pk_audio_play_sfx(0);
            g->dlg_result = 0;
            pk_object_return(g, o);
            return;
        } else if (g->key_code == PK_SC_BACK) {
            pk_audio_play_sfx(1);
            g->dlg_result = 1;
            pk_object_return(g, o);
            return;
        }
        if (g->dlg_hint) {
            g->dlg_hint(g);
        }
        break;
    default:
        break;
    }
}

static void arrow_visibility(pk_object *o, pk_game *g)
{
    if (o->arg1 == 0 ? g->list_top != 0 : g->dlg_count > g->list_top + 6) {
        o->flags |= PK_OBJ_SPRITE;
    } else {
        o->flags &= ~(Uint32)PK_OBJ_SPRITE;
    }
}

void pk_dialog_update(pk_object *o, pk_game *g)
{
    switch (o->state) {
    case ST_ROW:
        pk_ui_setup_sprite(g, o, IMG_UI, TBL_FRAMES, 0x4c1);
        o->frame = g->cursor == o->arg1 ? 0x3d : 0x3c;
        pk_object_clear_work(o);
        o->counter = 0;
        pk_ui_start_offset(o);
        o->work[0] = g->dlg_type;
        o->work[4] = g->list_top;
        draw_row_label(o, g);
        o->state = 0x11;
        break;
    case 0x11:
        pk_ui_slide_in(o);
        if (++o->counter > 5) {
            o->counter = 0;
            o->state = 0x12;
        }
        o->work[4] = g->list_top;
        draw_row_label(o, g);
        break;
    case 0x12:
        o->frame = g->cursor == o->arg1 ? 0x3d : 0x3c;
        if (pk_ui_root_left(g, pk_dialog_update)) {
            o->counter = 0;
            o->state = 0x13;
        }
        o->work[4] = g->list_top;
        draw_row_label(o, g);
        break;
    case 0x13:
        if (g->slide == 2) {
            if (!pk_ui_root_left(g, pk_dialog_update)) {
                o->counter = 0;
                o->state = 0x12;
            }
        } else if (pk_ui_slide_out(o, g)) {
            pk_object_kill(o);
            return;
        }
        draw_row_label(o, g);
        break;
    case ST_CURSOR:
        pk_ui_setup_sprite(g, o, IMG_UI, TBL_FRAMES, 0x4c1);
        o->frame = 0;
        o->layer = 1;
        pk_ui_cursor_anim(g, o);
        pk_object_clear_work(o);
        o->counter = 0;
        o->y = g->dlg_y + g->cursor * 0x10 + 4;
        pk_ui_start_offset(o);
        o->state = 0x19;
        break;
    case 0x19:
        pk_object_animate(o);
        o->y = g->dlg_y + g->cursor * 0x10 + 4;
        pk_ui_slide_in(o);
        if (++o->counter > 5) {
            o->counter = 0;
            o->state = 0x1a;
        }
        break;
    case 0x1a:
        pk_object_animate(o);
        o->x = 0x16;
        o->y = g->dlg_y + g->cursor * 0x10 + 4;
        if (pk_ui_root_left(g, pk_dialog_update)) {
            o->counter = 0;
            o->state = 0x1b;
        }
        break;
    case 0x1b:
        pk_object_animate(o);
        if (g->slide == 2) {
            if (!pk_ui_root_left(g, pk_dialog_update)) {
                o->counter = 0;
                o->state = 0x1a;
            }
        } else if (pk_ui_slide_out(o, g)) {
            pk_object_kill(o);
        }
        break;
    case ST_ARROW:
        pk_ui_setup_sprite(g, o, IMG_UI, TBL_FRAMES, 0x4c0);
        o->frame = o->arg1 + 0x31;
        pk_ui_start_offset(o);
        arrow_visibility(o, g);
        o->counter = 0;
        o->state = 0x31;
        break;
    case 0x32:
        arrow_visibility(o, g);
        if (pk_ui_root_left(g, pk_dialog_update)) {
            o->counter = 0;
            o->state = 0x33;
        }
        break;
    case 0x31:
        pk_ui_slide_in(o);
        if (++o->counter > 5) {
            o->counter = 0;
            o->state = 0x32;
        }
        break;
    case 0x33:
        if (g->slide != 2 && pk_ui_slide_out(o, g)) {
            pk_object_kill(o);
        }
        break;
    default:
        if (o->type == OBJ_ROOT) {
            root_update(o, g);
        }
        break;
    }
}

/* Root states are 0..2 and children use 0x10+, so one update function
 * serves both like in the original. */
