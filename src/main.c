/* SPDX-License-Identifier: MIT */
/* Pocket Kingdom reimplementation: program entry.
 *
 * No game data ships with this program. Point it at the contents of
 * your own copy's \System\Apps\6RAH\ folder with --data, the PK_DATA
 * environment variable, or by placing the files next to the executable
 * (see docs/USAGE.md). */
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3_net/SDL_net.h>

#include "audio/pk_audio.h"
#include "core/pk_app.h"
#include "core/pk_fs.h"
#include "core/pk_log.h"
#include "core/pk_text.h"
#include "engine/pk_game.h"
#include "game/pk_arena_game.h"
#include "game/pk_data.h"
#include "game/pk_scenes.h"
#include "game/pk_world.h"
#include "gfx/pk_font.h"
#include "gfx/pk_gfx.h"
#include "input/pk_haptics.h"
#include "input/pk_input.h"
#include "net/pk_arena.h"
#include "net/pk_identity.h"
#include "net/pk_online.h"
#include "net/pk_snap.h"
#include "platform/pk_platform.h"

#define PK_VERSION "0.1.0"

typedef struct options
{
    const char *data;
    const char *pref; /* --pref DIR */
    const char *imei;
    const char *screenshot;
    const char *arena_host;
    const char *arena_user;
    const char *arena_pass;
    const char *account_user, *account_pass;
    bool arena_host_set;
    int lang;
    int scale;
    int audio_rate;
    int frames;       /* run N timer ticks headless, then exit */
    bool realtime;    /* with --frames: pace the ticks (online tests) */
    const char *keys; /* "tick:key,..." injected presses (tests) */
    bool fullscreen;
    bool viewer;
    bool verbose;
    bool no_audio;
    bool no_haptics;
    bool smooth;
    bool lang_set;
} options;

static void usage(void)
{
    SDL_Log("Pocket Kingdom (reimplementation) " PK_VERSION "\n"
            "usage: pocketkingdom [options]\n"
            "  --data DIR          folder with your game files (6rah.app, pokekin.mbm, *.dat, *.pcm)\n"
            "  --pref DIR          folder for save.dat and arena.cfg (default: the user's preference folder)\n"
            "  --lang en|fr|de|it|es\n"
            "  --scale N           window scale (default: fit the screen)\n"
            "  --fullscreen\n"
            "  --audio-rate HZ     sample rate of the .pcm files (default 8000)\n"
            "  --no-audio\n"
            "  --no-haptics        no vibration on touch input\n"
            "  --smooth            interpolate movement and transitions at up to 60 FPS\n"
#ifdef PK_ENABLE_ASSET_VIEWER
            "  --viewer            start in the asset viewer\n"
#endif
            "  --imei DIGITS       use this IMEI instead of the generated one\n"
            "  --arena-host HOST   PhantomLink server for the N-Gage Arena (kept in arena.cfg)\n"
            "  --arena-user NAME --arena-pass PASSWORD   Arena account (kept in arena.cfg)\n"
            "  --arena-test USER PASSWORD   log in to the Community service and SNAP, then continue\n"
            "  --frames N          run N ticks without a window and exit (tests)\n"
            "  --screenshot FILE   with --frames: save the last frame as BMP\n"
            "  --realtime          with --frames: run at about real speed (online tests)\n"
            "  --keys LIST         with --frames: presses, e.g. 430:down,450:5 (keys: up down left right 5 lsk rsk 0-9;\n"
            "                      touch: tap/X/Y, hold/X/Y, swipe/X0/Y0/X1/Y1 in game pixels; blur: focus lost)\n"
            "  --verbose\n"
            "Keys: arrows, Enter/5 or F1/Q select (left soft key), F2/E/Esc back (right soft key), F11 fullscreen, F12 screenshot.");
}

static int parse_lang(const char *s)
{
    static const char *const k[] = { "en", "fr", "de", "it", "es" };
    int i;
    for (i = 0; i < 5; i++) {
        if (SDL_strcasecmp(s, k[i]) == 0) {
            return i;
        }
    }
    return 0;
}

