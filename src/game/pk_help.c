/* SPDX-License-Identifier: MIT */
/* The secretary's pop-ups (FUN_10071c74, window 0x1005ad94).
 *
 * The texts are UTF-16 files of the user's installation in
 * Fabbis/<language>/ (FUN_1005ab64 builds the name):
 *   kind 0  popupa###.txt  pop-ups when an area is entered
 *   kind 1  trigger###.txt explanations, most shown only once
 *           (once flag per id at 0x100aefc0, seen flags at +0x2c4)
 *   kind 2  info###.txt    Map > Info for the area, followed by the
 *           area's ingredient page Ingr###a.txt
 * Nothing is shown when the file is missing, like the original.
 * The port draws a plain window with word-wrapped pages; 5 or Back
 * turns the page, and screens see no key while a pop-up is open. */
#include "game/pk_help.h"
#include "audio/pk_audio.h"
#include "core/pk_app.h"
#include "core/pk_fs.h"
#include "core/pk_log.h"
#include "game/pk_data.h"
#include "game/pk_world.h"
#include "gfx/pk_font.h"
#include "gfx/pk_gfx.h"
#include <SDL3/SDL.h>

#define HELP_FONT PK_FONT_LEGEND /* the editor uses the game's font 0 (game+0x5e8) */
/* the window (0x1005ad94): the letter paper of image 0x4a (frames
 * 0x1009a3a0 / 0x1008e698: 0x10 at 0x58, 0x68 and the edges 8 / 3 / 4 /
 * 0xc), the secretary (image 0x4e + secretary * 2, frames 0x1009a7a8 ..
 * by secretary) at 0x84, 0xa0, the scroll marks (frames 0x100a0a24,
 * animations 0x100a0e4c up / 0x100a0e2c down) at 0x97, 0x1e / 0x97, 0x6c,
 * the kingdom's name (game+0x544) at 0x40, 0xc1 and the text in a box
 * at 0x10, 0x14, 0x95 x 0x7a (a native rich text editor there). */
#define HELP_X 0x10
/* the editor's rectangle (0x10, 0x14) - (0x95, 0x7a): inside the paper,
 * whose white ends at y 0x7b */
#define HELP_W       (0x95 - 0x10)
#define HELP_TOP     0x14
#define HELP_H       (0x7a - 0x14)
#define HELP_LINES   help_lines()
#define HELP_MAXL    96
#define HELP_LINELEN 48

static struct
{
    bool active;
    int line_count, page;
    Uint16 line[HELP_MAXL][HELP_LINELEN];
    bool page_break[HELP_MAXL]; /* a new page starts at this line */
    int delay;
    bool tall; /* detail views: a near full-screen window */
} s_help;

/* game+0x574: the word put in place of "XXX" (a unit's new class, ...) */
static Uint16 s_subst[32];

void pk_help_set_word(const Uint16 *w)
{
    int i = 0;
    while (w && w[i] && w[i] != 0x1b && i < 31) {
        s_subst[i] = w[i];
        i++;
    }
    s_subst[i] = 0;
}

static Uint16 *load_text(const char *name, int *len)
{
    char path[96];
    size_t n = 0;
    Uint8 *p;
    Uint16 *t;
    int i, k = 0;
    SDL_snprintf(path, sizeof(path), "Fabbis/%s/%s", pk_fs_language_dir(pk_fs_language()), name);
    p = (Uint8 *)pk_fs_load(path, &n);
    if (!p && pk_fs_language() != PK_LANG_ENGLISH) {
        SDL_snprintf(path, sizeof(path), "Fabbis/english/%s", name);
        p = (Uint8 *)pk_fs_load(path, &n);
    }
    if (!p) {
        return NULL;
    }
    t = (Uint16 *)SDL_malloc((n / 2 + 1) * sizeof(Uint16));
    if (!t) {
        SDL_free(p);
        return NULL;
    }
    for (i = 0; i + 1 < (int)n; i += 2) {
        Uint16 c = (Uint16)(p[i] | (p[i + 1] << 8));
        if (c == 0xfeff) {
            continue;
        }
        t[k++] = c;
    }
    t[k] = 0;
    SDL_free(p);
    /* "XXX" -> the current word */
    {
        int sl = 0, j, o = 0;
        Uint16 *r;
        while (s_subst[sl]) {
            sl++;
        }
        r = (Uint16 *)SDL_malloc(((size_t)k * (size_t)(sl + 1) + 1) * sizeof(Uint16));
        if (r) {
            for (j = 0; j < k; j++) {
                if (j + 2 < k && t[j] == 'X' && t[j + 1] == 'X' && t[j + 2] == 'X') {
                    SDL_memcpy(r + o, s_subst, (size_t)sl * sizeof(Uint16));
                    o += sl;
                    j += 2;
                } else {
                    r[o++] = t[j];
                }
            }
            r[o] = 0;
            SDL_free(t);
            t = r;
            k = o;
        }
    }
    *len = k;
    return t;
}

