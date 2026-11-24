/* SPDX-License-Identifier: MIT */
#ifndef PK_INPUT_H
#define PK_INPUT_H

#include <SDL3/SDL.h>
#include <stdbool.h>

/* N-Gage keypad as the game sees it. Default host mapping:
 *   arrows / WASD .... d-pad
 *   Enter, Space, 5 .. centre key / "5" (select)
 *   F1, Q ............ left soft key
 *   F2, E, Escape .... right soft key
 *   Backspace ........ clear key (C)
 *   0-9, *, # ........ number keys
 * Gamepads: d-pad/left stick, A = select, B = right soft, X = left soft,
 * Start = left soft, Back = clear. */
typedef enum pk_key
{
    PK_KEY_UP,
    PK_KEY_DOWN,
    PK_KEY_LEFT,
    PK_KEY_RIGHT,
    PK_KEY_SELECT,
    PK_KEY_LSK,
    PK_KEY_RSK,
    PK_KEY_CLEAR,
    PK_KEY_0,
    PK_KEY_1,
    PK_KEY_2,
    PK_KEY_3,
    PK_KEY_4,
    PK_KEY_5,
    PK_KEY_6,
    PK_KEY_7,
    PK_KEY_8,
    PK_KEY_9,
    PK_KEY_STAR,
    PK_KEY_HASH,
    PK_KEY_COUNT
} pk_key;

void pk_input_init(void);
void pk_input_quit(void);
void pk_input_event(const SDL_Event *e);

/* Called once per logic frame: makes the presses latched since the last
 * frame visible through pk_input_pressed and clears the latch. */
void pk_input_frame(void);
/* true once after the window lost focus or the app went to the background */
bool pk_input_focus_lost(void);

bool pk_input_held(pk_key k);
bool pk_input_pressed(pk_key k);
/* Pressed, or held long enough to auto-repeat (menu cursors). */
bool pk_input_repeat(pk_key k);
Uint32 pk_input_pressed_mask(void);
Uint32 pk_input_held_mask(void);

/* Touch and mouse (port: the phone had only the keypad). Pointer
 * events must already be in game coordinates (176 x 208), see
 * pk_platform_convert_event. A short touch without movement is a tap; a
 * drag scrolls in steps (one per row, about 12 px up/down or 20 px
 * left/right; finger up = the list moves on = down); a long press, a
 * two-finger tap, the right mouse button and the phone's back button are
 * the right soft key (back). The mouse wheel scrolls. */
#define PK_TOUCH_STEP_Y 12
#define PK_TOUCH_STEP_X 20
/* Takes this frame's tap; false when there was none. */
bool pk_input_tap(int *x, int *y);
/* Scroll steps of this frame: > 0 down / right, < 0 up / left. */
int pk_input_scroll_y(void);
int pk_input_scroll_x(void);
/* A finger or the left mouse button is down. */
bool pk_input_pointer_down(void);
/* The last pointer used was a finger, not the mouse (haptic feedback). */
bool pk_input_pointer_is_touch(void);

/* For tests and the attract mode: inject a press. */
void pk_input_inject(pk_key k, bool down);

/* Text entry (chat): letters go to the text, Enter/Escape/arrows still
 * act as keys, Backspace deletes. 8-bit ASCII only. */
void pk_input_text_begin(int max_chars);
/* the same for a number: digits only, a numeric on-screen keyboard */
void pk_input_text_begin_number(int max_digits);
void pk_input_text_end(void);
bool pk_input_text_active(void);
const char *pk_input_text(void);
void pk_input_text_append(const char *utf8); /* tests */
/* Shows the on-screen keyboard again (touch devices: a tap on the text). */
void pk_input_text_show(void);

/* tests: a synthetic touch at game coordinates (ms = how long it is held) */
void pk_input_test_touch(int x0, int y0, int x1, int y1, int ms);

#endif
