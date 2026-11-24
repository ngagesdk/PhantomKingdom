/* SPDX-License-Identifier: MIT */
#ifndef PK_TEXTQ_H
#define PK_TEXTQ_H

#include "core/pk_types.h"
#include "gfx/pk_gfx.h"

/* Immediate-mode draw queue. Objects queue what they want drawn during
 * their update; the render ticks draw the queue in two layers (0 = under,
 * 1 = over, the original's game+0x94 pass index) and the queue is reset
 * at the start of the next logic frame.
 *
 * Text entries mirror the original text queue: 64 entries of 0x84 bytes
 * at game+0x65c (count at +0xc4), filled by FUN_100654c0/FUN_100653f8 and
 * drawn by FUN_10065a20 with x, baseline y, colour, alignment, layer and
 * font. Entries on layer 0x20+n are numbers drawn with the bitmap digits
 * of the interface sheet in pass n (FUN_10065814, see pk_game.c). */

#define PK_TEXTQ_MAX   64
#define PK_SPRITEQ_MAX 512
#define PK_TEXTQ_CHARS 52

typedef struct pk_text_cmd
{
    Uint16 text[PK_TEXTQ_CHARS + 1];
    int x, y;
    pk_color color;
    pk_align align;
    int layer;
    int font;
} pk_text_cmd;

typedef struct pk_sprite_cmd
{
    const pk_image *img;
    pk_rect src;
    int x, y;
    int layer;
    int flags;
} pk_sprite_cmd;

typedef struct pk_drawq
{
    pk_text_cmd text[PK_TEXTQ_MAX];
    int text_count;
    pk_sprite_cmd sprite[PK_SPRITEQ_MAX];
    int sprite_count;
} pk_drawq;

void pk_drawq_reset(pk_drawq *q);
void pk_drawq_text(pk_drawq *q, const Uint16 *s, int x, int y, pk_color c, pk_align a, int layer, int font);
void pk_drawq_text_utf8(pk_drawq *q, const char *s, int x, int y, pk_color c, pk_align a, int layer, int font);
void pk_drawq_sprite(pk_drawq *q, const pk_image *img, const pk_rect *src, int x, int y, int layer, int flags);
void pk_drawq_render(const pk_drawq *q, int layer);

#endif
