/* SPDX-License-Identifier: MIT */
#ifndef PK_APP_H
#define PK_APP_H

#include <SDL3/SDL_stdinc.h>
#include <stdbool.h>
#include <stddef.h>

/* Read-only access to data tables inside the user's own copy of the game
 * executable (6RAH.APP, a Symbian E32 image). Layout data the original
 * keeps in its code section (sprite frame tables, animation tables,
 * menu layouts, ...) is read from there at run time, so none of it has
 * to be copied into this project. Addresses are the original link-time
 * addresses (code base 0x10000000), the same ones used in the reverse
 * engineering notes and in the Ghidra project. */

#define PK_APP_CODE_BASE 0x10000000u

typedef struct pk_app_version
{
    Uint32 uid3;
    Uint32 code_checksum;
    const char *name;
} pk_app_version;

bool pk_app_load(void);
void pk_app_unload(void);
bool pk_app_loaded(void);
const pk_app_version *pk_app_version_info(void);

/* Pointer to len bytes at an original address, or NULL. */
const Uint8 *pk_app_ptr(Uint32 addr, size_t len);
Sint32 pk_app_i32(Uint32 addr);
Uint32 pk_app_u32(Uint32 addr);

/* Sprite frame record, 0x18 bytes: source rectangle and draw offset. */
typedef struct pk_frame
{
    int sx, sy, w, h, ox, oy;
} pk_frame;

bool pk_app_frame(Uint32 table, int index, pk_frame *out);

#endif
