/* SPDX-License-Identifier: MIT */
/* Attract mode (0x10036e68): started when the title menu has waited
 * 0x1ad ticks. Captions are records of demo.dat (64 bytes each, read
 * from the user's installation; some records are growing copies of one
 * line, shown one after another as typing); pictures are images of
 * pokekin.mbm. Any key, or the end of the show, returns to the title.
 *
 *   step 2  background 0x95 scrolls in (camera y from -0xd0); typing
 *           records 0..0x12 at (0x18, 0x50) and 0x13..0x24 at
 *           (0x24, 0x78); record 0x25 centred at (0x58, 0x68); at tick
 *           200 the pictures 0..0xb appear
 *   step 3  two centred lines (records 0x26/0x27, port's reading); at
 *           tick 0x28 pictures 0xc..0xe
 *   step 4  records 0x28, 0x29, 0x2a typed at (0x3c, 100); picture 0xf
 *   step 5  record 0x2b centred; picture 0x10
 *   step 6  record 0x2c and 0x2b; then back to the title
 * Pictures (object type 7, by number): 0/0xc/0xf/0x10 set backgrounds
 * 0x96/0x9b/0xa1/0xa2; 1/2 images 0x97/0x99 (frames 0x10091154);
 * 3..0xb images 0xa9 0xa7 0xa5 0xb5 0xb3 0xb1 0xaf 0xad 0xab
 * (frames 0x1009119c) sliding sideways; 0xd/0xe images 0x9c/0x9e
 * rising. Each lasts 0x28 ticks. */
#include "audio/pk_audio.h"
#include "core/pk_fs.h"
#include "core/pk_log.h"
#include "game/pk_scenes.h"
#include "gfx/pk_font.h"
#include <SDL3/SDL.h>

#define OBJ_ATTRACT 0xc2

static Uint16 s_slot[3][33];

static void load_line(int rec, int slot)
{
    size_t n = 0;
    Uint8 *d = (Uint8 *)pk_fs_load_localized("demo.dat", &n);
    int i;
    s_slot[slot][0] = 0;
    if (d && (size_t)rec * 64 + 64 <= n) {
        for (i = 0; i < 32; i++) {
            Uint16 c = (Uint16)(d[rec * 64 + i * 2] | (d[rec * 64 + i * 2 + 1] << 8));
            if (c == 0x1b || c == 0) {
                break;
            }
            s_slot[slot][i] = c;
        }
        s_slot[slot][i] = 0;
    }
    SDL_free(d);
}

/* black text with a white outline, or the reverse */
static void draw_line(pk_game *g, int slot, int x, int y, bool white_text, pk_align al)
{
    Uint32 ol = white_text ? 0x000000u : 0xFFFFFFu, fg = white_text ? 0xFFFFFFu : 0x000000u;
    if (!s_slot[slot][0]) {
        return;
    }
    pk_drawq_text(&g->drawq, s_slot[slot], x - 1, y - 1, ol, al, 1, PK_FONT_LEGEND);
    pk_drawq_text(&g->drawq, s_slot[slot], x + 1, y + 1, ol, al, 1, PK_FONT_LEGEND);
    pk_drawq_text(&g->drawq, s_slot[slot], x, y, fg, al, 1, PK_FONT_LEGEND);
}

static void picture(pk_game *g, int id, int x, int y)
{
    pk_object_spawn(g, pk_attract_update, 7, 0x10, 0, id, x, y);
}

static void picture_update(pk_object *o, pk_game *g)
{
    static const int k_img[9] = { 0xa9, 0xa7, 0xa5, 0xb5, 0xb3, 0xb1, 0xaf, 0xad, 0xab };
    static const int k_x[3] = { 0x96, 0x1e, 0xbe };
    static const int k_dx[9] = { -12, -8, -4, 12, 8, 4, -12, -8, -4 };
    int id = o->arg1;
    if (o->state == 0x10) {
        o->layer = 0;
        o->flags = 0x4c1;
        o->frame = 0;
        if (id == 0 || id == 0xc || id == 0xf || id == 0x10) {
            int bg = id == 0 ? 0x96 : id == 0xc ? 0x9b
                                  : id == 0xf   ? 0xa1
                                                : 0xa2;
            pk_game_set_bg(g, 0, bg);
            g->cam_y = 0;
            g->bg_visible = 1;
            pk_object_kill(o);
            return;
        }
        if (id == 1 || id == 2) {
            o->image = pk_game_image_slot(g, id == 1 ? 0x97 : 0x99);
            pk_object_set_frames(o, 0x10091154u);
            o->frame = id;
            o->y = id == 1 ? 0x49 : 0x91;
        } else if (id >= 3 && id <= 0xb) {
            o->image = pk_game_image_slot(g, k_img[id - 3]);
            pk_object_set_frames(o, 0x1009119cu);
            o->x = k_x[(id - 3) / 3];
        } else { /* 0xd, 0xe */
            o->image = pk_game_image_slot(g, id == 0xd ? 0x9c : 0x9e);
            pk_object_set_frames(o, 0x10091154u);
            o->y = id == 0xd ? 0x96 : 200;
        }
        o->counter = 0;
        o->state = 0x11;
        return;
    }
    o->counter++;
    if (id >= 3 && id <= 0xb) {
        o->x += k_dx[id - 3];
    } else if (id == 0xd) {
        o->y -= 4;
    } else if (id == 0xe) {
        o->y -= 8;
    }
    if (o->counter >= 0x28) {
        pk_object_kill(o);
    }
}

