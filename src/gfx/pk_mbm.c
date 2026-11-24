/* SPDX-License-Identifier: MIT */
#include "gfx/pk_mbm.h"
#include "core/pk_fs.h"
#include "core/pk_log.h"

#include <SDL3/SDL.h>

#define MBM_UID1 0x10000037u
#define MBM_UID2 0x10000042u

static Uint32 rd32(const Uint8 *p)
{
    return (Uint32)p[0] | ((Uint32)p[1] << 8) | ((Uint32)p[2] << 16) | ((Uint32)p[3] << 24);
}

bool pk_mbm_open_mem(pk_mbm *m, Uint8 *data, size_t size)
{
    Uint32 trailer, n;

    SDL_zerop(m);
    if (!data || size < 24 || rd32(data) != MBM_UID1 || rd32(data + 4) != MBM_UID2) {
        SDL_free(data);
        return false;
    }
    trailer = rd32(data + 16);
    if (trailer + 4 > size) {
        SDL_free(data);
        return false;
    }
    n = rd32(data + trailer);
    if (n > 0x10000 || trailer + 4 + (size_t)n * 4 > size) {
        SDL_free(data);
        return false;
    }
    m->data = data;
    m->size = size;
    m->count = (int)n;
    m->offsets = (const Uint32 *)(void *)(data + trailer + 4); /* read via rd32 */
    return true;
}

bool pk_mbm_open(pk_mbm *m, const char *name)
{
    size_t size = 0;
    Uint8 *data = (Uint8 *)pk_fs_load(name, &size);
    if (!pk_mbm_open_mem(m, data, size)) {
        PK_ERROR("mbm: '%s' is missing or not a multi-bitmap file", name);
        return false;
    }
    PK_DEBUG("mbm: '%s' has %d bitmaps", name, m->count);
    return true;
}

void pk_mbm_close(pk_mbm *m)
{
    SDL_free(m->data);
    SDL_zerop(m);
}

static const Uint8 *header_at(const pk_mbm *m, int index)
{
    Uint32 off;
    if (!m->data || index < 0 || index >= m->count) {
        return NULL;
    }
    off = rd32((const Uint8 *)m->offsets + 4 * (size_t)index);
    if ((size_t)off + 40 > m->size) {
        return NULL;
    }
    return m->data + off;
}

bool pk_mbm_info_at(const pk_mbm *m, int index, pk_mbm_info *out)
{
    const Uint8 *h = header_at(m, index);
    if (!h) {
        return false;
    }
    out->width = (int)rd32(h + 8);
    out->height = (int)rd32(h + 12);
    out->twips_w = (int)rd32(h + 16);
    out->twips_h = (int)rd32(h + 20);
    out->bpp = (int)rd32(h + 24);
    out->color = (int)rd32(h + 28);
    out->compression = (int)rd32(h + 36);
    return true;
}

/* Expands the RLE variants into a raw scanline buffer of `need` bytes. */
static bool unpack(int comp, const Uint8 *src, size_t n, Uint8 *dst, size_t need)
{
    size_t i = 0, o = 0;
    switch (comp) {
    case 0:
        if (n < need) {
            SDL_memcpy(dst, src, n);
            SDL_memset(dst + n, 0, need - n);
        } else {
            SDL_memcpy(dst, src, need);
        }
        return true;
    case 1: /* byte RLE: 0x00..0x7f repeat next byte n+1 times, else 256-n literals */
        while (o < need && i < n) {
            Uint8 c = src[i++];
            if (c < 0x80) {
                size_t k = (size_t)c + 1;
                Uint8 v = i < n ? src[i++] : 0;
                while (k-- && o < need) {
                    dst[o++] = v;
                }
            } else {
                size_t k = 256 - (size_t)c;
                while (k-- && o < need && i < n) {
                    dst[o++] = src[i++];
                }
            }
        }
        break;
    case 2: /* 12-bit RLE: 16-bit words, top nibble = repeat-1, low 12 bits = colour */
        while (o + 1 < need && i + 1 < n) {
            Uint16 w = (Uint16)(src[i] | (src[i + 1] << 8));
            int k = (w >> 12) + 1;
            i += 2;
            while (k-- && o + 1 < need) {
                dst[o++] = (Uint8)(w & 0xff);
                dst[o++] = (Uint8)((w >> 8) & 0x0f);
            }
        }
        break;
    case 3: /* 16-bit RLE: like byte RLE but on 2-byte units */
    case 4:
    { /* 24-bit RLE: on 3-byte units */
        size_t u = comp == 3 ? 2 : 3;
        while (o < need && i < n) {
            Uint8 c = src[i++];
            if (c < 0x80) {
                size_t k = (size_t)c + 1;
                const Uint8 *v = src + i;
                i += u;
                if (i > n) {
                    break;
                }
                while (k-- && o + u <= need) {
                    SDL_memcpy(dst + o, v, u);
                    o += u;
                }
            } else {
                size_t k = (256 - (size_t)c) * u;
                while (k-- && o < need && i < n) {
                    dst[o++] = src[i++];
                }
            }
        }
        break;
    }
    default:
        return false;
    }
    if (o < need) {
        SDL_memset(dst + o, 0, need - o);
    }
    return true;
}

