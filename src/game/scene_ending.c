/* SPDX-License-Identifier: MIT */
/* Ending (0x10038208) and the staff roll.
 *
 * Beating the boss of area 0x17 sets game+0x58 = 3; after the results
 * the title mode hands over to the ending: background 0xb7, the screen
 * shakes for 70 frames (stronger, then weaker), then the lines of
 * ending.dat scroll up from y 0xdc (26 pixels apart, at most ten on
 * screen, centred at x 0x58 with a shadow) until frame 0x42a or 5. The
 * kingdom then goes on: game+0x43c (times cleared) + 1, game+0x448 = 1,
 * and enemies are found at higher levels from then on.
 *
 * The title menu's Credits (0x1003632c): background 0xba, music 7; a
 * staff.dat line every 10 ticks (183 lines), lines 20 pixels apart
 * moving up 2 pixels a tick, at most 13 on screen; with every tenth
 * line (counting from the sixth) a picture 0xbd + n (frames
 * 0x100910f4) slides in from x 0x108 at y 0x68 (8 px a tick for 0x16
 * ticks), rests 100 ticks and slides out. 5 ends it. The texts and
 * pictures are read from the user's installation. */
#include "audio/pk_audio.h"
#include "core/pk_log.h"
#include "core/pk_text.h"
#include "game/pk_data.h"
#include "game/pk_help.h"
#include "game/pk_scenes.h"
#include "game/pk_world.h"
#include "gfx/pk_font.h"
#include <SDL3/SDL.h>

static struct
{
    int state, counter, scroll, first, shown, next, lines, credits, pictures;
} s_end;

static int table_lines(pk_table t)
{
    const pk_text_table *tb = pk_data_table(t);
    return tb ? tb->count : 0;
}

static void roll_start(pk_game *g, bool credits)
{
    pk_game_set_curtain(g, PK_CURTAIN_NONE);
    pk_game_release_images(g);
    pk_objects_clear(g);
    g->cam_x = g->cam_y = 0;
    g->cam_x = g->cam_y = 0;
    SDL_zero(s_end);
    s_end.credits = credits;
    s_end.lines = table_lines(credits ? PK_TBL_STAFF : PK_TBL_ENDING);
    if (!credits && s_end.lines > 0x21) {
        s_end.lines = 0x21;
    }
    g->field_bg = credits ? 0xba : 0xb7;
    pk_game_set_bg(g, 0, g->field_bg);
    g->bg_visible = 1;
    pk_audio_play_bgm(7);
    s_end.state = credits ? 3 : 2;
    s_end.scroll = 0xdc;
}

static void roll_finish(pk_game *g);

/* credits picture (type 5 children of 0x1003632c) */
static void credit_picture(pk_object *o, pk_game *g)
{
    switch (o->state) {
    case 5:
        o->image = pk_game_image_slot(g, 0xbd + o->arg1);
        o->flags = 0x4e1;
        pk_object_set_frames(o, 0x100910f4u);
        o->frame = 0;
        o->counter = 0;
        o->state = 6;
        break;
    case 6:
        o->x -= 8;
        if (++o->counter == 0x16) {
            o->counter = 0;
            o->state = 7;
        }
        break;
    case 7:
        if (++o->counter == 100) {
            o->counter = 0;
            o->state = 8;
        }
        break;
    default:
        o->x -= 8;
        if (++o->counter == 0x18) {
            pk_object_kill(o);
        }
        break;
    }
}

static void credits_tick(pk_game *g)
{
    int i;
    if (g->key_code == PK_SC_5 || s_end.state == 4) {
        roll_finish(g);
        return;
    }
    if (s_end.counter % 10 == 0 && s_end.next < s_end.lines) {
        int line = s_end.next++;
        if ((line + 5) % 10 == 0 && s_end.pictures < 20) {
            pk_object_spawn(g, credit_picture, 5, 5, 0, s_end.pictures++, 0x108, 0x68);
        }
        if (s_end.shown < 0xd) {
            s_end.shown++;
        }
    }
    s_end.counter++;
    if (s_end.scroll < -0x14) {
        s_end.scroll += 0x12;
        s_end.first++;
        if (s_end.next >= s_end.lines) {
            if (s_end.shown < 1) {
                s_end.state = 4;
            } else {
                s_end.shown--;
            }
        }
    } else {
        s_end.scroll -= 2;
    }
    for (i = 0; i < s_end.shown; i++) {
        int line = s_end.first + i, y = i * 0x14 + s_end.scroll;
        const Uint16 *t;
        if (line >= s_end.lines) {
            break;
        }
        t = pk_data_text(PK_TBL_STAFF, line);
        if (!t) {
            continue;
        }
        pk_drawq_text(&g->drawq, t, 0x57, y - 1, 0, PK_ALIGN_CENTER, 1, PK_FONT_LEGEND);
        pk_drawq_text(&g->drawq, t, 0x58, y, 0xFFFFFFu, PK_ALIGN_CENTER, 1, PK_FONT_LEGEND);
    }
}