static bool parse_args(int argc, char **argv, options *o)
{
    int i;
    SDL_zerop(o);
    o->audio_rate = PK_AUDIO_DEFAULT_RATE;
    o->arena_host = "localhost";
    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        const char *next = i + 1 < argc ? argv[i + 1] : NULL;
#define NEED(x)           \
    do {                  \
        if (!next) {      \
            usage();      \
            return false; \
        }                 \
        x;                \
        i++;              \
    } while (0)
        if (!SDL_strcmp(a, "--data")) {
            NEED(o->data = next);
        } else if (!SDL_strcmp(a, "--lang")) {
            NEED((o->lang = parse_lang(next), o->lang_set = true));
        } else if (!SDL_strcmp(a, "--scale")) {
            NEED(o->scale = SDL_atoi(next));
        } else if (!SDL_strcmp(a, "--audio-rate")) {
            NEED(o->audio_rate = SDL_atoi(next));
        } else if (!SDL_strcmp(a, "--pref")) {
            NEED(o->pref = next);
        } else if (!SDL_strcmp(a, "--imei")) {
            NEED(o->imei = next);
        } else if (!SDL_strcmp(a, "--frames")) {
            NEED(o->frames = SDL_atoi(next));
        } else if (!SDL_strcmp(a, "--realtime")) {
            o->realtime = true;
        } else if (!SDL_strcmp(a, "--screenshot")) {
            NEED(o->screenshot = next);
        } else if (!SDL_strcmp(a, "--keys")) {
            NEED(o->keys = next);
        } else if (!SDL_strcmp(a, "--arena-host")) {
            NEED((o->arena_host = next, o->arena_host_set = true));
        } else if (!SDL_strcmp(a, "--arena-user")) {
            NEED(o->account_user = next);
        } else if (!SDL_strcmp(a, "--arena-pass")) {
            NEED(o->account_pass = next);
        } else if (!SDL_strcmp(a, "--arena-test")) {
            if (i + 2 >= argc) {
                usage();
                return false;
            }
            o->arena_user = argv[++i];
            o->arena_pass = argv[++i];
        } else if (!SDL_strcmp(a, "--fullscreen")) {
            o->fullscreen = true;
        }
#ifdef PK_ENABLE_ASSET_VIEWER
        else if (!SDL_strcmp(a, "--viewer")) {
            o->viewer = true;
        }
#endif
        else if (!SDL_strcmp(a, "--no-audio")) {
            o->no_audio = true;
        } else if (!SDL_strcmp(a, "--no-haptics")) {
            o->no_haptics = true;
        } else if (!SDL_strcmp(a, "--smooth")) {
            o->smooth = true;
        } else if (!SDL_strcmp(a, "--verbose")) {
            o->verbose = true;
        } else if (!SDL_strcmp(a, "--help") || !SDL_strcmp(a, "-h")) {
            usage();
            return false;
        } else if (!SDL_strcmp(a, "--version")) {
            SDL_Log("%s", PK_VERSION);
            return false;
        } else {
            SDL_Log("unknown option '%s'", a);
            usage();
            return false;
        }
#undef NEED
    }
    return true;
}

