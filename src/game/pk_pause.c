/* SPDX-License-Identifier: MIT */
/* The pause menu (FUN_1006e17c) and its window objects (0x10051a8c ..).
 *
 * Opened by either soft key in battle or on the kingdom screen
 * (FUN_10070100) and when the application loses focus. While it is
 * open the game objects are put aside (game+0x658) and only the pause
 * objects run; the last game frame stays underneath.
 *
 * On the kingdom screen (mode 1) it is the original's only way to save
 * and to go back to the title: page 0 lists resume, volume, save and
 * quit; page 1 confirms the save, page 2 offers to save before
 * quitting, pages 3..5 show the outcome. In the other modes
 * (FUN_100732d0) it only shows the paused plate (page 6).
 * All texts come from the user's mestext and pausemess.dat. */
#include "game/pk_pause.h"
#include "audio/pk_audio.h"
#include "engine/pk_game.h"
#include "engine/pk_object.h"
#include "game/pk_data.h"
#include "game/pk_help.h"
#include "game/pk_world.h"
#include "gfx/pk_font.h"
#include "gfx/pk_gfx.h"
#include <SDL3/SDL.h>

#define IMG_WINDOW    0x4a
#define FRAMES_WINDOW 0x1009a3a0u
#define TBL_Y         0x100aee88u /* not read: the layout below */
#define MES_BLANK     284         /* 0x11c: skipped */
#define FONT          PK_FONT_LEGEND

/* 0x100aee88: first baseline and step per page */
static const int k_y0[7] = { 30, 62, 47, 55, 40, 40, 62 };
static const int k_dy[7] = { 15, 16, 14, 16, 14, 14, 16 };
/* 0x100aeec0: mestext ids per page, -1 ends */
static const int k_items[7][6] = {
    { 397, 283, 284, 284, 204, 289 },
    { 65, 66, -1, 0, 0, 0 },
    { 204, 284, 65, 66, -1, 0 },
    { 202, -1, 0, 0, 0, 0 },
    { 329, 330, 331, 284, 202, -1 },
    { 329, 330, 331, 284, 202, -1 },
    { 397, -1, 0, 0, 0, 0 },
};
/* 0x100aef9c: pausemess record per cursor */
static const int k_help[10] = { 5, 0, -1, -1, 1, 2, 3, 4, 5, 1 };
/* the secretary's frame tables, eye and mouth animations by secretary
 * (0x10051f64, FUN_100523ac / FUN_100524ac) and their offsets
 * (0x1009ac80, 0x1009aca8) */
static const Uint32 k_sec_frames[5] = { 0x1009a7a8u, 0x1009a8c0u, 0x1009a9b0u, 0x1009aaa0u, 0x1009ab90u };
static const Uint32 k_sec_eye[5] = { 0x1009a890u, 0x1009a980u, 0x1009aa70u, 0x1009ab60u, 0x1009ac50u };
static const Uint32 k_sec_mouth[5] = { 0x1009a8a8u, 0x1009a998u, 0x1009aa88u, 0x1009ab78u, 0x1009ac68u };
static const int k_eye_off[5][2] = { { -18, -7 }, { -12, -12 }, { -21, -15 }, { -28, -17 }, { -20, -10 } };
static const int k_mouth_off[5][2] = { { -5, 6 }, { -3, 2 }, { -16, -3 }, { -24, -3 }, { -8, 4 } };
#define ANIM_SEC_OPEN 0x1009a868u

#define MAX_LINE 64
static struct
{
    bool active; /* game+0x630 */
    int wait;    /* game+0x634 */
    int page;    /* game+0x638 */
    int cursor;  /* game+0x130 */
    int talk;    /* game+0x63c */
    int help_id; /* game+0x1a9ac */
    int help_n;  /* game+0x1a9a8 */
    Uint16 line[4][MAX_LINE];
    bool quitting; /* port: the curtain closes, then the title */
    pk_object_list list;
    Uint32 *snapshot;
} P;

bool pk_pause_active(void) { return P.active; }
bool pk_pause_quitting(void) { return P.quitting; }

/* FUN_100732d0 */
static bool resume_only(const pk_game *g)
{
    return g->mode == PK_MODE_BATTLE || g->mode == PK_MODE_KINGDOM || g->mode == PK_MODE_BOOT;
}

static int rnd33(pk_game *g) { return (int)(pk_rng_next(&g->rng) & 0x7fff) % 0x21; } /* FUN_100714e8 % 33 */

