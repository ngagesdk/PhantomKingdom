/* SPDX-License-Identifier: MIT */
#include "engine/pk_game.h"
#include "audio/pk_audio.h"
#include "core/pk_app.h"
#include "core/pk_fs.h"
#include "core/pk_log.h"
#include "game/pk_arena_game.h"
#include "game/pk_data.h"
#include "game/pk_help.h"
#include "game/pk_pause.h"
#include "game/pk_rules.h"
#include "game/pk_scenes.h"
#include "input/pk_haptics.h"
#include "input/pk_input.h"

#include <SDL3/SDL.h>

bool pk_game_init(pk_game *g)
{
    SDL_zerop(g);
    g->objects = &g->list_a;
    g->saved = NULL;
    g->bg_id[0] = g->bg_id[1] = g->bg_id[2] = PK_IMAGE_NONE;
    g->overlay_id = -1;
    pk_rng_seed(&g->rng, SDL_GetPerformanceCounter());
    g->arena_available = 0;
    g->volume = 3;
    pk_world_reset(g);
    pk_rules_init();
    return true;
}

void pk_game_shutdown(pk_game *g)
{
    pk_object_list_free(&g->list_a);
    pk_object_list_free(&g->list_b);
    pk_game_release_images(g);
}

void pk_game_set_mode(pk_game *g, int mode)
{
    if (mode == PK_MODE_BOOT && g->arena_online) {
        pk_arena_go_offline(g);
    }
    g->mode = mode;
    g->overlay_id = -1;
    g->call_ret_fn = NULL;
    g->call_ret_state = 0;
    g->call_depth = 0;
    pk_game_set_phase(g, 0);
    g->title_ready = 0;
    g->timer = 0; /* +0xa8 in the original; reused as the mode's scratch timer here */
}

void pk_game_set_phase(pk_game *g, int phase)
{
    g->phase = phase;
}

void pk_game_set_curtain(pk_game *g, int mode)
{
    g->curtain = mode;
    switch (mode) {
    case PK_CURTAIN_OPEN:
    case PK_CURTAIN_BLACK:
    case PK_CURTAIN_HELD:
        g->curtain_w = 0xb0;
        break;
    default:
        g->curtain_w = 0;
        break;
    }
}

void pk_game_set_bg(pk_game *g, int slot, int id)
{
    if (slot >= 0 && slot < 3) {
        if (g->bg_id[slot] != PK_IMAGE_NONE && g->bg_id[slot] != id) {
            pk_gfx_release(PK_BANK_MAIN, g->bg_id[slot]);
        }
        g->bg_id[slot] = id;
    }
}

int pk_game_image_slot(pk_game *g, int id)
{
    int i;
    for (i = 0; i < g->slot_count; i++) {
        if (g->slots[i].id == id) {
            return i;
        }
    }
    return PK_IMAGE_NONE;
}

int pk_game_load_image(pk_game *g, int id, int mask_off)
{
    int s = pk_game_image_slot(g, id);
    if (s != PK_IMAGE_NONE) {
        return s;
    }
    if (g->slot_count >= PK_MAX_SLOTS) {
        PK_WARN("game: bitmap list full");
        return PK_IMAGE_NONE;
    }
    s = g->slot_count++;
    g->slots[s].id = id;
    g->slots[s].mask_off = mask_off;
    g->slots[s].img = NULL; /* decoded on first draw */
    SDL_zeroa(g->slots[s].halves_frames);
    SDL_zeroa(g->slots[s].halves_swapped);
    return s;
}

const pk_image *pk_game_slot_image(pk_game *g, int slot)
{
    pk_slot *s;
    if (slot < 0 || slot >= g->slot_count) {
        return NULL;
    }
    s = &g->slots[slot];
    if (!s->img) {
        /* masks always follow their bitmap directly; ids from 0x1e8 on
         * are the portraits of pokeport.mbm (FUN_100702e8) */
        if (s->id >= PK_PORTRAIT_BASE) {
            s->img = pk_gfx_image(PK_BANK_PORTRAIT, s->id - PK_PORTRAIT_BASE, s->mask_off != 0);
        } else {
            s->img = pk_gfx_image(PK_BANK_MAIN, s->id, s->mask_off != 0);
        }
    }
    return s->img;
}