/* lines that fit the 0x95 x 0x7a box */
static int help_lines(void)
{
    int lh = pk_gfx_line_height(HELP_FONT);
    return lh > 0 ? HELP_H / lh : 8;
}

static int width_of(const Uint16 *s, int n)
{
    Uint16 tmp[HELP_LINELEN];
    if (n >= HELP_LINELEN) {
        n = HELP_LINELEN - 1;
    }
    SDL_memcpy(tmp, s, (size_t)n * sizeof(Uint16));
    tmp[n] = 0;
    return pk_gfx_text_width(HELP_FONT, tmp);
}

static void push_line(const Uint16 *s, int n, bool brk)
{
    int i;
    if (s_help.line_count >= HELP_MAXL) {
        return;
    }
    if (n >= HELP_LINELEN) {
        n = HELP_LINELEN - 1;
    }
    for (i = 0; i < n; i++) {
        s_help.line[s_help.line_count][i] = s[i] == '\t' ? ' ' : s[i];
    }
    s_help.line[s_help.line_count][n] = 0;
    s_help.page_break[s_help.line_count] = brk;
    s_help.line_count++;
}

/* word wrap into lines of HELP_W pixels; CR/LF end a line */
static void add_text(const Uint16 *t, int len, bool new_page)
{
    int i = 0, first = 1;
    while (i < len) {
        int start = i, last_space = -1, end;
        while (i < len && t[i] != '\r' && t[i] != '\n') {
            if (t[i] == ' ' || t[i] == '\t') {
                last_space = i;
            }
            if (i - start >= HELP_LINELEN - 1 || width_of(t + start, i - start + 1) > HELP_W) {
                break;
            }
            i++;
        }
        if (i < len && t[i] != '\r' && t[i] != '\n' && last_space > start) {
            i = last_space + 1;
        }
        end = i;
        while (end > start && (t[end - 1] == ' ' || t[end - 1] == '\t')) {
            end--;
        }
        if (end > start || (i < len && (t[i] == '\r' || t[i] == '\n'))) {
            push_line(t + start, end - start, new_page && first);
            first = 0;
        }
        if (i < len && t[i] == '\r') {
            i++;
        }
        if (i < len && t[i] == '\n') {
            i++;
        }
    }
}

static bool open_file(const char *name, bool new_page)
{
    int len = 0;
    Uint16 *t = load_text(name, &len);
    if (!t) {
        return false;
    }
    add_text(t, len, new_page);
    SDL_free(t);
    return true;
}

/* kind 0 pop-ups already shown (game+0x174 + area * 8 in the
 * original's save); the port keeps them in popups.dat next to the save */
static Uint8 s_popup_seen[64];
static bool s_popup_loaded;

static void popups_load(void)
{
    size_t n = 0;
    Uint8 *p;
    if (s_popup_loaded) {
        return;
    }
    s_popup_loaded = true;
    p = (Uint8 *)pk_fs_load_pref("popups.dat", &n);
    if (p) {
        SDL_memcpy(s_popup_seen, p, n < sizeof(s_popup_seen) ? n : sizeof(s_popup_seen));
        SDL_free(p);
    }
}

void pk_help_reset_popups(void)
{
    SDL_memset(s_popup_seen, 0, sizeof(s_popup_seen));
    s_popup_loaded = true;
    pk_fs_save_pref("popups.dat", s_popup_seen, sizeof(s_popup_seen));
}

