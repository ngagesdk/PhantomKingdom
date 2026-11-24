/* SPDX-License-Identifier: MIT */
#ifndef PK_GFX_H
#define PK_GFX_H

#include "core/pk_types.h"
#include <stdbool.h>

/* Images are identified like in the original: by their index in
 * pokekin.mbm. An image with a transparency mask keeps the mask in the
 * next bitmap (index + 1); a mask pixel of 0 (black) is opaque. The
 * portraits live in pokeport.mbm (PK_BANK_PORTRAIT). */

typedef enum pk_bank
{
    PK_BANK_MAIN,     /* pokekin.mbm */
    PK_BANK_PORTRAIT, /* pokeport.mbm */
    PK_BANK_COUNT
} pk_bank;

typedef struct pk_image
{
    int w, h;
    Uint32 *px; /* 0xAARRGGBB, alpha is 0 or 255 */
} pk_image;

typedef struct pk_rect
{
    int x, y, w, h;
} pk_rect;

enum
{
    PK_BLIT_FLIP_X = 1 << 0,
    PK_BLIT_OPAQUE = 1 << 1 /* ignore alpha */
};

typedef enum pk_align
{
    PK_ALIGN_LEFT = 0,
    PK_ALIGN_CENTER = 1,
    PK_ALIGN_RIGHT = 2
} pk_align;

bool pk_gfx_init(void);
void pk_gfx_quit(void);

int pk_gfx_bank_count(pk_bank bank);

/* Loads (or returns the cached) image. with_mask combines bitmap id with
 * its mask id+1. Returns NULL if the id does not exist. */
const pk_image *pk_gfx_image(pk_bank bank, int id, bool with_mask);
void pk_gfx_release(pk_bank bank, int id);
void pk_gfx_release_all(void);

/* Frame buffer (PK_SCREEN_W x PK_SCREEN_H). */
Uint32 *pk_gfx_framebuffer(void);
void pk_gfx_set_clip(const pk_rect *r); /* NULL resets to the screen */
void pk_gfx_clear(pk_color c);
void pk_gfx_fill(int x, int y, int w, int h, pk_color c);
void pk_gfx_fill_blend(int x, int y, int w, int h, pk_color c, int alpha);
void pk_gfx_blit(const pk_image *img, int sx, int sy, int sw, int sh, int dx, int dy, int flags);
void pk_gfx_draw(const pk_image *img, int dx, int dy);
/* EDrawModeOR blit used for glow effects. */
void pk_gfx_blit_or(const pk_image *img, int sx, int sy, int sw, int sh, int dx, int dy);

/* Darkens (level > 0) or whitens (level < 0) the whole frame buffer,
 * |level| from 0 to 16, like the original fade overlay. */
void pk_gfx_fade(int level);

/* Text, drawn like the original's text queue (FUN_10065a20): CFbsBitGc::
 * DrawText into a box of the text width + 5 pixels, baseline offset =
 * font ascent, left margin 2. y is the baseline. font is a pk_font_id. */
int pk_gfx_text_width(int font, const Uint16 *s);
int pk_gfx_line_height(int font);
void pk_gfx_text(int font, int x, int y, const Uint16 *s, pk_color c, pk_align align);
void pk_gfx_text_utf8(int font, int x, int y, const char *s, pk_color c, pk_align align);

#endif
