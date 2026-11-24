/* SPDX-License-Identifier: MIT */
#ifndef PK_MBM_H
#define PK_MBM_H

#include "core/pk_types.h"
#include <stdbool.h>

/* Symbian multi-bitmap file (EPOC "MBM", uid 0x10000037/0x10000042).
 * Header: 4 uids/checksum words, then the offset of the trailer. The
 * trailer holds the bitmap count and one file offset per bitmap. Each
 * bitmap starts with a 40 byte SEpocBitmapHeader:
 *   size, header length, width, height, twips w, twips h,
 *   bits per pixel, colour flag, palette entries, compression.
 * Pocket Kingdom uses 12 bpp with 12-bit RLE for images and 1 bpp masks
 * with byte RLE; the other combinations are decoded for completeness. */

typedef struct pk_mbm
{
    Uint8 *data;
    size_t size;
    int count;
    const Uint32 *offsets;
} pk_mbm;

typedef struct pk_mbm_info
{
    int width;
    int height;
    int bpp;
    int color;
    int compression;
    int twips_w, twips_h; /* 0 in most bitmaps */
} pk_mbm_info;

bool pk_mbm_open(pk_mbm *m, const char *name);
bool pk_mbm_open_mem(pk_mbm *m, Uint8 *data, size_t size); /* takes ownership */
void pk_mbm_close(pk_mbm *m);

bool pk_mbm_info_at(const pk_mbm *m, int index, pk_mbm_info *out);

/* Decodes bitmap index into 0xAARRGGBB pixels (alpha 0xFF). For 1 bpp
 * bitmaps a set bit becomes white and a clear bit black. *px is
 * allocated with SDL_malloc. */
bool pk_mbm_decode(const pk_mbm *m, int index, int *w, int *h, Uint32 **px);

#endif
