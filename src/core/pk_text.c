/* SPDX-License-Identifier: MIT */
#include "core/pk_text.h"
#include "core/pk_fs.h"
#include "core/pk_log.h"

#include <SDL3/SDL.h>

bool pk_text_table_load(pk_text_table *t, const char *name, size_t record_bytes, bool localized)
{
    SDL_zerop(t);
    t->data = (Uint8 *)(localized ? pk_fs_load_localized(name, &t->size) : pk_fs_load(name, &t->size));
    if (!t->data || !record_bytes) {
        return false;
    }
    t->record_bytes = record_bytes;
    t->count = (int)(t->size / record_bytes);
    return true;
}

void pk_text_table_free(pk_text_table *t)
{
    SDL_free(t->data);
    SDL_zerop(t);
}

int pk_text_decode(const Uint8 *src, size_t max_units, Uint16 *out, int cap)
{
    int n = 0;
    size_t i;
    for (i = 0; i < max_units && n < cap - 1; i++) {
        Uint16 c = (Uint16)(src[2 * i] | (src[2 * i + 1] << 8));
        if (c == PK_TEXT_ESC || c == 0) {
            break;
        }
        out[n++] = c;
    }
    out[n] = 0;
    return n;
}

int pk_text_get16(const pk_text_table *t, int i, Uint16 *out, int cap)
{
    if (!t->data || i < 0 || i >= t->count) {
        if (cap > 0) {
            out[0] = 0;
        }
        return 0;
    }
    return pk_text_decode(t->data + (size_t)i * t->record_bytes, t->record_bytes / 2, out, cap);
}

int pk_text_len16(const Uint16 *s)
{
    int n = 0;
    while (s[n]) {
        n++;
    }
    return n;
}

void pk_text_to_utf8(const Uint16 *s, char *out, size_t cap)
{
    size_t o = 0;
    if (!cap) {
        return;
    }
    for (; *s; s++) {
        Uint32 c = *s;
        if (c < 0x80) {
            if (o + 1 >= cap) {
                break;
            }
            out[o++] = (char)c;
        } else if (c < 0x800) {
            if (o + 2 >= cap) {
                break;
            }
            out[o++] = (char)(0xC0 | (c >> 6));
            out[o++] = (char)(0x80 | (c & 0x3F));
        } else {
            if (o + 3 >= cap) {
                break;
            }
            out[o++] = (char)(0xE0 | (c >> 12));
            out[o++] = (char)(0x80 | ((c >> 6) & 0x3F));
            out[o++] = (char)(0x80 | (c & 0x3F));
        }
    }
    out[o] = '\0';
}

int pk_text_from_utf8(const char *s, Uint16 *out, int cap)
{
    int n = 0;
    size_t len = SDL_strlen(s);
    while (*s && n < cap - 1) {
        Uint32 cp = SDL_StepUTF8(&s, &len);
        out[n++] = (Uint16)(cp > 0xFFFF ? '?' : cp);
    }
    out[n] = 0;
    return n;
}
