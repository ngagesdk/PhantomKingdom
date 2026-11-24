/* SPDX-License-Identifier: MIT */
/* Mode 3: start-up screens, loading and the title screen.
 *
 * Reconstructed from FUN_1006b5c4 (phase dispatcher) and its phase
 * handlers FUN_1006aeec, FUN_1006b0cc, FUN_1006b128, FUN_1006b490, and
 * from the title object's update function at 0x10078580.
 *
 *   phase 0  Nokia copyright in the selected language (bitmap 2 + language)
 *   phase 1  after 33 frames: SEGA logo (bitmap 1)
 *   phase 2  after 33 frames: black screen, "Now Loading..." and game.id
 *   phase 3  (Arena return path, not used on a cold start)
 *   phase 4  load resources, spawn the title object (type 0xc4)
 *   phase 5  idle; the title object runs the screen
 */
#include "audio/pk_audio.h"
#include "core/pk_app.h"
#include "core/pk_log.h"
#include "game/pk_data.h"
#include "game/pk_help.h"
#include "game/pk_scenes.h"
#include "game/pk_world.h"
#include "gfx/pk_font.h"
#include "input/pk_haptics.h"

/* Original addresses of tables used here (read from 6rah.app). */
#define TBL_TITLE_FRAMES 0x100aff80u /* frames cut from bitmap 7 */
#define TBL_TITLE_ANIM_A 0x100b0130u /* "press" animation */
#define TBL_TITLE_ANIM_B 0x100b0158u /* banner swap between menus */
#define TBL_MENU_LAYOUT  0x100b0198u /* per menu: s32 first y, s32 line spacing */
#define TBL_MENU_ITEMS   0x100b01d8u /* per menu: 5 x s32 mestext id, -1 ends */

#define IMG_ESRB     0
#define IMG_SEGA     1
#define IMG_NOKIA_EN 2 /* +language: en, fr, de, it, es */
#define IMG_TITLE    7 /* 176x1060 sheet, mask in 8 */

#define OBJ_TITLE 0xc4
#define BOOT_WAIT 0x20 /* frames: the test is "> 0x20", so 33 * 90 ms */

static void draw_loading_text(pk_game *g)
{
    /* FUN_100653f8(game, 0, 0x58, 100, white, centre, ...) draws mestext
     * record 0; FUN_100654c0 adds the game.id line at y 0x82. */
    pk_game_text(g, pk_data_text(PK_TBL_MESTEXT, 0), 0x58, 100, 0xFFFFFFu, PK_ALIGN_CENTER, 0);
    if (g->show_version && pk_data_version()[0]) {
        pk_game_text(g, pk_data_version(), 0x58, 0x82, 0xFFFFFFu, PK_ALIGN_CENTER, 0);
    }
}

void pk_mode_boot(pk_game *g)
{
    switch (g->phase) {
    case 0: /* FUN_1006aeec */
        pk_objects_clear(g);
        pk_game_set_curtain(g, PK_CURTAIN_NONE);
        if (g->skip_intro) {
            g->skip_intro = false;
            pk_game_fill(g, 0, 0, PK_SCREEN_W, PK_SCREEN_H, 0xFF000000u);
            g->bg_visible = 0;
            pk_game_set_phase(g, 4);
            break;
        }
        pk_game_set_bg(g, 0, IMG_NOKIA_EN + g->language);
        g->bg_visible = 1;
        pk_game_set_phase(g, 1);
        g->mode_timer = 0;
        break;
    case 1: /* FUN_1006b0cc */
        if (g->mode_timer > BOOT_WAIT || g->key_code == PK_SC_5) {
            pk_game_set_curtain(g, PK_CURTAIN_NONE);
            pk_game_set_bg(g, 0, IMG_SEGA);
            g->bg_visible = 1;
            pk_game_set_phase(g, 2);
            g->mode_timer = 0;
        }
        g->mode_timer++;
        break;
    case 2: /* FUN_1006b128 */
        if (g->mode_timer > BOOT_WAIT || g->key_code == PK_SC_5) {
            pk_game_fill(g, 0, 0, PK_SCREEN_W, PK_SCREEN_H, 0xFF000000u);
            g->show_version = 1;
            draw_loading_text(g);
            g->bg_visible = 0;
            pk_game_set_phase(g, 4);
        }
        g->mode_timer++;
        break;
    case 4: /* FUN_1006b490: 14 load jobs, then the title object */
        pk_game_set_bg(g, 0, PK_IMAGE_NONE);
        pk_game_load_image(g, IMG_TITLE, 1);
        draw_loading_text(g);
        pk_object_spawn(g, pk_title_update, OBJ_TITLE, 0, 0, 0, 0, 0);
        pk_game_set_phase(g, 5);
        g->mode_timer = 0;
        pk_game_set_curtain(g, PK_CURTAIN_NONE);
        break;
    default:
        break;
    }
}

