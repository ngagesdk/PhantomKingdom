/* SPDX-License-Identifier: MIT */
#ifndef PK_ARENA_GAME_H
#define PK_ARENA_GAME_H
#include "engine/pk_game.h"
/* N-Gage Arena inside the game (scene_arena.c). */
#define PK_RIVALS_MAX 5
typedef struct pk_rival
{
    char name[17];
    Uint8 look[4];
    int scouted; /* units seen (135) */
} pk_rival;
extern pk_rival g_rivals[PK_RIVALS_MAX];
extern int g_rival_count, g_rival_area;

void pk_arena_set_account(const char *host, const char *user, const char *password);
bool pk_arena_has_account(void);
void pk_mode_arena(pk_game *g);
void pk_arena_tick(pk_game *g);
void pk_arena_send_online(pk_game *g);
void pk_arena_send_moved(pk_game *g);
void pk_arena_send_search(pk_game *g);
void pk_arena_go_offline(pk_game *g);
/* answers to our own scouting / attacks (135, 137, 10): scene_map.c */
void pk_auction_update(pk_object *o, pk_game *g); /* Shop > Global Auction */
void pk_auction_message(const Uint8 *m, int n);
void pk_chat_update(pk_object *o, pk_game *g); /* Map > Search > Chat */
bool pk_chat_pending(void);                    /* a received letter waits */
void pk_chat_compose(int rival, int team);     /* the base (or -1) and the messenger party */
/* defence: the attacker's message 9 while its battle is pending, or NULL */
const Uint8 *pk_arena_attack_msg(void);
const char *pk_map_rival_name(void); /* the rival kingdom the player attacks */
void pk_arena_attack_done(pk_game *g);
void pk_arena_game_message(pk_game *g, const Uint8 *m, int n);
#endif
