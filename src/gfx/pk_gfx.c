/* SPDX-License-Identifier: MIT */
#include "gfx/pk_gfx.h"
#include "core/pk_log.h"
#include "core/pk_text.h"
#include "gfx/pk_font.h"
#include "gfx/pk_mbm.h"

#include <SDL3/SDL.h>

#define PK_MAX_IMAGES 1024

typedef struct cached
{
    pk_image img;
    bool loaded;
    bool masked;
} cached;

static struct
{
    pk_mbm mbm[PK_BANK_COUNT];
    cached *cache[PK_BANK_COUNT];
    Uint32 fb[PK_SCREEN_W * PK_SCREEN_H];
    pk_rect clip;
} G;

static const char *const k_bank_files[PK_BANK_COUNT] = { "pokekin.mbm", "pokeport.mbm" };

bool pk_gfx_init(void)
{
    int b;
    SDL_zero(G);
    for (b = 0; b < PK_BANK_COUNT; b++) {
        if (!pk_mbm_open(&G.mbm[b], k_bank_files[b])) {
            pk_gfx_quit();
            return false;
        }
        G.cache[b] = (cached *)SDL_calloc((size_t)G.mbm[b].count, sizeof(cached));
        if (!G.cache[b]) {
            pk_gfx_quit();
            return false;
        }
    }
    pk_gfx_set_clip(NULL);
    PK_INFO("gfx: %d main bitmaps, %d portrait bitmaps", G.mbm[0].count, G.mbm[1].count);
    return true;
}

void pk_gfx_quit(void)
{
    int b;
    pk_gfx_release_all();
    for (b = 0; b < PK_BANK_COUNT; b++) {
        SDL_free(G.cache[b]);
        G.cache[b] = NULL;
        pk_mbm_close(&G.mbm[b]);
    }
}

int pk_gfx_bank_count(pk_bank bank)
{
    return bank < PK_BANK_COUNT ? G.mbm[bank].count : 0;
}

const pk_image *pk_gfx_image(pk_bank bank, int id, bool with_mask)
{
    cached *c;
    int w, h, mw, mh, i;
    Uint32 *px, *mask;

    if (bank >= PK_BANK_COUNT || !G.cache[bank] || id < 0 || id >= G.mbm[bank].count) {
        return NULL;
    }
    c = &G.cache[bank][id];
    if (c->loaded && c->masked == with_mask) {
        return &c->img;
    }
    if (c->loaded) {
        SDL_free(c->img.px);
        c->loaded = false;
    }
    if (!pk_mbm_decode(&G.mbm[bank], id, &w, &h, &px)) {
        PK_WARN("gfx: cannot decode bitmap %d of bank %d", id, (int)bank);
        return NULL;
    }
    if (with_mask && pk_mbm_decode(&G.mbm[bank], id + 1, &mw, &mh, &mask)) {
        if (mw == w && mh == h) {
            for (i = 0; i < w * h; i++) {
                if (mask[i] & 0x00FFFFFFu) {
                    px[i] &= 0x00FFFFFFu;
                }
            }
        } else {
            PK_WARN("gfx: mask %d does not match bitmap %d", id + 1, id);
        }
        SDL_free(mask);
        /* Unit sprite sheets (images 0x102 + type * 2) keep their white
         * frame margins opaque in the mask; the original shows no such
         * margins. Port: white areas (a white pixel with a white
         * neighbour) become transparent; lone white highlights stay. */
        if (bank == PK_BANK_MAIN && id >= 0x102 && id < 0x102 + 115 * 2) {
            Uint8 *wm = (Uint8 *)SDL_calloc((size_t)w * (size_t)h, 1);
            int x, y;
            Uint32 key = 0x00FFFFFFu;
            { /* the margin colour: white, or the commonest light opaque
               * colour (some sheets use 0xEEEEFF, 0xEEEEEE, ...) */
                Uint32 cand[8] = { 0 };
                int cnt[8] = { 0 }, n = 0, k, best = -1;
                for (i = 0; i < w * h; i += 3) {
                    Uint32 pc = px[i];
                    int r = (int)((pc >> 16) & 0xff), gg = (int)((pc >> 8) & 0xff), bb = (int)(pc & 0xff);
                    if (!(pc & 0xFF000000u) || r + gg + bb <= 600) {
                        continue;
                    }
                    pc &= 0x00FFFFFFu;
                    for (k = 0; k < n && cand[k] != pc; k++) {
                    }
                    if (k == n) {
                        if (n == 8) {
                            continue;
                        }
                        cand[n] = pc;
                        n++;
                    }
                    cnt[k]++;
                }
                for (k = 0; k < n; k++) {
                    if (best < 0 || cnt[k] > cnt[best]) {
                        best = k;
                    }
                }
                if (best >= 0 && cnt[best] > 100) {
                    key = cand[best];
                }
            }
            if (wm) {
                for (i = 0; i < w * h; i++) {
                    Uint32 pc = px[i] & 0x00FFFFFFu;
                    wm[i] = (px[i] & 0xFF000000u) && (pc == 0x00FFFFFFu || pc == key);
                }
                for (y = 0; y < h; y++) {
                    for (x = 0; x < w; x++) {
                        int k = y * w + x;
                        if (!wm[k]) {
                            continue;
                        }
                        if ((x > 0 && wm[k - 1]) || (x + 1 < w && wm[k + 1]) || (y > 0 && wm[k - w]) ||
                            (y + 1 < h && wm[k + w])) {
                            px[k] &= 0x00FFFFFFu;
                        }
                    }
                }
                SDL_free(wm);
            }
            /* Four sheets (types 0x11, 0x1e, 0x35, 0x54 in the known data)
             * were saved again with a size in twips, and saved mirrored:
             * their frame tables only fit the flipped bitmap. */
            {
                pk_mbm_info inf;
                if (pk_mbm_info_at(&G.mbm[bank], id, &inf) && inf.twips_w != 0) {
                    for (y = 0; y < h; y++) {
                        Uint32 *row = px + (size_t)y * (size_t)w;
                        for (x = 0; x < w / 2; x++) {
                            Uint32 t = row[x];
                            row[x] = row[w - 1 - x];
                            row[w - 1 - x] = t;
                        }
                    }
                }
            }
        }
    }
    c->img.w = w;
    c->img.h = h;
    c->img.px = px;
    c->loaded = true;
    c->masked = with_mask;
    return &c->img;
}

