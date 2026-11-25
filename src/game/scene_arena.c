/* SPDX-License-Identifier: MIT */
/* N-Gage Arena (port of the online side: 0x10065ce0..0x68400).
 *
 * The title's "N-Gage Arena" connects to a PhantomLink server
 * (net/pk_online.c) with the account given by --arena-host,
 * --arena-user and --arena-pass (kept in arena.cfg next to the save),
 * loads the kingdom and plays it online: game+0x1aa0c == 2 is
 * g->arena_online. While online the game sends
 *   1  online   [2] area, [3..6] look, name @8   (answer 129)
 *   2  keep alive every ~165 frames, name @4     (answer 130)
 *   5  moved    [2] new area, name @4
 *   3  offline  name @4
 *   6  search   [2] area, name @4                (answer 134: rivals)
 * and answers other players' scouting (7 -> 135) and attacks
 * (9 -> 137) with its defense party (FUN_100661e0 / FUN_100669a8).
 * Byte 1 of a message is a tag from one counter (game+0x1aa10) that the
 * answer repeats. Its own scouting and attacks on rival kingdoms are in
 * scene_map.c. g_rivals keeps the last answer 134 (game+0x1aa18).
 * Message layouts: PhantomLink docs/ARENA.md. */
#include "audio/pk_audio.h"
#include "core/pk_app.h"
#include "core/pk_fs.h"
#include "core/pk_log.h"
#include "core/pk_text.h"
#include "game/pk_arena_game.h"
#include "game/pk_help.h"
#include "game/pk_rules.h"
#include "game/pk_scenes.h"
#include "game/pk_world.h"
#include "game/ui_dialog.h"
#include "gfx/pk_font.h"
#include "gfx/pk_gfx.h"
#include "input/pk_input.h"
#include "net/pk_identity.h"
#include "net/pk_online.h"
#include <SDL3/SDL.h>

static struct
{
    char host[128], user[32], pass[64];
    bool loaded;
} s_acc;

pk_rival g_rivals[PK_RIVALS_MAX];
int g_rival_count;
int g_rival_area = -1;

static int s_keepalive, s_seq;
static int s_state, s_counter;

/* ---- account ---------------------------------------------------- */
/* received letters, read on the letter screen when the kingdom is idle */
static struct
{
    char from[17], text[80];
    int messenger;
} s_inbox[4];
static int s_inbox_n, s_reading;

static void account_load(void)
{
    size_t n = 0;
    char *p, *line, *save = NULL;
    if (s_acc.loaded) {
        return;
    }
    s_acc.loaded = true;
    p = (char *)pk_fs_load_pref("arena.cfg", &n);
    if (!p) {
        return;
    }
    p = (char *)SDL_realloc(p, n + 1);
    p[n] = 0;
    for (line = SDL_strtok_r(p, "\r\n", &save); line; line = SDL_strtok_r(NULL, "\r\n", &save)) {
        if (!SDL_strncmp(line, "host=", 5)) {
            SDL_strlcpy(s_acc.host, line + 5, sizeof(s_acc.host));
        } else if (!SDL_strncmp(line, "user=", 5)) {
            SDL_strlcpy(s_acc.user, line + 5, sizeof(s_acc.user));
        } else if (!SDL_strncmp(line, "password=", 9)) {
            SDL_strlcpy(s_acc.pass, line + 9, sizeof(s_acc.pass));
        }
    }
    SDL_free(p);
}

void pk_arena_set_account(const char *host, const char *user, const char *password)
{
    char buf[256];
    account_load();
    if (host) {
        SDL_strlcpy(s_acc.host, host, sizeof(s_acc.host));
    }
    if (user) {
        SDL_strlcpy(s_acc.user, user, sizeof(s_acc.user));
    }
    if (password) {
        SDL_strlcpy(s_acc.pass, password, sizeof(s_acc.pass));
    }
    SDL_snprintf(buf, sizeof(buf), "host=%s\nuser=%s\npassword=%s\n", s_acc.host, s_acc.user, s_acc.pass);
    pk_fs_save_pref("arena.cfg", buf, SDL_strlen(buf));
}

bool pk_arena_has_account(void)
{
    account_load();
    return s_acc.host[0] && s_acc.user[0] && s_acc.pass[0];
}

/* ---- messages ----------------------------------------------------- */
static void put_name(Uint8 *at, const char *name)
{
    SDL_memset(at, 0, 16);
    SDL_strlcpy((char *)at, name, 16);
}

/* the tag of the next message (game+0x1aa10, taken and counted on) */
int pk_arena_next_tag(void)
{
    return (Uint8)(s_seq++);
}

