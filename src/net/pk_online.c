/* SPDX-License-Identifier: MIT */
#include "net/pk_online.h"
#include "core/pk_log.h"
#include "net/pk_arena.h"
#include "net/pk_snap.h"
#include <SDL3/SDL.h>

#define QUEUE 32

static struct
{
    pk_online_state state;
    pk_arena *arena;
    pk_snap *snap;
    char host[128], user[32];
    Uint32 lobby, room;
    Uint64 t0;
    int step;
    Uint8 q[QUEUE][PK_ONLINE_MSG_MAX];
    int qlen[QUEUE], qhead, qcount;
    char status[64];
} O;

static Uint32 be32(const Uint8 *p) { return ((Uint32)p[0] << 24) | ((Uint32)p[1] << 16) | ((Uint32)p[2] << 8) | p[3]; }
static void put32(Uint8 *p, Uint32 v)
{
    p[0] = (Uint8)(v >> 24);
    p[1] = (Uint8)(v >> 16);
    p[2] = (Uint8)(v >> 8);
    p[3] = (Uint8)v;
}

static void fail(const char *why)
{
    PK_WARN("online: %s", why);
    SDL_strlcpy(O.status, why, sizeof(O.status));
    O.state = PK_ONLINE_FAILED;
}

static void push(const Uint8 *m, int len)
{
    int i;
    if (len <= 0 || O.qcount >= QUEUE) {
        return;
    }
    if (len > PK_ONLINE_MSG_MAX) {
        len = PK_ONLINE_MSG_MAX;
    }
    i = (O.qhead + O.qcount) % QUEUE;
    SDL_memcpy(O.q[i], m, (size_t)len);
    O.qlen[i] = len;
    O.qcount++;
}

static char s_room[17] = "Scrimmage";

void pk_online_set_room(const char *name)
{
    SDL_strlcpy(s_room, name && *name ? name : "Scrimmage", sizeof(s_room));
}

const char *pk_online_room(void) { return s_room; }

bool pk_online_is_ranked_server(bool online, const char *host, const char *room)
{
    return online && host && room && !SDL_strcmp(host, "188.68.43.183") && !SDL_strcasecmp(room, "Ranked");
}

static void on_snap(void *ud, const pk_snap_msg *m)
{
    (void)ud;
    switch (m->op) {
    case PK_SNAP_OP_LOBBY_BY_NAME:
        if (m->len >= 20 && O.state == PK_ONLINE_LOBBY && O.step == 0) {
            Uint8 p[4];
            O.lobby = be32(m->payload + 16);
            put32(p, O.lobby);
            pk_snap_send(O.snap, PK_SNAP_OP_ENTER, PK_SNAP_F_RELIABLE | PK_SNAP_F_LOBBY, p, 4);
            O.step = 1;
        }
        break;
    case PK_SNAP_OP_SEARCH_ROOMS:
        if (m->len >= 12 && O.state == PK_ONLINE_ROOM && O.step == 0) {
            Uint32 n = be32(m->payload + 8), j;
            for (j = 0; j < n && 12 + 36 * (int)(j + 1) <= m->len; j++) {
                const Uint8 *e = m->payload + 12 + 36 * j;
                if (!SDL_strncmp((const char *)e, s_room, 16)) {
                    Uint8 p[4];
                    O.room = be32(e + 32);
                    put32(p, O.room);
                    pk_snap_send(O.snap, PK_SNAP_OP_ENTER, PK_SNAP_F_RELIABLE, p, 4);
                    O.step = 1;
                    break;
                }
            }
            if (O.step == 0) {
                fail("the room was not found");
            }
        }
        break;
    case PK_SNAP_OP_OK:
        if (O.state == PK_ONLINE_LOBBY && O.step == 1) {
            Uint8 p[4];
            O.state = PK_ONLINE_ROOM;
            O.step = 0;
            put32(p, O.lobby);
            pk_snap_send(O.snap, PK_SNAP_OP_SEARCH_ROOMS, PK_SNAP_F_RELIABLE | PK_SNAP_F_LOBBY, p, 4);
            SDL_strlcpy(O.status, "Entering the room...", sizeof(O.status));
        } else if (O.state == PK_ONLINE_ROOM && O.step == 1) {
            O.state = PK_ONLINE_READY;
            SDL_strlcpy(O.status, "Connected", sizeof(O.status));
            PK_INFO("online: in the room %s as %s", s_room, O.user);
        }
        break;
    case PK_SNAP_OP_FAILED:
        if (O.state == PK_ONLINE_LOBBY || O.state == PK_ONLINE_ROOM) {
            fail("the server refused the lobby or room");
        }
        break;
    case PK_SNAP_OP_DATA_USERS: /* {u32 count, uids, message} */
        if (m->len >= 4) {
            Uint32 n = be32(m->payload);
            int off = 4 + 4 * (int)n;
            if (n < 64 && off < m->len) {
                push(m->payload + off, m->len - off);
            }
        }
        break;
    default:
        break;
    }
}

