/* SPDX-License-Identifier: MIT */
#ifndef PK_TEXT_H
#define PK_TEXT_H

#include <SDL3/SDL_stdinc.h>
#include <stdbool.h>
#include <stddef.h>

/* Text in the game's .dat files is UTF-16LE in fixed-size records. A
 * string ends at ESC (0x001B) or at the end of the record, '\n' (0x000A)
 * breaks a line. Record sizes seen: 48 (area, move, unit names), 64
 * (mestext, opening, ending, staff), 128 (battlemes), 256 (pausemess),
 * 320 (questmes). Some tables mix a name with binary fields in one
 * record (unit.dat, itemdata.dat); those are parsed in game/pk_data.c. */

#define PK_TEXT_ESC 0x001B
#define PK_TEXT_MAX 512

typedef struct pk_text_table
{
    Uint8 *data;
    size_t size;
    size_t record_bytes;
    int count;
} pk_text_table;

bool pk_text_table_load(pk_text_table *t, const char *name, size_t record_bytes, bool localized);
void pk_text_table_free(pk_text_table *t);

/* Copies record i as UTF-16 code units (without terminator) into out,
 * NUL-terminated. Returns the length. */
int pk_text_get16(const pk_text_table *t, int i, Uint16 *out, int cap);

/* Decodes a UTF-16LE run (stops at ESC, NUL or max units). */
int pk_text_decode(const Uint8 *src, size_t max_units, Uint16 *out, int cap);

/* UTF-16 to UTF-8 for logging and window titles. */
void pk_text_to_utf8(const Uint16 *s, char *out, size_t cap);

/* UTF-8 to UTF-16 for strings the engine creates itself. */
int pk_text_from_utf8(const char *s, Uint16 *out, int cap);

int pk_text_len16(const Uint16 *s);

#endif