void pk_gfx_release(pk_bank bank, int id)
{
    if (bank < PK_BANK_COUNT && G.cache[bank] && id >= 0 && id < G.mbm[bank].count && G.cache[bank][id].loaded) {
        SDL_free(G.cache[bank][id].img.px);
        SDL_zero(G.cache[bank][id]);
    }
}

void pk_gfx_release_all(void)
{
    int b, i;
    for (b = 0; b < PK_BANK_COUNT; b++) {
        for (i = 0; G.cache[b] && i < G.mbm[b].count; i++) {
            pk_gfx_release((pk_bank)b, i);
        }
    }
}

Uint32 *pk_gfx_framebuffer(void) { return G.fb; }

void pk_gfx_set_clip(const pk_rect *r)
{
    pk_rect s = { 0, 0, PK_SCREEN_W, PK_SCREEN_H };
    if (r) {
        int x0 = SDL_max(r->x, 0), y0 = SDL_max(r->y, 0);
        int x1 = SDL_min(r->x + r->w, PK_SCREEN_W), y1 = SDL_min(r->y + r->h, PK_SCREEN_H);
        s.x = x0;
        s.y = y0;
        s.w = SDL_max(x1 - x0, 0);
        s.h = SDL_max(y1 - y0, 0);
    }
    G.clip = s;
}

void pk_gfx_clear(pk_color c)
{
    int i;
    for (i = 0; i < PK_SCREEN_W * PK_SCREEN_H; i++) {
        G.fb[i] = c | 0xFF000000u;
    }
}

void pk_gfx_fill(int x, int y, int w, int h, pk_color c)
{
    pk_gfx_fill_blend(x, y, w, h, c, 256);
}

static Uint32 mix(Uint32 a, Uint32 b, int t) /* t of 256 towards b */
{
    Uint32 rb = (((a & 0xFF00FF) * (Uint32)(256 - t) + (b & 0xFF00FF) * (Uint32)t) >> 8) & 0xFF00FF;
    Uint32 g = (((a & 0x00FF00) * (Uint32)(256 - t) + (b & 0x00FF00) * (Uint32)t) >> 8) & 0x00FF00;
    return 0xFF000000u | rb | g;
}

void pk_gfx_fill_blend(int x, int y, int w, int h, pk_color c, int alpha)
{
    int x0 = SDL_max(x, G.clip.x), y0 = SDL_max(y, G.clip.y);
    int x1 = SDL_min(x + w, G.clip.x + G.clip.w), y1 = SDL_min(y + h, G.clip.y + G.clip.h);
    int i, j;
    for (j = y0; j < y1; j++) {
        Uint32 *d = G.fb + j * PK_SCREEN_W;
        for (i = x0; i < x1; i++) {
            d[i] = alpha >= 256 ? (c | 0xFF000000u) : mix(d[i], c, alpha);
        }
    }
}

void pk_gfx_blit(const pk_image *img, int sx, int sy, int sw, int sh, int dx, int dy, int flags)
{
    int i, j;
    if (!img || !img->px) {
        return;
    }
    /* clip the source rectangle to the image */
    if (sx < 0) {
        sw += sx;
        dx -= (flags & PK_BLIT_FLIP_X) ? 0 : sx;
        sx = 0;
    }
    if (sy < 0) {
        sh += sy;
        dy -= sy;
        sy = 0;
    }
    if (sx + sw > img->w) {
        sw = img->w - sx;
    }
    if (sy + sh > img->h) {
        sh = img->h - sy;
    }
    for (j = 0; j < sh; j++) {
        int y = dy + j;
        const Uint32 *s;
        Uint32 *d;
        if (y < G.clip.y || y >= G.clip.y + G.clip.h) {
            continue;
        }
        s = img->px + (size_t)(sy + j) * (size_t)img->w + sx;
        d = G.fb + y * PK_SCREEN_W;
        for (i = 0; i < sw; i++) {
            int x = dx + i;
            Uint32 p = (flags & PK_BLIT_FLIP_X) ? s[sw - 1 - i] : s[i];
            if (x < G.clip.x || x >= G.clip.x + G.clip.w) {
                continue;
            }
            if ((flags & PK_BLIT_OPAQUE) || (p >> 24)) {
                d[x] = p | 0xFF000000u;
            }
        }
    }
}

