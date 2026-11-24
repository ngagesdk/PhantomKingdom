/* SPDX-License-Identifier: MIT */
#include "game/pk_scenes.h"
#include "audio/pk_audio.h"
#include "game/pk_arena_game.h"
#include "game/pk_battle.h"
#include "gfx/pk_font.h"

void pk_scenes_register(pk_game *g)
{
    g->mode_handlers[PK_MODE_BATTLE] = pk_mode_battle;
    g->mode_handlers[PK_MODE_FIELD] = pk_mode_field;
    g->mode_handlers[PK_MODE_KINGDOM] = pk_mode_result;
    g->mode_handlers[PK_MODE_BOOT] = pk_mode_boot;
    g->mode_handlers[PK_MODE_ARENA_PENDING] = pk_mode_arena;
    g->mode_handlers[PK_MODE_CREDITS_PENDING] = pk_mode_ending; /* staff roll */
    g->mode_handlers[PK_MODE_ENDING] = pk_mode_ending;
#ifdef PK_ENABLE_ASSET_VIEWER
    g->mode_handlers[PK_MODE_VIEWER] = pk_mode_viewer;
#endif
}
