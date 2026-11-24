/* SPDX-License-Identifier: MIT */
#ifndef PK_HELP_H
#define PK_HELP_H
#include "engine/pk_game.h"
/* pk_help() itself is declared in game/pk_rules.h. */
bool pk_help_active(void);
/* New game: area pop-ups are shown again. */
void pk_help_reset_popups(void);
/* game+0x574: the word a pop-up shows in place of "XXX" */
void pk_help_set_word(const Uint16 *w);
/* A detail window with the given UTF-8 lines (port: the original's
 * detail screens 0x1003c224 / 0x1005fc0c drawn as text). */
void pk_help_show_lines(const char *const *lines, int n);
void pk_help_input(pk_game *g);  /* before the screens: turns pages, eats the key */
void pk_help_render(pk_game *g); /* after the objects */
#endif
