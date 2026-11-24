/* SPDX-License-Identifier: MIT */
#ifndef PK_TYPES_H
#define PK_TYPES_H

#include <SDL3/SDL_stdinc.h>
#include <stddef.h>

/* Native N-Gage screen size. The original draws everything into a
 * 176x208 off-screen bitmap (game+0x61c) and blits it to the window. */
#define PK_SCREEN_W 176
#define PK_SCREEN_H 208

/* The original drives everything from a CPeriodic with a 30 ms interval
 * (game+0x628, started in FUN_1006f27c). Logic, draw and present run in
 * three consecutive ticks (game+0x7c cycles 0,1,2), so game logic and
 * all frame counters advance once every 90 ms. */
#define PK_TIMER_INTERVAL_US 30000
#define PK_TICKS_PER_FRAME   3

#define PK_ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))

typedef Uint32 pk_color; /* 0xAARRGGBB */

#endif