static bool send_simple(int type, int b2, int len, int name_at, int tag)
{
    Uint8 m[32];
    SDL_zeroa(m);
    m[0] = (Uint8)type;
    m[1] = (Uint8)tag;
    m[2] = (Uint8)b2;
    put_name(m + name_at, pk_online_user());
    return pk_online_send(m, len);
}

void pk_arena_send_online(pk_game *g)
{
    Uint8 m[24];
    SDL_zeroa(m);
    m[0] = 1;
    m[1] = (Uint8)pk_arena_next_tag();
    m[2] = (Uint8)g->world.current_area;
    m[3] = (Uint8)(g->world.h_4e4 & 3);    /* castle */
    m[4] = (Uint8)(g->world.h_4e8 & 0xff); /* transporter */
    m[5] = (Uint8)(g->world.h_4ec & 0xff); /* mark */
    m[6] = (Uint8)(g->world.h_4e4 >> 2);   /* colour / style */
    put_name(m + 8, pk_online_user());
    pk_online_send(m, 24);
}

void pk_arena_send_moved(pk_game *g)
{
    if (g->arena_online) {
        send_simple(5, g->world.current_area, 20, 4, pk_arena_next_tag());
    }
}

/* FUN_10065ff0: 6, the area search; the tag the answer 134 repeats, or
 * -1 when it could not be sent */
int pk_arena_send_search(pk_game *g)
{
    int tag = pk_arena_next_tag();
    if (!g->arena_online || !send_simple(6, g->world.current_area, 20, 4, tag)) {
        return -1;
    }
    return tag;
}

static int s_defending; /* defence (see below): 1 message kept, 2 battle queued */

/* the connection was lost: the pop-up waits for a free moment */
static int s_lost;

void pk_arena_go_offline(pk_game *g)
{
    if (!g->arena_online) {
        return;
    }
    if (s_defending == 1) {
        pk_arena_attack_done(g);
    }
    send_simple(3, 0, 20, 4, pk_arena_next_tag());
    pk_online_update();
    pk_online_stop();
    g->arena_online = 0;
    g_rival_count = 0;
    pk_rivals_reset(g, true); /* FUN_100723e0 */
}

/* the defense party's units in the 12-byte form of messages 9 / 137 */
static void put_units(pk_game *g, Uint8 *at, int team)
{
    int i;
    for (i = 0; i < 4; i++) {
        Uint8 *e = at + i * 12;
        int u = (team >= 0 && team < PK_TEAMS) ? g->world.teams[team][i] : 0xff;
        SDL_memset(e, 0xff, 6);
        e[6] = 0;
        e[7] = 5;
        e[8] = 0;
        e[9] = 0xff;
        e[10] = e[11] = 0;
        if (u == 0xff || u >= PK_UNIT_MAX) {
            continue;
        }
        e[0] = (Uint8)g->world.units[u][0];
        if (g->world.units[u][2] != 0xff && g->world.units[u][2] < PK_EQUIP_MAX) {
            int *w = g->world.equip[g->world.units[u][2]];
            e[1] = (Uint8)w[0];
            e[2] = (Uint8)w[1];
            e[3] = (Uint8)w[3];
            e[4] = (Uint8)w[4];
            e[5] = (Uint8)w[2];
        }
        e[6] = (Uint8)g->world.units[u][1];
        e[7] = (Uint8)g->world.teams[team][4 + i];
        e[9] = (Uint8)g->world.units[u][7];
    }
}

/* another player scouts us (7): answer 135 (FUN_100661e0) */
static void answer_scout(pk_game *g, const Uint8 *in)
{
    Uint8 m[52];
    int team = g->world.h_dc, i;
    SDL_zeroa(m);
    m[0] = 135;
    m[1] = in[1];
    m[2] = (Uint8)g->world.h_d4;
    SDL_memcpy(m + 4, in + 4, 16); /* to: the scout's sender */
    put_name(m + 20, pk_online_user());
    for (i = 0; i < 4; i++) {
        m[36 + i * 4] = 0xff;
    }
    if (team != 0xff && team < PK_TEAMS) {
        for (i = 0; i < 4; i++) {
            int u = g->world.teams[team][i];
            if (u == 0xff || u >= PK_UNIT_MAX) {
                continue;
            }
            m[36 + i * 4] = (Uint8)g->world.units[u][0];
            m[37 + i * 4] = (Uint8)g->world.units[u][7];
            m[38 + i * 4] = (Uint8)g->world.units[u][17];
        }
        m[3] = (Uint8)g->world.teams[team][8];
    }
    pk_online_send(m, 52);
    PK_INFO("arena: scouted by %.16s", (const char *)in + 4);
}

/* The defence (FUN_100669a8 / FUN_10066de0): the attack message is kept
 * (+0xe4 = 1: further attacks find the kingdom busy) until the attacker's
 * 10 arrives; then an event of kind 0 with e[6] = 2 plays the battle
 * against its units here. */
