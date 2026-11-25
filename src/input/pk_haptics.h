/* SPDX-License-Identifier: MIT */
#ifndef PK_HAPTICS_H
#define PK_HAPTICS_H

#include <stdbool.h>

/* Haptic feedback for touch input (port). The N-Gage had a keypad, so
 * the game gives no sign that a tap was taken; a short vibration does.
 * It only adds feedback: nothing the game shows or does changes.
 *
 * Feedback is given only while the pointer is a finger (never for the
 * mouse) and not with --no-haptics. On Android it uses media-attributed
 * vibrator effects; elsewhere it uses an SDL haptic device with rumble.
 * The values of pk_haptic are passed to Java (PocketKingdomActivity.haptic):
 * keep them in step. */

typedef enum pk_haptic
{
    PK_HAPTIC_TAP = 0,  /* a tap was taken by a touch target */
    PK_HAPTIC_STEP = 1, /* a swipe moved a list or the cursor by a step */
    PK_HAPTIC_BACK = 2  /* a long press or two-finger tap acted as back */
} pk_haptic;

/* Opens the first haptic device; false (and no feedback) if there is none
 * or enabled is false. Safe to call without a window. */
bool pk_haptics_init(bool enabled);
void pk_haptics_quit(void);

/* Plays the feedback for an event, if touch is the current pointer. */
void pk_haptics_play(pk_haptic kind);

#endif