static void roll_lines(pk_game *g)
{
    int i;
    pk_table t = s_end.credits ? PK_TBL_STAFF : PK_TBL_ENDING;
    for (i = 0; i < s_end.shown; i++) {
        int line = s_end.first + i, y = s_end.scroll + i * 0x1a;
        const Uint16 *s;
        if (line >= s_end.lines) {
            break;
        }
        s = pk_data_text(t, line);
        if (!s) {
            continue;
        }
        pk_drawq_text(&g->drawq, s, 0x57, y - 1, 0, PK_ALIGN_CENTER, 1, PK_FONT_LEGEND);
        pk_drawq_text(&g->drawq, s, 0x59, y + 1, 0, PK_ALIGN_CENTER, 1, PK_FONT_LEGEND);
        pk_drawq_text(&g->drawq, s, 0x58, y, 0xFFFFFFu, PK_ALIGN_CENTER, 1, PK_FONT_LEGEND);
    }
}

static void roll_finish(pk_game *g)
{
    g->cam_x = g->cam_y = 0;
    if (s_end.credits) {
        PK_DEBUG("credits finished");
        g->entry_reason = 0;
        g->skip_intro = true;
        pk_game_set_mode(g, PK_MODE_BOOT);
        return;
    }
    g->world.h_43c++;
    g->world.h_448 = 1;
    g->ending_pending = 0;
    PK_DEBUG("ending seen: cleared %d time(s)", g->world.h_43c);
    pk_world_save(g);
    pk_game_set_mode(g, PK_MODE_FIELD);
    pk_help_show_opened_area(g); /* areas opened by the last boss, if any */
}

void pk_mode_ending(pk_game *g)
{
    if (g->phase == 0) {
        roll_start(g, g->mode == PK_MODE_CREDITS_PENDING);
        pk_game_set_phase(g, 1);
        return;
    }
    if (s_end.credits) {
        credits_tick(g);
        return;
    }
    switch (s_end.state) {
    case 2: /* the quake */
        s_end.counter++;
        if (g->key_code == PK_SC_5) {
            s_end.state = 4;
            break;
        }
        if (s_end.counter >= 0x46) {
            g->cam_x = g->cam_y = 0;
            s_end.counter = 0;
            s_end.state = 3;
        } else if (s_end.counter < 0x1e) {
            g->cam_x = pk_rng_range(&g->rng, 10) - 5;
            g->cam_y = pk_rng_range(&g->rng, 8) - 4;
        } else if (s_end.counter < 0x32) {
            g->cam_x = pk_rng_range(&g->rng, 6) - 3;
            g->cam_y = pk_rng_range(&g->rng, 5) - 2;
        } else {
            g->cam_x = pk_rng_range(&g->rng, 2);
            g->cam_y = pk_rng_range(&g->rng, 2);
        }
        break;
    case 3: /* the roll */
        if (g->key_code == PK_SC_5 || g->key_code == PK_SC_BACK) {
            s_end.state = 4;
            break;
        }
        if (s_end.counter % 0x1a == 0 && s_end.next < s_end.lines) {
            s_end.next++;
            if (s_end.shown < 10) {
                s_end.shown++;
            }
        }
        s_end.counter++;
        if (s_end.scroll < -0x14) {
            s_end.scroll += 0x1a;
            s_end.first++;
            if (s_end.next >= s_end.lines && s_end.shown > 0) {
                s_end.shown--;
            }
        } else {
            s_end.scroll--;
        }
        roll_lines(g);
        if ((s_end.next >= s_end.lines && s_end.shown == 0) || s_end.counter >= 0x42a + (s_end.credits ? 0x400 : 0)) {
            s_end.state = 4;
        }
        break;
    default:
        roll_finish(g);
        break;
    }
}