/* ------------------------------------------------------------------ */
/* Title object, update function 0x10078580. One function serves the
 * root object (type 0xc4, states 0..0x16) and its children (states
 * 0x17..0x21): the logo flash, the menu banner and the volume gauge.
 *
 * Root fields: work[5] (+0x2c) next menu, work[6] (+0x30) "swap the
 * banner to menu work[5]", work[7] (+0x34) state to enter after the
 * 14 frame swap wait (state 0x14).
 *
 * Menus (tables 0x100b0198 / 0x100b01d8 in 6RAH.APP, texts in
 * mestext.dat): 0 main, 1 "New Game" (no save), 3 options, 4 quit
 * confirmation, 5 "Continue / Delete Save File", 7 delete confirmation. */

#define W_NEXT_MENU 5
#define W_SWAP      6
#define W_NEXT      7

#define MENU_MAIN    0
#define MENU_NEW     1
#define MENU_OPTIONS 3
#define MENU_QUIT    4
#define MENU_SAVE    5
#define MENU_DELETE  7

#define MES_SKIP     0x11c /* empty entry */
#define MES_LANGUAGE 0x127 /* + language: "English", ... */
#define IDLE_ATTRACT 0x1ad

/* Symbian TRgb is 0x00BBGGRR; colours below are the original values
 * converted to 0xRRGGBB. */
#define COL_SELECTED 0xFFFFFFu
#define COL_NORMAL   0xFFDC00u /* TRgb 0x00dcff */
#define COL_DISABLED 0xB4B4B4u
#define COL_SHADOW   0x000000u

static void title_show(pk_object *o, pk_game *g, Uint32 flags, int frame)
{
    o->image = pk_game_image_slot(g, IMG_TITLE);
    o->flags = flags;
    pk_object_set_frames(o, TBL_TITLE_FRAMES);
    o->frame = frame;
}

static void menu_swap(pk_object *o, int menu, int next_state)
{
    o->work[W_SWAP] = 1;
    o->work[W_NEXT_MENU] = menu;
    o->counter = 0;
    o->state = next_state;
}

static void menu_back(pk_object *o, pk_game *g)
{
    /* LAB_100793c0 / LAB_100793ec: back to the main menu */
    (void)g;
    menu_swap(o, MENU_MAIN, 4);
}

static void menu_wait(pk_object *o, pk_game *g, int next, int cursor)
{
    g->cursor = cursor;
    o->work[W_SWAP] = 0;
    o->work[W_NEXT] = next;
    o->counter = 0;
    o->state = 0x14;
}

/* FUN_10079b18: draws menu o->arg0 through the text queue. */
static void menu_draw(pk_object *o, pk_game *g)
{
    int y, step, i;
    if (o->arg0 < 0 || o->arg0 >= 8) {
        return;
    }
    y = pk_app_i32(TBL_MENU_LAYOUT + (Uint32)o->arg0 * 8);
    step = pk_app_i32(TBL_MENU_LAYOUT + (Uint32)o->arg0 * 8 + 4);
    for (i = 0; i < 5; i++, y += step) {
        int id = pk_app_i32(TBL_MENU_ITEMS + (Uint32)(o->arg0 * 20 + i * 4));
        pk_color c;
        if (id == -1) {
            return;
        }
        if (id == MES_SKIP) {
            continue;
        }
        if (id == MES_LANGUAGE) {
            id += g->language;
        }
        pk_game_mestext(g, id, 0x59, y + 1, COL_SHADOW, PK_ALIGN_CENTER, 0, PK_FONT_LEGEND);
        if (i == g->cursor) {
            c = COL_SELECTED;
        } else if (id == 0x110 && !g->arena_available) {
            c = COL_DISABLED;
        } else {
            c = COL_NORMAL;
        }
        pk_game_mestext(g, id, 0x58, y, c, PK_ALIGN_CENTER, 0, PK_FONT_LEGEND);
    }
}

/* Touch (port): the entries of menu `menu` as drawn by menu_draw. A tap
 * on an entry moves the cursor there and selects it; on the volume and
 * language rows the left / right half turns the value. */