void pk_help(pk_game *g, int kind, int id)
{
    char name[32];
    if (s_help.active || id < 0) {
        return;
    }
    s_help.line_count = 0;
    s_help.page = 0;
    s_help.tall = false;
    if (kind == 0) {
        popups_load();
        if (id >= (int)sizeof(s_popup_seen) || s_popup_seen[id]) {
            return;
        }
        SDL_snprintf(name, sizeof(name), "popupa%03d.txt", id);
        if (!open_file(name, true)) {
            return;
        }
        s_popup_seen[id] = 1;
        pk_fs_save_pref("popups.dat", s_popup_seen, sizeof(s_popup_seen));
    } else if (kind == 1) {
        if (id < (int)sizeof(g->world.help_seen)) {
            if (pk_app_i32(0x100aefc0u + (Uint32)id * 4u) != 0 && g->world.help_seen[id]) {
                return;
            }
        }
        SDL_snprintf(name, sizeof(name), "trigger%03d.txt", id);
        if (!open_file(name, true)) {
            return;
        }
        if (id < (int)sizeof(g->world.help_seen)) {
            g->world.help_seen[id] = 1;
        }
    } else if (kind == 2) {
        SDL_snprintf(name, sizeof(name), "info%03d.txt", id);
        if (!open_file(name, true)) {
            return;
        }
        SDL_snprintf(name, sizeof(name), "ingr%03da.txt", id);
        open_file(name, true);
    } else {
        return;
    }
    while (s_help.line_count > 0 && s_help.line[s_help.line_count - 1][0] == 0) {
        s_help.line_count--;
    }
    if (s_help.line_count == 0) {
        return;
    }
    s_help.active = true;
    s_help.delay = 6;
    PK_DEBUG("help pop-up %d/%d: %d lines", kind, id, s_help.line_count);
}

bool pk_help_active(void) { return s_help.active; }

/* game+0x144: six pairs (0x53, area) of newly opened areas, written by
 * the area-boss win (battle controller state 4, 0x10001b1c). After the
 * results (0x1000b200) a non-empty queue requests trigger 9; opening it
 * (0x1005b0f0 -> FUN_1005aa2c) puts the head's area.dat name in the
 * "XXX" word (FUN_10063e3c) and drops the head; closing a pop-up
 * (0x1005aaa0) shows trigger 9 again while areas are left. */
#define OPENED_MAX 6
static int s_opened[OPENED_MAX];
static int s_opened_count;

void pk_help_queue_area(int area)
{
    if (s_opened_count < OPENED_MAX) {
        s_opened[s_opened_count++] = area;
    }
}

void pk_help_show_opened_area(pk_game *g)
{
    int i;
    if (s_help.active || s_opened_count == 0) {
        return;
    }
    pk_help_set_word(pk_data_text(PK_TBL_AREA, s_opened[0] * 5));
    for (i = 1; i < s_opened_count; i++) {
        s_opened[i - 1] = s_opened[i];
    }
    s_opened_count--;
    pk_help(g, 1, 9);
}

void pk_help_show_lines(const char *const *lines, int n)
{
    int i;
    if (s_help.active) {
        return;
    }
    s_help.line_count = 0;
    s_help.page = 0;
    s_help.tall = true;
    for (i = 0; i < n && s_help.line_count < HELP_MAXL; i++) {
        Uint16 u[HELP_LINELEN];
        const unsigned char *p = (const unsigned char *)lines[i];
        int k = 0;
        while (*p && k < HELP_LINELEN - 1) { /* UTF-8 -> UTF-16 (BMP) */
            Uint32 c = *p++;
            if (c >= 0xe0 && p[0] && p[1]) {
                c = ((c & 0xf) << 12) | ((p[0] & 0x3f) << 6) | (p[1] & 0x3f);
                p += 2;
            } else if (c >= 0xc0 && p[0]) {
                c = ((c & 0x1f) << 6) | (p[0] & 0x3f);
                p++;
            }
            u[k++] = (Uint16)c;
        }
        push_line(u, k, i == 0);
    }
    if (s_help.line_count == 0) {
        return;
    }
    s_help.active = true;
    s_help.delay = 6;
}

