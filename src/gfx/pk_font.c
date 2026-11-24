/* SPDX-License-Identifier: MIT */
#include "gfx/pk_font.h"
#include "core/pk_fs.h"
#include "core/pk_log.h"

#include <SDL3/SDL.h>

/* Symbian uids of the two faces in the Series 60 font store. */
#define UID_LEGEND 0x10005910u
#define UID_DENSE  0x10005914u

typedef struct font_face
{
    int cell_h, ascent;
    pk_glyph *glyphs; /* sorted by code point */
    int count;
    Uint8 *bits; /* owned when loaded at run time */
    bool owned;
} font_face;

static font_face g_faces[PK_FONT_COUNT];

static void free_face(font_face *f)
{
    if (f->owned) {
        SDL_free(f->bits);
    }
    SDL_free(f->glyphs);
    SDL_zerop(f);
}

void pk_font_quit(void)
{
    int i;
    for (i = 0; i < PK_FONT_COUNT; i++) {
        free_face(&g_faces[i]);
    }
}

/* ---- run-time reader for Symbian font stores (format: docs/FORMATS.md) ---- */

static Uint32 rd32(const Uint8 *p) { return (Uint32)p[0] | ((Uint32)p[1] << 8) | ((Uint32)p[2] << 16) | ((Uint32)p[3] << 24); }
static Uint16 rd16(const Uint8 *p) { return (Uint16)(p[0] | (p[1] << 8)); }

typedef struct reader
{
    const Uint8 *b;
    size_t size;
} reader;

static bool in(const reader *r, size_t off, size_t n) { return off <= r->size && n <= r->size - off; }

static int get_bits(const reader *r, size_t *bit, int n)
{
    int v = 0, i;
    for (i = 0; i < n; i++, (*bit)++) {
        size_t byte = *bit >> 3;
        if (byte >= r->size) {
            return v;
        }
        v |= ((r->b[byte] >> (*bit & 7)) & 1) << i;
    }
    return v;
}

static const Uint32 k_ranges[][2] = {
    { 0x20, 0x7e }, { 0xa0, 0x17f }, { 0x2010, 0x2026 }, { 0x20ac, 0x20ac }, { 0x2116, 0x2116 }, { 0x2122, 0x2122 }
};