void pk_gfx_blit_or(const pk_image *img, int sx, int sy, int sw, int sh, int dx, int dy)
{
    int i, j;
    if (!img || !img->px || sx < 0 || sy < 0) {
        return;
    }
    if (sx + sw > img->w) {
        sw = img->w - sx;
    }
    if (sy + sh > img->h) {
        sh = img->h - sy;
    }
    for (j = 0; j < sh; j++) {
        int y = dy + j;
        if (y < G.clip.y || y >= G.clip.y + G.clip.h) {
            continue;
        }
        for (i = 0; i < sw; i++) {
            int x = dx + i;
            if (x >= G.clip.x && x < G.clip.x + G.clip.w) {
                G.fb[y * PK_SCREEN_W + x] |= img->px[(size_t)(sy + j) * (size_t)img->w + sx + i] | 0xFF000000u;
            }
        }
    }
}

void pk_gfx_draw(const pk_image *img, int dx, int dy)
{
    if (img) {
        pk_gfx_blit(img, 0, 0, img->w, img->h, dx, dy, 0);
    }
}

void pk_gfx_fade(int level)
{
    int i, t;
    Uint32 to;
    if (level == 0) {
        return;
    }
    to = level > 0 ? 0xFF000000u : 0xFFFFFFFFu;
    t = SDL_min(SDL_abs(level), 16) * 16;
    for (i = 0; i < PK_SCREEN_W * PK_SCREEN_H; i++) {
        G.fb[i] = mix(G.fb[i], to, t);
    }
}

int pk_gfx_line_height(int font)
{
    return pk_font_height((pk_font_id)font);
}

int pk_gfx_text_width(int font, const Uint16 *s)
{
    int w = 0, best = 0;
    for (; *s; s++) {
        if (*s == '\n') {
            best = SDL_max(best, w);
            w = 0;
        } else {
            w += pk_font_glyph((pk_font_id)font, *s)->move;
        }
    }
    return SDL_max(best, w);
}

static void draw_glyph(const pk_glyph *g, int pen, int baseline, const pk_rect *box, pk_color c)
{
    int r, x, bpr = (g->width + 7) / 8;
    for (r = 0; r < g->height; r++) {
        int y = baseline - g->ascent + r;
        if (y < box->y || y >= box->y + box->h || y < G.clip.y || y >= G.clip.y + G.clip.h) {
            continue;
        }
        for (x = 0; x < g->width; x++) {
            int px = pen + g->left + x;
            if ((g->rows[r * bpr + (x >> 3)] & (0x80 >> (x & 7))) && px >= box->x && px < box->x + box->w &&
                px >= G.clip.x && px < G.clip.x + G.clip.w) {
                G.fb[y * PK_SCREEN_W + px] = c | 0xFF000000u;
            }
        }
    }
}

void pk_gfx_text(int font, int x, int y, const Uint16 *s, pk_color c, pk_align align)
{
    pk_font_id f = (pk_font_id)font;
    while (*s) {
        const Uint16 *e = s;
        int n = 0, tw, bw, pen;
        pk_rect box;
        while (*e && *e != '\n') {
            e++;
            n++;
        }
        tw = pk_font_text_width(f, s, n);
        bw = tw + 5;
        box.y = y - pk_font_height(f) + 2;
        box.h = pk_font_height(f);
        if (align == PK_ALIGN_CENTER) {
            box.x = x - bw / 2;
            box.w = 2 * (bw / 2);
            pen = box.x + (box.w - tw) / 2;
        } else if (align == PK_ALIGN_RIGHT) {
            box.x = x - bw;
            box.w = bw;
            pen = x - tw - 2;
        } else {
            box.x = x;
            box.w = bw;
            pen = x + 2;
        }
        for (; s < e; s++) {
            const pk_glyph *g = pk_font_glyph(f, *s);
            draw_glyph(g, pen, y, &box, c);
            pen += g->move;
        }
        if (*s == '\n') {
            s++;
            y += pk_font_height(f);
        }
    }
}

void pk_gfx_text_utf8(int font, int x, int y, const char *s, pk_color c, pk_align align)
{
    Uint16 buf[PK_TEXT_MAX];
    pk_text_from_utf8(s, buf, PK_TEXT_MAX);
    pk_gfx_text(font, x, y, buf, c, align);
}