static void menu_touch(pk_game *g, int menu)
{
    int y, step, i;
    y = pk_app_i32(TBL_MENU_LAYOUT + (Uint32)menu * 8);
    step = pk_app_i32(TBL_MENU_LAYOUT + (Uint32)menu * 8 + 4);
    for (i = 0; i < 5; i++, y += step) {
        int id = pk_app_i32(TBL_MENU_ITEMS + (Uint32)(menu * 20 + i * 4));
        if (id == -1) {
            break;
        }
        if (id == MES_SKIP) {
            continue;
        }
        if (menu == MENU_OPTIONS && (i == 0 || i == 4)) {
            /* the row and the gauge below it */
            int h = i == 0 ? step + 0x14 : step;
            if (pk_tap(g, 0x08, y - step + 3, 0x50, h)) {
                g->cursor = i;
                g->key_code = PK_SC_LEFT;
            }
            if (pk_tap(g, 0x58, y - step + 3, 0x50, h)) {
                g->cursor = i;
                g->key_code = PK_SC_RIGHT;
            }
            continue;
        }
        /* y is the baseline: the row is the line of text above it */
        if (pk_tap(g, 0x10, y - step + 3, 0x90, step)) {
            if (id == 0x110 && !g->arena_available) {
                continue; /* N-Gage Arena still locked */
            }
            g->cursor = i;
            g->key_code = PK_SC_5;
        }
    }
    {
        int d = pk_swipe_y(g); /* the menus are short: scrolling moves the cursor */
        if (d && g->key_code == PK_SC_NONE) {
            g->key_code = d > 0 ? PK_SC_DOWN : PK_SC_UP;
            pk_haptics_play(PK_HAPTIC_STEP);
        }
    }
}

static void leave_title(pk_game *g, int entry_reason, int slide)
{
    g->entry_reason = entry_reason;
    g->slide = slide;
    pk_game_set_curtain(g, PK_CURTAIN_CLOSE);
}

static void root_main_menu(pk_object *o, pk_game *g)
{
    o->counter++;
    switch (g->key_code) {
    case PK_SC_5:
        o->counter = 0;
        pk_audio_play_sfx(0);
        switch (g->cursor) {
        case 0: /* Offline Play */
            g->slide = 0;
            menu_swap(o, pk_save_exists() ? MENU_SAVE : MENU_NEW, 6);
            break;
        case 1: /* N-Gage Arena */
            leave_title(g, 100, 2);
            o->work[W_SWAP] = 0;
            o->counter = 0;
            o->state = 0x15;
            break;
        case 2: /* Options */
            g->slide = 0;
            menu_swap(o, MENU_OPTIONS, 0xc);
            break;
        default: /* Quit */
            g->slide = 0;
            menu_swap(o, MENU_QUIT, 0x10);
            break;
        }
        break;
    case PK_SC_UP:
        o->counter = 0;
        pk_audio_play_sfx(2);
        if (g->cursor > 0) {
            g->cursor--;
        }
        if (g->cursor == 1 && !g->arena_available) {
            g->cursor = 0;
        }
        break;
    case PK_SC_DOWN:
        o->counter = 0;
        pk_audio_play_sfx(2);
        if (g->cursor < 3) {
            g->cursor++;
        }
        if (g->cursor == 1 && !g->arena_available) {
            g->cursor = 2;
        }
        break;
    case PK_SC_LEFT:
    case PK_SC_RIGHT:
        o->counter = 0;
        break;
    default:
        break;
    }
    if (o->counter == IDLE_ATTRACT) {
        /* the attract mode (0x10036e68) */
        pk_object_kill_others(g, o);
        o->counter = 0;
        o->state = 0;
        pk_object_set_update(o, pk_attract_update);
    }
}

