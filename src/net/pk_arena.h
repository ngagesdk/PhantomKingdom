/* SPDX-License-Identifier: MIT */
#ifndef PK_ARENA_H
#define PK_ARENA_H

#include <SDL3/SDL_stdinc.h>
#include <stdbool.h>

/* N-Gage Arena Community login (SOAP over HTTP, TCP 80), as answered by
 * PhantomLink (gcs_native.c): POST /services/Community with SOAPAction
 * authenticateUser2 and the fields <username>, <password> and <imei>.
 * The first login with a new name creates the account and binds it to
 * the IMEI; the answer carries authenticateUser2Return true/false and the
 * account's spelling of the user name. PhantomLink accepts a SNAP login
 * from the same address only after this Community login.
 *
 * Point the port at a PhantomLink server running with direct = 1 (it
 * listens on TCP 80, TCP 5222 and UDP 9090). */

typedef enum pk_arena_state
{
    PK_ARENA_IDLE,
    PK_ARENA_CONNECTING,
    PK_ARENA_WAITING,
    PK_ARENA_OK,
    PK_ARENA_REFUSED,
    PK_ARENA_ERROR
} pk_arena_state;

typedef struct pk_arena pk_arena;

pk_arena *pk_arena_login(const char *host, Uint16 port, const char *user, const char *password, const char *imei);
void pk_arena_update(pk_arena *a);
pk_arena_state pk_arena_get_state(const pk_arena *a);
const char *pk_arena_user(const pk_arena *a); /* account spelling after OK */
void pk_arena_destroy(pk_arena *a);

/* Builds the request (exposed for tests). Returns the length or -1. */
int pk_arena_build_request(char *out, int cap, const char *host, const char *user, const char *password,
                           const char *imei);

#endif
