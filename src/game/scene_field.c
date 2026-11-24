/* SPDX-License-Identifier: MIT */
/* Mode 1: the kingdom screen.
 *
 * Mode handler FUN_10069334 (phases FUN_1006904c, FUN_10069198,
 * FUN_100692d0), the kingdom root object 0x1002479c (type 0xc2) and its
 * interface children 0x10025354: top bar with the level, points and loot
 * counters, the 2x2 menu (Party, Shop, Map, Lab, and the Party and Lab
 * sub-menus), the pointer and the animated background decoration.
 *
 * The screens behind the menu entries are in their own files
 * (scene_form.c, scene_lab.c, scene_map.c, scene_shop.c, ...). */
#include "game/pk_arena_game.h"
#include "game/pk_help.h"
#include "game/pk_scenes.h"

#include "audio/pk_audio.h"
#include "core/pk_app.h"
#include "core/pk_text.h"
#include "game/pk_data.h"
#include "game/pk_rules.h"
#include "gfx/pk_font.h"
#include "net/pk_online.h"

#include <SDL3/SDL.h>

#define OBJ_KINGDOM 0xc2
#define IMG_UI      0x17

#define TBL_UI_FRAMES   0x1008bc68u /* frames of bitmap 0x17 */
#define TBL_MENU_LABELS 0x1008b7bcu /* 5 menus x 4 mestext ids */
#define TBL_MENU_ICONS  0x1008b80cu /* 5 menus x 4 icon frames */
#define TBL_LEVELS      0x1008d280u /* 10 point thresholds */

/* root work[]: 0 (+0x18) current menu, 1 (+0x1c) cursor (bit 1 column,
 * bit 0 row). Menus: 0 top (Party Shop Map Lab), 1 Party (Form Medical
 * Defense List), 2 Shop, 3 Map, 4 Lab (Combine Vault RankUp Upgrade). */
#define W_MENU   0
#define W_CURSOR 1

#define COL_SELECTED 0xFFFFFFu
#define COL_NORMAL   0xFFDC00u

typedef struct bg_deco
{
    int bg, image;
    Uint32 frames, anim;
    int x, y;
} bg_deco;

/* Background decoration per kingdom background (child state 0xd0). Only
 * the entries whose tails were decoded are listed; others draw nothing. */
static const bg_deco k_decos[] = {
    { 0xf, 0x15, 0x1008d150u, 0x1008d260u, 0x38, 0x41 },
};

/* ------------------------------------------------------------------ */
/* mode handler */

void pk_mode_field(pk_game *g)
{
    switch (g->phase) {
    case 0: /* FUN_1006904c */
        pk_game_set_curtain(g, PK_CURTAIN_NONE);
        pk_game_release_images(g);
        pk_objects_clear(g);
        g->cam_x = g->cam_y = 0;
        pk_game_fill(g, 0, 0, PK_SCREEN_W, PK_SCREEN_H, 0xFF000000u);
        pk_game_mestext(g, 0, 0x58, 100, 0xFFFFFFu, PK_ALIGN_CENTER, 0, PK_FONT_LEGEND);
        pk_audio_set_volume(g->volume * 2, g->volume * 2);
        pk_game_set_phase(g, 1);
        break;
    case 1: /* FUN_10069198 */
        /* FUN_1007163c: the background follows the points */
        g->field_bg = g->fame > 300 ? 0xe : g->fame < 0x97 ? 0xf
                                                           : 9;
        pk_game_set_bg(g, 0, g->field_bg);
        g->bg_visible = 1;
        pk_game_load_image(g, IMG_UI, 1);
        /* Port: also save on entering the kingdom screen. */
        pk_world_save(g);
        pk_object_spawn(g, pk_kingdom_update, OBJ_KINGDOM, 0, 0, 0, 0, 0);
        pk_game_set_curtain(g, PK_CURTAIN_HELD);
        pk_game_set_phase(g, 2);
        break;
    default:
    { /* FUN_100692d0 */
        pk_object *root = pk_object_find(g, OBJ_KINGDOM, 0, 0);
        int ev;
        pk_invasion_tick(g);
        pk_events_tick(g);
        /* the kingdom screen hands finished missions to 0x10028538 */
        if (root && root->update == pk_kingdom_update && root->state == 4 && root->work[0] == 0 &&
            g->curtain == PK_CURTAIN_NONE && (ev = pk_event_finished(g)) != 0xff) {
            pk_event_handle(g, ev);
        }
        break;
    }
    }
}