static Uint32 expand4(Uint32 v) { return (v << 4) | v; }

bool pk_mbm_decode(const pk_mbm *m, int index, int *w, int *h, Uint32 **px)
{
    const Uint8 *hd = header_at(m, index);
    Uint32 size, hlen;
    int width, height, bpp, comp, x, y;
    size_t stride, need;
    Uint8 *raw;
    Uint32 *out;

    if (!hd) {
        return false;
    }
    size = rd32(hd);
    hlen = rd32(hd + 4);
    width = (int)rd32(hd + 8);
    height = (int)rd32(hd + 12);
    bpp = (int)rd32(hd + 24);
    comp = (int)rd32(hd + 36);
    if (width <= 0 || height <= 0 || width > 4096 || height > 4096 || hlen > size ||
        (size_t)(hd - m->data) + size > m->size) {
        return false;
    }
    /* Scanlines are padded to 32 bits; 12 bpp occupies 16 bits a pixel. */
    switch (bpp) {
    case 1:
        stride = ((size_t)width + 31) / 32 * 4;
        break;
    case 2:
        stride = ((size_t)width + 15) / 16 * 4;
        break;
    case 4:
        stride = ((size_t)width + 7) / 8 * 4;
        break;
    case 8:
        stride = ((size_t)width + 3) / 4 * 4;
        break;
    case 12:
    case 16:
        stride = ((size_t)width * 2 + 3) / 4 * 4;
        break;
    case 24:
        stride = ((size_t)width * 3 + 3) / 4 * 4;
        break;
    case 32:
        stride = (size_t)width * 4;
        break;
    default:
        return false;
    }
    need = stride * (size_t)height;
    raw = (Uint8 *)SDL_malloc(need);
    out = (Uint32 *)SDL_malloc((size_t)width * (size_t)height * 4);
    if (!raw || !out || !unpack(comp, hd + hlen, size - hlen, raw, need)) {
        SDL_free(raw);
        SDL_free(out);
        return false;
    }
    for (y = 0; y < height; y++) {
        const Uint8 *s = raw + stride * (size_t)y;
        Uint32 *d = out + (size_t)width * (size_t)y;
        for (x = 0; x < width; x++) {
            Uint32 r, g, b, v;
            switch (bpp) {
            case 1:
                v = (s[x >> 3] >> (x & 7)) & 1;
                r = g = b = v ? 255 : 0;
                break;
            case 2:
                v = (s[x >> 2] >> ((x & 3) * 2)) & 3;
                r = g = b = v * 85;
                break;
            case 4:
                v = (s[x >> 1] >> ((x & 1) * 4)) & 15;
                r = g = b = expand4(v);
                break;
            case 8:
                r = g = b = s[x];
                break;
            case 12:
                v = (Uint32)(s[2 * x] | (s[2 * x + 1] << 8));
                r = expand4((v >> 8) & 15);
                g = expand4((v >> 4) & 15);
                b = expand4(v & 15);
                break;
            case 16:
                v = (Uint32)(s[2 * x] | (s[2 * x + 1] << 8));
                r = ((v >> 11) & 31) * 255 / 31;
                g = ((v >> 5) & 63) * 255 / 63;
                b = (v & 31) * 255 / 31;
                break;
            case 24:
                b = s[3 * x];
                g = s[3 * x + 1];
                r = s[3 * x + 2];
                break;
            default:
                b = s[4 * x];
                g = s[4 * x + 1];
                r = s[4 * x + 2];
                break;
            }
            d[x] = 0xFF000000u | (r << 16) | (g << 8) | b;
        }
    }
    SDL_free(raw);
    *w = width;
    *h = height;
    *px = out;
    return true;
}
