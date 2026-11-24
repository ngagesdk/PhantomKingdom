/* SPDX-License-Identifier: MIT */
/* Port-only asset viewer. Not part of the original game: a tool for
 * checking the user's data and for reverse engineering.
 *   left/right   previous/next bitmap (hold to scroll)
 *   up/down      scroll tall bitmaps
 *   5 / select   toggle mask (bitmap + following mask)
 *   1            next bank (pokekin.mbm / pokeport.mbm)
 *   2 / 3        previous/next music track
 *   4 / 6        previous/next sound effect
 *   #            text tables: next table, 8/9 previous/next record
 *   right soft, 7 back to the title
 * Built only with -DPK_ENABLE_ASSET_VIEWER=ON. */
#include "audio/pk_audio.h"
#include "core/pk_text.h"
#include "game/pk_data.h"
#include "game/pk_scenes.h"

#include "gfx/pk_font.h"

#include <SDL3/SDL.h>

#ifdef PK_ENABLE_ASSET_VIEWER

static struct
{
    int bank, id, scroll, bgm, sfx, table, record;
    bool masked, text_mode;
} V;

void pk_mode_viewer(pk_game *g)
{
    const pk_image *img;
    char line[96];

    if (g->phase == 0) {
        pk_objects_clear(g);
        pk_audio_stop_bgm();
        g->bg_visible = 0;
        pk_game_set_curtain(g, PK_CURTAIN_NONE);
        V.bgm = -1;
        pk_game_set_phase(g, 1);
    }
    switch (g->key_code) {
    case PK_SC_RIGHT:
        V.id = (V.id + 1) % SDL_max(pk_gfx_bank_count((pk_bank)V.bank), 1);
        V.scroll = 0;
        break;
    case PK_SC_LEFT:
        V.id = (V.id + pk_gfx_bank_count((pk_bank)V.bank) - 1) % SDL_max(pk_gfx_bank_count((pk_bank)V.bank), 1);
        V.scroll = 0;
        break;
    case PK_SC_DOWN:
        V.scroll += 32;
        break;
    case PK_SC_UP:
        V.scroll = SDL_max(0, V.scroll - 32);
        break;
    case PK_SC_5:
        V.masked = !V.masked;
        break;
    case 0x31:
        V.bank = (V.bank + 1) % PK_BANK_COUNT;
        V.id = 0;
        V.scroll = 0;
        break;
    case 0x32:
        V.bgm = (V.bgm + PK_BGM_COUNT) % (PK_BGM_COUNT + 1) - 1;
        if (V.bgm < 0) {
            pk_audio_stop_bgm();
        } else {
            pk_audio_play_bgm(V.bgm);
        }
        break;
    case 0x33:
        V.bgm = (V.bgm + 1) % PK_BGM_COUNT;
        pk_audio_play_bgm(V.bgm);
        break;
    case 0x34:
        V.sfx = (V.sfx + PK_SFX_COUNT - 1) % PK_SFX_COUNT;
        pk_audio_play_sfx(V.sfx);
        break;
    case 0x36:
        V.sfx = (V.sfx + 1) % PK_SFX_COUNT;
        pk_audio_play_sfx(V.sfx);
        break;
    case PK_SC_HASH:
        if (V.text_mode) {
            V.table = (V.table + 1) % PK_TBL_COUNT;
        }
        V.text_mode = true;
        V.record = 0;
        break;
    case 0x38:
        V.record = SDL_max(0, V.record - 1);
        V.text_mode = true;
        break;
    case 0x39:
        V.record++;
        V.text_mode = true;
        break;
    case PK_SC_BACKSPACE:
        V.text_mode = false;
        break;
    case PK_SC_BACK:
        V.text_mode = false;
        g->skip_intro = true;
        pk_game_set_mode(g, PK_MODE_BOOT);
        return;
    default:
        break;
    }

    pk_gfx_clear(0xFF303030u);
    if (V.text_mode) {
        const pk_text_table *t = pk_data_table((pk_table)V.table);
        if (t && V.record >= t->count) {
            V.record = SDL_max(t->count - 1, 0);
        }
        SDL_snprintf(line, sizeof(line), "table %d  record %d/%d", V.table, V.record, t ? t->count : 0);
        pk_gfx_text_utf8(PK_FONT_DENSE, 0, 11, line, 0xFFFFD040u, PK_ALIGN_LEFT);
        pk_gfx_text(PK_FONT_DENSE, 0, 30, pk_data_text((pk_table)V.table, V.record), 0xFFFFFFFFu, PK_ALIGN_LEFT);
        return;
    }
    img = pk_gfx_image((pk_bank)V.bank, V.id, V.masked);
    if (img) {
        int maxs = SDL_max(img->h - (PK_SCREEN_H - 14), 0);
        V.scroll = SDL_min(V.scroll, maxs);
        /* checkerboard shows transparency */
        if (V.masked) {
            int x, y;
            for (y = 14; y < PK_SCREEN_H; y += 8) {
                for (x = 0; x < PK_SCREEN_W; x += 8) {
                    pk_gfx_fill(x, y, 8, 8, ((x ^ y) & 8) ? 0xFF505050u : 0xFF707070u);
                }
            }
        }
        pk_gfx_blit(img, 0, V.scroll, img->w, img->h - V.scroll, 0, 14, 0);
        SDL_snprintf(line, sizeof(line), "%s #%d %dx%d%s", V.bank ? "port" : "kin", V.id, img->w, img->h, V.masked ? " m" : "");
    } else {
        SDL_snprintf(line, sizeof(line), "%s #%d (none)", V.bank ? "port" : "kin", V.id);
    }
    pk_gfx_fill(0, 0, PK_SCREEN_W, 13, 0xFF000000u);
    pk_gfx_text_utf8(PK_FONT_DENSE, 0, 10, line, 0xFFFFD040u, PK_ALIGN_LEFT);
}

#endif /* PK_ENABLE_ASSET_VIEWER */
