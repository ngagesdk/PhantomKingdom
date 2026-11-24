/* SPDX-License-Identifier: MIT */
#ifndef PK_ONLINE_H
#define PK_ONLINE_H
#include <SDL3/SDL_stdinc.h>
#include <stdbool.h>
/* An N-Gage Arena session for Pocket Kingdom against PhantomLink:
 * Community login (pk_arena), SNAP login (game class 1), lobby
 * "Scrimmage" or "Ranked" (op 3 by name, op 6 join) and the room of the
 * same name (op 11 room list, op 6 join); kingdoms only meet inside one
 * room. Game messages go to the room's game server as
 * op 17 and come back as op 16 {u32 count, count x u32 uid, message}.
 * Message formats: PhantomLink docs/ARENA.md "Pocket Kingdom: game
 * server". */
typedef enum pk_online_state
{
    PK_ONLINE_OFF,
    PK_ONLINE_COMMUNITY,
    PK_ONLINE_SNAP,
    PK_ONLINE_LOBBY,
    PK_ONLINE_ROOM,
    PK_ONLINE_READY,
    PK_ONLINE_FAILED
} pk_online_state;

#define PK_ONLINE_MSG_MAX 256

/* Play Now mode: the lobby and room "Scrimmage" (default) or "Ranked". */
void pk_online_set_room(const char *name);
const char *pk_online_room(void);
bool pk_online_start(const char *host, const char *user, const char *password, const char *imei);
void pk_online_update(void);
void pk_online_stop(void);
pk_online_state pk_online_get_state(void);
const char *pk_online_status(void); /* a short line for the connect screen */
const char *pk_online_user(void);   /* the account's spelling */
/* Sends one game message (byte 0 type, byte 1 sequence). */
bool pk_online_send(const Uint8 *msg, int len);
/* Takes the next received game message; returns its length or 0. */
int pk_online_poll(Uint8 *out, int cap);
#endif