static Uint8 s_attack[88];
static Uint64 s_defend_t;

const Uint8 *pk_arena_attack_msg(void) { return s_defending ? s_attack : NULL; }

void pk_arena_attack_done(pk_game *g)
{
    s_defending = 0;
    if (g->world.h_dc != 0xff && g->world.h_dc < PK_TEAMS) {
        g->world.teams[g->world.h_dc][12] = 0;
    }
}

static void defence_start(pk_game *g, const Uint8 *m)
{
    int i, ev;
    if (s_defending != 1 || SDL_strncmp((const char *)m + 4, (const char *)s_attack + 4, 16) != 0) {
        return;
    }
    for (i = 0; i < PK_EVENTS; i++) {
        if (g->world.events[i][1] != 0 && g->world.events[i][2] == 0) {
            pk_event_clear(g, i);
        }
    }
    ev = pk_event_start(g, 0, 0xff);
    if (ev == 0xff) {
        s_defending = 0;
        return;
    }
    g->world.events[ev][6] = 2;
    g->world.events[ev][0] = 0;
    g->world.events[ev][1] = 3; /* finished: the screens hand it on */
    s_defending = 2;
    PK_INFO("arena: %.16s attacks, the defense party fights", (const char *)m + 4);
}

/* another player attacks us (9): answer 137 with the defense party, or
 * 255 ("nobody home") when there is none (FUN_100669a8) */
static void answer_attack(pk_game *g, const Uint8 *in)
{
    Uint8 m[88];
    int team = g->world.h_dc;
    SDL_zeroa(m);
    m[0] = 137;
    m[1] = in[1];
    SDL_memcpy(m + 8, in + 4, 16);
    put_name(m + 24, pk_online_user());
    if (s_defending || team == 0xff || team >= PK_TEAMS || g->world.teams[team][13] == 0) {
        m[2] = 0xff;
    } else {
        SDL_memcpy(s_attack, in, 88);
        s_defending = 1;
        s_defend_t = SDL_GetTicks();
        g->world.teams[team][12] = 1;
        m[2] = (Uint8)g->world.h_d4;
        m[3] = (Uint8)g->world.teams[team][8];
        m[4] = (Uint8)g->world.h_4f4;
        put_units(g, m + 40, team);
    }
    pk_online_send(m, 88);
    PK_INFO("arena: attacked by %.16s (%s)", (const char *)in + 4, m[2] == 0xff ? "no defense" : "defending");
}

static void handle(pk_game *g, const Uint8 *m, int n)
{
    int i;
    switch (m[0]) {
    case 134: /* rivals in the area (0x10067f9c): kept for the search's
               * report, the waiting search (kind 4) ends */
        g_rival_count = 0;
        g_rival_area = g->world.current_area;
        for (i = 0; i < PK_RIVALS_MAX && 4 + i * 20 + 20 <= n; i++) {
            const Uint8 *e = m + 4 + i * 20;
            if (!e[0]) {
                break;
            }
            SDL_memcpy(g_rivals[g_rival_count].name, e, 16);
            g_rivals[g_rival_count].name[16] = 0;
            SDL_memcpy(g_rivals[g_rival_count].look, e + 16, 4);
            g_rival_count++;
        }
        pk_event_answered(g, 4, m[1]);
        PK_INFO("arena: %d rival kingdom(s) in area %d", g_rival_count, g_rival_area);
        break;
    case 7:
        if (n >= 36) {
            answer_scout(g, m);
        }
        break;
    case 9:
        if (n >= 88) {
            answer_attack(g, m);
        }
        break;
    case 10: /* someone else's 10 starts our defence; our own confirms our attack */
        if (n >= 36 && SDL_strncasecmp((const char *)m + 4, pk_online_user(), 16) != 0) {
            defence_start(g, m);
            break;
        }
        pk_arena_game_message(g, m, n);
        break;
    case 135:
    case 137:
        pk_arena_game_message(g, m, n);
        break;
    case 18: /* a chat message for us: from @4, text @36, messenger @115 */
        if (n >= 114 && s_inbox_n < 4) {
            SDL_memcpy(s_inbox[s_inbox_n].from, m + 4, 16);
            s_inbox[s_inbox_n].from[16] = 0;
            SDL_memcpy(s_inbox[s_inbox_n].text, m + 36, 78);
            s_inbox[s_inbox_n].text[78] = 0;
            s_inbox[s_inbox_n].messenger = n >= 116 ? m[115] : 0xff;
            s_inbox_n++;
            PK_INFO("arena: message from %.16s", (const char *)(m + 4));
        }
        break;
    case 146: /* our message: [2] 0 delivered */
        if (n >= 3 && m[2] != 0) {
            pk_help(g, 1, 0x25); /* the message could not be delivered */
        }
        break;
    case 141:
    case 142:
    case 143:
    case 144:
        pk_auction_message(m, n);
        break;
    case 255: /* the server sends us offline (FUN_100723e0: trigger 0x24) */
        pk_arena_go_offline(g);
        s_lost = 1;
        break;
    default:
        break;
    }
}

