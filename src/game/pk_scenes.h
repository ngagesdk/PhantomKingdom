/* SPDX-License-Identifier: MIT */
#ifndef PK_SCENES_H
#define PK_SCENES_H

#include "engine/pk_game.h"

/* Registers the mode handlers that are reconstructed so far. */
void pk_scenes_register(pk_game *g);

/* Mode 3 (FUN_1006b5c4) and the title object (0x10078580). */
void pk_mode_boot(pk_game *g);
void pk_title_update(pk_object *o, pk_game *g);

#ifdef PK_ENABLE_ASSET_VIEWER
/* Port-only asset viewer (images, frames, text tables, sounds). */
void pk_mode_viewer(pk_game *g);
#endif

/* New game set-up and the opening story (0x1004597c). */
void pk_newgame_start(pk_game *g);
void pk_opening_update(pk_object *o, pk_game *g);

/* Mode 1: the kingdom screen (FUN_10069334, object 0x1002479c). */
void pk_mode_field(pk_game *g);
void pk_kingdom_update(pk_object *o, pk_game *g);    /* root 0x1002479c, type 0xc2 */
void pk_kingdom_ui_update(pk_object *o, pk_game *g); /* children 0x10025354 */
/* port: the kingdom screen waits in its main menu (the Back key opens
 * the pause menu there instead of going back) */
bool pk_field_at_top(pk_game *g);

/* Shop (0x1005b754) and its lists. */
void pk_shop_update(pk_object *o, pk_game *g);

/* Party > Form (0x100493c4) and the shared lists. */
void pk_form_update(pk_object *o, pk_game *g);
void pk_team_list_update(pk_object *o, pk_game *g); /* 0x1004faec */
void pk_unit_list_update(pk_object *o, pk_game *g); /* 0x1004a4a0 */
void pk_medical_update(pk_object *o, pk_game *g);   /* 0x10045630 */
void pk_defense_update(pk_object *o, pk_game *g);   /* 0x10036b8c */
void pk_team_update_rank(pk_game *g, int team);     /* FUN_10051a18 */

/* List of owned items / equipment of kind g->dlg_type (0x10039b34). */
void pk_item_list_update(pk_object *o, pk_game *g);

/* Map (0x1003db80) and mission events. */
void pk_map_update(pk_object *o, pk_game *g);
void pk_move_update(pk_object *o, pk_game *g);    /* 0x1004017c */
void pk_combine_update(pk_object *o, pk_game *g); /* 0x100329a0 Lab > Combine */
void pk_vault_update(pk_object *o, pk_game *g);   /* 0x1003b9a8 Lab > Vault */
void pk_rankup_update(pk_object *o, pk_game *g);  /* 0x10053a70 Lab > Rank Up */
void pk_upgrade_update(pk_object *o, pk_game *g); /* 0x10045dc8 Lab > Upgrade */
void pk_quest_update(pk_object *o, pk_game *g);   /* 0x100525ac quest spots */
void pk_quest_begin(pk_game *g, int area, int spot, int team);
void pk_attract_update(pk_object *o, pk_game *g);     /* 0x10036e68 attract mode */
void pk_list_update(pk_object *o, pk_game *g);        /* 0x1003d8a0 Party > List */
void pk_attack_list_update(pk_object *o, pk_game *g); /* 0x10043060 */
int pk_event_start(pk_game *g, int kind, int team);   /* FUN_10070c78 */
void pk_event_clear(pk_game *g, int index);           /* FUN_10070e28 */
void pk_events_tick(pk_game *g);                      /* FUN_10070b20 */
int pk_event_finished(pk_game *g);                    /* FUN_10070e6c */
bool pk_event_handle(pk_game *g, int index);          /* 0x10028538 */
void pk_invasion_tick(pk_game *g);                    /* FUN_10071588 */

/* The ending and the staff roll (credits from the options). */
void pk_mode_ending(pk_game *g); /* 0x10038208 + staff roll */

#endif