/* --keys: presses a key at a timer tick and releases it 3 ticks later. */
static void scripted_keys(const char *list, int tick)
{
    static const struct
    {
        const char *name;
        pk_key key;
    } k_names[] = {
        { "up", PK_KEY_UP },
        { "down", PK_KEY_DOWN },
        { "left", PK_KEY_LEFT },
        { "right", PK_KEY_RIGHT },
        { "5", PK_KEY_SELECT },
        { "lsk", PK_KEY_LSK },
        { "rsk", PK_KEY_RSK },
        { "c", PK_KEY_CLEAR },
        { "0", PK_KEY_0 },
        { "1", PK_KEY_1 },
        { "2", PK_KEY_2 },
        { "3", PK_KEY_3 },
        { "4", PK_KEY_4 },
        { "6", PK_KEY_6 },
        { "7", PK_KEY_7 },
        { "8", PK_KEY_8 },
        { "9", PK_KEY_9 },
    };
    const char *p = list;
    while (p && *p) {
        int t = SDL_atoi(p);
        const char *c = SDL_strchr(p, ':');
        const char *e = SDL_strchr(p, ',');
        size_t n, i;
        if (!c) {
            break;
        }
        c++;
        n = e ? (size_t)(e - c) : SDL_strlen(c);
        if (tick == t && (!SDL_strncmp(c, "tap/", 4) || !SDL_strncmp(c, "hold/", 5) || !SDL_strncmp(c, "swipe/", 6))) {
            int v[4] = { 0, 0, 0, 0 }, k = 0;
            const char *q = SDL_strchr(c, '/');
            while (q && k < 4 && (!e || q < e)) {
                v[k++] = SDL_atoi(q + 1);
                q = SDL_strchr(q + 1, '/');
            }
            if (c[0] == 's') {
                pk_input_test_touch(v[0], v[1], v[2], v[3], 100);
            } else {
                pk_input_test_touch(v[0], v[1], v[0], v[1], c[0] == 'h' ? 1000 : 50);
            }
        }
        if (tick == t && n == 4 && !SDL_strncmp(c, "blur", 4)) {
            SDL_Event ev;
            SDL_zero(ev);
            ev.type = SDL_EVENT_WINDOW_FOCUS_LOST;
            pk_input_event(&ev);
        }
        for (i = 0; i < SDL_arraysize(k_names); i++) {
            if (SDL_strlen(k_names[i].name) == n && SDL_strncmp(c, k_names[i].name, n) == 0) {
                if (tick == t) {
                    pk_input_inject(k_names[i].key, true);
                } else if (tick == t + 3) {
                    pk_input_inject(k_names[i].key, false);
                }
            }
        }
        p = e ? e + 1 : NULL;
    }
}

/* Logs in to PhantomLink's Community service, then SNAP, and reports. */
static void arena_test(const options *o)
{
    Uint64 t0 = SDL_GetTicks();
    Uint8 msg[PK_ONLINE_MSG_MAX];
    int n;
    if (!pk_online_start(o->arena_host, o->arena_user, o->arena_pass, pk_identity_imei())) {
        PK_ERROR("arena test: %s", pk_online_status());
        return;
    }
    while (SDL_GetTicks() - t0 < 60000 && pk_online_get_state() != PK_ONLINE_READY &&
           pk_online_get_state() != PK_ONLINE_FAILED) {
        pk_online_update();
        SDL_Delay(10);
    }
    PK_INFO("arena test: %s (%s)", pk_online_get_state() == PK_ONLINE_READY ? "in the room" : "failed",
            pk_online_status());
    if (pk_online_get_state() == PK_ONLINE_READY) {
        SDL_zeroa(msg);
        msg[0] = 2; /* keep alive, answered by 130 */
        msg[1] = 7;
        SDL_strlcpy((char *)msg + 4, pk_online_user(), 16);
        pk_online_send(msg, 20);
        t0 = SDL_GetTicks();
        while (SDL_GetTicks() - t0 < 3000) {
            pk_online_update();
            while ((n = pk_online_poll(msg, sizeof(msg))) > 0) {
                PK_INFO("arena test: game message %u (%d bytes, seq %u)", msg[0], n, n > 1 ? msg[1] : 0);
            }
            SDL_Delay(10);
        }
    }
    pk_online_stop();
}

