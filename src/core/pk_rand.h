/* SPDX-License-Identifier: MIT */
#ifndef PK_RAND_H
#define PK_RAND_H

#include <SDL3/SDL_stdinc.h>
#include <stdbool.h>
#include <stddef.h>

/* The game's random generator (game+0x5f0..+0x618): a Mersenne Twister
 * with a state of ten words (N 10, M 5, the usual MT19937 constants and
 * tempering). FUN_10071190 seeds it (init_genrand), FUN_100711fc mixes a
 * key into it (init_by_array, started from the seed instead of 19650218)
 * and FUN_10071310 draws (genrand_int32). */
#define PK_RNG_N 10
typedef struct pk_rng
{
    Uint32 mt[PK_RNG_N];
    int mti;
    Uint32 seed; /* game+0xa0 */
} pk_rng;

void pk_rng_seed(pk_rng *r, Uint64 seed);                                   /* FUN_10071190 */
void pk_rng_seed_key(pk_rng *r, Uint32 seed, const Uint32 *key, int count); /* FUN_100711fc */
Uint32 pk_rng_next(pk_rng *r);                                              /* FUN_10071310 */
int pk_rng_range(pk_rng *r, int n);                                         /* 0..n-1 */

/* Cryptographically strong bytes from the operating system. Returns
 * false only if no OS source was available (a mixed fallback is still
 * written so the caller always gets bytes). */
bool pk_secure_random(void *buf, size_t len);

#endif