/* FUN_10065224: one pausemess record, split at line feeds */
static void load_help(int id)
{
    const Uint16 *s = pk_data_text(PK_TBL_PAUSEMESS, id);
    int n = 0, k = 0;
    SDL_zeroa(P.line);
    while (s && *s && n < 4) {
        if (*s == '\n') {
            n++;
            k = 0;
        } else if (*s != '\r' && k < MAX_LINE - 1) {
            P.line[n][k++] = *s;
        }
        s++;
    }
    P.help_n = n < 4 && k > 0 ? n + 1 : n;
    P.help_id = id;
}

/* ---- window objects -------------------------------------------- */

static void window_volume(pk_object *o, pk_game *g) /* 0x10051d84 */
{
    if (o->state == 0) {
        o->image = pk_game_image_slot(g, IMG_WINDOW);
        o->flags = 0x4c0;
        pk_object_set_frames(o, FRAMES_WINDOW);
        o->frame = o->arg1;
        o->state = 1;
        if (o->arg1 == 0x14) {
            if (g->volume == 0) {
                return;
            }
            o->frame = g->volume + 0x13;
        }
    } else if (o->state != 1) {
        return;
    }
    if (P.page != 0) {
        o->flags &= ~1u;
    } else if (o->arg1 == 0x14) {
        if (g->volume != 0) {
            o->flags |= 1;
            o->frame = o->arg1 + g->volume - 1;
        } else {
            o->flags &= ~1u;
        }
    } else {
        o->flags |= 1;
    }
}

static void window_plate(pk_object *o, pk_game *g) /* 0x10051e6c */
{
    if (o->state != 0) {
        return;
    }
    o->image = pk_game_image_slot(g, IMG_WINDOW);
    o->flags = 0x4c1;
    pk_object_set_frames(o, FRAMES_WINDOW);
    if (!P.active) {
        o->frame = 0x11;
        o->y = 0x4c;
    } else {
        o->frame = 0x12;
    }
    o->layer = 0;
    pk_object_clear_work(o);
    o->state = 1;
}

static void window_edge(pk_object *o, pk_game *g) /* 0x10051ccc */
{
    if (o->state != 0) {
        return;
    }
    o->image = pk_game_image_slot(g, IMG_WINDOW);
    pk_object_set_frames(o, FRAMES_WINDOW);
    o->flags = 0x4c1;
    o->frame = o->arg1;
    o->layer = 1;
    if (o->arg1 == 8) {
        pk_object_spawn(g, window_plate, 0, 0, 0, 0, 0x58, 0x91);
    }
    o->state = 1;
}

static void window_paused(pk_object *o, pk_game *g) /* 0x10051ef8 */
{
    if (o->state != 0) {
        return;
    }
    o->image = pk_game_image_slot(g, IMG_WINDOW);
    o->flags = 0x4c1;
    pk_object_set_frames(o, FRAMES_WINDOW);
    o->frame = 0x2a;
    o->layer = 0;
    pk_object_clear_work(o);
    o->state = 1;
}

static void secretary(pk_object *o, pk_game *g) /* 0x10051f64 */
{
    int sec = g->rival_index;
    bool ok = sec >= 0 && sec < 5;
    switch (o->state) {
    case 0:
        o->image = pk_game_image_slot(g, 0x4e + sec * 2);
        o->flags = 0xc1;
        o->state = 1;
        o->layer = 1;
        o->counter = 2;
        if (ok) {
            pk_object_set_frames(o, k_sec_frames[sec]);
        }
        pk_object_set_anim(o, ANIM_SEC_OPEN);
        break;
    case 1:
        pk_object_animate(o);
        if (o->counter >= 0 && --o->counter < 1) {
            if (P.active && !resume_only(g)) {
                pk_object_spawn(g, window_volume, 0, 0, 0, 0x13, 0x1f, 0x3e);
                pk_object_spawn(g, window_volume, 0, 0, 0, 0x14, 0x1f, 0x3e);
            }
            pk_object_spawn(g, secretary, 0, 3, 0, 1, 0x84, 0xa0);
            pk_object_spawn(g, secretary, 0, 3, 0, 3, 0x84, 0xa0);
            o->state = 2;
            o->counter = 1;
        }
        break;
    case 2:
        if (o->counter < 0) {
            return;
        }
        if (--o->counter > 0) {
            return;
        }
        o->frame = 0;
        break;
    case 3: /* eye (1) or mouth (3) */
        o->flags = 0x4c1;
        o->frame = o->arg1;
        o->state = o->arg1 == 1 ? 4 : 6;
        o->layer = 1;
        o->counter = rnd33(g);
        o->image = pk_game_image_slot(g, 0x4e + sec * 2);
        if (ok) {
            const int (*off)[2] = o->arg1 == 1 ? k_eye_off : k_mouth_off;
            pk_object_set_frames(o, k_sec_frames[sec]);
            pk_object_set_anim(o, o->arg1 == 1 ? k_sec_eye[sec] : k_sec_mouth[sec]);
            o->x += off[sec][0];
            o->y += off[sec][1];
        }
        P.talk = 0x37;
        break;
    case 4:
        pk_object_animate(o);
        if (--o->counter < 1) {
            o->counter = rnd33(g);
            o->state = 5;
        }
        break;
    case 5:
        o->frame = 1;
        if (--o->counter < 1) {
            o->counter = rnd33(g);
            o->state = 4;
        }
        break;
    case 6:
        if (P.talk > 0) {
            P.talk--;
            pk_object_animate(o);
            return;
        }
        o->frame = 4;
        break;
    default:
        break;
    }
}