int main(int argc, char **argv)
{
    options opt;
    pk_game game;
    pk_platform_config pc;
    char missing[1024];
    bool headless, smooth_ready = false;
    Uint64 last, acc = 0, next_present = 0;
    static Uint32 smooth_frame[PK_SCREEN_W * PK_SCREEN_H];
    int ticks = 0;

    if (!parse_args(argc, argv, &opt)) {
        return 1;
    }
    headless = opt.frames > 0;
    pk_log_set_level(opt.verbose ? PK_LOG_DEBUG : PK_LOG_INFO);
    if (headless) {
        SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
        SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
    }
    /* touch and mouse are handled apart (input/pk_input.c); the phone's
     * back button is the right soft key, not "quit" */
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
    SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");
    SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD | SDL_INIT_EVENTS)) {
        SDL_Log("SDL_Init: %s", SDL_GetError());
        return 1;
    }
    if (!NET_Init()) {
        PK_WARN("SDL_net: %s (online features disabled)", SDL_GetError());
    }

    pk_fs_set_pref_dir(opt.pref);
    if (!pk_fs_init(opt.data)) {
        const char *dir = opt.data ? opt.data : "(searched default locations)";
#ifdef SDL_PLATFORM_ANDROID
        const char *ext = SDL_GetAndroidExternalStoragePath();
        (void)dir;
        if (ext) { /* an empty folder to copy the files into */
            char target[PK_PATH_MAX];
            SDL_snprintf(target, sizeof(target), "%s/6rah", ext);
            SDL_CreateDirectory(target);
        }
        SDL_snprintf(missing, sizeof(missing),
                     "No Pocket Kingdom game data found.\n\n"
                     "This app does not include any game files. Copy the contents of the\n"
                     "\\System\\Apps\\6RAH\\ folder from your own copy of the game into\n"
                     "  %s/6rah\n"
                     "(for example over USB: Android/data/<this app>/files/6rah) and start again.",
                     ext ? ext : "(the app's storage folder)");
#else
        SDL_snprintf(missing, sizeof(missing),
                     "No Pocket Kingdom game data found in %s.\n\n"
                     "This program does not include any game files. Copy the contents of the\n"
                     "\\System\\Apps\\6RAH\\ folder from your own copy of the game, then start with\n"
                     "  --data <folder>\n"
                     "or place the files in a 'data' folder next to the program.",
                     dir);
#endif
        if (opt.data) {
            char list[512];
            if (pk_fs_check_dir(opt.data, list, sizeof(list)) > 0) {
                SDL_strlcat(missing, "\n\nMissing files:\n", sizeof(missing));
                SDL_strlcat(missing, list, sizeof(missing));
            }
        }
        pk_platform_error("Pocket Kingdom", missing);
        SDL_Quit();
        return 2;
    }
    pk_fs_set_language((pk_language)opt.lang);
    if (!pk_app_load() || !pk_gfx_init() || !pk_font_init()) {
        pk_platform_error("Pocket Kingdom",
                          "The game data could not be read. This build supports the 6RAH.APP of\n"
                          "Pocket Kingdom v. 110804. Builds without the built-in font also need\n"
                          "ceurope.gdr in the data folder. See the log for details.");
        SDL_Quit();
        return 3;
    }
    pk_data_load();
    if (!pk_identity_init(opt.imei)) {
        SDL_Quit();
        return 4;
    }
    if (opt.account_user || opt.account_pass || opt.arena_host_set) {
        pk_arena_set_account(opt.arena_host_set ? opt.arena_host : NULL, opt.account_user, opt.account_pass);
    }
    if (!opt.no_audio) {
        pk_audio_init(opt.audio_rate);
    }
    pk_input_init();
    pk_haptics_init(!opt.no_haptics && !headless);

    pc.scale = opt.scale;
    pc.fullscreen = opt.fullscreen;
    pc.vsync = false;
    if (!headless && !pk_platform_init(&pc)) {
        SDL_Quit();
        return 5;
    }
    if (opt.arena_user) {
        arena_test(&opt);
    }

    pk_game_init(&game);
    pk_scenes_register(&game);
    game.language = opt.lang;
    pk_options_load(&game);
    if (opt.lang_set) {
        game.language = opt.lang;
    }
    pk_game_apply_language(&game);
    pk_game_set_mode(&game, opt.viewer ? PK_MODE_VIEWER : PK_MODE_BOOT);

    last = SDL_GetTicksNS();
    while (!game.quit) {
        SDL_Event e;
        Uint64 now;
        bool drawn = false;

        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT) {
                game.quit = true;
            } else if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_F11) {
                pk_platform_toggle_fullscreen();
            } else if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_F12) {
                char path[PK_PATH_MAX];
                SDL_snprintf(path, sizeof(path), "%sscreenshot-%llu.bmp", pk_fs_pref_dir(),
                             (unsigned long long)SDL_GetTicks());
                if (pk_platform_screenshot(path)) {
                    PK_INFO("screenshot: %s", path);
                }
            } else {
                pk_platform_convert_event(&e); /* pointer positions in game pixels */
                pk_input_event(&e);
            }
        }
        if (headless) {
            scripted_keys(opt.keys, ticks);
            pk_game_tick(&game);
            if (opt.realtime) {
                SDL_Delay(10); /* tests with two online clients */
            }
            if (++ticks >= opt.frames) {
                PK_DEBUG("headless end: mode %d phase %d curtain %d/%d objects %d", game.mode, game.phase, game.curtain, game.curtain_w, game.objects->count);
                {
                    int q;
                    for (q = 0; q < PK_EVENTS; q++) {
                        if (game.world.events[q][1] != 0) {
                            PK_DEBUG("event %d: kind %d status %d timer %d team %d", q, game.world.events[q][2], game.world.events[q][1], game.world.events[q][0], game.world.events[q][3]);
                        }
                    }
                    for (q = 0; q < game.objects->count; q++) {
                        pk_object *ob = game.objects->items[q];
                        PK_DEBUG("obj %d: type %#x state %#x a0 %#x a1 %#x at %d,%d fl %#x fr %d", q, ob->type, ob->state, ob->arg0, ob->arg1, ob->x, ob->y, (unsigned)ob->flags, ob->frame);
                    }
                    for (q = 0; q < game.drawq.text_count; q++) {
                        char u8[128];
                        pk_text_to_utf8(game.drawq.text[q].text, u8, sizeof(u8));
                        PK_DEBUG("text %d,%d L%d '%s'", game.drawq.text[q].x, game.drawq.text[q].y, game.drawq.text[q].layer, u8);
                    }
                }
                if (opt.screenshot) {
                    pk_platform_screenshot(opt.screenshot);
                }
                break;
            }
            continue;
        }
        now = SDL_GetTicksNS();
        acc += now - last;
        last = now;
        if (acc > 10ull * PK_TIMER_INTERVAL_US * 1000ull) {
            acc = PK_TIMER_INTERVAL_US * 1000ull; /* after a stall, do not race to catch up */
        }
        while (acc >= PK_TIMER_INTERVAL_US * 1000ull) {
            int phase = game.tick_phase;
            acc -= PK_TIMER_INTERVAL_US * 1000ull;
            if (opt.smooth && phase == 0) {
                pk_game_smooth_begin(&game);
            }
            drawn |= pk_game_tick(&game);
            if (opt.smooth && phase == 0) {
                smooth_ready = pk_game_smooth_capture(&game);
            }
        }
        if (opt.smooth && smooth_ready && now >= next_present) {
            Uint64 elapsed = acc;
            if (game.tick_phase == 2) {
                elapsed += PK_TIMER_INTERVAL_US * 1000ull;
            } else if (game.tick_phase == 0) {
                elapsed += 2ull * PK_TIMER_INTERVAL_US * 1000ull;
            }
            pk_game_smooth_render(&game, smooth_frame, elapsed);
            pk_platform_present_frame(smooth_frame);
            next_present = now + 1000000000ull / 60;
        } else if (drawn && (!opt.smooth || !smooth_ready)) {
            pk_platform_present();
        }
        SDL_DelayNS(1000000);
    }

    pk_game_shutdown(&game);
    pk_platform_quit();
    pk_haptics_quit();
    pk_input_quit();
    pk_audio_quit();
    pk_data_unload();
    pk_font_quit();
    pk_gfx_quit();
    pk_app_unload();
    pk_fs_quit();
    NET_Quit();
    SDL_Quit();
    return 0;
}
