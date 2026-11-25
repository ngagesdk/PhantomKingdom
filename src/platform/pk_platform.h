/* SPDX-License-Identifier: MIT */
#ifndef PK_PLATFORM_H
#define PK_PLATFORM_H

#include <stdbool.h>
#include <SDL3/SDL_stdinc.h>

typedef struct pk_platform_config
{
    int scale; /* initial window scale, 0 = automatic */
    bool fullscreen;
    bool vsync;
} pk_platform_config;

bool pk_platform_init(const pk_platform_config *cfg);
void pk_platform_quit(void);

/* Uploads the frame buffer and presents it. */
void pk_platform_present(void);
void pk_platform_present_frame(const Uint32 *pixels);

/* Writes the frame buffer to a BMP (F12 screenshots, tests). */
bool pk_platform_screenshot(const char *path);

/* Shows an error dialog (falls back to the log without a window). */
void pk_platform_error(const char *title, const char *msg);

void pk_platform_toggle_fullscreen(void);

/* Converts mouse and touch positions of an event to game pixels
 * (176 x 208, the letterbox taken into account). */
union SDL_Event;
void pk_platform_convert_event(union SDL_Event *e);

#endif
