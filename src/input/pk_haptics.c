/* SPDX-License-Identifier: MIT */
#include "input/pk_haptics.h"
#include "core/pk_log.h"
#include "input/pk_input.h"

#include <SDL3/SDL.h>

/* Steps of a fast swipe come in bursts; one buzz per this many ms. */
#define STEP_GAP_MS 45

static bool s_on;
static Uint64 s_last_step;
static bool s_logged_play, s_logged_mouse; /* port: one log line each (logcat: "haptics") */

#ifdef SDL_PLATFORM_ANDROID
/* Android: the system's touch feedback (View.performHapticFeedback with
 * KEYBOARD_TAP, CLOCK_TICK, LONG_PRESS), the same short clicks as the
 * keyboard and buttons. It needs no permission, is tuned for the phone's
 * motor and follows the user's "Touch feedback" setting. Raw vibrator
 * pulses (SDL haptics) of a few ms are too weak on many phones and fall
 * under the volume rules for app vibrations. The activity's
 * haptic(int) method (PocketKingdomActivity.java) takes the kinds of
 * pk_haptic. */
#include <jni.h>

static jobject s_activity; /* global reference */
static jmethodID s_haptic_mid;

static bool backend_init(void)
{
    JNIEnv *env = (JNIEnv *)SDL_GetAndroidJNIEnv();
    jobject act;
    jclass cls;
    if (!env) {
        return false;
    }
    act = (jobject)SDL_GetAndroidActivity();
    if (!act) {
        return false;
    }
    cls = (*env)->GetObjectClass(env, act);
    s_haptic_mid = cls ? (*env)->GetMethodID(env, cls, "haptic", "(I)V") : NULL;
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env); /* NoSuchMethodError */
        s_haptic_mid = NULL;
    }
    if (cls) {
        (*env)->DeleteLocalRef(env, cls);
    }
    if (s_haptic_mid) {
        s_activity = (*env)->NewGlobalRef(env, act);
    }
    (*env)->DeleteLocalRef(env, act);
    if (!s_activity) {
        PK_WARN("haptics: the activity has no haptic(int) method");
        return false;
    }
    PK_INFO("haptics: Android touch feedback");
    return true;
}

static void backend_quit(void)
{
    JNIEnv *env = (JNIEnv *)SDL_GetAndroidJNIEnv();
    if (env && s_activity) {
        (*env)->DeleteGlobalRef(env, s_activity);
    }
    s_activity = NULL;
    s_haptic_mid = NULL;
}

static void backend_play(pk_haptic kind)
{
    JNIEnv *env = (JNIEnv *)SDL_GetAndroidJNIEnv();
    if (!env || !s_activity) {
        return;
    }
    (*env)->CallVoidMethod(env, s_activity, s_haptic_mid, (jint)kind);
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
    }
}

#else
/* Elsewhere: an SDL haptic device with rumble, strength (0..1) and length
 * (ms) per kind. */
static const struct
{
    float strength;
    Uint32 ms;
} k_feel[] = {
    { 0.45f, 14 }, /* PK_HAPTIC_TAP */
    { 0.25f, 10 }, /* PK_HAPTIC_STEP */
    { 0.70f, 30 }, /* PK_HAPTIC_BACK */
};

static SDL_Haptic *s_haptic;

static bool backend_init(void)
{
    SDL_HapticID *ids;
    int i, n = 0;
    if (!SDL_InitSubSystem(SDL_INIT_HAPTIC)) {
        PK_INFO("haptics: off (%s)", SDL_GetError());
        return false;
    }
    ids = SDL_GetHaptics(&n);
    for (i = 0; ids && i < n && !s_haptic; i++) {
        SDL_Haptic *h = SDL_OpenHaptic(ids[i]);
        if (!h) {
            continue;
        }
        if (SDL_InitHapticRumble(h)) {
            s_haptic = h;
            PK_INFO("haptics: %s", SDL_GetHapticNameForID(ids[i]) ? SDL_GetHapticNameForID(ids[i]) : "rumble");
        } else {
            SDL_CloseHaptic(h);
        }
    }
    SDL_free(ids);
    if (!s_haptic) {
        PK_INFO("haptics: off (no rumble device)");
        SDL_QuitSubSystem(SDL_INIT_HAPTIC);
        return false;
    }
    return true;
}

static void backend_quit(void)
{
    if (s_haptic) {
        SDL_CloseHaptic(s_haptic);
        s_haptic = NULL;
        SDL_QuitSubSystem(SDL_INIT_HAPTIC);
    }
}

static void backend_play(pk_haptic kind)
{
    if (s_haptic && (size_t)kind < SDL_arraysize(k_feel)) {
        SDL_PlayHapticRumble(s_haptic, k_feel[kind].strength, k_feel[kind].ms);
    }
}
#endif

bool pk_haptics_init(bool enabled)
{
    if (!enabled) {
        PK_INFO("haptics: off (--no-haptics)");
        return false;
    }
    s_on = backend_init();
    return s_on;
}

void pk_haptics_quit(void)
{
    if (s_on) {
        backend_quit();
        s_on = false;
    }
}

void pk_haptics_play(pk_haptic kind)
{
    if (!s_on || (int)kind < 0 || kind > PK_HAPTIC_BACK) {
        return;
    }
    if (!pk_input_pointer_is_touch()) {
        if (!s_logged_mouse) {
            s_logged_mouse = true;
            PK_INFO("haptics: skipped, the pointer is a mouse");
        }
        return;
    }
    if (!s_logged_play) {
        s_logged_play = true;
        PK_INFO("haptics: first feedback (kind %d)", (int)kind);
    }
    if (kind == PK_HAPTIC_STEP) {
        Uint64 now = SDL_GetTicks();
        if (now - s_last_step < STEP_GAP_MS) {
            return;
        }
        s_last_step = now;
    }
    backend_play(kind);
}
