/* SPDX-License-Identifier: MIT */
#ifndef PK_BATTLE_H
#define PK_BATTLE_H

/* Battles (mode 0). The battle record is the original's structure at
 * *(game+0x1a590): two sides of four units. Side 0 of the record is the
 * player's team (copies of owned units, +0x0), side 1 the opponents
 * (+0x228) built from a formation; `player_side` (+0x498) says on which
 * screen side the record's side 0 fights (0 left, 1 right). */

#include "engine/pk_game.h"

typedef struct pk_battle
{
    /* the original structure, field for field (offsets as in the game) */
    int rec[2][6][23];   /* +0x0 / +0x228: unit records (slots 4 / 5: summoned units); f22 = 1 when down */
    int target[2][6];    /* +0x450 / +0x468: targeting per unit */
    int order[2];        /* +0x480 / +0x484 */
    int team;            /* +0x488 */
    int formation;       /* +0x48c */
    int hp_total[2];     /* +0x490 / +0x494 */
    int player_side;     /* +0x498 */
    int time;            /* +0x49c logic frames left */
    int leader[2];       /* +0x4a0 / +0x4a4 */
    int eq[2][6][13];    /* +0x4a8 / +0x5e0: equipment records */
    int r718[73];        /* +0x718: per-unit counters, [side * 6 + slot] at +0x748, +0x7a8, +0x7d8 */
    int status[2][6];    /* +0x83c / +0x854 status bits */
    int weight[2][6][5]; /* +0x86c / +0x8e4: action weights (action.dat) */
    int r95c[2];         /* +0x95c */
    int field;           /* +0x964 battlefield 0..10 */
    int mode;            /* +0x968 */
    int area;            /* +0x96c */
    int spot;            /* +0x970 map spot of an area search */
    int r974;            /* +0x974 */
    int trap;            /* +0x978 */
    int r97c[8];         /* +0x97c (+0x984: four flags) */
    int won;             /* +0x99c */
    int r9a0[24];        /* +0x9a0 .. +0xa00 */

    /* port */
    int started;                  /* game+0x458 */
    int trap_done;                /* port: 0 waiting, 1 over, 2 cards, 3 trap 0 drop, 4 traps 1/2/4/5, 5 trap 6, 6 trap 7 */
    int trap_revealed;            /* game+0x4f8 = 1: the trap cards have turned */
    int special_msg;              /* port: frames the special move's panel stays (game+0x464 != 0xff) */
    int special_rs, special_slot; /* the speaking unit: record side, slot */
    int panel_kind;               /* battlemes.dat group: 0 start, 6 boss start, 2 special move */
    int sealed[2];                /* status 8 on a record side: no special moves */
    int over;                     /* game+0x44c: everyone stops (battle over, or a special move runs) */
    int cam[4];                   /* game+0xa4..0xb0: camera mode (cam[1]) and the point to show (cam[2], cam[3]) */
    int event_kind;               /* port: what started the battle (event kind) */
    int ranked_pvp;
    int opponent_owned[4];
} pk_battle;

extern pk_battle pk_b;

/* FUN_1000c738: partydata.dat formation (area, level) into formation slot */
void pk_battle_load_formation(pk_game *g, int area, int level, int slot);
/* FUN_1000cc40: fills the battle record (team 0xff = none) */
void pk_battle_setup(pk_game *g, int team, int formation, int player_side, int mode);

void pk_mode_battle(pk_game *g); /* mode 0, FUN_10068db0 */
void pk_mode_result(pk_game *g); /* mode 2 after a battle */

void pk_battle_unit_look(int type, Uint32 *frames, Uint32 *idle);
#endif
