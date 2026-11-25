/* SPDX-License-Identifier: MIT */
#ifndef PK_BATTLE_UNIT_H
#define PK_BATTLE_UNIT_H

/* Battle units, their attack boxes and hit detection (the unit dispatcher
 * 0x10020f3c, the behaviour of every unit class, 0x100122f0, FUN_1000dfcc).
 * Used by pk_battle.c. */

#include "engine/pk_game.h"

#define PK_OBJ_TYPE_ATTACK 0xc0

/* a unit object of `type` (unit type) for record side / screen side and
 * slot; summons pass `slot | summoner << 8` */
pk_object *pk_bu_spawn(pk_game *g, int type, int screen_side, int slot, int x, int y);
bool pk_bu_is_unit(const pk_object *o);
void pk_bu_damage(pk_game *g, pk_object *u, int d);         /* FUN_10008af4 */
void pk_bu_status_set(const pk_object *o, int b);           /* FUN_100081f0 */
void pk_bu_status_clr(const pk_object *o, int b);           /* FUN_1000826c */
void pk_bu_trap_status(pk_game *g, pk_object *u, int trap); /* trap 1 poison, 2 seal */
/* the unit object of a record (FUN_10009a10 for an object's side / slot) */
pk_object *pk_bu_find(pk_game *g, int screen_side, int slot);

/* provided by pk_battle.c */
pk_object *pk_battle_fx(pk_game *g, int state, int a0, int a1, int x, int y);
void pk_battle_panel(pk_game *g, const pk_object *o, int kind);
void pk_battle_template_record(int *r);
void pk_battle_action_weights(int type, int *w);

#endif