/* first line of page p and its length */
static int page_start(int p, int *count)
{
    int i = 0, cur = 0, n = 0;
    while (i < s_help.line_count) {
        int start = i;
        n = 0;
        /* skip blank lines at the top of a page */
        while (i < s_help.line_count && s_help.line[i][0] == 0 && n == 0 && !s_help.page_break[i]) {
            i++;
            start = i;
        }
        while (i < s_help.line_count && n < HELP_LINES && (n == 0 || !s_help.page_break[i])) {
            i++;
            n++;
        }
        if (cur == p) {
            *count = n;
            return start;
        }
        cur++;
    }
    *count = 0;
    return -1;
}

void pk_help_input(pk_game *g)
{
    int n;
    if (!s_help.active) {
        return;
    }
    if (g->tap) { /* touch: a tap turns the page */
        g->tap = 0;
        if (g->key_code == PK_SC_NONE) {
            g->key_code = PK_SC_5;
        }
    }
    g->swipe_y = g->swipe_x = 0;
    g->tap_targets++; /* the pop-up is a target: taps are not keys for the screen */
    if (s_help.delay > 0) {
        s_help.delay--;
    } else if (g->key_code == PK_SC_5 || g->key_code == PK_SC_BACK) {
        pk_audio_play_sfx(0);
        s_help.page++;
        if (page_start(s_help.page, &n) < 0 || n == 0) {
            s_help.active = false;
            pk_help_show_opened_area(g); /* 0x1005aaa0: the next opened area */
        }
        s_help.delay = 3;
    }
    g->key_code = PK_SC_NONE; /* the screens wait */
}

static void help_blit(pk_game *g, int image, Uint32 table, int frame, int x, int y)
{
    int slot = pk_game_load_image(g, image, 1);
    const pk_image *img = pk_game_slot_image(g, slot);
    pk_frame f;
    if (img && pk_app_frame(table, frame, &f)) {
        pk_gfx_blit(img, f.sx, f.sy, f.w, f.h, x + f.ox, y + f.oy, 0);
    }
}

void pk_help_render(pk_game *g)
{
    static const Uint32 k_sec[5] = { 0x1009a7a8u, 0x1009a8c0u, 0x1009a9b0u, 0x1009aaa0u, 0x1009ab90u };
    int n, first, i, more, sec = g->rival_index;
    if (!s_help.active) {
        return;
    }
    first = page_start(s_help.page, &n);
    if (first < 0) {
        return;
    }
    more = page_start(s_help.page + 1, &i) >= 0 && i > 0;
    /* layer 0: the shaded window and, as the pop-up is not the pause
     * menu, the paper (0x10051e6c frame 0x11 at 0x58, 0x4c); layer 1:
     * the frame and the secretary */
    help_blit(g, 0x4a, 0x1009a3a0u, 0x10, 0x58, 0x68);
    help_blit(g, 0x4a, 0x1009a3a0u, 0x11, 0x58, 0x4c);
    help_blit(g, 0x4a, 0x1009a3a0u, 8, 0x58, 0);
    help_blit(g, 0x4a, 0x1009a3a0u, 3, 0, 0x68);
    help_blit(g, 0x4a, 0x1009a3a0u, 4, 0xb0, 0x68);
    help_blit(g, 0x4a, 0x1009a3a0u, 0xc, 0x58, 0xd0);
    if (sec >= 0 && sec < 5) {
        help_blit(g, 0x4e + sec * 2, k_sec[sec], 0, 0x84, 0xa0);
    }
    {
        int ph = (int)((SDL_GetTicks() / 120) % 3);
        if (s_help.page > 0) {
            help_blit(g, 0x4a, 0x100a0a24u, 0x1c + ph, 0x97, 0x1e);
        }
        if (more) {
            help_blit(g, 0x4a, 0x100a0a24u, 0x19 + ph, 0x97, 0x6c);
        }
    }
    /* the text: black on the paper (FUN_1007166c) */
    for (i = 0; i < n; i++) {
        int y = HELP_TOP + pk_font_ascent(HELP_FONT) + i * pk_gfx_line_height(HELP_FONT);
        pk_gfx_text(HELP_FONT, HELP_X, y, s_help.line[first + i], 0, PK_ALIGN_LEFT);
    }
    if (g->kingdom_name[0]) {
        pk_gfx_text(HELP_FONT, 0x41, 0xc2, g->kingdom_name, 0, PK_ALIGN_CENTER);
        pk_gfx_text(HELP_FONT, 0x40, 0xc1, g->kingdom_name, 0xFFFFFFu, PK_ALIGN_CENTER);
    }
}
