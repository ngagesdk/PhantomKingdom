/* SPDX-License-Identifier: MIT */
#include "platform/pk_play_games.h"
#include "core/pk_log.h"
#include <SDL3/SDL.h>

#ifdef SDL_PLATFORM_ANDROID
#include <jni.h>

static void send(const char *method, const char *signature, int a, int b)
{
    JNIEnv *env = (JNIEnv *)SDL_GetAndroidJNIEnv();
    jobject activity;
    jclass cls;
    jmethodID mid;
    if (!env || !(activity = (jobject)SDL_GetAndroidActivity())) {
        PK_WARN("play games: Android activity unavailable");
        return;
    }
    cls = (*env)->GetObjectClass(env, activity);
    mid = cls ? (*env)->GetMethodID(env, cls, method, signature) : NULL;
    if (mid) {
        if (signature[2] == ')') {
            (*env)->CallVoidMethod(env, activity, mid, (jint)a);
        } else {
            (*env)->CallVoidMethod(env, activity, mid, (jint)a, (jint)b);
        }
    }
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionDescribe(env);
        (*env)->ExceptionClear(env);
        PK_WARN("play games: Android call %s failed", method);
    } else if (!mid) {
        PK_WARN("play games: Android method %s unavailable", method);
    }
    if (cls) {
        (*env)->DeleteLocalRef(env, cls);
    }
    (*env)->DeleteLocalRef(env, activity);
}

void pk_play_games_treasures(int mask) { send("playGamesTreasures", "(I)V", mask, 0); }
void pk_play_games_online_crest(void) { send("playGamesOnlineCrest", "()V", 0, 0); }
void pk_play_games_ranked_result(int won, int owned) { send("playGamesRankedResult", "(II)V", won, owned); }
#else
void pk_play_games_treasures(int mask) { (void)mask; }
void pk_play_games_online_crest(void) {}
void pk_play_games_ranked_result(int won, int owned)
{
    (void)won;
    (void)owned;
}
#endif