bool pk_field_at_top(pk_game *g)
{
    pk_object *root = pk_object_find(g, OBJ_KINGDOM, 0, 0);
    return g->mode == PK_MODE_FIELD && root && root->update == pk_kingdom_update && root->state == 4 &&
           root->work[W_MENU] == 0 && g->curtain == PK_CURTAIN_NONE;
}

/* ------------------------------------------------------------------ */
/* helpers */

static bool cell_disabled(pk_game *g, int idx) { return (g->menu_disabled >> idx) & 1; } /* FUN_10073320 */
static void cell_enable(pk_game *g, int idx) { g->menu_disabled &= ~(1u << idx); }       /* FUN_100732f8 */
static void cell_disable(pk_game *g, int idx) { g->menu_disabled |= 1u << idx; }         /* FUN_1007330c */

/* FUN_10024740: flip one cursor bit, skipping disabled cells */
static int cursor_move(int cursor, int axis, pk_game *g)
{
    int c = cursor ^ (1 << axis);
    if (cell_disabled(g, c)) {
        c ^= 1 << (1 - axis);
        if (cell_disabled(g, c)) {
            return cursor;
        }
    }
    return c;
}

/* FUN_100245d0: number of level thresholds reached */
static int kingdom_level(pk_game *g)
{
    int i, n = 0;
    for (i = 0; i < 10; i++) {
        if (pk_app_i32(TBL_LEVELS + (Uint32)i * 4) <= g->fame) {
            n++;
        }
    }
    return n;
}

static bool party_has_injured(pk_game *g)
{
    return pk_heal_cost_all(g) > 0; /* FUN_1007b688 */
}

/* conditions shared by button states 0x91 and 0x92 */
/* Shop cells 1 (Global Auction) and 3 exist only with the Arena
 * connected (game+0x1aa0c == 2); offline they are disabled and blank.
 * Online cell 1 is one wide button (frames 0xba / selected 0xb9, label at
 * x + 0x5a) and cell 3 is hidden. */
static pk_game *s_ui_game;
static bool shop_wide(pk_object *root, int idx)
{
    return root && root->work[W_MENU] == 2 && idx == 1 && s_ui_game && s_ui_game->arena_online;
}
static bool shop_blank(pk_object *root, int idx)
{
    return root && root->work[W_MENU] == 2 && (idx == 1 || idx == 3) && !shop_wide(root, idx);
}

static void update_disabled(pk_game *g, pk_object *root, int idx)
{
    int menu = root ? root->work[W_MENU] : 0;
    cell_enable(g, idx);
    if (shop_blank(root, idx)) {
        cell_disable(g, idx);
        return;
    }
    if (root && menu == 1 && idx == 1 && !party_has_injured(g)) {
        cell_disable(g, 1); /* Medical */
    }
    if (root && g->progress < 10 &&
        ((g->progress == 0 && menu == 0 && idx == 3) || (g->progress < 2 && menu == 1 && idx == 2))) {
        cell_disable(g, idx); /* Lab, Defense early in the story */
    }
}

