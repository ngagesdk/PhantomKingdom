/* SPDX-License-Identifier: MIT */
#ifndef PK_RAND_H
#define PK_RAND_H

#include <SDL3/SDL_stdinc.h>
#include <stdbool.h>
#include <stddef.h>

/* Deterministic generator for game logic (battles, drops). The original
 * calls EUSER Math::Rand() with a seed kept in the game object; the exact
 * constants are still to be confirmed, see docs/RE_NOTES.md. */
typedef struct pk_rng
{
    Uint64 state;
} pk_rng;

void pk_rng_seed(pk_rng *r, Uint64 seed);
Uint32 pk_rng_next(pk_rng *r);
int pk_rng_range(pk_rng *r, int n); /* 0..n-1 */

/* Cryptographically strong bytes from the operating system. Returns
 * false only if no OS source was available (a mixed fallback is still
 * written so the caller always gets bytes). */
bool pk_secure_random(void *buf, size_t len);

#endif