static void window_main(pk_object *o, pk_game *g) /* 0x10051a8c */
{
    if (o->state == 0) {
        o->image = pk_game_image_slot(g, IMG_WINDOW);
        o->flags = 0xc0;
        pk_object_set_frames(o, FRAMES_WINDOW);
        o->frame = 0x10;
        o->layer = 0;
        o->counter = 1;
        pk_object_clear_work(o);
        o->state = 1;
        if (!resume_only(g) || o->arg0 != 1) {
            pk_object_spawn(g, window_edge, 0, 0, 0, 8, 0x58, 0);
            pk_object_spawn(g, window_edge, 0, 0, 0, 3, 0, 0x68);
            pk_object_spawn(g, window_edge, 0, 0, 0, 4, 0xb0, 0x68);
            pk_object_spawn(g, window_edge, 0, 0, 0, 0xc, 0x58, 0xd0);
            pk_object_spawn(g, secretary, 0, 0, 0, 7, 0x84, 0xa0);
        } else {
            pk_object_spawn(g, window_paused, 0, 0, 0, 0, 0, 0);
        }
    } else if (o->state == 1 && (!resume_only(g) || o->arg0 != 1) && o->counter > 0 && --o->counter < 1) {
        o->flags |= 1;
    }
}

/* ---- the menu ---------------------------------------------------- */

static void open_menu(pk_game *g)
{
    Uint32 *fb = pk_gfx_framebuffer();
    pk_audio_play_sfx(0x22);
    pk_drawq_reset(&g->drawq);
    P.wait = 3;
    P.cursor = 0;
    load_help(0);
    P.talk = 0;
    P.page = resume_only(g) ? 6 : 0;
    if (!P.snapshot) {
        P.snapshot = (Uint32 *)SDL_malloc(PK_SCREEN_W * PK_SCREEN_H * sizeof(Uint32));
    }
    if (P.snapshot && fb) {
        SDL_memcpy(P.snapshot, fb, PK_SCREEN_W * PK_SCREEN_H * sizeof(Uint32));
    }
    g->saved = g->objects;
    SDL_zero(P.list);
    g->objects = &P.list;
    pk_game_load_image(g, g->rival_index * 2 + 0x4e, 1);
    pk_game_load_image(g, IMG_WINDOW, 1);
    pk_object_spawn(g, window_main, 0, 0, 1, 0, 0x58, 0x68);
}

static void close_menu(pk_game *g)
{
    if (g->saved) {
        pk_object_list_free(&P.list);
        g->objects = g->saved;
        g->saved = NULL;
    }
    P.active = false;
    g->key_code = PK_SC_NONE;
    pk_drawq_reset(&g->drawq);
}

void pk_pause_open(pk_game *g)
{
    if (P.active || P.quitting) {
        return;
    }
    P.active = true;
    P.wait = -1;
    (void)g;
}

/* port: the original goes on with +0x58 = 2 under the closing curtain */
static void quit_to_title(pk_game *g)
{
    close_menu(g);
    pk_game_set_curtain(g, PK_CURTAIN_CLOSE);
    P.quitting = true;
}

void pk_pause_quit_tick(pk_game *g)
{
    g->key_code = PK_SC_NONE;
    g->tap = 0;
    if (g->curtain == PK_CURTAIN_HELD) {
        P.quitting = false;
        g->entry_reason = 0;
        g->skip_intro = true;
        pk_game_set_mode(g, PK_MODE_BOOT);
    }
}