static void root_options(pk_object *o, pk_game *g)
{
    switch (g->key_code) {
    case PK_SC_5:
        if (g->cursor == 3) { /* Credits */
            leave_title(g, g->entry_reason, g->slide);
            pk_audio_play_sfx(0);
            o->counter = 0;
            o->state = 0x30; /* port: wait for the curtain, then the credits */
        }
        return;
    case PK_SC_BACK:
        pk_audio_play_sfx(1);
        g->slide = 1;
        menu_swap(o, MENU_MAIN, 4);
        pk_options_save(g);
        return;
    case PK_SC_UP:
        pk_audio_play_sfx(2);
        if (g->cursor > 0) {
            g->cursor = g->cursor == 3 ? 0 : g->cursor - 1;
        }
        break;
    case PK_SC_DOWN:
        pk_audio_play_sfx(2);
        if (g->cursor == 0) {
            g->cursor = 3;
        } else if (g->cursor == 3) {
            g->cursor = 4;
        }
        break;
    default:
        break;
    }
    if (g->cursor == 0) {
        if (g->key_code == PK_SC_LEFT && g->volume > 0) {
            g->volume--;
            pk_audio_set_volume(g->volume * 2, g->volume * 2);
        } else if (g->key_code == PK_SC_RIGHT && g->volume < 5) {
            g->volume++;
            pk_audio_set_volume(g->volume * 2, g->volume * 2);
        }
    } else if (g->cursor == 4) {
        if (g->key_code == PK_SC_LEFT) {
            pk_audio_play_sfx(0);
            g->language = g->language == 0 ? 4 : g->language - 1;
            pk_game_apply_language(g);
        } else if (g->key_code == PK_SC_RIGHT) {
            pk_audio_play_sfx(0);
            g->language = g->language == 4 ? 0 : g->language + 1;
            pk_game_apply_language(g);
        }
    }
}