static bool decode_face(const reader *r, size_t hdr, font_face *out)
{
    Uint32 mid, mcount, nsec, s, cp;
    size_t o = hdr + 4, bits_cap = 4096, bits_len = 0;
    int cap = 512;

    SDL_zerop(out);
    if (!in(r, hdr, 4 + 7 + 16)) {
        return false;
    }
    out->cell_h = r->b[o + 3];
    out->ascent = r->b[o + 4];
    o += 7;
    mid = rd32(r->b + o + 4);
    mcount = rd32(r->b + o + 8);
    nsec = rd32(r->b + o + 12);
    o += 16;
    if (!in(r, o, (size_t)nsec * 12) || !in(r, (size_t)mid + 4, (size_t)mcount * 5)) {
        return false;
    }
    out->glyphs = (pk_glyph *)SDL_calloc((size_t)cap, sizeof(pk_glyph));
    out->bits = (Uint8 *)SDL_calloc(bits_cap, 1);
    out->owned = true;
    if (!out->glyphs || !out->bits) {
        return false;
    }
    for (s = 0; s < SDL_arraysize(k_ranges); s++) {
        for (cp = k_ranges[s][0]; cp <= k_ranges[s][1]; cp++) {
            Uint32 k;
            for (k = 0; k < nsec; k++) {
                const Uint8 *sec = r->b + o + 12 * k;
                Uint32 first = rd16(sec), last = rd16(sec + 2), offid = rd32(sec + 4), bmid = rd32(sec + 8);
                size_t p, bit;
                Uint32 mi, off;
                const Uint8 *m;
                int asc, h, la, mv, ra, w, bpr, row = 0;
                pk_glyph *g;
                if (cp < first || cp > last) {
                    continue;
                }
                if (!in(r, (size_t)offid + 4 + 2 * (cp - first), 2)) {
                    return false;
                }
                off = rd16(r->b + offid + 4 + 2 * (cp - first));
                if (off == 0x7fff) {
                    break;
                }
                p = (size_t)bmid + 4 + off;
                if (!in(r, p, 2)) {
                    return false;
                }
                mi = r->b[p];
                if (mi & 1) {
                    mi = (Uint32)rd16(r->b + p) >> 1;
                    p += 2;
                } else {
                    mi >>= 1;
                    p += 1;
                }
                if (mi >= mcount) {
                    return false;
                }
                m = r->b + mid + 4 + 5 * mi;
                asc = (Sint8)m[0];
                h = m[1] & 0x7f;
                la = (Sint8)m[2];
                mv = m[3];
                ra = (Sint8)m[4];
                w = SDL_max(mv - la - ra, 0);
                bpr = (w + 7) / 8;
                if (out->count == cap || bits_len + (size_t)(h * bpr) > bits_cap) {
                    return false; /* larger than any Series 60 12 px face */
                }
                g = &out->glyphs[out->count++];
                g->cp = (Uint16)cp;
                g->ascent = (Sint8)asc;
                g->height = (Uint8)h;
                g->left = (Sint8)la;
                g->move = (Uint8)mv;
                g->width = (Uint8)w;
                g->rows = (const Uint8 *)(uintptr_t)bits_len; /* fixed up below */
                bit = p * 8;
                while (row < h) {
                    int kind = get_bits(r, &bit, 1), cnt = get_bits(r, &bit, 4), i, x;
                    if (!cnt) {
                        break;
                    }
                    for (i = 0; i < cnt && row < h; i++, row++) {
                        if (kind == 1 || i == 0) {
                            Uint8 *d = out->bits + bits_len + (size_t)row * (size_t)bpr;
                            for (x = 0; x < w; x++) {
                                if (get_bits(r, &bit, 1)) {
                                    d[x >> 3] |= (Uint8)(0x80 >> (x & 7));
                                }
                            }
                        } else {
                            SDL_memcpy(out->bits + bits_len + (size_t)row * (size_t)bpr,
                                       out->bits + bits_len + (size_t)(row - 1) * (size_t)bpr, (size_t)bpr);
                        }
                    }
                }
                bits_len += (size_t)(h * bpr);
                break;
            }
        }
    }
    {
        int i;
        for (i = 0; i < out->count; i++) {
            out->glyphs[i].rows = out->bits + (uintptr_t)out->glyphs[i].rows;
        }
    }
    return out->count > 0;
}

bool pk_font_load_gdr_mem(const Uint8 *data, size_t size)
{
    reader r;
    Uint32 root, data_off, n, i;
    size_t o;
    font_face faces[PK_FONT_COUNT];
    bool found[PK_FONT_COUNT] = { false, false };

    r.b = data;
    r.size = size;
    if (!data || size < 20 || rd32(data) != 0x10000037u || rd32(data + 4) != 0x10000039u) {
        return false;
    }
    root = rd32(data + 16);
    if (!in(&r, root, 16) || rd32(data + root) != 39) {
        return false;
    }
    data_off = rd32(data + root + 12);
    if (!in(&r, data_off, 4)) {
        return false;
    }
    n = rd32(data + data_off);
    o = data_off + 4;
    SDL_zero(faces);
    for (i = 0; i < n && in(&r, o, 27); i++) {
        Uint32 uid = rd32(data + o), nsec = rd32(data + o + 4 + 7 + 12);
        int which = uid == UID_LEGEND ? PK_FONT_LEGEND : uid == UID_DENSE ? PK_FONT_DENSE
                                                                          : -1;
        if (which >= 0 && !found[which]) {
            found[which] = decode_face(&r, o, &faces[which]);
        }
        o += 4 + 7 + 16 + (size_t)nsec * 12;
    }
    if (!found[0] || !found[1]) {
        free_face(&faces[0]);
        free_face(&faces[1]);
        return false;
    }
    pk_font_quit();
    g_faces[0] = faces[0];
    g_faces[1] = faces[1];
    return true;
}

/* ---- shipped font files: assets/fonts/pk_bold12.png + .fnt ---- */

static const char *const k_asset_names[PK_FONT_COUNT] = { "pk_bold12", "pk_plain12" };