/* every tick while online (all modes) */
void pk_arena_tick(pk_game *g)
{
    Uint8 m[PK_ONLINE_MSG_MAX];
    int n;
    if (s_lost && !pk_help_active() && g->curtain == PK_CURTAIN_NONE) {
        s_lost = 0;
        pk_help(g, 1, 0x24); /* FUN_100723e0 sets +0x3b8, 0x1006f3c4 tells: we are offline now */
    }
    if (!g->arena_online) {
        return;
    }
    pk_online_update();
    if (pk_online_get_state() != PK_ONLINE_READY) {
        g->arena_online = 0;
        g_rival_count = 0;
        if (s_defending == 1) {
            pk_arena_attack_done(g);
        }
        pk_rivals_reset(g, true); /* FUN_100723e0 */
        s_lost = 1;
        return;
    }
    if (s_defending == 1 && SDL_GetTicks() - s_defend_t > 60000) {
        pk_arena_attack_done(g); /* the attacker's 10 never came */
    }
    if (++s_keepalive >= 165) {
        s_keepalive = 0;
        send_simple(2, 0, 20, 4, pk_arena_next_tag());
    }
    while ((n = pk_online_poll(m, sizeof(m))) > 0) {
        handle(g, m, n);
    }
}

/* ---- the connect screen (PK_MODE_ARENA_PENDING) -------------------
 * Port only: the original hands over to the phone's N-Gage Arena dialogs
 * here (ArenaFramework, FUN_10071fd0). The screen uses the title's
 * picture (bitmap 7, frames 0x100aff80: 0 the background, 7 the open
 * menu banner at 0x58, 0x9e) and its text colours. */
static void arena_blit(pk_game *g, int frame, int x, int y)
{
    int slot = pk_game_load_image(g, 7, 1);
    const pk_image *img = frame == 0 ? pk_gfx_image(PK_BANK_MAIN, 7, false) : pk_game_slot_image(g, slot);
    pk_frame f;
    pk_rect r;
    if (!img || !pk_app_frame(0x100aff80u, frame, &f)) {
        return;
    }
    r.x = f.sx;
    r.y = f.sy;
    r.w = f.w;
    r.h = f.h;
    pk_drawq_sprite(&g->drawq, img, &r, x + f.ox, y + f.oy, frame == 0 ? 0 : 1, 0);
}

static void arena_line(pk_game *g, const char *t, int y, pk_color c)
{
    pk_drawq_text_utf8(&g->drawq, t, 0x59, y + 1, 0, PK_ALIGN_CENTER, 1, PK_FONT_LEGEND);
    pk_drawq_text_utf8(&g->drawq, t, 0x58, y, c, PK_ALIGN_CENTER, 1, PK_FONT_LEGEND);
}