/* Sheets drawn both ways hold the mirrored copy in the upper half and the
 * plain one in the lower half, the layout the original's drawing assumes.
 * A few unit sheets in the game data keep them the other way round; the
 * frames of the plain copy then cover the upper half. The port picks the
 * half whose pixels the frames cover best. */
static long halves_cover(const pk_image *img, Uint32 frames, bool mirrored)
{
    long n = 0;
    int i, x, y;
    pk_frame f;
    for (i = 0; i < 48 && pk_app_frame(frames, i, &f); i++) {
        int x0;
        if (f.w <= 0 || f.h <= 0 || f.sx < 0 || f.sy < 0 || f.sx + f.w > img->w || f.sy + f.h > img->h / 2) {
            break;
        }
        x0 = mirrored ? img->w - f.sx - f.w : f.sx;
        for (y = f.sy; y < f.sy + f.h; y++) {
            const Uint32 *row = img->px + (size_t)y * (size_t)img->w;
            for (x = x0; x < x0 + f.w; x++) {
                n += (row[x] >> 24) != 0;
            }
        }
    }
    return n;
}

bool pk_game_slot_halves_swapped(pk_game *g, int slot, Uint32 frames)
{
    pk_slot *s;
    int i;
    const pk_image *img = pk_game_slot_image(g, slot);
    if (!img || !frames) {
        return false;
    }
    s = &g->slots[slot];
    /* only unit sheets (0x102..0x1e7) are affected; effect sheets such as
     * 0x60 spread their frames too evenly for the check */
    if (s->id < 0x102 || s->id >= PK_PORTRAIT_BASE) {
        return false;
    }
    for (i = 0; i < 4; i++) {
        if (s->halves_frames[i] == frames) {
            return s->halves_swapped[i] != 0;
        }
    }
    for (i = 0; i < 4 && s->halves_frames[i]; i++) {
    }
    if (i == 4) {
        i = 3;
    }
    {
        long plain = halves_cover(img, frames, false), mirrored = halves_cover(img, frames, true);
        s->halves_frames[i] = frames;
        s->halves_swapped[i] = plain > mirrored + mirrored / 16;
    }
    return s->halves_swapped[i] != 0;
}

void pk_game_release_images(pk_game *g)
{
    int i;
    for (i = 0; i < g->slot_count; i++) {
        if (g->slots[i].id >= PK_PORTRAIT_BASE) {
            pk_gfx_release(PK_BANK_PORTRAIT, g->slots[i].id - PK_PORTRAIT_BASE);
        } else {
            pk_gfx_release(PK_BANK_MAIN, g->slots[i].id);
        }
    }
    g->slot_count = 0;
}

void pk_game_fill(pk_game *g, int x, int y, int w, int h, pk_color c)
{
    (void)g;
    pk_gfx_fill(x, y, w, h, c);
}

void pk_game_text(pk_game *g, const Uint16 *s, int x, int y, pk_color c, pk_align a, int layer)
{
    pk_drawq_text(&g->drawq, s, x, y, c, a, layer, 0);
}

void pk_game_mestext(pk_game *g, int id, int x, int y, pk_color c, pk_align a, int layer, int font)
{
    pk_drawq_text(&g->drawq, pk_data_text(PK_TBL_MESTEXT, id), x, y, c, a, layer, font);
}

void pk_game_apply_language(pk_game *g)
{
    pk_fs_set_language((pk_language)g->language);
    pk_data_unload();
    pk_data_load();
}

void pk_options_load(pk_game *g)
{
    size_t len = 0;
    char *t = (char *)pk_fs_load_pref("options.cfg", &len);
    char *p;
    if (!t) {
        return;
    }
    t = (char *)SDL_realloc(t, len + 1);
    if (!t) {
        return;
    }
    t[len] = '\0';
    if ((p = SDL_strstr(t, "volume=")) != NULL) {
        g->volume = SDL_clamp(SDL_atoi(p + 7), 0, 5);
    }
    if ((p = SDL_strstr(t, "language=")) != NULL) {
        g->language = SDL_clamp(SDL_atoi(p + 9), 0, 4);
    }
    SDL_free(t);
}

