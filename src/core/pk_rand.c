/* SPDX-License-Identifier: MIT */
#include "core/pk_rand.h"

#include <SDL3/SDL.h>
#include <time.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
/* windows.h first: bcrypt.h needs its types (separate blocks keep
 * clang-format from sorting them) */
#include <windows.h>

#include <bcrypt.h>
#if defined(_MSC_VER)
#pragma comment(lib, "bcrypt.lib")
#endif
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
#include <stdlib.h>
#define PK_HAVE_ARC4RANDOM 1
#else
#include <stdio.h>
#endif

void pk_rng_seed(pk_rng *r, Uint64 seed)
{
    int i;
    r->seed = (Uint32)seed;
    r->mt[0] = r->seed;
    for (i = 1; i < PK_RNG_N; i++) {
        r->mt[i] = 1812433253u * (r->mt[i - 1] ^ (r->mt[i - 1] >> 30)) + (Uint32)i;
    }
    r->mti = PK_RNG_N;
}

void pk_rng_seed_key(pk_rng *r, Uint32 seed, const Uint32 *key, int count)
{
    int i = 1, j = 0, k;
    pk_rng_seed(r, seed);
    for (k = count > PK_RNG_N - 1 ? count : PK_RNG_N; k > 0; k--) {
        r->mt[i] = (r->mt[i] ^ ((r->mt[i - 1] ^ (r->mt[i - 1] >> 30)) * 1664525u)) + key[j] + (Uint32)j;
        if (++i >= PK_RNG_N) {
            r->mt[0] = r->mt[PK_RNG_N - 1];
            i = 1;
        }
        if (++j >= count) {
            j = 0;
        }
    }
    for (k = PK_RNG_N - 1; k > 0; k--) {
        r->mt[i] = (r->mt[i] ^ ((r->mt[i - 1] ^ (r->mt[i - 1] >> 30)) * 1566083941u)) - (Uint32)i;
        if (++i >= PK_RNG_N) {
            r->mt[0] = r->mt[PK_RNG_N - 1];
            i = 1;
        }
    }
    r->mt[0] = 0x80000000u;
}

Uint32 pk_rng_next(pk_rng *r)
{
    static const Uint32 mag[2] = { 0, 0x9908b0dfu };
    Uint32 y;
    int kk;
    if (r->mti >= PK_RNG_N) {
        if (r->mti != PK_RNG_N) { /* never seeded */
            pk_rng_seed(r, r->seed);
        }
        for (kk = 0; kk < PK_RNG_N - 5; kk++) {
            y = (r->mt[kk] & 0x80000000u) | (r->mt[kk + 1] & 0x7fffffffu);
            r->mt[kk] = r->mt[kk + 5] ^ (y >> 1) ^ mag[y & 1];
        }
        for (; kk < PK_RNG_N - 1; kk++) {
            y = (r->mt[kk] & 0x80000000u) | (r->mt[kk + 1] & 0x7fffffffu);
            r->mt[kk] = r->mt[kk + 5 - PK_RNG_N] ^ (y >> 1) ^ mag[y & 1];
        }
        y = (r->mt[PK_RNG_N - 1] & 0x80000000u) | (r->mt[0] & 0x7fffffffu);
        r->mt[PK_RNG_N - 1] = r->mt[4] ^ (y >> 1) ^ mag[y & 1];
        r->mti = 0;
    }
    y = r->mt[r->mti++];
    y ^= y >> 11;
    y ^= (y << 7) & 0x9d2c5680u;
    y ^= (y << 15) & 0xefc60000u;
    y ^= y >> 18;
    return y;
}

int pk_rng_range(pk_rng *r, int n)
{
    return n > 0 ? (int)(pk_rng_next(r) % (Uint32)n) : 0;
}

static Uint64 splitmix(Uint64 *s)
{
    Uint64 z = (*s += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

bool pk_secure_random(void *buf, size_t len)
{
    Uint8 *p = (Uint8 *)buf;
    bool ok = false;
    Uint64 s;
    size_t i;

#if defined(_WIN32)
    ok = BCRYPT_SUCCESS(BCryptGenRandom(NULL, p, (ULONG)len, BCRYPT_USE_SYSTEM_PREFERRED_RNG));
#elif defined(PK_HAVE_ARC4RANDOM)
    arc4random_buf(p, len);
    ok = true;
#else
    {
        FILE *f = fopen("/dev/urandom", "rb");
        if (f) {
            ok = fread(p, 1, len, f) == len;
            fclose(f);
        }
    }
#endif
    if (ok) {
        return true;
    }
    /* Last resort: mix whatever entropy is around. Not secure, but unique
     * enough for an identifier; callers log a warning. */
    s = SDL_GetPerformanceCounter() ^ ((Uint64)time(NULL) << 20) ^ (Uint64)(uintptr_t)buf ^ SDL_GetTicksNS();
    for (i = 0; i < len; i++) {
        p[i] ^= (Uint8)splitmix(&s);
    }
    return false;
}