void pk_mode_arena(pk_game *g)
{
    char line[64];
    if (g->phase == 0) {
        pk_game_release_images(g);
        pk_objects_clear(g);
        g->bg_visible = 0;
        pk_game_set_curtain(g, PK_CURTAIN_OPEN);
        s_counter = 0;
        if (!pk_arena_has_account()) {
            s_state = 9;
        } else if (!pk_save_exists()) {
            s_state = 8;
        } else {
            s_state = 2; /* Play Now: Scrimmage or Ranked */
        }
        pk_game_set_phase(g, 1);
        return;
    }
    arena_blit(g, 0, 0, 0);
    arena_blit(g, 1, 0, 0);
    arena_blit(g, 7, 0x58, 0x9e);
    pk_game_mestext(g, 0x110, 0x59, 0x85, 0, PK_ALIGN_CENTER, 1, PK_FONT_LEGEND);
    pk_game_mestext(g, 0x110, 0x58, 0x84, 0xFFFFFFu, PK_ALIGN_CENTER, 1, PK_FONT_LEGEND);
    s_counter++;
    switch (s_state) {
    case 2:
    { /* the rooms of PhantomLink's Pocket Kingdom lobbies; kingdoms
       * only meet players of the same room */
        static const char *const k_room[2] = { "Scrimmage", "Ranked" };
        static int cur;
        int i;
        for (i = 0; i < 2; i++) {
            arena_line(g, k_room[i], 0x9c + i * 0x12, i == cur ? 0xFFFFFFu : 0xFFDC00u);
        }
        if (s_counter < 6) {
            break;
        }
        for (i = 0; i < 2; i++) { /* touch: a room */
            if (pk_tap(g, 0x10, 0x9c + i * 0x12 - 0xc, 0x90, 0x12)) {
                cur = i;
                g->key_code = PK_SC_5;
            }
        }
        if (g->key_code == PK_SC_UP || g->key_code == PK_SC_DOWN) {
            pk_audio_play_sfx(2);
            cur ^= 1;
        } else if (g->key_code == PK_SC_5) {
            pk_audio_play_sfx(0);
            pk_online_set_room(k_room[cur]);
            pk_online_start(s_acc.host, s_acc.user, s_acc.pass, pk_identity_imei());
            s_state = 1;
        } else if (g->key_code == PK_SC_BACK) {
            pk_audio_play_sfx(1);
            s_state = 10;
        }
        break;
    }
    case 1:
        pk_online_update();
        SDL_snprintf(line, sizeof(line), "%s", pk_online_status());
        pk_drawq_text_utf8(&g->drawq, line, 0x58, 0x9c, 0xFFDC00u, PK_ALIGN_CENTER, 1, PK_FONT_DENSE);
        pk_drawq_text_utf8(&g->drawq, s_acc.host, 0x58, 0xae, 0xB4B4B4u, PK_ALIGN_CENTER, 1, PK_FONT_DENSE);
        if (pk_online_get_state() == PK_ONLINE_READY) {
            if (pk_world_load(g)) {
                g->arena_online = 1;
                s_keepalive = 0;
                g_rival_count = 0;
                pk_arena_send_online(g);
                pk_game_set_mode(g, PK_MODE_FIELD);
            } else {
                pk_online_stop();
                s_state = 8;
            }
        } else if (pk_online_get_state() == PK_ONLINE_FAILED) {
            s_state = 7;
        } else if (g->key_code == PK_SC_BACK) {
            pk_online_stop();
            s_state = 10;
        }
        break;
    case 7:
        pk_drawq_text_utf8(&g->drawq, "Could not connect:", 0x58, 0x9c, 0xFFDC00u, PK_ALIGN_CENTER, 1, PK_FONT_DENSE);
        pk_drawq_text_utf8(&g->drawq, pk_online_status(), 0x58, 0xab, 0xFFFFFFu, PK_ALIGN_CENTER, 1, PK_FONT_DENSE);
        goto wait_key;
    case 8:
        pk_drawq_text_utf8(&g->drawq, "Play Offline first to found", 0x58, 0x9c, 0xFFDC00u, PK_ALIGN_CENTER, 1,
                           PK_FONT_DENSE);
        pk_drawq_text_utf8(&g->drawq, "your kingdom.", 0x58, 0xab, 0xFFDC00u, PK_ALIGN_CENTER, 1, PK_FONT_DENSE);
        goto wait_key;
    case 9:
        pk_drawq_text_utf8(&g->drawq, "No Arena account set.", 0x58, 0x98, 0xFFDC00u, PK_ALIGN_CENTER, 1,
                           PK_FONT_DENSE);
        pk_drawq_text_utf8(&g->drawq, "Start with --arena-host,", 0x58, 0xa7, 0xFFFFFFu, PK_ALIGN_CENTER, 1,
                           PK_FONT_DENSE);
        pk_drawq_text_utf8(&g->drawq, "--arena-user, --arena-pass", 0x58, 0xb6, 0xFFFFFFu, PK_ALIGN_CENTER, 1,
                           PK_FONT_DENSE);
    wait_key:
        if (s_counter > 10 && (g->key_code == PK_SC_5 || g->key_code == PK_SC_BACK)) {
            s_state = 10;
        }
        break;
    default: /* back to the title */
        g->entry_reason = 0;
        g->skip_intro = true;
        pk_game_set_mode(g, PK_MODE_BOOT);
        break;
    }
}

/* ---- chat (Map > Search > Chat; message 18, FUN_10066f14) --------- */
static struct
{
    int replying;
    char to[17]; /* the recipient, empty when the base cannot be written to */
    int team;    /* the messenger party (+0xec) */
} s_chat;
static char s_chat_text[80];

bool pk_chat_pending(void)
{
    return s_inbox_n > 0;
}

/* Map > Search > Chat: the kingdom of the base chosen (NULL for an enemy
 * base, which cannot be written to) and the party that carries the
 * letter */
void pk_chat_compose(const char *to, int team)
{
    SDL_strlcpy(s_chat.to, to ? to : "", sizeof(s_chat.to));
    s_chat.team = team;
    s_chat_text[0] = 0;
    s_chat.replying = 1;
}