static int fnt_value(const char *line, const char *key)
{
    const char *p = line;
    size_t n = SDL_strlen(key);
    while ((p = SDL_strstr(p, key)) != NULL) {
        if ((p == line || p[-1] == ' ') && p[n] == '=') {
            return SDL_atoi(p + n + 1);
        }
        p += n;
    }
    return 0;
}

/* Loads one face from an atlas PNG (opaque pixel = ink) and its BMFont
 * text file (char lines: x y width height xoffset yoffset xadvance). */
static bool load_atlas(const char *dir, const char *name, font_face *out)
{
    char png[PK_PATH_MAX], fnt[PK_PATH_MAX];
    SDL_Surface *raw, *img;
    char *text, *line;
    size_t len = 0, bits_cap = 0, bits_len = 0;
    int cap = 0;

    SDL_zerop(out);
    SDL_snprintf(png, sizeof(png), "%s/%s.png", dir, name);
    SDL_snprintf(fnt, sizeof(fnt), "%s/%s.fnt", dir, name);
    text = (char *)SDL_LoadFile(fnt, &len);
    if (!text) {
        return false;
    }
    raw = SDL_LoadPNG(png);
    img = raw ? SDL_ConvertSurface(raw, SDL_PIXELFORMAT_RGBA32) : NULL;
    SDL_DestroySurface(raw);
    if (!img) {
        SDL_free(text);
        return false;
    }
    out->owned = true;
    for (line = text; line && *line;) {
        char *eol = SDL_strchr(line, '\n');
        if (eol) {
            *eol = '\0';
        }
        if (SDL_strncmp(line, "common ", 7) == 0) {
            out->cell_h = fnt_value(line, "lineHeight");
            out->ascent = fnt_value(line, "base");
        } else if (SDL_strncmp(line, "char ", 5) == 0) {
            int x0 = fnt_value(line, "x"), y0 = fnt_value(line, "y");
            int w = fnt_value(line, "width"), h = fnt_value(line, "height");
            int bpr = (w + 7) / 8, x, y;
            pk_glyph *g;
            if (w < 0 || h < 0 || w > 64 || h > 64 || x0 < 0 || y0 < 0 || x0 + w > img->w || y0 + h > img->h) {
                break;
            }
            if (out->count == cap) {
                int ncap = cap ? cap * 2 : 256;
                pk_glyph *ng = (pk_glyph *)SDL_realloc(out->glyphs, (size_t)ncap * sizeof(pk_glyph));
                if (!ng) {
                    break;
                }
                out->glyphs = ng;
                cap = ncap;
            }
            if (bits_len + (size_t)(bpr * h) > bits_cap) {
                size_t nb = bits_cap ? bits_cap * 2 : 4096;
                Uint8 *nbits = (Uint8 *)SDL_realloc(out->bits, nb);
                if (!nbits) {
                    break;
                }
                SDL_memset(nbits + bits_cap, 0, nb - bits_cap);
                out->bits = nbits;
                bits_cap = nb;
            }
            g = &out->glyphs[out->count++];
            g->cp = (Uint16)fnt_value(line, "id");
            g->width = (Uint8)w;
            g->height = (Uint8)h;
            g->left = (Sint8)fnt_value(line, "xoffset");
            g->move = (Uint8)fnt_value(line, "xadvance");
            g->rows = (const Uint8 *)(uintptr_t)bits_len;
            for (y = 0; y < h; y++) {
                const Uint8 *src = (const Uint8 *)img->pixels + (size_t)(y0 + y) * (size_t)img->pitch;
                for (x = 0; x < w; x++) {
                    if (src[(x0 + x) * 4 + 3] >= 128) {
                        out->bits[bits_len + (size_t)(y * bpr) + (size_t)(x >> 3)] |= (Uint8)(0x80 >> (x & 7));
                    }
                }
            }
            bits_len += (size_t)(bpr * h);
            /* yoffset is from the top of the line; the engine keeps the ascent */
            g->ascent = (Sint8)(out->ascent - fnt_value(line, "yoffset"));
        }
        line = eol ? eol + 1 : NULL;
    }
    SDL_DestroySurface(img);
    SDL_free(text);
    {
        int i;
        for (i = 0; i < out->count; i++) {
            out->glyphs[i].rows = out->bits + (uintptr_t)out->glyphs[i].rows;
        }
    }
    /* the binary search needs code point order */
    {
        int i, j;
        for (i = 1; i < out->count; i++) {
            pk_glyph t = out->glyphs[i];
            for (j = i - 1; j >= 0 && out->glyphs[j].cp > t.cp; j--) {
                out->glyphs[j + 1] = out->glyphs[j];
            }
            out->glyphs[j + 1] = t;
        }
    }
    if (out->count == 0 || out->cell_h <= 0) {
        free_face(out);
        return false;
    }
    return true;
}