void pk_title_update(pk_object *o, pk_game *g)
{
    pk_object *root;
    switch (o->state) {
    /* ---- root object ---- */
    case 0:
        o->flags = PK_OBJ_SCREEN | PK_OBJ_KEEP_OFFSCREEN; /* 0x440 */
        pk_object_clear_work(o);
        o->counter = 0;
        o->state = 1;
        pk_game_set_curtain(g, PK_CURTAIN_CLOSE);
        pk_audio_set_volume(g->volume * 2, g->volume * 2);
        break;
    case 1:
        if (g->curtain == PK_CURTAIN_HELD) {
            g->title_ready = 0;
            pk_object_spawn(g, pk_title_update, 0, 0x17, 0, 0, 0, 0);
            pk_object_spawn(g, pk_title_update, 2, 0x1a, MENU_MAIN, 3, 0x58, 0x9e);
            pk_audio_play_bgm(0);
            g->bg_visible = 0;
            title_show(o, g, 0xe1, 0);
            o->x = o->y = 0;
            o->counter = 0;
            o->state = 2;
        }
        break;
    case 2:
        if (o->counter == 0) {
            o->counter = 1;
            pk_game_set_curtain(g, PK_CURTAIN_OPEN);
        }
        if (g->curtain == PK_CURTAIN_NONE) {
            o->counter = 0;
            o->state = 3;
        }
        break;
    case 3:
        o->counter++;
        o->flags &= ~(Uint32)PK_OBJ_SPRITE;
        if (g->title_ready == 1) {
            g->arena_available = pk_world_arena_unlocked();
            g->cursor = 0;
            o->counter = 0;
            o->state = 5;
        }
        break;
    case 4:
        g->arena_available = pk_world_arena_unlocked(); /* game+0x43c / +0x440 from the save */
        menu_wait(o, g, 5, 0);
        break;
    case 5:
        menu_touch(g, MENU_MAIN);
        root_main_menu(o, g);
        break;
    case 6: /* Offline Play: "New Game" or "Continue / Delete Save File" */
        g->cursor = 0;
        menu_wait(o, g, pk_save_exists() ? 8 : 7, 0);
        break;
    case 7: /* menu 1: New Game */
        menu_touch(g, MENU_NEW);
        if (g->key_code == PK_SC_5) {
            pk_audio_play_sfx(0);
            pk_object_kill_others(g, o);
            leave_title(g, 1, 2);
            pk_newgame_start(g);
            o->work[W_SWAP] = 0;
            o->counter = 0;
            o->state = 0;
            pk_object_set_update(o, pk_opening_update);
        } else if (g->key_code == PK_SC_BACK) {
            pk_audio_play_sfx(1);
            menu_back(o, g);
        }
        break;
    case 8: /* menu 5: Continue / Delete Save File */
        menu_touch(g, MENU_SAVE);
        if (g->key_code == PK_SC_5) {
            pk_audio_play_sfx(0);
            if (g->cursor == 0) {
                leave_title(g, 0, 2);
                o->work[W_SWAP] = 0;
                o->counter = 0;
                o->state = 0x15;
            } else {
                menu_swap(o, MENU_DELETE, 0x12);
            }
        } else if (g->key_code == PK_SC_BACK) {
            pk_audio_play_sfx(1);
            menu_back(o, g);
        } else if (g->key_code == PK_SC_UP) {
            pk_audio_play_sfx(2);
            if (g->cursor > 0) {
                g->cursor--;
            }
        } else if (g->key_code == PK_SC_DOWN) {
            pk_audio_play_sfx(2);
            if (g->cursor < 1) {
                g->cursor++;
            }
        }
        break;
    case 0xc: /* options: the volume gauge is two sprites */
        g->cursor = 0;
        pk_object_spawn(g, pk_title_update, 0, 0xe, 0, 0xc, 0x1f, 0x96);
        pk_object_spawn(g, pk_title_update, 0, 0xe, 0, 0xd, 0x1f, 0x96);
        menu_wait(o, g, 0xd, 0);
        break;
    case 0xd:
        menu_touch(g, MENU_OPTIONS);
        root_options(o, g);
        break;
    case 0x10: /* quit confirmation, "No" preselected */
        menu_wait(o, g, 0x11, 1);
        break;
    case 0x11:
        menu_touch(g, MENU_QUIT);
        if (g->key_code == PK_SC_5) {
            if (g->cursor == 0) {
                pk_audio_play_sfx(0);
                g->entry_reason = 5; /* the application exits */
                g->quit = true;
            } else {
                pk_audio_play_sfx(1);
                g->slide = 1;
                menu_back(o, g);
            }
        } else if (g->key_code == PK_SC_BACK) {
            pk_audio_play_sfx(1);
            menu_back(o, g);
        } else if (g->key_code == PK_SC_UP || g->key_code == PK_SC_DOWN) {
            pk_audio_play_sfx(2);
            g->cursor ^= 1;
        }
        /* the question, mestext 0x144 and 0x145, gold with a shadow
         * (FUN_10065578) */
        pk_game_mestext(g, 0x144, 0x59, 0x88, COL_SHADOW, PK_ALIGN_CENTER, 0, PK_FONT_LEGEND);
        pk_game_mestext(g, 0x144, 0x58, 0x87, 0xFFDC00u, PK_ALIGN_CENTER, 0, PK_FONT_LEGEND);
        pk_game_mestext(g, 0x145, 0x59, 0x97, COL_SHADOW, PK_ALIGN_CENTER, 0, PK_FONT_LEGEND);
        pk_game_mestext(g, 0x145, 0x58, 0x96, 0xFFDC00u, PK_ALIGN_CENTER, 0, PK_FONT_LEGEND);
        break;
    case 0x12: /* delete confirmation, "No" preselected */
        menu_wait(o, g, 0x13, 3);
        break;
    case 0x13:
        menu_touch(g, MENU_DELETE);
        if (g->key_code == PK_SC_5) {
            if (g->cursor == 2) {
                pk_audio_play_sfx(0);
                pk_save_delete();
            } else {
                pk_audio_play_sfx(1);
            }
            g->slide = 1;
            menu_back(o, g);
        } else if (g->key_code == PK_SC_BACK) {
            pk_audio_play_sfx(1);
            menu_back(o, g);
        } else if (g->key_code == PK_SC_UP || g->key_code == PK_SC_DOWN) {
            pk_audio_play_sfx(2);
            g->cursor = g->cursor == 2 ? 3 : 2;
        }
        break;
    case 0x14: /* wait for the banner swap */
        if (++o->counter > 0xd) {
            o->counter = 0;
            o->state = o->work[W_NEXT];
        }
        break;
    case 0x15: /* leaving the title: Continue (0) or Arena (100) */
        if (g->curtain == PK_CURTAIN_HELD) {
            pk_object_kill_others(g, o);
            if (g->entry_reason == 100) {
                pk_game_set_mode(g, PK_MODE_ARENA_PENDING);
            } else if (pk_world_load(g)) { /* FUN_10075e80 */
                pk_game_set_mode(g, PK_MODE_FIELD);
            } else {
                /* damaged save: message screen (state 0x16) */
                pk_game_set_curtain(g, PK_CURTAIN_OPEN);
                pk_game_fill(g, 0, 0, PK_SCREEN_W, PK_SCREEN_H, 0xFF000000u);
                g->bg_visible = 0;
                o->flags &= ~(Uint32)PK_OBJ_SPRITE;
                o->counter = 300;
                o->state = 0x16;
            }
        }
        break;
    case 0x16:
        if (g->curtain == PK_CURTAIN_NONE) {
            pk_game_fill(g, 0, 0, PK_SCREEN_W, PK_SCREEN_H, 0xFF000000u);
            pk_game_mestext(g, 0x14c, 0x58, 0x46, 0xFFFFFFu, PK_ALIGN_CENTER, 0, PK_FONT_LEGEND);
            pk_game_mestext(g, 0x14d, 0x58, 100, 0xFFFFFFu, PK_ALIGN_CENTER, 0, PK_FONT_LEGEND);
            pk_game_mestext(g, 0x14e, 0x58, 0x82, 0xFFFFFFu, PK_ALIGN_CENTER, 0, PK_FONT_LEGEND);
        }
        if (--o->counter == 0 || g->key_code == PK_SC_5 || g->key_code == PK_SC_BACK) {
            pk_world_reset(g); /* FUN_1006c228(game, 1) */
            pk_help_reset_popups();
            g->skip_intro = true;
            pk_game_set_mode(g, PK_MODE_BOOT);
        }
        break;
    case 0x30: /* port: credits (0x1003632c) once the curtain is closed */
        if (g->curtain == PK_CURTAIN_HELD) {
            pk_object_kill_others(g, o);
            pk_game_set_mode(g, PK_MODE_CREDITS_PENDING);
        }
        break;

    /* ---- child: volume gauge (arg1 0xc frame, 0xd level) ---- */
    case 0xe:
        o->image = pk_game_image_slot(g, IMG_TITLE);
        o->flags = 0x4e0;
        pk_object_set_frames(o, TBL_TITLE_FRAMES);
        o->frame = o->arg1;
        o->counter = 0xe;
        o->state = 0xf;
        if (o->arg1 == 0xd && g->volume != 0) {
            o->frame = g->volume + 0xc;
        }
        break;
    case 0xf:
        root = pk_object_find(g, OBJ_TITLE, 0, 0);
        if (o->counter > 0) {
            o->counter--;
            break;
        }
        if (o->arg1 == 0xd) {
            if (g->volume == 0) {
                o->flags &= ~(Uint32)PK_OBJ_SPRITE;
            } else {
                o->flags |= PK_OBJ_SPRITE;
                o->frame = o->arg1 + g->volume - 1;
            }
        } else {
            o->flags |= PK_OBJ_SPRITE;
        }
        if (root && root->work[W_SWAP] == 1) {
            pk_object_kill(o);
        }
        break;

    /* ---- child: light flash over the logo ---- */
    case 0x17:
        title_show(o, g, 0xe0, 3);
        pk_object_clear_work(o);
        o->counter = 0;
        o->state = 0x18;
        break;
    case 0x18:
        if (g->curtain == PK_CURTAIN_NONE) {
            o->counter = 0;
            o->state = 0x19;
        }
        break;
    case 0x19:
        o->counter++;
        if (o->counter == 0xc) {
            o->flags |= PK_OBJ_SPRITE;
        } else if (o->counter == 0xe) {
            o->frame = 2;
        } else if (o->counter == 0x10) {
            o->frame = 1;
        } else if (o->counter > 0x11) {
            o->flags &= ~(Uint32)PK_OBJ_SPRITE;
        }
        break;

    /* ---- child: menu banner ---- */
    case 0x1a:
        title_show(o, g, 0x4e0, 4);
        pk_object_set_anim(o, TBL_TITLE_ANIM_A);
        o->frame = 4;
        pk_object_clear_work(o);
        o->counter = 0;
        o->state = 0x1c;
        break;
    case 0x1b: /* swap to another menu */
        pk_object_set_anim(o, TBL_TITLE_ANIM_B);
        o->frame = 8;
        pk_object_clear_work(o);
        o->work[0] = 0xe;
        o->counter = 0;
        o->state = 0x1f;
        break;
    case 0x1c:
        if (g->curtain == PK_CURTAIN_NONE && ++o->counter > 0x17) {
            o->counter = 0;
            o->flags |= PK_OBJ_SPRITE;
            o->work[0] = 8;
            o->state = 0x1e;
        }
        break;
    case 0x1e:
    case 0x1f:
        pk_object_animate(o);
        if (++o->counter >= o->work[0]) {
            g->title_ready = 1;
            o->state = 0x20;
        }
        break;
    case 0x20:
        root = pk_object_find(g, OBJ_TITLE, 0, 0);
        if (root && root->work[W_SWAP] == 1) {
            o->counter = 0;
            o->arg0 = root->work[W_NEXT_MENU];
            o->state = 0x1b;
            break;
        }
        menu_draw(o, g);
        break;
    default:
        break;
    }
}