static void chat_send(pk_game *g, const char *to, const char *text)
{
    Uint8 m[116];
    int team = s_chat.team, lead;
    SDL_zeroa(m);
    m[0] = 18;
    m[1] = (Uint8)(++s_seq);
    m[2] = (Uint8)g->world.current_area;
    put_name(m + 4, pk_online_user());
    put_name(m + 20, to);
    SDL_strlcpy((char *)m + 36, text, 79);
    m[115] = 0xff; /* the messenger: the leader of the party (+0xec) */
    if (team >= 0 && team < PK_TEAMS && (lead = g->world.teams[team][9]) >= 0 && lead < 4 &&
        g->world.teams[team][lead] != 0xff && g->world.teams[team][lead] < PK_UNIT_MAX) {
        m[115] = (Uint8)g->world.units[g->world.teams[team][lead]][0];
    }
    pk_online_send(m, 116);
    PK_INFO("arena: message to %s", to);
}

/* the letter screen (FUN_10031a54, write state 0x10032114): image 0x4a,
 * frames 0x1008e698: 0x10 the paper (176 x 208) at (0x58, 0x68), the edge
 * pieces 8 / 3 / 4 / 0xc, the text field 0x20 at (0xb, 0xe) and the marks
 * 0x21..0x24 under it (each + 5 while the field is not being typed in), the
 * Send button (image 0x17, frames 0x1008eaa0, 0xb / 0xc) at (0x17, 0x84),
 * the panel 0x7a, the cursor at (0xd, 0x14) or (0x17, 0x89), and the
 * messenger's portrait (frames 0x1008fd30) at (0x8e, 0x96 / 0xaf) with
 * mestext 0x152. Reading a letter (0x10032114) shows the sender at
 * (0xd, 0xad) and 0x150 on the button; writing (0x10031e9c) shows 0x151.
 * The original uses a native text editor at (0x10, 0x1c), 0x8b wide,
 * seven lines. The recipient is the base chosen first (Map > Search >
 * Chat: 0x100414e8) or the letter's sender, shown where the sender's name
 * goes; the party chosen next (0x1004faec) carries it. */
static void chat_obj_update(pk_object *o, pk_game *g)
{
    pk_object *root = pk_ui_root(g);
    bool typing = pk_input_text_active();
    if (!root || root->update != pk_chat_update || root->state >= 9) {
        pk_object_kill(o);
        return;
    }
    switch (o->state) {
    case 1: /* pieces of image 0x4a */
        pk_ui_setup_sprite(g, o, 0x4a, 0x1008e698u, o->arg1 == 0x10 ? 0xc1 : 0x4c1);
        o->frame = o->arg1 >= 0x20 && !typing ? o->arg1 + 5 : o->arg1;
        o->layer = o->arg1 == 0x10 ? 0 : 1;
        break;
    case 2: /* the Send button */
        pk_ui_setup_sprite(g, o, 0x17, 0x1008eaa0u, 0x4c1);
        o->frame = typing ? 0xb : 0xc;
        o->layer = 1;
        break;
    case 3: /* the cursor */
        if (!o->work[0]) {
            pk_ui_setup_sprite(g, o, 0x17, 0x1008eaa0u, 0x4c3);
            pk_ui_cursor_anim(g, o);
            o->work[0] = 1;
        }
        pk_object_animate(o);
        o->x = typing ? 0xd : 0x17;
        o->y = typing ? 0x14 : 0x89;
        o->layer = 1;
        break;
    case 4: /* the messenger's portrait: name plate (arg0 0) and face (1) */
        o->image = pk_game_load_image(g, PK_PORTRAIT_BASE + o->arg1 * 2, 1);
        pk_object_set_frames(o, 0x1008fd30u);
        o->flags = 0x4c1;
        o->frame = o->arg0;
        o->layer = 1;
        if (o->arg0 == 0) {
            pk_game_mestext(g, 0x152, 0x76, 0xbe, 0, PK_ALIGN_RIGHT, 1, PK_FONT_DENSE);
            pk_game_mestext(g, 0x152, 0x75, 0xbd, 0xC0C0C0u, PK_ALIGN_RIGHT, 1, PK_FONT_DENSE);
        }
        break;
    default:
        break;
    }
}

static int chat_leader(pk_game *g)
{
    int team = s_chat.team, lead;
    if (team >= 0 && team < PK_TEAMS && (lead = g->world.teams[team][9]) >= 0 && lead < 4 &&
        g->world.teams[team][lead] != 0xff && g->world.teams[team][lead] < PK_UNIT_MAX) {
        return g->world.units[g->world.teams[team][lead]][0];
    }
    return 0xff;
}

/* the typed text in the field, wrapped at words within 0x8b pixels,
 * seven lines (the native editor's look) */
