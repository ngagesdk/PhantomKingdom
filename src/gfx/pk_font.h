/* SPDX-License-Identifier: MIT */
#ifndef PK_FONT_H
#define PK_FONT_H

#include <SDL3/SDL_stdinc.h>
#include <stdbool.h>

/* The two fonts Pocket Kingdom draws text with. The original obtains
 * them from the phone at start-up (FUN_1006b5c4's init): font 0 is
 * CEikonEnv::LegendFont(), font 1 CEikonEnv::DenseFont(); on Series 60
 * 1.x these are the 12 pixel bold and plain Latin faces (cell height 12,
 * ascent 10, proportional).
 *
 * Glyphs come from, in this order:
 *   1. ceurope.gdr in the data folder (the Series 60 font store from the
 *      phone's Z:\System\Fonts or the N-Gage SDK), read at run time;
 *   2. the font files shipped with the program: assets/fonts/pk_bold12 and
 *      pk_plain12, each a PNG atlas plus a BMFont text file (.fnt). */

typedef enum pk_font_id
{
    PK_FONT_LEGEND = 0, /* bold 12 */
    PK_FONT_DENSE = 1,  /* plain 12 */
    PK_FONT_COUNT
} pk_font_id;

typedef struct pk_glyph
{
    Uint16 cp;
    Sint8 ascent; /* rows above the baseline */
    Uint8 height;
    Sint8 left; /* left adjust from the pen position */
    Uint8 move; /* advance */
    Uint8 width;
    const Uint8 *rows; /* height rows, (width+7)/8 bytes each, MSB = left */
} pk_glyph;

bool pk_font_init(void);
void pk_font_quit(void);

int pk_font_height(pk_font_id f);
int pk_font_ascent(pk_font_id f);
/* Never NULL: unknown characters fall back to '?'. */
const pk_glyph *pk_font_glyph(pk_font_id f, Uint32 cp);
int pk_font_text_width(pk_font_id f, const Uint16 *s, int n);

/* Loads pk_bold12/pk_plain12 (.png + .fnt) from a folder. */
bool pk_font_load_assets(const char *dir);

/* Exposed for tests: decodes a Symbian font store from memory. */
bool pk_font_load_gdr_mem(const Uint8 *data, size_t size);

#endif