bool pk_online_start(const char *host, const char *user, const char *password, const char *imei)
{
    pk_online_stop();
    SDL_zero(O);
    if (!host || !user || !password || !imei) {
        return false;
    }
    SDL_strlcpy(O.host, host, sizeof(O.host));
    O.arena = pk_arena_login(host, 80, user, password, imei);
    if (!O.arena) {
        fail("cannot reach the server");
        return false;
    }
    O.state = PK_ONLINE_COMMUNITY;
    O.t0 = SDL_GetTicks();
    SDL_strlcpy(O.status, "Logging in...", sizeof(O.status));
    return true;
}

void pk_online_update(void)
{
    switch (O.state) {
    case PK_ONLINE_COMMUNITY:
        pk_arena_update(O.arena);
        if (pk_arena_get_state(O.arena) == PK_ARENA_OK) {
            SDL_strlcpy(O.user, pk_arena_user(O.arena), sizeof(O.user));
            O.snap = pk_snap_create(O.host, PK_SNAP_PORT, on_snap, NULL);
            if (!O.snap || !pk_snap_login(O.snap, O.user, PK_SNAP_GAME_CLASS_PK)) {
                fail("cannot open the game connection");
                break;
            }
            O.state = PK_ONLINE_SNAP;
            SDL_strlcpy(O.status, "Connecting to the game server...", sizeof(O.status));
        } else if (pk_arena_get_state(O.arena) == PK_ARENA_REFUSED) {
            fail("wrong user name or password");
        } else if (pk_arena_get_state(O.arena) == PK_ARENA_ERROR || SDL_GetTicks() - O.t0 > 20000) {
            fail("the login server did not answer");
        }
        break;
    case PK_ONLINE_SNAP:
        pk_snap_update(O.snap);
        if (pk_snap_get_state(O.snap) == PK_SNAP_ONLINE) {
            Uint8 p[16];
            SDL_zeroa(p);
            SDL_strlcpy((char *)p, s_room, sizeof(p));
            pk_snap_send(O.snap, PK_SNAP_OP_LOBBY_BY_NAME, PK_SNAP_F_RELIABLE | PK_SNAP_F_LOBBY, p, 16);
            O.state = PK_ONLINE_LOBBY;
            O.step = 0;
            O.t0 = SDL_GetTicks();
            SDL_strlcpy(O.status, "Entering the lobby...", sizeof(O.status));
        } else if (pk_snap_get_state(O.snap) == PK_SNAP_FAILED || SDL_GetTicks() - O.t0 > 40000) {
            fail("the game server refused the login");
        }
        break;
    case PK_ONLINE_LOBBY:
    case PK_ONLINE_ROOM:
        pk_snap_update(O.snap);
        if (pk_snap_get_state(O.snap) == PK_SNAP_FAILED) {
            fail("connection lost");
        } else if (SDL_GetTicks() - O.t0 > 30000) {
            fail("the lobby did not answer");
        }
        break;
    case PK_ONLINE_READY:
        pk_snap_update(O.snap);
        if (pk_snap_get_state(O.snap) == PK_SNAP_FAILED) {
            fail("connection lost");
        }
        break;
    default:
        break;
    }
}

void pk_online_stop(void)
{
    if (O.snap) {
        pk_snap_logout(O.snap);
        pk_snap_update(O.snap);
        pk_snap_destroy(O.snap);
    }
    if (O.arena) {
        pk_arena_destroy(O.arena);
    }
    O.snap = NULL;
    O.arena = NULL;
    O.state = PK_ONLINE_OFF;
}

pk_online_state pk_online_get_state(void) { return O.state; }
const char *pk_online_status(void) { return O.status; }
const char *pk_online_user(void) { return O.user; }
const char *pk_online_host(void) { return O.host; }

bool pk_online_send(const Uint8 *msg, int len)
{
    if (O.state != PK_ONLINE_READY || !O.snap) {
        return false;
    }
    return pk_snap_send_game_data(O.snap, msg, len);
}

int pk_online_poll(Uint8 *out, int cap)
{
    int n;
    if (O.qcount == 0) {
        return 0;
    }
    n = O.qlen[O.qhead];
    if (n > cap) {
        n = cap;
    }
    SDL_memcpy(out, O.q[O.qhead], (size_t)n);
    O.qhead = (O.qhead + 1) % QUEUE;
    O.qcount--;
    return n;
}