static void set_page(int page, int cursor)
{
    P.page = page;
    P.cursor = cursor;
    P.talk = 0x37;
}

/* port: touch. A tap on an item row selects it and presses 5; on the
 * volume row a tap left or right of the middle steps the volume, a tap
 * on the bar sets it, a sideways swipe drags it; a tap anywhere else
 * leaves the page (Back, or 5 on a message page). */
static void set_volume(pk_game *g, int v)
{
    v = SDL_clamp(v, 0, 5);
    P.cursor = 1;
    if (v == g->volume) {
        return;
    }
    g->volume = v;
    pk_audio_set_volume(g->volume * 2, g->volume * 2); /* FUN_10069dec */
    pk_options_save(g);
}

static void touch(pk_game *g)
{
    int i, y, dy;
    /* a sideways swipe on page 0 moves the volume with the finger
     * (the shared swipe-to-key mapping runs the other way, like a list) */
    if (P.page == 0 && g->swipe_x) {
        set_volume(g, g->volume - g->swipe_x);
        g->swipe_x = 0;
    }
    if (!g->tap) {
        return;
    }
    /* a tap on the volume bar (frame 0x13 at 0x1f, 0x3e: 0x71 wide, five
     * steps of 23) sets the volume there */
    if (P.page == 0 && g->tap_y >= 0x34 && g->tap_y < 0x48 && g->tap_x >= 0x10 && g->tap_x < 0xa0) {
        g->tap = 0;
        set_volume(g, (g->tap_x - 0x1f + 11) / 23);
        return;
    }
    g->tap = 0;
    if (P.page == 6 || P.page == 3) {
        g->key_code = PK_SC_5;
        return;
    }
    y = k_y0[P.page];
    dy = k_dy[P.page];
    for (i = 0; i < 6 && k_items[P.page][i] >= 0; i++, y += dy) {
        bool sel = (P.page == 0 && i != 2 && i != 3) || (P.page == 1 && i < 2) || (P.page == 2 && i >= 2) ||
                   ((P.page == 4 || P.page == 5) && i == 4);
        int top = y - dy + 3, h = dy;
        if (!sel || g->tap_y < top || g->tap_y >= top + h) {
            continue;
        }
        if (P.page == 0 && i == 1) {
            P.cursor = 1;
            g->key_code = g->tap_x < 0x58 ? PK_SC_LEFT : PK_SC_RIGHT;
        } else if (P.cursor != i && P.page != 4 && P.page != 5) {
            P.cursor = i;
            P.talk = 0x37;
            g->key_code = PK_SC_5;
        } else {
            g->key_code = PK_SC_5;
        }
        return;
    }
    /* anywhere else (the secretary's paper, the margins): dismiss, as
     * Back does on pages 0..2 and 5 does on the message pages, so a
     * pause opened by accident (a long press is Back) is easy to leave */
    g->key_code = P.page <= 2 ? PK_SC_BACK : PK_SC_5;
}

