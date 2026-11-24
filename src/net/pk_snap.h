/* SPDX-License-Identifier: MIT */
#ifndef PK_SNAP_H
#define PK_SNAP_H

#include <SDL3/SDL_stdinc.h>
#include <stdbool.h>

/* SNAP: the N-Gage Arena lobby/room protocol (snapcomm.dll), one UDP
 * socket to port 9090 of the Arena game server. Wire format as
 * documented by PhantomLink (docs/ARENA.md, "SNAP"):
 *
 *   datagram = message... + trailer BA 47 66 11, at most 896 bytes
 *   message header, 16 bytes, big endian:
 *     u16 flags|length  0x8000 reliable, 0x4000 ack valid, 0x2000 single
 *                       packet, 0x1000 lobby target, 0x0800 more follow,
 *                       0x0400 game settings; bits 0..9 total length
 *     u8  command sequence (echoed by answers)
 *     u8  opcode
 *     u32 user id (from the login answer)
 *     u32 sequence number of reliable messages (from 0 after login)
 *     u32 acknowledged sequence number
 *
 * Reliable messages are resent until acknowledged (17 tries, then the
 * connection counts as lost) and delivered in order.
 *
 * Pocket Kingdom logs in with game class 1, enters lobby and room
 * "Scrimmage" (or "Auction"/"Ranked"), sends its game messages with op
 * 17 (to the room's game server) and receives them as op 16. */

#define PK_SNAP_PORT          9090
#define PK_SNAP_MAX_DGRAM     896
#define PK_SNAP_HEADER        16
#define PK_SNAP_MAX_PAYLOAD   (PK_SNAP_MAX_DGRAM - PK_SNAP_HEADER - 4)
#define PK_SNAP_GAME_CLASS_PK 1

enum
{
    PK_SNAP_F_RELIABLE = 0x8000,
    PK_SNAP_F_ACK = 0x4000,
    PK_SNAP_F_SINGLE = 0x2000,
    PK_SNAP_F_LOBBY = 0x1000,
    PK_SNAP_F_MORE = 0x0800,
    PK_SNAP_F_SETTINGS = 0x0400
};

enum
{
    PK_SNAP_OP_ACK = 0,
    PK_SNAP_OP_LOGOUT = 2,
    PK_SNAP_OP_LOBBY_BY_NAME = 3,
    PK_SNAP_OP_ENTER = 6,
    PK_SNAP_OP_LEAVE = 7,
    PK_SNAP_OP_SEARCH_ROOMS = 11,
    PK_SNAP_OP_LOBBY_LIST = 14,
    PK_SNAP_OP_DATA_ROOM = 15,
    PK_SNAP_OP_DATA_USERS = 16,
    PK_SNAP_OP_DATA_SERVER = 17,
    PK_SNAP_OP_FAILED = 39,
    PK_SNAP_OP_OK = 40,
    PK_SNAP_OP_LOGIN_OK = 41,
    PK_SNAP_OP_LOGIN = 44
};

typedef enum pk_snap_state
{
    PK_SNAP_IDLE,
    PK_SNAP_RESOLVING,
    PK_SNAP_LOGGING_IN,
    PK_SNAP_ONLINE,
    PK_SNAP_FAILED
} pk_snap_state;

typedef struct pk_snap_msg
{
    Uint16 flags; /* without the length bits */
    Uint8 cmd_seq;
    Uint8 op;
    Uint32 user_id;
    Uint32 seq;
    Uint32 ack;
    const Uint8 *payload;
    int len;
} pk_snap_msg;

typedef struct pk_snap pk_snap;
typedef void (*pk_snap_handler)(void *ud, const pk_snap_msg *m);

pk_snap *pk_snap_create(const char *host, Uint16 port, pk_snap_handler h, void *ud);
void pk_snap_destroy(pk_snap *s);

/* Starts the login (op 44) as user in a game class. */
bool pk_snap_login(pk_snap *s, const char *user, Uint32 game_class);
void pk_snap_logout(pk_snap *s);

/* Queues a message. Reliable messages are numbered, resent and ordered. */
bool pk_snap_send(pk_snap *s, Uint8 op, Uint16 flags, const void *payload, int len);

/* Sends op 17 game data to the room's game server. */
bool pk_snap_send_game_data(pk_snap *s, const void *data, int len);

/* Pumps the socket: receive, acknowledge, resend. Call every frame. */
void pk_snap_update(pk_snap *s);

pk_snap_state pk_snap_get_state(const pk_snap *s);
Uint32 pk_snap_user_id(const pk_snap *s);

/* Encoding helpers (exposed for tests). */
int pk_snap_encode(Uint8 *out, int cap, const pk_snap_msg *m);
int pk_snap_decode(const Uint8 *in, int len, pk_snap_msg *out, int max);

#endif