void pk_options_save(const pk_game *g)
{
    char buf[128];
    SDL_snprintf(buf, sizeof(buf), "volume=%d\nlanguage=%d\n", g->volume, g->language);
    pk_fs_save_pref("options.cfg", buf, SDL_strlen(buf));
}

bool pk_save_exists(void)
{
    size_t len = 0;
    void *p = pk_fs_load_pref("save.dat", &len);
    SDL_free(p);
    return p != NULL;
}

void pk_save_delete(void)
{
    char path[PK_PATH_MAX];
    SDL_snprintf(path, sizeof(path), "%ssave.dat", pk_fs_pref_dir());
    SDL_RemovePath(path);
}

void pk_object_call(pk_game *g, pk_object *o, pk_object_fn fn, int state, pk_object_fn ret_fn, int ret_state)
{
    if (g->call_ret_fn) {
        if (g->call_depth == 4) { /* drop the oldest */
            SDL_memmove(g->call_stack_fn, g->call_stack_fn + 1, sizeof(g->call_stack_fn[0]) * 3);
            SDL_memmove(g->call_stack_state, g->call_stack_state + 1, sizeof(g->call_stack_state[0]) * 3);
            g->call_depth = 3;
        }
        g->call_stack_fn[g->call_depth] = g->call_ret_fn;
        g->call_stack_state[g->call_depth++] = g->call_ret_state;
    }
    g->call_ret_fn = ret_fn;
    g->call_ret_state = ret_state;
    o->update = fn;
    o->state = state;
}

void pk_object_return(pk_game *g, pk_object *o)
{
    o->update = g->call_ret_fn;
    o->state = g->call_ret_state;
    g->call_ret_fn = NULL;
    g->call_ret_state = 0;
    if (g->call_depth > 0) {
        g->call_ret_fn = g->call_stack_fn[--g->call_depth];
        g->call_ret_state = g->call_stack_state[g->call_depth];
    }
}

void pk_object_kill_others(pk_game *g, pk_object *keep)
{
    int i;
    for (i = 0; i < g->objects->count; i++) {
        if (g->objects->items[i] != keep) {
            g->objects->items[i]->dead = true;
        }
    }
}

/* ------------------------------------------------------------------ */

static int scan_code(void)
{
    static const struct
    {
        pk_key k;
        int sc;
    } map[] = {
        { PK_KEY_SELECT, PK_SC_5 },
        { PK_KEY_5, PK_SC_5 },
        { PK_KEY_UP, PK_SC_UP },
        { PK_KEY_DOWN, PK_SC_DOWN },
        { PK_KEY_LEFT, PK_SC_LEFT },
        { PK_KEY_RIGHT, PK_SC_RIGHT },
        { PK_KEY_LSK, PK_SC_5 },
        { PK_KEY_RSK, PK_SC_BACK },
        { PK_KEY_CLEAR, PK_SC_BACKSPACE },
        { PK_KEY_STAR, PK_SC_STAR },
        { PK_KEY_HASH, PK_SC_HASH },
        { PK_KEY_0, 0x30 },
        { PK_KEY_1, 0x31 },
        { PK_KEY_2, 0x32 },
        { PK_KEY_3, 0x33 },
        { PK_KEY_4, 0x34 },
        { PK_KEY_6, 0x36 },
        { PK_KEY_7, 0x37 },
        { PK_KEY_8, 0x38 },
        { PK_KEY_9, 0x39 },
    };
    size_t i;
    for (i = 0; i < SDL_arraysize(map); i++) {
        if (pk_input_repeat(map[i].k)) {
            return map[i].sc;
        }
    }
    return PK_SC_NONE;
}

/* ---- touch (port) ---- */
bool pk_tap(pk_game *g, int x, int y, int w, int h)
{
    g->tap_targets++;
    if (!g->tap || g->tap_x < x || g->tap_y < y || g->tap_x >= x + w || g->tap_y >= y + h) {
        return false;
    }
    g->tap = 0;
    pk_haptics_play(PK_HAPTIC_TAP); /* port: the tap was taken */
    return true;
}

