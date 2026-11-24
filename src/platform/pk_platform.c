/* SPDX-License-Identifier: MIT */
#include "platform/pk_platform.h"
#include "core/pk_log.h"
#include "core/pk_types.h"
#include "gfx/pk_gfx.h"

#include <SDL3/SDL.h>

static struct
{
    SDL_Window *win;
    SDL_Renderer *ren;
    SDL_Texture *tex;
    bool fullscreen;
} P;

bool pk_platform_init(const pk_platform_config *cfg)
{
    int scale = cfg->scale;
    SDL_DisplayID d;
    const SDL_DisplayMode *dm;

    SDL_zero(P);
    if (scale <= 0) {
        scale = 3;
        d = SDL_GetPrimaryDisplay();
        dm = d ? SDL_GetDesktopDisplayMode(d) : NULL;
        if (dm) {
            scale = SDL_max(1, SDL_min((dm->h - 120) / PK_SCREEN_H, (dm->w - 120) / PK_SCREEN_W));
        }
    }
    if (!SDL_CreateWindowAndRenderer("Pocket Kingdom", PK_SCREEN_W * scale, PK_SCREEN_H * scale,
                                     SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY, &P.win, &P.ren)) {
        PK_ERROR("platform: %s", SDL_GetError());
        return false;
    }
    SDL_SetWindowMinimumSize(P.win, PK_SCREEN_W, PK_SCREEN_H);
    SDL_SetRenderVSync(P.ren, cfg->vsync ? 1 : 0);
    /* every platform (phones too): the largest whole-number scale that fits
     * the window, centred */
    SDL_SetRenderLogicalPresentation(P.ren, PK_SCREEN_W, PK_SCREEN_H, SDL_LOGICAL_PRESENTATION_INTEGER_SCALE);
    P.tex = SDL_CreateTexture(P.ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, PK_SCREEN_W, PK_SCREEN_H);
    if (!P.tex) {
        PK_ERROR("platform: %s", SDL_GetError());
        return false;
    }
    SDL_SetTextureScaleMode(P.tex, SDL_SCALEMODE_NEAREST);
    if (cfg->fullscreen) {
        pk_platform_toggle_fullscreen();
    }
    return true;
}

void pk_platform_quit(void)
{
    if (P.tex) {
        SDL_DestroyTexture(P.tex);
    }
    if (P.ren) {
        SDL_DestroyRenderer(P.ren);
    }
    if (P.win) {
        SDL_DestroyWindow(P.win);
    }
    SDL_zero(P);
}

void pk_platform_present(void)
{
    if (!P.ren) {
        return;
    }
    SDL_UpdateTexture(P.tex, NULL, pk_gfx_framebuffer(), PK_SCREEN_W * 4);
    SDL_SetRenderDrawColor(P.ren, 0, 0, 0, 255);
    SDL_RenderClear(P.ren);
    SDL_RenderTexture(P.ren, P.tex, NULL, NULL);
    SDL_RenderPresent(P.ren);
}

bool pk_platform_screenshot(const char *path)
{
    bool ok;
    SDL_Surface *s = SDL_CreateSurfaceFrom(PK_SCREEN_W, PK_SCREEN_H, SDL_PIXELFORMAT_ARGB8888,
                                           pk_gfx_framebuffer(), PK_SCREEN_W * 4);
    if (!s) {
        return false;
    }
    ok = SDL_SaveBMP(s, path);
    SDL_DestroySurface(s);
    return ok;
}

void pk_platform_error(const char *title, const char *msg)
{
    PK_ERROR("%s: %s", title, msg);
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, title, msg, P.win);
}

void pk_platform_toggle_fullscreen(void)
{
    P.fullscreen = !P.fullscreen;
    SDL_SetWindowFullscreen(P.win, P.fullscreen);
}

void pk_platform_convert_event(SDL_Event *e)
{
    if (P.ren) {
        SDL_ConvertEventToRenderCoordinates(P.ren, e);
    }
}
