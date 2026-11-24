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
    r->state = seed ? seed : 0x9E3779B97F4A7C15ull;
}

Uint32 pk_rng_next(pk_rng *r)
{
    /* xorshift64* */
    Uint64 x = r->state;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    r->state = x;
    return (Uint32)((x * 0x2545F4914F6CDD1Dull) >> 32);
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