static void chat_draw_text(pk_game *g, const char *t, bool caret)
{
    Uint16 all[100], line[100];
    int n = 0, len, start = 0, lines = 0, lh = pk_font_height(PK_FONT_DENSE);
    int y = 0x1c + pk_font_ascent(PK_FONT_DENSE);
    pk_text_from_utf8(t, all, 96);
    len = 0;
    while (all[len]) {
        len++;
    }
    if (caret && (SDL_GetTicks() / 400) % 2 == 0) {
        all[len++] = '_';
        all[len] = 0;
    }
    while (start < len && lines < 7) {
        int end = start, brk = -1;
        while (end < len) {
            if (pk_font_text_width(PK_FONT_DENSE, all + start, end - start + 1) > 0x8b) {
                break;
            }
            if (all[end] == ' ') {
                brk = end;
            }
            end++;
        }
        if (end < len && brk >= start) {
            end = brk + 1; /* at the last space */
        }
        for (n = 0; n < end - start; n++) {
            line[n] = all[start + n];
        }
        line[n] = 0;
        pk_drawq_text(&g->drawq, line, 0x10, y, 0, PK_ALIGN_LEFT, 1, PK_FONT_DENSE);
        y += lh;
        lines++;
        start = end > start ? end : start + 1;
    }
}

