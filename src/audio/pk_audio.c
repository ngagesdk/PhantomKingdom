/* SPDX-License-Identifier: MIT */
#include "audio/pk_audio.h"
#include "core/pk_fs.h"
#include "core/pk_log.h"

#include <SDL3/SDL.h>

typedef struct pk_pcm
{
    Sint16 *s;
    size_t n;
    bool tried;
} pk_pcm;

typedef struct pk_voice
{
    const pk_pcm *pcm;
    size_t pos;
    bool loop;
} pk_voice;

static struct
{
    SDL_AudioStream *stream;
    SDL_Mutex *lock;
    pk_pcm bgm[PK_BGM_COUNT];
    pk_pcm sfx[PK_SFX_COUNT];
    pk_voice music;
    pk_voice effect;
    int bgm_id;
    int vol_bgm, vol_sfx;
    bool ok;
} A;

static const char *bgm_name(int id, char *buf, size_t cap)
{
    SDL_snprintf(buf, cap, "bgm%02d.pcm", id + 1);
    return buf;
}

static const char *sfx_name(int id, char *buf, size_t cap)
{
    if (id <= 4) {
        SDL_snprintf(buf, cap, "sys%02d.pcm", id + 1);
    } else if (id <= 0x1e) {
        SDL_snprintf(buf, cap, "atk%02d.pcm", id - 4);
    } else {
        static const char *const extra[] = { "grow.pcm", "whiskaway1.pcm", "neon_light.pcm", "whoot.pcm", "yeeaht.pcm" };
        SDL_strlcpy(buf, extra[id - 0x1f], cap);
    }
    return buf;
}

static const pk_pcm *get_pcm(pk_pcm *p, const char *name)
{
    if (!p->tried) {
        size_t len = 0;
        p->tried = true;
        p->s = (Sint16 *)pk_fs_load(name, &len);
        p->n = len / 2;
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
        {
            size_t i;
            for (i = 0; i < p->n; i++) {
                p->s[i] = (Sint16)SDL_Swap16((Uint16)p->s[i]);
            }
        }
#endif
    }
    return p->s ? p : NULL;
}

static void SDLCALL feed(void *ud, SDL_AudioStream *st, int additional, int total)
{
    Sint16 buf[1024];
    (void)ud;
    (void)total;
    while (additional > 0) {
        int n = SDL_min(additional / 2, (int)SDL_arraysize(buf)), i;
        if (n <= 0) {
            break;
        }
        SDL_LockMutex(A.lock);
        for (i = 0; i < n; i++) {
            int v = 0;
            pk_voice *vs[2];
            int k;
            vs[0] = &A.music;
            vs[1] = &A.effect;
            for (k = 0; k < 2; k++) {
                pk_voice *vo = vs[k];
                if (!vo->pcm) {
                    continue;
                }
                if (vo->pos >= vo->pcm->n) {
                    if (vo->loop && vo->pcm->n) {
                        vo->pos = 0;
                    } else {
                        vo->pcm = NULL;
                        continue;
                    }
                }
                v += vo->pcm->s[vo->pos++] * (k == 0 ? A.vol_bgm : A.vol_sfx) / 10;
            }
            buf[i] = (Sint16)SDL_clamp(v, -32768, 32767);
        }
        SDL_UnlockMutex(A.lock);
        SDL_PutAudioStreamData(st, buf, n * 2);
        additional -= n * 2;
    }
}

bool pk_audio_init(int rate)
{
    SDL_AudioSpec spec;
    SDL_zero(A);
    A.bgm_id = PK_BGM_NONE;
    A.vol_bgm = A.vol_sfx = 10;
    A.lock = SDL_CreateMutex();
    spec.format = SDL_AUDIO_S16;
    spec.channels = 1;
    spec.freq = rate > 0 ? rate : PK_AUDIO_DEFAULT_RATE;
    A.stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, feed, NULL);
    if (!A.stream) {
        PK_WARN("audio: no output device (%s); continuing silently", SDL_GetError());
        return false;
    }
    SDL_ResumeAudioStreamDevice(A.stream);
    A.ok = true;
    PK_INFO("audio: %d Hz mono s16", spec.freq);
    return true;
}

void pk_audio_quit(void)
{
    int i;
    if (A.stream) {
        SDL_DestroyAudioStream(A.stream);
    }
    for (i = 0; i < PK_BGM_COUNT; i++) {
        SDL_free(A.bgm[i].s);
    }
    for (i = 0; i < PK_SFX_COUNT; i++) {
        SDL_free(A.sfx[i].s);
    }
    if (A.lock) {
        SDL_DestroyMutex(A.lock);
    }
    SDL_zero(A);
}

void pk_audio_play_bgm(int id)
{
    char name[32];
    const pk_pcm *p;
    if (id < 0 || id >= PK_BGM_COUNT) {
        pk_audio_stop_bgm();
        return;
    }
    if (id == A.bgm_id) {
        return; /* same track keeps playing, like FUN_10077fd4 */
    }
    p = get_pcm(&A.bgm[id], bgm_name(id, name, sizeof(name)));
    SDL_LockMutex(A.lock);
    A.bgm_id = id;
    A.music.pcm = p;
    A.music.pos = 0;
    A.music.loop = true;
    SDL_UnlockMutex(A.lock);
}

void pk_audio_stop_bgm(void)
{
    SDL_LockMutex(A.lock);
    A.music.pcm = NULL;
    A.bgm_id = PK_BGM_NONE;
    SDL_UnlockMutex(A.lock);
}

int pk_audio_current_bgm(void)
{
    return A.bgm_id;
}

void pk_audio_play_sfx(int id)
{
    char name[32];
    const pk_pcm *p;
    if (id < 0 || id >= PK_SFX_COUNT) {
        return;
    }
    p = get_pcm(&A.sfx[id], sfx_name(id, name, sizeof(name)));
    SDL_LockMutex(A.lock);
    A.effect.pcm = p;
    A.effect.pos = 0;
    A.effect.loop = false;
    SDL_UnlockMutex(A.lock);
}

void pk_audio_stop_sfx(void)
{
    SDL_LockMutex(A.lock);
    A.effect.pcm = NULL;
    SDL_UnlockMutex(A.lock);
}

void pk_audio_set_volume(int bgm, int sfx)
{
    SDL_LockMutex(A.lock);
    A.vol_bgm = SDL_clamp(bgm, 0, 10);
    A.vol_sfx = SDL_clamp(sfx, 0, 10);
    SDL_UnlockMutex(A.lock);
}

void pk_audio_pause(bool paused)
{
    if (!A.ok) {
        return;
    }
    if (paused) {
        SDL_PauseAudioStreamDevice(A.stream);
    } else {
        SDL_ResumeAudioStreamDevice(A.stream);
    }
}