static void back_to_title(pk_object *o, pk_game *g)
{
    g->cam_x = g->cam_y = 0;
    pk_object_kill_others(g, o);
    o->state = 0;
    o->counter = 0;
    pk_object_set_update(o, pk_title_update);
}

void pk_attract_update(pk_object *o, pk_game *g)
{
    if (o->type == 7) {
        picture_update(o, g);
        return;
    }
    if (o->state >= 2 && (g->key_code != PK_SC_NONE)) {
        back_to_title(o, g);
        return;
    }
    switch (o->state) {
    case 0:
        if (g->curtain == PK_CURTAIN_NONE) {
            pk_game_set_curtain(g, PK_CURTAIN_CLOSE);
        }
        if (g->curtain == PK_CURTAIN_HELD && ++o->counter > 3) {
            pk_object_kill_others(g, o);
            g->cam_x = 0;
            g->cam_y = -0xd0;
            pk_game_set_bg(g, 0, 0x95);
            g->bg_visible = 1;
            o->flags &= ~(Uint32)PK_OBJ_SPRITE;
            pk_audio_play_bgm(3);
            pk_game_set_curtain(g, PK_CURTAIN_OPEN);
            SDL_memset(s_slot, 0, sizeof(s_slot));
            o->state = 1;
        }
        break;
    case 1:
        if (g->curtain != PK_CURTAIN_NONE) {
            break;
        }
        o->work[0] = 0;    /* +0x18 typing 0..0x12 */
        o->work[1] = 0x13; /* +0x1c typing 0x13..0x24 */
        o->counter = 0;
        o->state = 2;
        break;
    case 2:
    {
        int t = ++o->counter;
        if (t == 200) {
            static const int k_at[12][3] = { { 0, 0, 0x26 }, { 3, 0, 0x26 }, { 4, 0, 0x26 }, { 5, 0, 0x26 }, { 1, 0x58, 0x68 }, { 6, 0, 0x68 }, { 7, 0, 0x68 }, { 8, 0, 0x68 }, { 2, 0x58, 0x68 }, { 9, 0, 0xb1 }, { 10, 0, 0xb1 }, { 11, 0, 0xb1 } };
            int i;
            for (i = 0; i < 12; i++) {
                picture(g, k_at[i][0], k_at[i][1], k_at[i][2]);
            }
            SDL_memset(s_slot, 0, sizeof(s_slot));
            o->counter = 0;
            o->state = 3;
            break;
        }
        if (t < 0xaa) {
            if (t > 0x14 && g->cam_y < 0) {
                g->cam_y++;
            }
        } else {
            if (t == 0xaa) {
                load_line(0x25, 0);
            }
            if (g->cam_y < 0) {
                g->cam_y += 4;
                if (g->cam_y > 0) {
                    g->cam_y = 0;
                }
            } else {
                draw_line(g, 0, 0x58, 0x68, false, PK_ALIGN_CENTER);
            }
        }
        if (t - 0x1f < 0x8b) {
            if (t % 2 == 1 && o->work[0] < 0x13) {
                load_line(o->work[0]++, 0);
            }
            draw_line(g, 0, 0x18, 0x50, false, PK_ALIGN_LEFT);
        }
        if (t - 0x51 < 0x59 && t >= 0x51) {
            if (t % 2 == 1 && o->work[1] < 0x25) {
                load_line(o->work[1]++, 1);
            }
            draw_line(g, 1, 0x24, 0x78, false, PK_ALIGN_LEFT);
        }
        break;
    }
    case 3:
    {
        int t = ++o->counter;
        if (t == 0xf) {
            load_line(0x26, 0);
        }
        if (t == 0x14) {
            load_line(0x27, 1);
        }
        if (t > 0xf) {
            draw_line(g, 0, 0x58, 0x4b, false, PK_ALIGN_CENTER);
        }
        if (t > 0x14) {
            draw_line(g, 1, 0x58, 0x91, false, PK_ALIGN_CENTER);
        }
        if (t == 0x28) {
            picture(g, 0xc, 0, 0);
            picture(g, 0xd, 0x58, 0);
            picture(g, 0xe, 0x58, 0);
            SDL_memset(s_slot, 0, sizeof(s_slot));
            o->counter = 0;
            o->state = 4;
        }
        break;
    }
    case 4:
    {
        int t = ++o->counter;
        if (t == 0xe) {
            load_line(0x28, 0);
        }
        if (t == 0x11) {
            load_line(0x29, 0);
        }
        if (t == 0x13) {
            load_line(0x2a, 0);
        }
        if (t > 0xe) {
            draw_line(g, 0, 0x3c, 100, false, PK_ALIGN_LEFT);
        }
        if (t == 0x28) {
            picture(g, 0xf, 0, 0);
            load_line(0x2b, 1);
            o->counter = 0;
            o->state = 5;
        }
        break;
    }
    case 5:
    {
        int t = ++o->counter;
        if (t > 10) {
            draw_line(g, 1, 0x58, 100, true, PK_ALIGN_CENTER);
        }
        if (t == 0x28) {
            picture(g, 0x10, 0, 0);
            load_line(0x2c, 2);
            o->counter = 0;
            o->state = 6;
        }
        break;
    }
    case 6:
    {
        int t = ++o->counter;
        if (t > 10) {
            draw_line(g, 2, 0x58, 0x50, true, PK_ALIGN_CENTER);
            draw_line(g, 1, 0x58, 0x78, true, PK_ALIGN_CENTER);
        }
        if (t == 0x28) {
            back_to_title(o, g);
        }
        break;
    }
    default:
        break;
    }
}
