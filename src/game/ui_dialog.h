/* SPDX-License-Identifier: MIT */
#ifndef PK_UI_DIALOG_H
#define PK_UI_DIALOG_H

#include "engine/pk_game.h"

/* Generic selection dialog 0x1002637c; see ui_dialog.c. Call it with
 * pk_object_call(g, root, pk_dialog_update, 0, ret_fn, ret_state) after
 * setting g->dlg_type, g->list_top, g->cursor and g->slide. */
void pk_dialog_update(pk_object *o, pk_game *g);

/* Helpers shared by the list screens. */
pk_object *pk_ui_root(pk_game *g);                /* FUN_100707a4(g, 0xc2, 0, 0) */
void pk_ui_cursor_anim(pk_game *g, pk_object *o); /* FUN_10071f3c */
void pk_ui_setup_sprite(pk_game *g, pk_object *o, int image, Uint32 frames, Uint32 flags);
void pk_ui_start_offset(pk_object *o);             /* x +/- 0xc0 by slide side */
void pk_ui_slide_in(pk_object *o);                 /* x -/+ 0x20 */
bool pk_ui_slide_out(pk_object *o, pk_game *g);    /* true after six steps */
bool pk_ui_root_left(pk_game *g, pk_object_fn fn); /* root no longer runs fn */

#endif
