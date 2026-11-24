/* SPDX-License-Identifier: MIT */
/* New game: kingdom set-up and the opening story.
 *
 * pk_newgame_start mirrors title state 7 (0x10078580) when "New Game" is
 * confirmed; pk_opening_update is the update function the title root
 * object switches to (0x1004597c): opening.dat scrolls up over bitmaps
 * 0x92, 0x93 and 0x94 with music 3, then mode 1 starts. */
#include "audio/pk_audio.h"
#include "game/pk_data.h"
#include "game/pk_help.h"
#include "game/pk_scenes.h"
#include "gfx/pk_font.h"

#define IMG_OPENING_1 0x92
#define IMG_OPENING_2 0x93
#define IMG_OPENING_3 0x94
#define LINE_SPACING  0x1a
#define RING          10 /* the original keeps ten lines (game+0x9064) */

/* work[] of the object: 3 line count (+0x24), 4 visible lines (+0x28),
 * 5 lines loaded (+0x2c), 6 first visible line (+0x30), 7 y (+0x34). */
#define W_LINES   3
#define W_VISIBLE 4
#define W_LOADED  5
#define W_FIRST   6
#define W_Y       7

void pk_newgame_start(pk_game *g)
{
    int names = pk_data_kingdom_name_count();
    int idx, tries;

    /* save game reset (FUN_1006c228) */
    pk_world_reset(g);
    pk_help_reset_popups();
    g->menu_disabled = 0;
    /* +0x138 = rand % 5, +0x13c = FUN_1007b018(g, 1): a name from records
     * 200..399 that nobody else uses */
    g->rival_index = (int)(pk_rng_next(&g->rng) & 0x7fff) % 5;
    idx = 200 + (int)((pk_rng_next(&g->rng) & 0x7fff) % 200);
    for (tries = 0; tries < 200 && idx == g->rival_index; tries++) {
        idx = idx + 1 >= 400 ? 200 : idx + 1;
    }
    g->name_index = idx;
    if (idx < names) {
        pk_data_kingdom_name(idx, g->kingdom_name, (int)SDL_arraysize(g->kingdom_name));
    }
    g->field_bg = 0xf;
}

static int opening_lines(void)
{
    const pk_text_table *t = pk_data_table(PK_TBL_OPENING);
    return t ? t->count : 0;
}

void pk_opening_update(pk_object *o, pk_game *g)
{
    int i, total, fh;
    switch (o->state) {
    case 0:
        if (g->curtain == PK_CURTAIN_HELD && ++o->counter > 3) {
            pk_object_kill_others(g, o);
            pk_game_set_curtain(g, PK_CURTAIN_OPEN);
            pk_game_set_bg(g, 0, IMG_OPENING_1);
            g->cam_y = 0;
            g->bg_visible = 1;
            o->flags &= ~(Uint32)PK_OBJ_SPRITE;
            pk_audio_play_bgm(3);
            o->state = 1;
            o->work[W_LINES] = opening_lines();
        }
        break;
    case 1:
        if (g->curtain == PK_CURTAIN_NONE) {
            g->cursor = 0;
            o->counter = 0;
            o->work[W_VISIBLE] = 0;
            o->work[W_LOADED] = 0;
            o->work[W_FIRST] = 0;
            o->work[W_Y] = 0xdc;
            o->state = 2;
        }
        break;
    case 2:
        total = (o->work[W_LINES] + 8) * LINE_SPACING;
        fh = pk_font_height(PK_FONT_LEGEND);
        if (g->key_code == PK_SC_5) {
            pk_game_set_curtain(g, PK_CURTAIN_CLOSE);
            o->counter = 0;
            o->state = 3;
        }
        if (o->counter % LINE_SPACING == 0 && o->work[W_LOADED] < o->work[W_LINES]) {
            o->work[W_LOADED]++;
            if (o->work[W_VISIBLE] < RING) {
                o->work[W_VISIBLE]++;
            }
        }
        o->counter++;
        if (o->work[W_Y] < -0x14) {
            o->work[W_Y] += LINE_SPACING;
            o->work[W_FIRST]++;
            if (o->work[W_LINES] * LINE_SPACING < o->counter && o->work[W_VISIBLE] > 0) {
                o->work[W_VISIBLE]--;
            }
        } else {
            o->work[W_Y]--;
        }
        if (o->counter == total / 3) {
            pk_game_set_bg(g, 0, IMG_OPENING_2);
        } else if (o->counter == total * 2 / 3) {
            pk_game_set_bg(g, 0, IMG_OPENING_3);
        } else if (o->counter >= total + fh && o->state == 2) {
            pk_game_set_curtain(g, PK_CURTAIN_CLOSE);
            o->counter = 0;
            o->state = 3;
            break;
        }
        for (i = 0; i < o->work[W_VISIBLE]; i++) {
            int line = o->work[W_FIRST] + i, y = i * LINE_SPACING + o->work[W_Y];
            const Uint16 *s;
            if (line >= o->work[W_LOADED]) {
                break;
            }
            s = pk_data_text(PK_TBL_OPENING, line);
            pk_drawq_text(&g->drawq, s, 0x57, y - 1, 0x000000u, PK_ALIGN_CENTER, 1, PK_FONT_LEGEND);
            pk_drawq_text(&g->drawq, s, 0x59, y + 1, 0x000000u, PK_ALIGN_CENTER, 1, PK_FONT_LEGEND);
            pk_drawq_text(&g->drawq, s, 0x58, y, 0xFFFFFFu, PK_ALIGN_CENTER, 1, PK_FONT_LEGEND);
        }
        break;
    case 3:
        if (g->curtain == PK_CURTAIN_HELD) {
            pk_game_set_mode(g, PK_MODE_FIELD);
        }
        break;
    default:
        break;
    }
}