bool pk_tap_rows(pk_game *g, int x, int y, int w, int h, int rows, int *cursor)
{
    int i;
    bool hit = false;
    for (i = 0; i < rows; i++) {
        if (pk_tap(g, x, y + i * h, w, h)) {
            *cursor = i;
            g->key_code = PK_SC_5;
            hit = true;
        }
    }
    if (rows <= 0) {
        g->tap_targets++;
    }
    return hit;
}

bool pk_tap_any(pk_game *g)
{
    g->tap_targets++;
    if (!g->tap) {
        return false;
    }
    g->tap = 0;
    pk_haptics_play(PK_HAPTIC_TAP);
    return true;
}

bool pk_tap_peek(pk_game *g, int x, int y, int w, int h)
{
    return g->tap && g->tap_x >= x && g->tap_y >= y && g->tap_x < x + w && g->tap_y < y + h;
}

int pk_swipe_y(pk_game *g)
{
    int v = g->swipe_y;
    g->swipe_users_y++;
    g->swipe_y = 0;
    return v;
}

int pk_swipe_x(pk_game *g)
{
    int v = g->swipe_x;
    g->swipe_users_x++;
    g->swipe_x = 0;
    return v;
}

bool pk_swipe_list(pk_game *g, int *top, int *cursor, int count, int visible)
{
    int d = pk_swipe_y(g), max = count - visible, abs_pos, t;
    if (!d || max <= 0) {
        return false;
    }
    abs_pos = *top + *cursor;
    t = *top + d;
    if (t > max) {
        t = max;
    }
    if (t < 0) {
        t = 0;
    }
    if (t == *top) {
        return false;
    }
    *top = t;
    /* the cursor stays on its entry while that is visible */
    abs_pos = abs_pos < t ? t : abs_pos > t + visible - 1 ? t + visible - 1
                                                          : abs_pos;
    *cursor = abs_pos - t;
    pk_audio_play_sfx(2);
    pk_haptics_play(PK_HAPTIC_STEP);
    return true;
}

static void touch_begin(pk_game *g)
{
    int x = 0, y = 0;
    g->tap = pk_input_tap(&x, &y);
    g->tap_x = x;
    g->tap_y = y;
    g->swipe_y = pk_input_scroll_y();
    g->swipe_x = pk_input_scroll_x();
    g->tap_targets_prev = g->tap_targets;
    g->swipe_users_y_prev = g->swipe_users_y;
    g->swipe_users_x_prev = g->swipe_users_x;
    g->tap_targets = g->swipe_users_y = g->swipe_users_x = 0;
    /* a screen without touch targets: a tap selects, scrolling moves the
     * cursor one key per tick */
    if (!g->swipe_users_y_prev) {
        g->swipe_pend_y = SDL_clamp(g->swipe_pend_y + g->swipe_y, -8, 8);
        g->swipe_y = 0;
    } else {
        g->swipe_pend_y = 0;
    }
    if (!g->swipe_users_x_prev) {
        g->swipe_pend_x = SDL_clamp(g->swipe_pend_x + g->swipe_x, -4, 4);
        g->swipe_x = 0;
    } else {
        g->swipe_pend_x = 0;
    }
    if (g->key_code != PK_SC_NONE) {
        return;
    }
    if (g->tap && !g->tap_targets_prev) {
        g->tap = 0;
        g->key_code = PK_SC_5;
        pk_haptics_play(PK_HAPTIC_TAP);
    } else if (g->swipe_pend_y) {
        g->key_code = g->swipe_pend_y > 0 ? PK_SC_DOWN : PK_SC_UP;
        g->swipe_pend_y += g->swipe_pend_y > 0 ? -1 : 1;
        pk_haptics_play(PK_HAPTIC_STEP);
    } else if (g->swipe_pend_x) {
        g->key_code = g->swipe_pend_x > 0 ? PK_SC_RIGHT : PK_SC_LEFT;
        g->swipe_pend_x += g->swipe_pend_x > 0 ? -1 : 1;
        pk_haptics_play(PK_HAPTIC_STEP);
    }
}

