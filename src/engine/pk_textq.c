/* SPDX-License-Identifier: MIT */
#include "engine/pk_textq.h"
#include "core/pk_text.h"

#include <SDL3/SDL.h>

void pk_drawq_reset(pk_drawq *q)
{
    q->text_count = 0;
    q->sprite_count = 0;
}

void pk_drawq_text(pk_drawq *q, const Uint16 *s, int x, int y, pk_color c, pk_align a, int layer, int font)
{
    pk_text_cmd *t;
    int i;
    if (q->text_count >= PK_TEXTQ_MAX) {
        static int warned;
        if (!warned++) {
            SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "text queue full");
        }
        return; /* the original silently drops entries past 64 too */
    }
    t = &q->text[q->text_count++];
    for (i = 0; i < PK_TEXTQ_CHARS && s[i]; i++) {
        t->text[i] = s[i];
    }
    t->text[i] = 0;
    t->x = x;
    t->y = y;
    t->color = c;
    t->align = a;
    t->layer = layer;
    t->font = font;
}

void pk_drawq_text_utf8(pk_drawq *q, const char *s, int x, int y, pk_color c, pk_align a, int layer, int font)
{
    Uint16 buf[PK_TEXTQ_CHARS + 1];
    pk_text_from_utf8(s, buf, PK_TEXTQ_CHARS + 1);
    pk_drawq_text(q, buf, x, y, c, a, layer, font);
}

void pk_drawq_sprite(pk_drawq *q, const pk_image *img, const pk_rect *src, int x, int y, int layer, int flags)
{
    pk_sprite_cmd *s;
    if (!img || q->sprite_count >= PK_SPRITEQ_MAX) {
        return;
    }
    s = &q->sprite[q->sprite_count++];
    s->img = img;
    if (src) {
        s->src = *src;
    } else {
        s->src.x = s->src.y = 0;
        s->src.w = img->w;
        s->src.h = img->h;
    }
    s->x = x;
    s->y = y;
    s->layer = layer;
    s->flags = flags;
}

void pk_drawq_render(const pk_drawq *q, int layer)
{
    int i;
    for (i = 0; i < q->sprite_count; i++) {
        const pk_sprite_cmd *s = &q->sprite[i];
        if (s->layer == layer) {
            pk_gfx_blit(s->img, s->src.x, s->src.y, s->src.w, s->src.h, s->x, s->y, s->flags);
        }
    }
    for (i = 0; i < q->text_count; i++) {
        const pk_text_cmd *t = &q->text[i];
        if (t->layer == layer) {
            pk_gfx_text(t->font, t->x, t->y, t->text, t->color, t->align);
        }
    }
}
