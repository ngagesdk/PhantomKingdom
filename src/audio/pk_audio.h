/* SPDX-License-Identifier: MIT */
#ifndef PK_AUDIO_H
#define PK_AUDIO_H

#include <stdbool.h>

/* Sound follows the original sound thread (FUN_10069f70 and friends):
 * one looping music track (BGM01..08.pcm, ids 0..7) and one sound effect
 * voice (ids below). The .pcm files are raw signed 16-bit little endian
 * mono. The rate defaults to 8000 Hz (Symbian audio stream default) and
 * can be changed with --audio-rate until it is confirmed. */

#define PK_AUDIO_DEFAULT_RATE 8000
#define PK_BGM_COUNT          8
#define PK_BGM_NONE           0xff

enum
{
    PK_SFX_SYS01 = 0x00, /* .. 0x04 SYS05 */
    PK_SFX_ATK01 = 0x05, /* .. 0x1e ATK26 */
    PK_SFX_GROW = 0x1f,
    PK_SFX_WHISKAWAY = 0x20,
    PK_SFX_NEON_LIGHT = 0x21,
    PK_SFX_WHOOT = 0x22,  /* provisional mapping */
    PK_SFX_YEEAHT = 0x23, /* provisional mapping */
    PK_SFX_COUNT
};

bool pk_audio_init(int rate);
void pk_audio_quit(void);

void pk_audio_play_bgm(int id);
void pk_audio_stop_bgm(void);
int pk_audio_current_bgm(void);
void pk_audio_play_sfx(int id);
void pk_audio_stop_sfx(void);

/* 0..10 like the game's option screen. */
void pk_audio_set_volume(int bgm, int sfx);
void pk_audio_pause(bool paused);

#endif