static bool load_assets_from(const char *dir)
{
    font_face faces[PK_FONT_COUNT];
    int i;
    SDL_zero(faces);
    for (i = 0; i < PK_FONT_COUNT; i++) {
        if (!load_atlas(dir, k_asset_names[i], &faces[i])) {
            while (i >= 0) {
                free_face(&faces[i--]);
            }
            return false;
        }
    }
    pk_font_quit();
    for (i = 0; i < PK_FONT_COUNT; i++) {
        g_faces[i] = faces[i];
    }
    PK_INFO("font: using %s", dir);
    return true;
}

bool pk_font_load_assets(const char *dir)
{
    return load_assets_from(dir);
}

bool pk_font_init(void)
{
    size_t size = 0;
    char path[PK_PATH_MAX];
    Uint8 *gdr = NULL;
    const char *base = SDL_GetBasePath();

    /* 1. the font store from the user's phone or SDK, if provided */
    if (pk_fs_resolve("ceurope.gdr", path, sizeof(path))) {
        gdr = (Uint8 *)SDL_LoadFile(path, &size);
    }
    if (gdr) {
        bool ok = pk_font_load_gdr_mem(gdr, size);
        SDL_free(gdr);
        if (ok) {
            PK_INFO("font: using ceurope.gdr from the data folder");
            return true;
        }
        PK_WARN("font: ceurope.gdr could not be read");
    }
    /* 2. the font files shipped with the program */
    if (base) {
        SDL_snprintf(path, sizeof(path), "%sassets/fonts", base);
        if (load_assets_from(path)) {
            return true;
        }
        SDL_snprintf(path, sizeof(path), "%s../assets/fonts", base);
        if (load_assets_from(path)) {
            return true;
        }
    }
    if (load_assets_from("assets/fonts")) {
        return true;
    }
#ifdef SDL_PLATFORM_ANDROID
    if (load_assets_from("fonts")) { /* the APK's assets */
        return true;
    }
#endif
#ifdef PK_SOURCE_ASSETS_DIR
    if (load_assets_from(PK_SOURCE_ASSETS_DIR "/fonts")) {
        return true;
    }
#endif
    PK_ERROR("font: assets/fonts (pk_bold12/pk_plain12 .png + .fnt) not found, and no ceurope.gdr in the data folder");
    return false;
}

int pk_font_height(pk_font_id f) { return g_faces[f % PK_FONT_COUNT].cell_h; }
int pk_font_ascent(pk_font_id f) { return g_faces[f % PK_FONT_COUNT].ascent; }

static const pk_glyph *find(const font_face *f, Uint32 cp)
{
    int lo = 0, hi = f->count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (f->glyphs[mid].cp == cp) {
            return &f->glyphs[mid];
        }
        if (f->glyphs[mid].cp < cp) {
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    return NULL;
}

const pk_glyph *pk_font_glyph(pk_font_id id, Uint32 cp)
{
    static const pk_glyph k_none = { '?', 0, 0, 0, 4, 0, NULL };
    const font_face *f = &g_faces[id % PK_FONT_COUNT];
    const pk_glyph *g = find(f, cp);
    if (!g) {
        g = find(f, '?');
    }
    return g ? g : &k_none;
}

int pk_font_text_width(pk_font_id f, const Uint16 *s, int n)
{
    int w = 0, i;
    for (i = 0; (n < 0 || i < n) && s[i]; i++) {
        w += pk_font_glyph(f, s[i])->move;
    }
    return w;
}
