/* SPDX-License-Identifier: MIT */
#ifndef PK_PAUSE_H
#define PK_PAUSE_H

#include <stdbool.h>

#ifndef PK_GAME_TYPEDEF
#define PK_GAME_TYPEDEF
typedef struct pk_game pk_game;
#endif

/* The pause menu (FUN_1006e17c): resume, volume, save and quit to the
 * title on the kingdom screen; a paused plate elsewhere. */
bool pk_pause_active(void);
void pk_pause_open(pk_game *g);
void pk_pause_tick(pk_game *g);
void pk_pause_render(pk_game *g);
/* after "quit": the curtain closes, then the title */
bool pk_pause_quitting(void);
void pk_pause_quit_tick(pk_game *g);

#endif
