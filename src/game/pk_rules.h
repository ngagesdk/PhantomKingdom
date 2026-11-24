/* SPDX-License-Identifier: MIT */
#ifndef PK_RULES_H
#define PK_RULES_H

/* Game rules shared by the screens: the unit, item and shop data files
 * of the user's installation and the functions of the original that
 * add, remove and sort units, items and equipment. Function comments
 * give the original address.
 *
 * Kinds of things the player owns (world arrays, see pk_world.h):
 *   units[50]   (+0x291c) characters; f0 type (unit.dat record)
 *   items[250]  (+0x3b18) materials, kind 0..4: 0 molds, 1 ore, 2 jewels,
 *               3 others, 4 rank-up emblems; f0 kind, f1 id
 *   equip[100]  (+0x5e40) kind 5 weapons, 6 armour, 7 accessories;
 *               f0 kind, f1 id, f10 owning unit or 0xff
 * itemdata.dat holds the records of all eight kinds (0x40 bytes: 24
 * character name, 16 bytes of values), unit.dat 0xa0 byte records
 * (name, class, ability, 16 bytes of values). */

#include "engine/pk_game.h"

#define PK_UNIT_MAX   50
#define PK_ITEM_MAX   250
#define PK_EQUIP_MAX  100
#define PK_KIND_EQUIP 5 /* kinds >= 5 are equipment */

/* Item preview (game+0x1791c + slot * 0x90): rec mirrors the 9 words an
 * owned item stores. */
typedef struct pk_item_info
{
    Uint16 name[25];
    int rec[9]; /* kind, id, b0, b1, b4, b5, rank, b3, - */
    int price;  /* +0x8c; the shop asks twice this */
} pk_item_info;

/* Equipment preview (game+0x17f4c + slot * 0xa0). */
typedef struct pk_equip_info
{
    Uint16 name[25];
    int rec[13]; /* kind, id, -, -, -, rank, -, atk, def, atk, owner, -, - */
    int price;   /* +0x9c */
} pk_equip_info;

/* Unit template (game+0x1862c + slot * 0x1e0), FUN_1007a614. */
typedef struct pk_unit_info
{
    Uint16 name[25];  /* +0x000 */
    Uint16 race[25];  /* +0x068 class shown as "Race" */
    Uint16 skill[25]; /* +0x118 ability */
    int type;         /* +0x180 */
    int b[15];        /* values in file order */
    int price;        /* +0x1dc */
} pk_unit_info;

#define PK_PREVIEW_SLOTS 11 /* 0..9 lists, 10 = scratch of the add functions */

typedef struct pk_rules
{
    pk_item_info item[PK_PREVIEW_SLOTS];
    pk_equip_info equip[PK_PREVIEW_SLOTS];
    pk_unit_info unit[PK_PREVIEW_SLOTS];
    /* shop stock of the current area: itemshop.dat (+0x1a598) and
     * unitshop.dat (+0x1a59c) */
    int shop_kind[5], shop_id[5], shop_count;
    int unit_shop[5], unit_shop_count;
} pk_rules;

extern pk_rules pk_r;

/* Clears previews (constructor state). */
void pk_rules_init(void);

void pk_item_preview(int kind, int id, int slot);                  /* FUN_100622e8 (kinds 0..7) */
void pk_unit_template(int type, int slot);                         /* FUN_1007a614 */
void pk_item_attr_name(int mode, int value, Uint16 *out, int cap); /* FUN_100627d8 */
void pk_shop_load_items(int area);                                 /* FUN_10063870 */
void pk_shop_load_units(int area);                                 /* FUN_1007af6c */

int pk_item_add(pk_game *g, int kind, int id);   /* FUN_10063038: index or -1 */
void pk_item_remove(pk_game *g, int index);      /* FUN_100633fc */
void pk_equip_remove(pk_game *g, int index);     /* FUN_100634a4 */
int pk_item_find(pk_game *g, int kind, int n);   /* FUN_10062240: n-th of a kind */
int pk_item_count_kind(pk_game *g, int kind);    /* FUN_100621d4 */
void pk_items_sort(pk_game *g);                  /* FUN_10061b20 */
void pk_equip_sort(pk_game *g, bool owned_last); /* FUN_10061b4c */

int pk_unit_add(pk_game *g, int type); /* FUN_1007abf4 */
void pk_unit_remove(pk_game *g, int index);
void pk_unit_set_type(pk_game *g, int index, int type); /* FUN_10057dd4 */ /* FUN_1007ae48 */
void pk_units_sort(pk_game *g, bool assigned_last);                        /* FUN_1007a018 */
void pk_units_level(pk_game *g);                                           /* FUN_1007b8a0 */
int pk_unit_name_pick(pk_game *g, int group);                              /* FUN_1007b018 */

/* Medical (0x10045630): healing costs rank * missing HP * 10 per unit;
 * units of a team on a mission are skipped. */
int pk_heal_cost_unit(pk_game *g, int unit); /* FUN_1007b7cc */
int pk_heal_cost_team(pk_game *g, int team); /* FUN_1007b764 */
int pk_heal_cost_all(pk_game *g);            /* FUN_1007b688 */
void pk_heal_unit(pk_game *g, int unit);     /* FUN_1007b5b4 */
void pk_heal_all(pk_game *g);                /* FUN_1007b4e0 */

void pk_loot_add(pk_game *g, int n); /* FUN_100710d0 */
void pk_loot_sub(pk_game *g, int n); /* FUN_10071100 */

/* Unit name (name.dat) of an owned unit. */
void pk_unit_name(const pk_game *g, int index, Uint16 *out, int cap);

/* FUN_10071c74(game, kind, id): the secretary's help pop-up. Kind 1
 * entries are shown once (help_seen). game/pk_help.c */
void pk_help(pk_game *g, int kind, int id);

#endif