/* FUN_1002501c: label of a menu button */
static void draw_label(pk_object *o, pk_game *g, pk_object *root)
{
    if (shop_blank(root, o->arg0)) {
        return;
    }
    int id = pk_app_i32(TBL_MENU_LABELS + (Uint32)((root->work[W_MENU] * 4 + o->arg0) * 4));
    pk_color c = root->work[W_CURSOR] == o->arg0 ? COL_SELECTED : COL_NORMAL;
    int lx = shop_wide(root, o->arg0) ? 0x5a : 0x32;
    pk_game_mestext(g, id, o->x + lx, o->y + 0xe, 0x000000u, PK_ALIGN_CENTER, 0, PK_FONT_LEGEND);
    pk_game_mestext(g, id, o->x + lx + 1, o->y + 0xf, c, PK_ALIGN_CENTER, 0, PK_FONT_LEGEND);
}

/* FUN_10025188: icon frame of a menu button, 1 = none */
static int icon_frame(pk_object *o, pk_object *root)
{
    if (!root || shop_blank(root, o->arg0)) {
        return 1;
    }
    return pk_app_i32(TBL_MENU_ICONS + (Uint32)((root->work[W_MENU] * 4 + o->arg0) * 4));
}

/* ------------------------------------------------------------------ */
/* kingdom root object (0x1002479c) */

static int s_scrim_told;

