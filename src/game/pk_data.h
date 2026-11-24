/* SPDX-License-Identifier: MIT */
#ifndef PK_DATA_H
#define PK_DATA_H

#include "core/pk_text.h"
#include <stdbool.h>

/* Text tables read from the user's data. Record sizes come from the
 * files' layout (see docs/FORMATS.md). */
typedef enum pk_table
{
    PK_TBL_MESTEXT,   /* mestext.dat, 64 bytes: system messages ("Now Loading...") */
    PK_TBL_AREA,      /* area.dat, 48 bytes: area names */
    PK_TBL_MOVE,      /* move.dat, 48 bytes */
    PK_TBL_OPENING,   /* opening.dat, 64 bytes: intro story */
    PK_TBL_ENDING,    /* ending.dat, 64 bytes */
    PK_TBL_STAFF,     /* staff.dat, 64 bytes: credits */
    PK_TBL_BATTLEMES, /* battlemes.dat, 128 bytes */
    PK_TBL_PAUSEMESS, /* pausemess.dat, 256 bytes */
    PK_TBL_QUESTMES,  /* questmes.dat, 320 bytes */
    PK_TBL_COUNT
} pk_table;

bool pk_data_load(void);
void pk_data_unload(void);

const pk_text_table *pk_data_table(pk_table t);
/* Text of record i of a table, or an empty string. */
const Uint16 *pk_data_text(pk_table t, int i);

/* Optional "game.id" from the root of the memory card ("Pocket Kingdom
 * v. 110804"), shown under "Now Loading". Empty when not found. */
const Uint16 *pk_data_version(void);

/* Kingdom names: name.dat, 16 byte header then 24 byte zero-padded
 * records in the phone's 8-bit character set (FUN_10064dc8). */
int pk_data_kingdom_name_count(void);
void pk_data_kingdom_name(int index, Uint16 *out, int cap);

#endif