/* FUN_10070100: either soft key opens the pause menu in battle and on
 * the kingdom screen; focus loss opens it too (vf_521ac). Port: the
 * right soft key is also Back (Escape, Android back), so on the
 * kingdom screen it opens the menu only from the main menu. */
static bool pause_requested(pk_game *g, bool focus_lost)
{
    bool lsk, rsk;
    if (pk_pause_quitting() || pk_help_active()) {
        return false;
    }
    if (focus_lost) {
        return g->mode >= PK_MODE_BATTLE && g->mode <= PK_MODE_BOOT && g->entry_reason != 0x65;
    }
    if (g->mode != PK_MODE_BATTLE && g->mode != PK_MODE_FIELD) {
        return false;
    }
    lsk = pk_input_pressed(PK_KEY_LSK);
    rsk = pk_input_pressed(PK_KEY_RSK);
    if (!lsk && !rsk) {
        return false;
    }
    if (g->mode == PK_MODE_FIELD && !pk_field_at_top(g)) {
        return false;
    }
    return true;
}

static void dispatch_mode(pk_game *g)
{
    if (g->mode >= 0 && g->mode < PK_MODE_COUNT_MAX && g->mode_handlers[g->mode]) {
        g->mode_handlers[g->mode](g);
    }
}

static void draw_background(pk_game *g)
{
    /* FUN_1006f1a8, common case: layer 0 starts with the background
     * bitmap when bg_visible is set. Mode specific scrolling variants are
     * added with the modes that use them. */
    if (g->render_pass == 0 && g->mode == PK_MODE_FIELD && g->bg_visible == 2) {
        /* mode 1 with bg_visible 2 (the world map, Map > Move): the
         * page's three pictures in turn by frame (game+0x430 & 15:
         * 0..3 first, 8..11 third, else second), scrolled by the
         * camera within the page */
        int k = g->frame_count & 15, id = g->bg_id[k < 4 ? 0 : (k >= 8 && k < 12) ? 2
                                                                                  : 1];
        const pk_image *bg = id != PK_IMAGE_NONE ? pk_gfx_image(PK_BANK_MAIN, id, false) : NULL;
        pk_gfx_fill(0, 0, PK_SCREEN_W, PK_SCREEN_H, 0xFF000000u);
        if (bg) {
            pk_gfx_blit(bg, 0, 0, bg->w, bg->h, 0, -(g->cam_y - (g->map_page / 3) * 0xd0), PK_BLIT_OPAQUE);
        }
        if (g->entry_reason != 4) {
            pk_gfx_fill(0, 0xa2, PK_SCREEN_W, PK_SCREEN_H - 0xa2, 0xFF000000u);
        }
    } else if (g->render_pass == 0 && g->bg_visible && g->bg_id[0] != PK_IMAGE_NONE) {
        const pk_image *bg = pk_gfx_image(PK_BANK_MAIN, g->bg_id[0], false);
        if (bg) {
            /* the battlefield is twice the screen width and follows the camera */
            int x = 0, y = 0;
            if (g->mode == PK_MODE_BATTLE || g->mode == PK_MODE_KINGDOM) { /* port: the field strip sits under the top display; mode 2 = the results */
                x = -g->cam_x;
                y = PK_BATTLE_FIELD_Y;
                pk_gfx_fill(0, 0, PK_SCREEN_W, PK_SCREEN_H, 0xFF000000u);
            }
            pk_gfx_blit(bg, 0, 0, bg->w, bg->h, x, y, PK_BLIT_OPAQUE);
        }
    }
    if (g->render_pass == 0 && g->overlay_id >= 0) {
        const pk_image *ov = pk_gfx_image(PK_BANK_MAIN, g->overlay_id, false);
        if (ov) {
            pk_gfx_blit(ov, 0, 0, ov->w, ov->h, g->overlay_x, g->overlay_y, PK_BLIT_OPAQUE);
        }
    }
}