static void menu_logic(pk_game *g)
{
    int key = g->key_code;
    int help = -1;
    switch (P.page) {
    case 0:
        if (key == PK_SC_BACK || (key == PK_SC_5 && P.cursor == 0)) {
            pk_audio_play_sfx(0x23);
            close_menu(g);
            return;
        }
        if (P.cursor == 1 && ((key == PK_SC_LEFT && g->volume > 0) || (key == PK_SC_RIGHT && g->volume < 5))) {
            g->volume += key == PK_SC_LEFT ? -1 : 1;
            pk_audio_set_volume(g->volume * 2, g->volume * 2); /* FUN_10069dec */
            pk_options_save(g);
        }
        if (key == PK_SC_5) {
            if (P.cursor == 4) {
                pk_audio_play_sfx(0);
                set_page(1, 1);
            } else if (P.cursor == 5) {
                pk_audio_play_sfx(0);
                set_page(2, 2); /* the online exit (game+0x1aa0c == 2) is not ported */
            }
        } else if (key == PK_SC_UP) {
            pk_audio_play_sfx(2);
            if (P.cursor > 0) {
                P.cursor = P.cursor == 4 ? 1 : P.cursor - 1;
                P.talk = 0x37;
            }
        } else if (key == PK_SC_DOWN) {
            pk_audio_play_sfx(2);
            if (P.cursor < 5) {
                if (P.cursor != 1) {
                    P.cursor++;
                    P.talk = 0x37;
                } else {
                    P.cursor = 4;
                }
            }
        }
        if (P.page == 0) {
            help = k_help[P.cursor];
        }
        break;
    case 1:
        if (key == PK_SC_UP || key == PK_SC_DOWN) {
            pk_audio_play_sfx(2);
            P.cursor ^= 1;
        } else if (key == PK_SC_5) {
            pk_audio_play_sfx(0);
            if (P.cursor != 0) {
                set_page(0, 4);
            } else if (pk_world_save(g)) { /* FUN_100747b8, FUN_1007709c */
                set_page(3, P.cursor);
            } else {
                set_page(4, 4);
            }
        } else if (key == PK_SC_BACK) {
            pk_audio_play_sfx(2);
            set_page(0, 4);
        }
        help = P.page == 1 ? k_help[P.cursor + 6] : P.page == 0 ? k_help[4]
                                                                : k_help[8];
        break;
    case 2:
        if (key == PK_SC_UP || key == PK_SC_DOWN) {
            pk_audio_play_sfx(2);
            P.cursor = 5 - P.cursor;
        } else if (key == PK_SC_5) {
            pk_audio_play_sfx(0);
            if (P.cursor != 2) {
                pk_audio_play_sfx(0x23);
                quit_to_title(g);
                return;
            }
            if (pk_world_save(g)) {
                pk_audio_play_sfx(0x23);
                quit_to_title(g);
                return;
            }
            set_page(5, 4);
        } else if (key == PK_SC_BACK) {
            pk_audio_play_sfx(2);
            set_page(0, 5);
        }
        help = P.page == 2 ? k_help[P.cursor == 3 ? 7 : 6] : P.page == 0 ? k_help[5]
                                                                         : k_help[8];
        break;
    case 3:
    case 4:
    case 5:
        if (key == PK_SC_5 || key == PK_SC_BACK) {
            pk_audio_play_sfx(0);
            if (P.page == 5) {
                set_page(2, 2);
                help = k_help[6];
            } else {
                set_page(0, 4);
                help = k_help[4];
            }
        } else {
            help = k_help[8];
        }
        break;
    case 6:
        if (key == PK_SC_5 || key == PK_SC_BACK) {
            pk_audio_play_sfx(0x23);
            close_menu(g);
            return;
        }
        break;
    default:
        break;
    }
    if (P.page != 6) {
        int i, y = k_y0[P.page];
        for (i = 0; i < 6; i++, y += k_dy[P.page]) {
            int id = k_items[P.page][i];
            if (id == -1) {
                break;
            }
            if (id == MES_BLANK) {
                continue;
            }
            pk_game_mestext(g, id, 0x59, y + 1, 0, PK_ALIGN_CENTER, 0, FONT);
            pk_game_mestext(g, id, 0x58, y, i == P.cursor ? 0xFFFFFFu : 0xFFDC00u, PK_ALIGN_CENTER, 0, FONT);
        }
        pk_drawq_text(&g->drawq, g->kingdom_name, 0x41, 0xc2, 0, PK_ALIGN_CENTER, 0, FONT);
        pk_drawq_text(&g->drawq, g->kingdom_name, 0x40, 0xc1, 0xFFFFFFu, PK_ALIGN_CENTER, 0, FONT);
        if (help >= 0) {
            if (help != P.help_id) {
                load_help(help);
            }
            for (i = 0; i < P.help_n; i++) {
                pk_drawq_text(&g->drawq, P.line[i], 0x10, 0x84 + i * 12, 0, PK_ALIGN_LEFT, 1, FONT);
            }
        }
    }
}

void pk_pause_tick(pk_game *g)
{
    g->tap_targets++; /* taps are handled here, not turned into 5 */
    g->swipe_users_y++;
    if (P.page == 0) {
        g->swipe_users_x++; /* sideways swipes are the volume (touch) */
    }
    if (P.wait < 0) {
        open_menu(g);
    } else if (P.wait > 0) {
        P.wait--;
        g->tap = 0;
    } else {
        touch(g);
        menu_logic(g);
        if (!P.active) {
            return;
        }
    }
    pk_objects_update(g);
    pk_objects_reap(g);
}

void pk_pause_render(pk_game *g)
{
    Uint32 *fb = pk_gfx_framebuffer();
    if (P.snapshot && fb) {
        SDL_memcpy(fb, P.snapshot, PK_SCREEN_W * PK_SCREEN_H * sizeof(Uint32));
    }
    for (g->render_pass = 0; g->render_pass < 2; g->render_pass++) {
        pk_objects_draw(g, g->render_pass);
        pk_drawq_render(&g->drawq, g->render_pass);
    }
}