void pk_chat_update(pk_object *o, pk_game *g)
{
    switch (o->state) {
    case 0:
        if (g->curtain == PK_CURTAIN_NONE) {
            pk_game_set_curtain(g, PK_CURTAIN_CLOSE);
        }
        if (g->curtain != PK_CURTAIN_HELD) {
            break;
        }
        pk_object_kill_others(g, o);
        g->bg_visible = 0;
        pk_audio_play_sfx(0x22);
        s_reading = s_inbox_n > 0 && !s_chat.replying; /* a received letter first (read state) */
        {
            int lead = s_reading ? s_inbox[0].messenger : chat_leader(g);
            pk_object_spawn(g, chat_obj_update, 0, 1, 0, 0x10, 0x58, 0x68);
            pk_object_spawn(g, chat_obj_update, 0, 1, 0, 8, 0x58, 0);
            pk_object_spawn(g, chat_obj_update, 0, 1, 0, 3, 0, 0x68);
            pk_object_spawn(g, chat_obj_update, 0, 1, 0, 4, 0xb0, 0x68);
            pk_object_spawn(g, chat_obj_update, 0, 1, 0, 0xc, 0x58, 0xd0);
            pk_object_spawn(g, chat_obj_update, 0, 1, 0, 0x20, 0xb, 0xe);
            pk_object_spawn(g, chat_obj_update, 0, 1, 0, 0x21, 0xb, 0x76);
            pk_object_spawn(g, chat_obj_update, 0, 1, 0, 0x22, 0x6f, 0x76);
            pk_object_spawn(g, chat_obj_update, 0, 1, 0, 0x24, 0x47, 0x76);
            pk_object_spawn(g, chat_obj_update, 0, 1, 0, 0x23, 0x5f, 0x76);
            pk_object_spawn(g, chat_obj_update, 0, 2, 0, 0xb, 0x17, 0x84);
            pk_object_spawn(g, pk_kingdom_ui_update, o->dir, 0x80, 0, 0x7a, 0, 0x97);
            pk_object_spawn(g, chat_obj_update, 0, 3, 0, 0, 0x14, 0x14);
            if (lead != 0xff) {
                pk_object_spawn(g, chat_obj_update, 0, 4, 0, lead, 0x8e, 0x96);
                pk_object_spawn(g, chat_obj_update, 0, 4, 1, lead, 0x8e, 0xaf);
            }
        }
        pk_game_set_curtain(g, PK_CURTAIN_OPEN);
        o->counter = 0;
        o->state = 1;
        break;
    case 10: /* back from the party list */
        g->list_120 = 0;
        if (g->dlg_result == 0) {
            s_chat.team = g->list_top + g->cursor;
            o->state = 0;
        } else {
            s_chat.replying = 0;
            pk_game_set_curtain(g, PK_CURTAIN_CLOSE);
            o->state = 9;
        }
        break;
    case 1:
    { /* writing (0x10032114 state 1) */
        bool typing = pk_input_text_active();
        if (s_reading) { /* a letter: its text, the sender, 0x151 on the button */
            pk_drawq_text_utf8(&g->drawq, s_inbox[0].from, 0xe, 0xae, 0, PK_ALIGN_LEFT, 1, PK_FONT_DENSE);
            pk_drawq_text_utf8(&g->drawq, s_inbox[0].from, 0xd, 0xad, 0xFFFFFFu, PK_ALIGN_LEFT, 1, PK_FONT_DENSE);
            pk_game_mestext(g, 0x150, 0x3c, 0x90, 0, PK_ALIGN_CENTER, 1, PK_FONT_DENSE);
            chat_draw_text(g, s_inbox[0].text, false);
            if (g->curtain != PK_CURTAIN_NONE || ++o->counter < 6) {
                break;
            }
            if (pk_tap(g, 0x17, 0x84, 0x4a, 0x16)) {
                g->key_code = PK_SC_5; /* touch: Reply */
            }
            if (g->key_code == PK_SC_5 || g->key_code == PK_SC_BACK) {
                bool reply = g->key_code == PK_SC_5;
                if (reply) { /* the reply goes to the sender, also one not seen in the area */
                    SDL_strlcpy(s_chat.to, s_inbox[0].from, sizeof(s_chat.to));
                }
                SDL_memmove(s_inbox, s_inbox + 1, sizeof(s_inbox[0]) * 3);
                s_inbox_n--;
                pk_audio_play_sfx(reply ? 0 : 1);
                if (reply) { /* the messenger party (0x1004faec), then the
                              * writing screen (0x10031e9c): states 10, 0 */
                    s_chat_text[0] = 0;
                    s_chat.replying = 1;
                    pk_object_kill_others(g, o);
                    g->bg_visible = 1;
                    g->slide = 0;
                    g->list_top = g->cursor = 0;
                    g->list_120 = 1;
                    g->list_124 = 0;
                    pk_object_call(g, o, pk_team_list_update, 0, pk_chat_update, 10);
                } else {
                    pk_game_set_curtain(g, PK_CURTAIN_CLOSE);
                    o->state = 9;
                }
            }
            break;
        }
        if (s_chat.to[0]) { /* the recipient */
            pk_drawq_text_utf8(&g->drawq, s_chat.to, 0xe, 0xae, 0, PK_ALIGN_LEFT, 1, PK_FONT_DENSE);
            pk_drawq_text_utf8(&g->drawq, s_chat.to, 0xd, 0xad, 0xFFFFFFu, PK_ALIGN_LEFT, 1, PK_FONT_DENSE);
        }
        pk_game_mestext(g, 0x151, 0x3c, 0x90, 0, PK_ALIGN_CENTER, 1, PK_FONT_DENSE);
        chat_draw_text(g, typing ? pk_input_text() : s_chat_text, typing);
        if (g->curtain != PK_CURTAIN_NONE || ++o->counter < 4 || pk_help_active()) {
            break;
        }
        if (o->counter == 4) {
            pk_input_text_begin(78); /* the editor takes the keys (0x10031e9c) */
        }
        { /* touch: the paper is the text (keyboard), the button sends */
            bool tb = pk_tap(g, 0x17, 0x84, 0x4a, 0x16), tt = pk_tap(g, 0xb, 0xe, 0x9a, 0x68);
            if (typing) {
                if (tt) {
                    pk_input_text_show();
                }
                if (tb) {
                    SDL_strlcpy(s_chat_text, pk_input_text(), sizeof(s_chat_text));
                    pk_input_text_end();
                    typing = false;
                    g->key_code = PK_SC_5;
                }
            } else {
                if (tt) {
                    pk_input_text_begin(78);
                    pk_input_text_append(s_chat_text);
                }
                if (tb) {
                    g->key_code = PK_SC_5;
                }
            }
        }
        if (typing) {
            SDL_strlcpy(s_chat_text, pk_input_text(), sizeof(s_chat_text));
            if (g->key_code == PK_SC_UP || g->key_code == PK_SC_DOWN || g->key_code == PK_SC_5) {
                pk_audio_play_sfx(2);
                pk_input_text_end(); /* to the Send button */
            } else if (g->key_code == PK_SC_BACK) {
                pk_input_text_end();
                pk_audio_play_sfx(1);
                pk_game_set_curtain(g, PK_CURTAIN_CLOSE);
                o->state = 9;
            }
            break;
        }
        if (g->key_code == PK_SC_UP || g->key_code == PK_SC_DOWN) {
            pk_audio_play_sfx(2);
            pk_input_text_begin(78);
            pk_input_text_append(s_chat_text);
        } else if (g->key_code == PK_SC_5 && s_chat_text[0]) {
            /* map state 0x23: sent only to a rival kingdom */
            if (s_chat.to[0]) {
                chat_send(g, s_chat.to, s_chat_text);
            }
            s_chat_text[0] = 0;
            pk_audio_play_sfx(0);
            pk_game_set_curtain(g, PK_CURTAIN_CLOSE);
            o->state = 9;
        } else if (g->key_code == PK_SC_BACK) {
            pk_audio_play_sfx(1);
            pk_game_set_curtain(g, PK_CURTAIN_CLOSE);
            o->state = 9;
        } else if (g->key_code == PK_SC_5) {
            pk_audio_play_sfx(4);
        }
        break;
    }
    default:
        if (g->curtain != PK_CURTAIN_HELD) {
            break;
        }
        if (pk_input_text_active()) {
            pk_input_text_end();
        }
        pk_object_kill_others(g, o);
        s_chat.replying = 0;
        g->bg_visible = 1;
        g->dlg_result = 1;
        o->counter = 0;
        pk_object_return(g, o);
        break;
    }
}