/* FUN_10065814: text queue entries on layer 0x20+pass are drawn with the
 * digit sprites of the interface sheet (bitmap 0x17, frame table
 * 0x100ad638): '1'..'9' frames 0x7e..0x86, '0' 0x87, '+' 0xbd, '/' 0xbe.
 * Each glyph advances by its width + 1; right alignment subtracts the
 * total width first. A string that does not start with one of these is
 * not drawn. */
#define TBL_DIGIT_FRAMES 0x100ad638u
#define IMG_UI_SHEET     0x17

static int digit_frame(Uint16 c)
{
    if (c == '/') {
        return 0xbe;
    }
    if (c == '+') {
        return 0xbd;
    }
    if (c < '0' || c > '9') {
        return 0;
    }
    return c == '0' ? 0x87 : 0x7d + (c - '0');
}

static void draw_digits(pk_game *g, const pk_text_cmd *t)
{
    int slot = pk_game_load_image(g, IMG_UI_SHEET, 1);
    const pk_image *img = pk_game_slot_image(g, slot);
    int x = t->x, i;
    pk_frame f;
    if (!img || !digit_frame(t->text[0])) {
        return;
    }
    if (t->align == PK_ALIGN_RIGHT) {
        int w = 0;
        for (i = 0; t->text[i] && digit_frame(t->text[i]); i++) {
            if (pk_app_frame(TBL_DIGIT_FRAMES, digit_frame(t->text[i]), &f)) {
                w += f.w + 1;
            }
        }
        x -= w;
    }
    for (i = 0; t->text[i]; i++) {
        int fr = digit_frame(t->text[i]);
        if (!fr || !pk_app_frame(TBL_DIGIT_FRAMES, fr, &f)) {
            return;
        }
        pk_gfx_blit(img, f.sx, f.sy, f.w, f.h, x + f.ox, t->y + f.oy, 0);
        x += f.w + 1;
    }
}

static void render(pk_game *g)
{
    int i;
    for (g->render_pass = 0; g->render_pass < 2; g->render_pass++) {
        draw_background(g);
        pk_objects_draw(g, g->render_pass);
        pk_drawq_render(&g->drawq, g->render_pass);
        for (i = 0; i < g->drawq.text_count; i++) {
            if (g->drawq.text[i].layer == 0x20 + g->render_pass) {
                draw_digits(g, &g->drawq.text[i]);
            }
        }
    }
    pk_help_render(g);
    /* FUN_100708e0: the curtain covers the left curtain_w columns. */
    if (g->curtain != PK_CURTAIN_NONE) {
        pk_gfx_fill(0, 0, g->curtain_w, PK_SCREEN_H, 0xFF000000u);
    }
}

static void advance_curtain(pk_game *g)
{
    /* FUN_10070944 */
    switch (g->curtain) {
    case PK_CURTAIN_CLOSE:
    case PK_CURTAIN_CLOSE_ALT:
        g->curtain_w += 0x20;
        if (g->curtain_w > 0xcf) {
            pk_game_set_curtain(g, PK_CURTAIN_HELD);
        }
        break;
    case PK_CURTAIN_OPEN:
        g->curtain_w -= 0x20;
        if (g->curtain_w < 1) {
            pk_game_set_curtain(g, PK_CURTAIN_NONE);
        }
        break;
    default:
        break;
    }
}

/* The canonical framebuffer and draw queue must survive extra renders: some
 * scenes draw into a persistent back buffer during their logic update. */
static struct
{
    Uint32 base[PK_SCREEN_W * PK_SCREEN_H];
    Uint32 canonical[PK_SCREEN_W * PK_SCREEN_H];
    pk_drawq queue, previous_queue, saved_queue;
    pk_object_list *list;
    int mode, phase, cam_x, cam_y, bg_visible, bg_id[3], overlay_id;
    int curtain, curtain_w, previous_curtain_w;
    bool have_curtain, have_queue, paused, same_scene, can_redraw;
} smooth;

