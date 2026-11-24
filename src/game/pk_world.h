/* SPDX-License-Identifier: MIT */
#ifndef PK_WORLD_H
#define PK_WORLD_H

#include <SDL3/SDL_stdinc.h>
#include <stdbool.h>
#include <stddef.h>

/* The game's persistent state, as stored in save.dat. Array sizes,
 * record strides and the byte layout of every record follow the
 * original loader FUN_10075e80 and writer FUN_100748dc (+ FUN_10075288 ..
 * FUN_10075b14). Field meanings are filled in as the screens using them
 * are reconstructed; until then fields are kept by index (f[n] is the
 * n-th 32-bit word of the original record).
 *
 * File: 0x2958 bytes. 32 byte header, 20 byte player name, the arrays
 * below in this order, two bytes of padding, CRC-32 (IEEE, reflected,
 * init and final xor 0xffffffff) of everything before it, little endian.
 * The phone version additionally encrypts the file with a device key;
 * the port writes it unencrypted (see docs/FORMATS.md). */

#define PK_SAVE_SIZE 0x2958

#define PK_UNITS      50  /* +0x291c, 23 words: the player's units; f0 type, f19 name.dat record */
#define PK_TEAMS      8   /* +0x275c, 14 words */
#define PK_ITEMS      250 /* +0x3b18, 9 words: materials (kind 0..4), 0xff when empty */
#define PK_EQUIPS     100 /* +0x5e40, 13 words: equipment (kind 5..7), 0xff when empty */
#define PK_AREAS      42  /* +0x7810, 37 words: map areas with five unit slots each */
#define PK_EVENTS     22  /* +0x7394, 13 words: f2 = event kind (6, 7 ...) */
#define PK_FORMATIONS 14  /* +0x19ad8, 49 words */

typedef struct pk_world
{
    /* header */
    int unit_count;   /* +0xc8 */
    int item_count;   /* +0xcc */
    int equip_count;  /* +0xd0 */
    int unit_level;   /* +0xd8 root mean square of the units' ranks (FUN_1007b8a0) */
    int h_dc;         /* +0xdc */
    int h_140;        /* +0x140 */
    int current_area; /* +0x424 */
    int h_428;        /* +0x428 */
    int h_43c;        /* +0x43c */
    int h_444, h_4e4, h_4e8, h_4ec, h_450, h_d4, h_4f4, h_448;
    Uint16 player_name[21]; /* +0x4fc, "dangerboy" by default */

    int units[PK_UNITS][23];
    int teams[PK_TEAMS][14];
    int items[PK_ITEMS][9];
    int equip[PK_EQUIPS][13];
    int areas[PK_AREAS][37];
    int events[PK_EVENTS][13];
    int formations[PK_FORMATIONS][49];
    Uint8 b5bc[16];      /* +0x5bc */
    int pairs[42][2];    /* +0x174 / +0x178 */
    Uint8 help_seen[58]; /* +0x2c4: help pop-ups already shown */
} pk_world;

typedef struct pk_game pk_game;

/* FUN_1006c228: the reset of a new game (saved part). */
void pk_world_reset(pk_game *g);

/* Serialises g into the original byte layout (PK_SAVE_SIZE bytes). */
void pk_world_serialize(const pk_game *g, Uint8 *out);
/* Checks the CRC and loads. Returns false for a damaged file. */
bool pk_world_deserialize(pk_game *g, const Uint8 *in, size_t len);

bool pk_world_save(const pk_game *g);
int pk_world_arena_unlocked(void);
bool pk_world_load(pk_game *g);

Uint32 pk_crc32(const Uint8 *p, size_t n);

#endif