void pk_kingdom_update(pk_object *o, pk_game *g)
{
    int i;
    switch (o->state) {
    case 0:
        o->image = 0;
        o->frame = 0;
        o->counter = 0;
        o->dir = 1; /* +0x44: interface child type */
        o->flags = 0x440;
        pk_object_clear_work(o);
        o->state = 1;
        break;
    case 1:
        if (g->curtain != PK_CURTAIN_HELD) {
            break;
        }
        g->bg_visible = 1;
        pk_game_set_bg(g, 0, g->field_bg);
        pk_object_spawn(g, pk_kingdom_ui_update, o->dir, 0xd0, 0, 0, 0, 0)->layer = 0;
        /* FUN_10024604: top bar and panel */
        pk_object_spawn(g, pk_kingdom_ui_update, o->dir, 0x80, 1, 2, 0x10, 3);
        pk_object_spawn(g, pk_kingdom_ui_update, o->dir, 0x80, 1, 3, 0x61, 3);
        pk_object_spawn(g, pk_kingdom_ui_update, o->dir, 0x80, 1, 4, 0xa1, 3);
        pk_object_spawn(g, pk_kingdom_ui_update, o->dir, 0x80, 0, 0x7a, 0, 0x92);
        pk_game_set_curtain(g, PK_CURTAIN_OPEN);
        pk_audio_play_bgm(2);
        o->state = 2;
        break;
    case 2:
        if (g->curtain != PK_CURTAIN_NONE) {
            break;
        }
        g->cam_x = g->cam_y = 0;
        o->counter = 0;
        for (i = 0; i < 4; i++) {
            int x = i < 2 ? -0x54 : 0xb0, y = (i & 1) ? 0xb2 : 0x9a;
            pk_object_spawn(g, pk_kingdom_ui_update, o->dir, 0x90, i, 6, x, y);
        }
        for (i = 0; i < 4; i++) {
            int x = i < 2 ? -0x50 : 0xb4, y = (i & 1) ? 0xb3 : 0x9b;
            pk_object_spawn(g, pk_kingdom_ui_update, o->dir, 0x90, i, 7, x, y);
        }
        pk_object_spawn(g, pk_kingdom_ui_update, o->dir, 0xa0, 0, 1, -0x1f, 0x9a);
        o->flags &= ~(Uint32)PK_OBJ_SPRITE;
        o->state = 3;
        break;
    case 3:
        if (++o->counter > 4) {
            o->counter = 0;
            o->state = 4;
        }
        break;
    case 4:
        if (g->arena_online && !s_scrim_told && g->curtain == PK_CURTAIN_NONE && !pk_help_active()) {
            /* 0x1005ad94 state 1: once online, the room's rules (trigger 0x37) */
            s_scrim_told = 1;
            if (!SDL_strcasecmp(pk_online_room(), "Scrimmage")) {
                pk_help(g, 1, 0x37);
            }
        }
        if (!g->arena_online) {
            s_scrim_told = 0;
        }
        if (pk_chat_pending() && g->curtain == PK_CURTAIN_NONE && !pk_help_active()) { /* a letter arrived */
            pk_object_call(g, o, pk_chat_update, 0, pk_kingdom_update, 1);
            break;
        }
        for (i = 0; i < 4; i++) { /* touch: a tap on a cell selects it */
            if (pk_tap(g, (i & 2) ? 0x58 : 4, (i & 1) ? 0xb2 : 0x9a, 0x54, 0x18) && !cell_disabled(g, i)) {
                o->work[W_CURSOR] = i;
                g->key_code = PK_SC_5;
            }
        }
        if (g->key_code == PK_SC_LEFT || g->key_code == PK_SC_RIGHT) {
            pk_audio_play_sfx(2);
            o->work[W_CURSOR] = cursor_move(o->work[W_CURSOR], 1, g);
        } else if (g->key_code == PK_SC_UP || g->key_code == PK_SC_DOWN) {
            pk_audio_play_sfx(2);
            o->work[W_CURSOR] = cursor_move(o->work[W_CURSOR], 0, g);
        }
        if (g->key_code == PK_SC_5) {
            int menu = o->work[W_MENU], cur = o->work[W_CURSOR];
            pk_audio_play_sfx(0);
            if (menu == 1 && cur == 0) {
                pk_game_set_curtain(g, PK_CURTAIN_CLOSE); /* 0x100493c4 */
                o->counter = 0;
                o->dir = 1;
                o->state = 0xc;
                pk_object_set_update(o, pk_form_update);
            } else if (menu == 1 && cur == 1) {
                pk_game_set_curtain(g, PK_CURTAIN_CLOSE); /* 0x10045630 */
                o->counter = 0;
                o->dir = 1;
                o->state = 0x20;
                pk_object_set_update(o, pk_medical_update);
            } else if (menu == 1 && cur == 2) {
                pk_game_set_curtain(g, PK_CURTAIN_CLOSE); /* 0x10036b8c */
                o->counter = 0;
                o->dir = 1;
                o->state = 0xc;
                pk_object_set_update(o, pk_defense_update);
            } else if (menu == 1 && cur == 3) {
                pk_game_set_curtain(g, PK_CURTAIN_CLOSE); /* 0x1003d8a0 */
                o->counter = 0;
                o->dir = 1;
                o->state = 0xc;
                pk_object_set_update(o, pk_list_update);
            } else if (menu == 4 && cur == 0) {
                pk_game_set_curtain(g, PK_CURTAIN_CLOSE); /* 0x100329a0 */
                o->counter = 0;
                o->dir = 1;
                o->state = 0xc;
                pk_object_set_update(o, pk_combine_update);
            } else if (menu == 4 && cur == 1) {
                pk_game_set_curtain(g, PK_CURTAIN_CLOSE); /* 0x1003b9a8 */
                o->counter = 0;
                o->dir = 1;
                o->state = 0xc;
                pk_object_set_update(o, pk_vault_update);
            } else if (menu == 4 && cur == 2) {
                pk_game_set_curtain(g, PK_CURTAIN_CLOSE); /* 0x10053a70 */
                o->counter = 0;
                o->dir = 1;
                o->state = 0xc;
                pk_object_set_update(o, pk_rankup_update);
            } else if (menu == 4 && cur == 3) {
                pk_game_set_curtain(g, PK_CURTAIN_CLOSE); /* 0x10045dc8 */
                o->counter = 0;
                o->dir = 1;
                o->state = 0xc;
                pk_object_set_update(o, pk_upgrade_update);
            } else if (menu == 0 && cur == 1) {
                pk_game_set_curtain(g, PK_CURTAIN_CLOSE); /* 0x1005b754 */
                o->counter = 0;
                o->dir = 1;
                o->state = 2;
                pk_object_set_update(o, pk_shop_update);
            } else if (menu == 0 && cur == 2) {
                pk_game_set_curtain(g, PK_CURTAIN_CLOSE); /* 0x1003db80 */
                o->counter = 0;
                o->dir = 1;
                o->state = 2;
                pk_object_set_update(o, pk_map_update);
            } else if (menu == 0) {
                o->counter = 0;
                o->dir = 2;
                o->state = 5; /* open the Party or Lab sub-menu */
            }
        } else if (o->work[W_MENU] != 0 && g->key_code == PK_SC_BACK) {
            pk_audio_play_sfx(1);
            o->counter = 0;
            o->dir = 2;
            o->state = 6;
        }
        break;
    case 5:
    case 6:
        if (++o->counter >= 4) {
            o->work[W_MENU] = o->state == 5 ? o->work[W_CURSOR] + 1 : 0;
            o->work[W_CURSOR] = 0;
            o->work[2] = 0;
            o->counter = 0;
            o->state = 3;
        }
        break;
    default:
        break;
    }
}