void pk_game_smooth_begin(pk_game *g)
{
    int i;
    smooth.list = g->objects;
    smooth.mode = g->mode;
    smooth.phase = g->phase;
    smooth.cam_x = g->cam_x;
    smooth.cam_y = g->cam_y;
    smooth.bg_visible = g->bg_visible;
    SDL_memcpy(smooth.bg_id, g->bg_id, sizeof(smooth.bg_id));
    smooth.overlay_id = g->overlay_id;
    smooth.paused = pk_pause_active();
    for (i = 0; i < g->objects->count; i++) {
        pk_object *o = g->objects->items[i];
        o->smooth_x = o->x;
        o->smooth_y = o->y;
        o->smooth_image = o->image;
        o->smooth_frames = o->frames;
        o->smooth_flags = o->flags;
        o->smooth_valid = true;
    }
}

bool pk_game_smooth_capture(pk_game *g)
{
    SDL_memcpy(smooth.base, pk_gfx_framebuffer(), sizeof(smooth.base));
    smooth.have_queue = smooth.have_curtain;
    smooth.previous_queue = smooth.queue;
    smooth.queue = g->drawq;
    smooth.same_scene = smooth.list == g->objects && smooth.mode == g->mode && smooth.phase == g->phase &&
                        smooth.paused == pk_pause_active() && smooth.bg_visible == g->bg_visible &&
                        smooth.overlay_id == g->overlay_id &&
                        SDL_memcmp(smooth.bg_id, g->bg_id, sizeof(smooth.bg_id)) == 0;
    smooth.can_redraw = !pk_pause_active() && g->bg_visible && g->bg_id[0] != PK_IMAGE_NONE;
    smooth.previous_curtain_w = SDL_clamp(smooth.curtain_w, 0, PK_SCREEN_W);
    smooth.curtain = g->curtain;
    smooth.curtain_w = g->curtain == PK_CURTAIN_NONE ? 0 : SDL_clamp(g->curtain_w, 0, PK_SCREEN_W);
    if (!smooth.have_curtain || !smooth.same_scene) {
        smooth.previous_curtain_w = smooth.curtain_w;
    }
    smooth.have_curtain = true;
    return smooth.can_redraw && smooth.same_scene;
}

static int smooth_lerp(int from, int to, Uint64 elapsed_ns)
{
    const Uint64 duration = (Uint64)PK_TICKS_PER_FRAME * PK_TIMER_INTERVAL_US * 1000ull;
    Sint64 delta = (Sint64)to - from;
    if (elapsed_ns >= duration || delta > 64 || delta < -64) {
        return to;
    }
    return from + (int)(delta * (Sint64)elapsed_ns / (Sint64)duration);
}