/* ------------------------------------------------------------------ */
/* interface children (0x10025354) */

static void ui_show(pk_object *o, pk_game *g, int frame)
{
    o->image = pk_game_image_slot(g, IMG_UI);
    pk_object_set_frames(o, TBL_UI_FRAMES);
    o->frame = frame;
    o->flags = 0x4c1;
}

void pk_kingdom_ui_update(pk_object *o, pk_game *g)
{
    pk_object *root = pk_object_find(g, OBJ_KINGDOM, PK_ANY, PK_ANY);
    char buf[16];
    size_t k;
    int idx = o->arg0;

    s_ui_game = g;
    switch (o->state) {
    /* top bar icons (arg1 2..4) and the menu panel (arg1 0x7a) */
    case 0x80:
        ui_show(o, g, o->arg1);
        if (o->arg0 == 1) {
            o->layer = 1;
        }
        pk_object_clear_work(o);
        o->work[1] = o->y;
        o->state = 0x81;
        return;
    case 0x81:
        if (o->arg1 == 2) {
            int level = kingdom_level(g);
            if (g->progress > 1 && level == 0) {
                level = 1;
            }
            SDL_snprintf(buf, sizeof(buf), "%d", level);
            pk_drawq_text_utf8(&g->drawq, buf, 2, 3, 0, PK_ALIGN_LEFT, o->layer + 0x20, 0);
            SDL_snprintf(buf, sizeof(buf), "%d", g->fame);
            pk_drawq_text_utf8(&g->drawq, buf, 0xa1, 3, 0, PK_ALIGN_RIGHT, o->layer + 0x20, 0);
            SDL_snprintf(buf, sizeof(buf), "%d", g->loot);
            pk_drawq_text_utf8(&g->drawq, buf, 0x60, 3, 0, PK_ALIGN_RIGHT, o->layer + 0x20, 0);
        }
        break;

    /* menu buttons (arg1 6) and their icons (arg1 7) */
    case 0x90:
        ui_show(o, g, o->arg1 == 6 ? 7 : 1);
        o->state = o->arg1 == 6 ? 0x91 : 0x93;
        if (o->arg1 == 6 && shop_wide(root, idx)) {
            o->x <<= 1;
        }
        if (o->arg1 == 6 && root && root->work[W_MENU] == 2 && idx == 3 && g->arena_online) {
            o->flags &= ~(Uint32)PK_OBJ_SPRITE;
        }
        return;
    case 0x91: /* slide in */
        update_disabled(g, root, idx);
        o->frame = shop_wide(root, idx) ? 0xba : cell_disabled(g, idx) ? 8
                                                                       : 7;
        if (idx < 2) {
            o->x += shop_wide(root, idx) ? 0x2c : 0x16;
            if (o->x >= 4) {
                o->x = 4;
                o->state = 0x92;
            }
        } else {
            o->x -= 0x16;
            if (o->x <= 0x58) {
                o->x = 0x58;
                o->state = 0x92;
            }
        }
        if (root && !cell_disabled(g, idx)) {
            draw_label(o, g, root);
        }
        return;
    case 0x92: /* idle */
        update_disabled(g, root, idx);
        if (shop_wide(root, idx)) {
            o->frame = root->work[W_CURSOR] == idx ? 0xb9 : 0xba;
        } else if (root && root->work[W_CURSOR] == idx) {
            o->frame = 6;
        } else {
            o->frame = cell_disabled(g, idx) ? 8 : 7;
        }
        if (root && !cell_disabled(g, idx)) {
            draw_label(o, g, root);
        }
        if (root && (root->state == 5 || root->state == 6)) {
            o->state = 0x94;
        }
        break;
    case 0x93:
    { /* icon follows its button */
        pk_object *btn = pk_object_find(g, o->type, idx, 6);
        int fr;
        if (btn) {
            o->x = btn->x + 4;
            o->y = btn->y;
        }
        fr = icon_frame(o, root);
        o->frame = fr;
        if (fr == 1) {
            o->flags &= ~(Uint32)PK_OBJ_SPRITE;
        } else {
            o->flags |= PK_OBJ_SPRITE;
        }
        if (g->progress < 10 && ((g->progress == 0 && fr == 0x21) || (g->progress < 2 && fr == 0x23))) {
            o->flags &= ~(Uint32)PK_OBJ_SPRITE;
        }
        if (fr == 0x25 && !party_has_injured(g)) {
            o->flags &= ~(Uint32)PK_OBJ_SPRITE;
        }
        break;
    }
    case 0x94: /* slide out, then in again with the new menu */
        if (idx < 2) {
            int old = o->x;
            o->x -= 0x16;
            if (old == -0x3e || o->x <= -0x55) {
                o->frame = 6;
                o->x = -0x54;
                o->state = 0x91;
            }
        } else {
            o->x += 0x16;
            if (o->x >= 0xb0) {
                o->frame = 6;
                o->x = 0xb0;
                o->state = 0x91;
            }
        }
        return;

    /* pointer */
    case 0xa0:
        ui_show(o, g, 0);
        o->layer = 1;
        pk_object_clear_work(o);
        o->state = 0xa1;
        return;
    case 0xa1:
        pk_object_animate(o);
        o->x += 10;
        if (o->x >= 4) {
            o->x = 9;
            o->state = 0xa2;
        }
        return;
    case 0xa2:
        pk_object_animate(o);
        if (root) {
            int c = root->work[W_CURSOR];
            o->x = (c & 2) ? 0x5d : 9;
            o->y = (c & 1) ? 0xb2 : 0x9a;
            if (root->state == 5 || root->state == 6) {
                o->state = 0xa3;
            }
        }
        break;
    case 0xa3:
        if (o->x > 9 && (o->x -= 0xb) < 10) {
            o->x = 9;
        }
        if (o->y > 0x9a && (o->y -= 2) < 0x9b) {
            o->y = 0x9a;
        }
        if (o->x == 9 && o->y == 0x9a) {
            o->state = 0xa2;
        }
        return;

    /* background decoration */
    case 0xd0:
        o->state = 0xd1;
        o->flags = 0;
        for (k = 0; k < SDL_arraysize(k_decos); k++) {
            if (k_decos[k].bg == g->field_bg) {
                pk_game_load_image(g, k_decos[k].image, 0);
                o->image = pk_game_image_slot(g, k_decos[k].image);
                pk_object_set_frames(o, k_decos[k].frames);
                pk_object_set_anim(o, k_decos[k].anim);
                o->x = k_decos[k].x;
                o->y = k_decos[k].y;
                o->frame = 0;
                o->counter = 0;
                o->flags |= 0xe1 | PK_OBJ_ANIMATING | PK_OBJ_KEEP_OFFSCREEN;
            }
        }
        return;
    case 0xd1:
        pk_object_animate(o);
        break;
    default:
        return;
    }
    /* children disappear when the curtain has closed */
    if (g->curtain == PK_CURTAIN_HELD) {
        pk_object_kill(o);
    }
}