static bool smooth_text_equal(const Uint16 *a, const Uint16 *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

static void smooth_queue_positions(pk_drawq *q, Uint64 elapsed_ns)
{
    int i;
    for (i = 0; i < q->text_count && i < smooth.previous_queue.text_count; i++) {
        pk_text_cmd *t = &q->text[i];
        const pk_text_cmd *old = &smooth.previous_queue.text[i];
        if (t->align == old->align && t->layer == old->layer && t->font == old->font &&
            smooth_text_equal(t->text, old->text)) {
            t->x = smooth_lerp(old->x, t->x, elapsed_ns);
            t->y = smooth_lerp(old->y, t->y, elapsed_ns);
        }
    }
    for (i = 0; i < q->sprite_count && i < smooth.previous_queue.sprite_count; i++) {
        pk_sprite_cmd *s = &q->sprite[i];
        const pk_sprite_cmd *old = &smooth.previous_queue.sprite[i];
        if (s->img == old->img && s->layer == old->layer && s->flags == old->flags &&
            SDL_memcmp(&s->src, &old->src, sizeof(s->src)) == 0) {
            s->x = smooth_lerp(old->x, s->x, elapsed_ns);
            s->y = smooth_lerp(old->y, s->y, elapsed_ns);
        }
    }
}

void pk_game_smooth_render(pk_game *g, Uint32 *out, Uint64 elapsed_ns)
{
    int i, cam_x = g->cam_x, cam_y = g->cam_y;
    int curtain = g->curtain, curtain_w = g->curtain_w, render_pass = g->render_pass;
    bool same_scene = smooth.same_scene && smooth.list == g->objects && smooth.mode == g->mode &&
                      smooth.paused == pk_pause_active();
    Uint32 *fb = pk_gfx_framebuffer();

    if (!smooth.can_redraw) {
        SDL_memcpy(out, fb, sizeof(smooth.base));
        return;
    }
    SDL_memcpy(smooth.canonical, fb, sizeof(smooth.canonical));
    SDL_memcpy(fb, smooth.base, sizeof(smooth.base));
    smooth.saved_queue = g->drawq;
    g->drawq = smooth.queue;
    if (same_scene) {
        if (smooth.have_queue) {
            smooth_queue_positions(&g->drawq, elapsed_ns);
        }
        g->cam_x = smooth_lerp(smooth.cam_x, cam_x, elapsed_ns);
        g->cam_y = smooth_lerp(smooth.cam_y, cam_y, elapsed_ns);
        for (i = 0; i < g->objects->count; i++) {
            pk_object *o = g->objects->items[i];
            if (o->smooth_valid && o->image == o->smooth_image && o->frames == o->smooth_frames &&
                ((o->flags ^ o->smooth_flags) & ~((Uint32)PK_OBJ_ANIMATING)) == 0) {
                o->smooth_current_x = o->x;
                o->smooth_current_y = o->y;
                o->x = smooth_lerp(o->smooth_x, o->x, elapsed_ns);
                o->y = smooth_lerp(o->smooth_y, o->y, elapsed_ns);
            } else {
                o->smooth_valid = false;
            }
        }
    }
    g->curtain = smooth.curtain;
    g->curtain_w = smooth_lerp(smooth.previous_curtain_w, smooth.curtain_w, elapsed_ns);
    if (g->curtain_w > 0 && g->curtain == PK_CURTAIN_NONE) {
        g->curtain = PK_CURTAIN_OPEN;
    }
    if (pk_pause_active()) {
        pk_pause_render(g);
    } else {
        render(g);
    }
    SDL_memcpy(out, fb, sizeof(smooth.base));
    if (same_scene) {
        for (i = 0; i < g->objects->count; i++) {
            pk_object *o = g->objects->items[i];
            if (o->smooth_valid) {
                o->x = o->smooth_current_x;
                o->y = o->smooth_current_y;
            }
        }
    }
    g->cam_x = cam_x;
    g->cam_y = cam_y;
    g->curtain = curtain;
    g->curtain_w = curtain_w;
    g->render_pass = render_pass;
    g->drawq = smooth.saved_queue;
    SDL_memcpy(fb, smooth.canonical, sizeof(smooth.canonical));
}

bool pk_game_tick(pk_game *g)
{
    bool drawn = false;
    switch (g->tick_phase) {
    case 0:
    {
        int mode = g->mode;
        bool focus_lost;
        pk_input_frame();
        focus_lost = pk_input_focus_lost();
        g->key_code = scan_code();
        touch_begin(g);
        g->frame_count++;
        pk_arena_tick(g);
        if (pk_pause_active()) {
            pk_pause_tick(g);
            g->key_code = PK_SC_NONE;
            g->tick_phase = 1;
            break;
        }
        /* port: not while the on-screen keyboard is up (chat, amounts):
         * some phones report a focus loss when it opens */
        if (pause_requested(g, focus_lost && !pk_input_text_active())) {
            pk_pause_open(g);
            pk_pause_tick(g);
            g->key_code = PK_SC_NONE;
            g->tick_phase = 1;
            break;
        }
        if (pk_pause_quitting()) {
            pk_pause_quit_tick(g);
        }
        pk_help_input(g);
        dispatch_mode(g);
        pk_objects_update(g);
        pk_objects_reap(g);
        if (g->mode != mode) {
            dispatch_mode(g);
            pk_objects_reap(g);
        }
        g->key_code = PK_SC_NONE;
        g->tick_phase = 1;
        break;
    }
    case 1:
        if (pk_pause_active()) {
            pk_pause_render(g);
        } else {
            render(g);
        }
        drawn = true;
        g->tick_phase = 2;
        break;
    default:
        /* FUN_10065c30 clears the text queue after the frame is shown. */
        pk_drawq_reset(&g->drawq);
        advance_curtain(g);
        g->timer++;
        g->tick_phase = 0;
        break;
    }
    return drawn;
}
