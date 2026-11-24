/* SPDX-License-Identifier: MIT */
/* Battles: record setup (FUN_1000c738, FUN_1000cc40), mode 0
 * (FUN_10068db0), the battle controller (0x10000824), the HUD, traps and
 * the camera. The units themselves (the dispatcher 0x10020f3c, the 49
 * class behaviours, attack boxes and hit detection) are in
 * pk_battle_unit.c. */
#include "game/pk_battle.h"
#include "audio/pk_audio.h"
#include "core/pk_app.h"
#include "core/pk_fs.h"
#include "core/pk_log.h"
#include "game/pk_arena_game.h"
#include "game/pk_battle_unit.h"
#include "game/pk_data.h"
#include "game/pk_help.h"
#include "game/pk_rules.h"
#include "game/pk_scenes.h"
#include "game/pk_world.h"
#include "gfx/pk_font.h"
#include "input/pk_input.h"

#include <SDL3/SDL.h>

pk_battle pk_b;

/* the record is laid out like the original's (offsets used by the unit code) */
#include <stddef.h>
typedef char pk_b_at_0x450[offsetof(pk_battle, target) == 0x450 ? 1 : -1];
typedef char pk_b_at_0x4a8[offsetof(pk_battle, eq) == 0x4a8 ? 1 : -1];
typedef char pk_b_at_0x83c[offsetof(pk_battle, status) == 0x83c ? 1 : -1];
typedef char pk_b_at_0x86c[offsetof(pk_battle, weight) == 0x86c ? 1 : -1];
typedef char pk_b_at_0x964[offsetof(pk_battle, field) == 0x964 ? 1 : -1];
typedef char pk_b_at_0x978[offsetof(pk_battle, trap) == 0x978 ? 1 : -1];
typedef char pk_b_at_0x99c[offsetof(pk_battle, won) == 0x99c ? 1 : -1];
typedef char pk_b_at_0xa00[offsetof(pk_battle, started) == 0xa00 ? 1 : -1];

#define OBJ_CONTROLLER 0xc3
#define OBJ_HUD        0xc6 /* port: HUD pieces (own type, so lookups of the controller stay unique) */
#define OBJ_ATTACK     0xc0
#define IMG_BANNER     0x5e
#define TBL_BANNER     0x1007da64u
#define FIELD_LEFT     0x14
#define FIELD_RIGHT    0x14c
#define GROUND_Y       0x99

/* ------------------------------------------------------------------ */
/* record helpers: a unit object's arg0 is its screen side, arg1 slot */

static int rside(int screen_side) { return screen_side == pk_b.player_side ? 0 : 1; }
static int *urec(const pk_object *o) { return pk_b.rec[rside(o->arg0)][o->arg1 & 3]; }

#define IS_UNIT(o) pk_bu_is_unit(o)

static pk_object *unit_object(pk_game *g, int screen_side, int slot)
{
    int *r = pk_b.rec[rside(screen_side)][slot];
    if (r[0] == 0xff) {
        return NULL;
    }
    return pk_object_find(g, r[0], screen_side, slot);
}

static void fx_update(pk_object *o, pk_game *g);
#include "game/pk_battle_fx.inc"

/* ------------------------------------------------------------------ */
/* record setup */

void pk_battle_load_formation(pk_game *g, int area, int level, int slot)
{
    Uint8 b[0x20];
    size_t n = 0;
    Uint8 *p = (Uint8 *)pk_fs_load("partydata.dat", &n);
    size_t off = (size_t)(level * 0x20 + area * 0x120 + 0x10);
    int *f = g->world.formations[slot], i;
    if (!p || off + sizeof(b) > n) {
        SDL_free(p);
        return;
    }
    SDL_memcpy(b, p + off, sizeof(b));
    SDL_free(p);
    f[0] = b[0];
    f[1] = b[1];
    f[2] = b[2];
    for (i = 0; i < 4; i++) {
        f[3 + i] = b[3 + i * 7];
        f[11 + i] = b[4 + i * 7];
        f[15 + i] = b[5 + i * 7];
        f[19 + i] = b[6 + i * 7];
        f[23 + i] = b[7 + i * 7];
        f[27 + i] = b[8 + i * 7];
        f[31 + i] = b[9 + i * 7];
    }
    g->world.h_450++;
    for (i = 0; i < 4; i++) {
        if (f[3 + i] != 0xff) {
            pk_unit_template(f[3 + i], 10);
            f[39 + i] = f[35 + i] = pk_r.unit[10].b[4];
        }
    }
}

static void action_weights(int type, int *w) /* FUN_1007b604 */
{
    size_t n = 0;
    Uint8 *p = (Uint8 *)pk_fs_load("action.dat", &n);
    size_t off = (size_t)(type * 8 + 8);
    int i;
    for (i = 0; i < 5; i++) {
        w[i] = (p && off + 5 <= n) ? p[off + i] : 20;
    }
    SDL_free(p);
}

/* the template's words from +0x180 form a unit record (FUN_1007abf4) */
static void template_record(int *r)
{
    const pk_unit_info *t = &pk_r.unit[10];
    SDL_memset(r, 0, sizeof(int) * 23);
    r[0] = t->type;
    r[2] = r[3] = 0xff;
    r[5] = t->b[5];
    r[6] = t->b[6];
    r[7] = r[8] = t->b[4];
    r[9] = t->b[7];
    r[10] = t->b[8];
    r[11] = t->b[9];
    r[12] = t->b[10];
    r[13] = t->b[11];
    r[14] = t->b[1];
    r[15] = t->b[0];
    r[16] = t->b[2];
    r[17] = t->b[3];
    r[19] = 0xffff;
    r[20] = t->b[14];
}

/* FUN_10069764: the empty record, all six slots of both sides (slots 4
 * and 5 stay free for summoned units) */
static void battle_reset(void)
{
    static const int k_rec[23] = {
        0xff, 0, 0xff, 0xff, 0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0, 0xffff, 0, 0xff, 0
    };
    static const int k_eq[13] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0, 0xff, 0, 0, 0, 0xff, 0, 0xff };
    int s, i;
    SDL_zero(pk_b);
    for (s = 0; s < 2; s++) {
        for (i = 0; i < 6; i++) {
            SDL_memcpy(pk_b.rec[s][i], k_rec, sizeof(k_rec));
            SDL_memcpy(pk_b.eq[s][i], k_eq, sizeof(k_eq));
            pk_b.target[s][i] = 5;
        }
    }
    pk_b.team = pk_b.formation = 0xff;
    pk_b.leader[0] = pk_b.leader[1] = 0xff;
    for (i = 0; i < 12; i++) {
        pk_b.r718[60 + i] = 0xff; /* +0x808 / +0x820 */
    }
    pk_b.field = 0xff;
    pk_b.trap = 0xff;
    pk_b.r97c[0] = pk_b.r97c[1] = 0xff;
}

void pk_battle_setup(pk_game *g, int team, int formation, int player_side, int mode)
{
    int i, s, first = 4;
    int *fm = g->world.formations[formation & 15];
    battle_reset();
    pk_b.area = g->world.current_area;
    pk_b.team = team;
    pk_b.formation = formation;
    if (team != 0xff) {
        const int *tm = g->world.teams[team & 7];
        for (i = 0; i < 4; i++) {
            int u = tm[i];
            if (u == 0xff) {
                continue;
            }
            if (i < first) {
                first = i;
            }
            SDL_memcpy(pk_b.rec[0][i], g->world.units[u], sizeof(pk_b.rec[0][i]));
            action_weights(g->world.units[u][0], pk_b.weight[0][i]);
            if (pk_b.rec[0][i][2] != 0xff) {
                SDL_memcpy(pk_b.eq[0][i], g->world.equip[pk_b.rec[0][i][2]], sizeof(pk_b.eq[0][i]));
            }
        }
        pk_b.leader[0] = (tm[9] == 0xff || pk_b.rec[0][tm[9] & 3][0] == 0xff) ? first : tm[9];
    }
    pk_b.trap = player_side == 1 ? (g->world.h_4f4 != 0xff ? g->world.h_4f4 : 0xff) : fm[2];
    for (i = 0; i < 4; i++) {
        int t = fm[3 + i];
        if (t == 0xff) {
            continue;
        }
        if (i < first) {
            first = i;
        }
        pk_unit_template(t, 10);
        action_weights(t, pk_b.weight[1][i]);
        template_record(pk_b.rec[1][i]);
        pk_b.rec[1][i][7] = fm[35 + i];
        pk_b.rec[1][i][1] = fm[7 + i];
        if (fm[11 + i] != 0xff) {
            pk_item_preview(fm[11 + i], fm[15 + i], 10);
            SDL_memcpy(pk_b.eq[1][i], pk_r.equip[10].rec, sizeof(pk_b.eq[1][i]));
            pk_b.eq[1][i][3] = fm[19 + i];
            pk_b.eq[1][i][4] = fm[23 + i];
            pk_b.eq[1][i][2] = fm[27 + i];
        }
    }
    if (g->boss_battle == 1) {
        pk_b.leader[1] = (int)pk_app_i32(0x1008000cu + (Uint32)pk_b.area * 4u);
        pk_b.time = 0x441;
    } else {
        pk_b.leader[1] = first;
        pk_b.time = 0x14a;
    }
    if (team != 0xff) {
        pk_b.order[0] = g->world.teams[team & 7][8];
        pk_b.order[1] = fm[1];
        for (i = 0; i < 4; i++) {
            pk_b.target[0][i] = g->world.teams[team & 7][4 + i];
            pk_b.target[1][i] = fm[31 + i];
        }
    }
    for (i = 0; i < 4; i++) {
        if (pk_b.rec[0][i][0] != 0xff && pk_b.rec[0][i][22] == 0) {
            pk_b.hp_total[0] += pk_b.rec[0][i][7];
        }
        if (pk_b.rec[1][i][0] != 0xff && pk_b.rec[1][i][22] == 0) {
            pk_b.hp_total[1] += pk_b.rec[1][i][7];
        }
    }
    pk_b.player_side = player_side;
    pk_b.mode = mode;
    pk_b.field = player_side == 0 ? fm[0] : g->world.h_d4;
    /* FUN_1000de6c: weapons add attack, armour defense */
    for (s = 0; s < 2; s++) {
        for (i = 0; i < 4; i++) {
            if (pk_b.rec[s][i][0] == 0xff) {
                continue;
            }
            if (pk_b.eq[s][i][0] == 5) {
                pk_b.rec[s][i][5] += pk_b.eq[s][i][7];
            } else if (pk_b.eq[s][i][0] == 6) {
                pk_b.rec[s][i][6] += pk_b.eq[s][i][8];
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* controller (0x10000824) */

static int alive_all_down(void) /* FUN_10004ccc */
{
    int i, n0 = 0, d0 = 0, n1 = 0, d1 = 0;
    for (i = 0; i < 4; i++) {
        if (pk_b.rec[0][i][0] != 0xff) {
            n0++;
            d0 += pk_b.rec[0][i][22] != 0;
        }
        if (pk_b.rec[1][i][0] != 0xff) {
            n1++;
            d1 += pk_b.rec[1][i][22] != 0;
        }
    }
    return (d0 == n0 ? 1 : 0) | (d1 == n1 ? 0x10 : 0);
}

static int hp_percent(int s) /* FUN_10004e80 (side 1 = team) */
{
    int i, sum = 0, total = s ? pk_b.hp_total[0] : pk_b.hp_total[1];
    const int(*rec)[23] = s ? pk_b.rec[0] : pk_b.rec[1];
    for (i = 0; i < 4; i++) {
        if (rec[i][0] != 0xff) {
            sum += rec[i][7];
        }
    }
    if (total <= 0) {
        return 0;
    }
    sum = sum * 100 / total;
    return SDL_clamp(sum, 0, 100);
}

static void banner_update(pk_object *o, pk_game *g)
{
    (void)g;
    switch (o->state) {
    case 0:
        o->image = pk_game_image_slot(g, IMG_BANNER);
        pk_object_set_frames(o, TBL_BANNER);
        o->frame = o->arg1 == 0 ? 0x23 : o->arg1 == 1 ? 0x24
                                                      : 0x69;
        o->layer = 1;
        o->flags = 0x4c1;
        o->x -= 0xb0;
        o->work[0] = 0;
        o->state = 1;
        break;
    case 1:
        if (o->work[0] == 0) {
            o->x += 0x2c;
            if (o->x >= 0x58) {
                o->x = 0x58;
                o->work[0] = 1;
                o->counter = 0;
            }
        } else if (o->work[0] == 1) {
            if (++o->counter >= 0xb) {
                o->work[0] = 2;
            }
        } else {
            o->x += 0x2c;
            if (o->x >= 0x108) {
                pk_object_kill(o);
            }
        }
        break;
    default:
        break;
    }
}

/* FUN_100051a8, top bar: one name, bold (font 0), pass 1, unless the
 * curtain is closing. A normal battle shows the player's team leader
 * (record side 0, slot +0x4a0; its name from the unit's name index +0x4c,
 * loaded into the template slot by the controller): red at (0xc, 0xc)
 * when the player fights on the left, blue right-aligned at (0xa4, 0xc)
 * when on the right. A battle against a rival kingdom (formation word 47)
 * centres a name at (0x58, 0xc): the attacker's (red) when the player
 * defends, the rival's (blue) when the player attacks. Colours: TRgb
 * 0x1030f7 and 0xf7b372. */
static void draw_team_name(pk_game *g)
{
    const int *fm = g->world.formations[pk_b.formation & 15];
    Uint16 name[25];
    int i, x, align;
    pk_color c;
    name[0] = 0;
    if (g->curtain == PK_CURTAIN_CLOSE) {
        return;
    }
    if (fm[47] == 0) {
        int l = pk_b.leader[0];
        const int *r;
        if (l < 0 || l > 5) {
            return;
        }
        r = pk_b.rec[0][l];
        if (r[19] == 0xffff || r[19] == 0xff) {
            return;
        }
        pk_data_kingdom_name(r[19], name, 25);
        if (pk_b.player_side == 1) {
            x = 0xa4;
            align = PK_ALIGN_RIGHT;
            c = 0x72b3f7u;
        } else {
            x = 0xc;
            align = PK_ALIGN_LEFT;
            c = 0xf73010u;
        }
    } else {
        const char *n = NULL;
        Uint8 tmp[17];
        if (pk_b.player_side == 1) {
            const Uint8 *am = pk_arena_attack_msg();
            if (am) {
                SDL_memcpy(tmp, am + 4, 16);
                tmp[16] = 0;
                n = (const char *)tmp;
            }
            c = 0xf73010u;
        } else {
            n = pk_map_rival_name();
            c = 0x72b3f7u;
        }
        for (i = 0; n && n[i] && i < 24; i++) {
            name[i] = (Uint8)n[i];
        }
        name[i] = 0;
        x = 0x58;
        align = PK_ALIGN_CENTER;
    }
    if (name[0]) {
        pk_drawq_text(&g->drawq, name, x, 0xc, c, (pk_align)align, 1, PK_FONT_LEGEND);
    }
}

static void draw_hud(pk_game *g)
{
    draw_team_name(g);
    if (pk_b.special_msg > 0) {
        /* FUN_100051a8: while game+0x45c is set and game+0x460 > 1, the
         * speaking unit's name right-aligned, bold, in white with a black shadow
         * on the bar under the box (0x7e, 0xca), its line from
         * battlemes.dat (FUN_1000ea00: group * 13 + record word 20, split at
         * line feeds) in black at x 6 from y 0xab, 13 px apart; a boss's
         * first line goes on the strip above the box (6, 0x9e) */
        const int *r = pk_b.rec[pk_b.special_rs][pk_b.special_slot];
        Uint16 name[25], line[4][40];
        const Uint16 *t;
        int n = 0, k = 0, i, first = 0, y = 0xab;
        name[0] = 0;
        if (pk_b.special_msg > 1 && r[0] != 0xff) {
            pk_unit_template(r[0], 6);
            if (r[19] != 0xffff && r[19] != 0xff) {
                pk_data_kingdom_name(r[19], name, 25);
            } else {
                SDL_memcpy(name, pk_r.unit[6].name, sizeof(name));
            }
            t = pk_data_text(PK_TBL_BATTLEMES, pk_b.panel_kind * 13 + (r[20] >= 0 && r[20] < 13 ? r[20] : 0));
            for (i = 0; i < 4; i++) {
                line[i][0] = 0;
            }
            for (i = 0; t && t[i] && t[i] != 0x1b && n < 4; i++) {
                if (t[i] == '\n') {
                    n++;
                    k = 0;
                    continue;
                }
                if (t[i] == '\r' || k >= 39) {
                    continue;
                }
                line[n][k++] = t[i];
                line[n][k] = 0;
            }
            if (k > 0 && n < 4) {
                n++;
            }
            if (g->boss_battle == 1 && n > 0) {
                pk_drawq_text(&g->drawq, line[0], 6, 0x9e, 0, PK_ALIGN_LEFT, 1, PK_FONT_DENSE);
                first = 1;
            }
            pk_drawq_text(&g->drawq, name, 0x7e, 0xca, 0, PK_ALIGN_RIGHT, 1, PK_FONT_LEGEND);
            pk_drawq_text(&g->drawq, name, 0x7d, 0xc9, 0xffffffu, PK_ALIGN_RIGHT, 1, PK_FONT_LEGEND);
            for (i = first; i < n && y < 0xc5; i++, y += 0xd) {
                pk_drawq_text(&g->drawq, line[i], 6, y, 0, PK_ALIGN_LEFT, 1, PK_FONT_DENSE);
            }
        }
        pk_b.special_msg--;
    }
}

/* FUN_10009c2c: the speaker panel (kind 2 special move, 3 down, 6 boss) */
void pk_battle_panel(pk_game *g, const pk_object *o, int kind)
{
    (void)g;
    if (!o) {
        return;
    }
    pk_b.special_msg = 0x21;
    pk_b.special_rs = kind == 6 ? 1 : rside(o->arg0);
    pk_b.special_slot = o->arg1 & 0xff;
    pk_b.panel_kind = kind;
}

void pk_battle_template_record(int *r) { template_record(r); }
void pk_battle_action_weights(int type, int *w) { action_weights(type, w); }

pk_object *pk_battle_fx(pk_game *g, int state, int a0, int a1, int x, int y)
{
    return pk_object_spawn(g, fx_update, OBJ_FX, state, a0, a1, x, y);
}

/* port: horizontal swipe distance (game pixels) still to be panned, so a
 * swipe pans at the original's 8 px a tick like the held key */
static int s_swipe_pan;

/* FUN_100049e0: the camera (game+0xa8 mode, +0xac / +0xb0 the point to
 * show). Mode 0 hands it to the player (8): the held left / right key
 * pans 8 px a tick between 0 and 0xb0. Mode 7 jumps to the point, mode 5
 * pans there 12 px a tick; both then report 6, which special moves wait
 * for. Port: in mode 8 a horizontal swipe pans the same way (the field
 * follows the finger: a swipe to the left shows more to the right). */
static void camera_follow(pk_game *g)
{
    int *mode = &pk_b.cam[1], t;
    if (*mode != 8 || !pk_b.started) {
        s_swipe_pan = 0;
    }
    switch (*mode) {
    case 0:
        *mode = 8;
        break;
    case 5:
    case 7:
        t = SDL_clamp(pk_b.cam[2] - 0x58, 0, 0xb0);
        if (*mode == 5 && t < g->cam_x) {
            g->cam_x -= 0xc;
            if (t < g->cam_x) {
                break;
            }
        } else if (*mode == 5 && g->cam_x < t) {
            g->cam_x += 0xc;
            if (g->cam_x < t) {
                break;
            }
        }
        g->cam_x = t;
        *mode = 6;
        break;
    case 8:
        if (!pk_b.started) {
            break;
        }
        s_swipe_pan += pk_swipe_x(g) * PK_TOUCH_STEP_X;
        if (pk_input_held(PK_KEY_LEFT)) {
            s_swipe_pan = 0;
            g->cam_x = SDL_max(0, g->cam_x - 8);
        } else if (pk_input_held(PK_KEY_RIGHT)) {
            s_swipe_pan = 0;
            g->cam_x = SDL_min(0xb0, g->cam_x + 8);
        } else if (s_swipe_pan != 0) {
            int step = SDL_clamp(s_swipe_pan, -8, 8);
            int x = SDL_clamp(g->cam_x + step, 0, 0xb0);
            s_swipe_pan = x == g->cam_x ? 0 : s_swipe_pan - step; /* an edge ends it */
            g->cam_x = x;
        }
        break;
    default:
        break;
    }
}

/* controller state 4 for a defeated area boss: progress and the areas
 * whose prerequisites (0x1007e7f0, three per area) are all beaten */
static void boss_beaten(pk_game *g)
{
    int a = pk_b.area, k, i;
    if (a < 0 || a >= PK_AREAS || g->world.areas[a][35] != 0) {
        return;
    }
    if (g->progress < 10) {
        g->progress++;
    }
    g->world.areas[a][35] = 1;
    if (a == 0x17) {
        g->ending_pending = 1; /* game+0x58 = 3: the ending follows */
    }
    { /* treasures for the Vault: one bit per boss area */
        static const int k_treasure_area[8] = { 13, 19, 36, 9, 18, 32, 14, 39 };
        for (i = 0; i < 8; i++) {
            if (g->world.areas[k_treasure_area[i]][35]) {
                g->world.h_444 |= 1 << i;
            }
        }
    }
    for (k = 0; k < 0x2a; k++) {
        int need = 0, have = 0;
        for (i = 0; i < 3; i++) {
            int p = (int)pk_app_i32(0x1007e7f0u + (Uint32)(k * 12 + i * 4));
            if (p == 0xff) {
                continue;
            }
            need++;
            if (p < PK_AREAS && g->world.areas[p][35] != 0) {
                have++;
            }
        }
        if (need > 0 && need == have && g->world.areas[k][36] == 0) {
            g->world.areas[k][36] = 1;
            PK_DEBUG("area %d unlocked", k);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Traps (0x10002030 at the start, 0x10002910 later). The defender's
 * trap (pk_b.trap: formation byte 2, or game+0x4f4 for the player's
 * defense) always hits the attacking side, units not yet down:
 *   0 pit (start): non-flying units lose a fifth of their HP
 *     (FUN_10005684: HP / 5)
 *   3 (start): the side's positions are swapped around
 *   8 (start): status 8, which stops special moves (0x10022000)
 *   1 (time < 0xa6): units stunned 40 ticks (status 0x20, FUN_10007b28)
 *   2 (time < 0xa6): units stunned 13 ticks (status 0x100, FUN_10007f48)
 *   6 (time < 0xa6): blast, a fifth of the HP (FUN_10005a9c)
 *   7 (time < 0xa6): positions swapped (FUN_10009930 / FUN_1000998c)
 *   3 (start): both sides placed mixed up (x tables 0x1007d9b4 / 0x1007d9c4)
 *   4, 5 (a non-flying unit reaches x 0xa0..0xc0): port approximation,
 *     40 ticks (status 0x40) / 13 ticks (status 0x400) of stun
 * Units whose object has flag 0x1000 are immune (not modelled). */

static int attacker_rside(void) { return pk_b.player_side == 0 ? 0 : 1; }

static int trap_targets(pk_game *g, pk_object **out)
{
    int i, n = 0, rs = attacker_rside();
    for (i = 0; i < g->objects->count && n < 4; i++) {
        pk_object *u = g->objects->items[i];
        if (u->dead || !IS_UNIT(u) || rside(u->arg0) != rs) {
            continue;
        }
        if (urec(u)[0] == 0xff || urec(u)[22] != 0 || u->state == 7 || u->state == 0x20) {
            continue;
        }
        out[n++] = u;
    }
    return n;
}

static void trap_swap(pk_object **u, int n)
{
    int i;
    for (i = 0; i < n / 2; i++) {
        int x = u[i]->x;
        u[i]->x = u[n - 1 - i]->x;
        u[n - 1 - i]->x = x;
    }
}

/* after a trap the defending side's leader speaks (FUN_10009c2c with
 * group 4, or 5 for the swap of trap 3) */
static void trap_speech(void)
{
    int rs = 1 - attacker_rside(), sl = pk_b.leader[rs] & 3;
    if (pk_b.rec[rs][sl][0] == 0xff || pk_b.rec[rs][sl][22]) {
        return;
    }
    pk_b.special_rs = rs;
    pk_b.special_slot = sl;
    pk_b.panel_kind = pk_b.trap == 3 ? 5 : 4;
    pk_b.special_msg = 0x21;
}

/* 0x10003670: the trap's reveal, eight cards (image 0x5a, frames
 * 0x1007e4e4, 44 x 64, unmasked) that slide in from both sides in two
 * rows (y 0x65 from the left, 0x25 from the right), turn over
 * (animation 0x1007e74c), show the trap's picture, raise game+0x4f8 and
 * slide out again. */
static void trap_card_update(pk_object *o, pk_game *g)
{
    static const int k_face[10] = { 0xe, 0xd, 0xc, 9, 5, 6, 8, 10, 7, 0xb };
    switch (o->state) {
    case 0:
        o->image = pk_game_load_image(g, 0x5a, 1);
        pk_object_set_frames(o, 0x1007e4e4u);
        o->frame = 0;
        o->layer = 1;
        o->flags = 0x4e1;
        pk_object_clear_work(o);
        if (o->arg1 < 4) {
            o->x = -0x2c;
            o->y = 0x65;
            o->work[0] = o->arg1 * 0x2c;
            o->counter = (3 - o->arg1) * 2;
        } else {
            o->x = 0xb0;
            o->y = 0x25;
            o->work[0] = o->arg1 * 0x2c - 0xb0;
            o->counter = (o->arg1 - 4) * 2;
        }
        o->state = 1;
        break;
    case 1:
        if (o->arg1 < 4) {
            o->x += 0x16;
            if (o->x < o->work[0]) {
                break;
            }
        } else {
            o->x -= 0x16;
            if (o->work[0] < o->x) {
                break;
            }
        }
        o->x = o->work[0];
        o->state = 2;
        break;
    case 2:
        if (o->counter >= 1) {
            o->counter--;
            break;
        }
        pk_object_set_anim(o, 0x1007e74cu);
        o->counter = 0;
        o->state = 3;
        break;
    case 3:
        pk_object_animate(o);
        if (++o->counter < 6) {
            break;
        }
        o->flags &= ~(Uint32)PK_OBJ_ANIMATING;
        if (pk_b.trap >= 0 && pk_b.trap <= 9) {
            o->frame = k_face[pk_b.trap];
        }
        o->counter = 0;
        o->state = 4;
        break;
    case 4:
        if (++o->counter <= 10) {
            break;
        }
        o->counter = (o->arg1 < 4 ? 3 - o->arg1 : o->arg1 - 4) * 2;
        pk_b.trap_revealed = 1; /* game+0x4f8 = 1 */
        o->state = 5;
        break;
    case 5:
        if (o->counter >= 1) {
            o->counter--;
            break;
        }
        o->state = 6;
        break;
    case 6:
        if (o->arg1 < 4) {
            o->x -= 0x16;
            if (o->x > -0x2d) {
                break;
            }
        } else {
            o->x += 0x16;
            if (o->x < 0xb0) {
                break;
            }
        }
        pk_object_kill(o);
        break;
    default:
        break;
    }
}

static void trap_fire(pk_game *g)
{
    pk_object *u[4];
    int n = trap_targets(g, u), i;
    pk_b.trap_done = 1;
    if (pk_b.trap == 0 || pk_b.trap == 3) {
        trap_speech();
    }
    for (i = 0; i < n; i++) {
        int *r = urec(u[i]);
        switch (pk_b.trap) {
        case 0:
            if (r[16] == 1) {
                break; /* flying */
            }
            /* fallthrough */
        case 6:
            r[7] -= r[7] / 5;
            if (r[7] < 1) {
                r[7] = 1;
            }
            pk_object_spawn(g, fx_update, OBJ_FX, 1, u[i]->arg0, u[i]->arg1, u[i]->x, u[i]->y - 0x14);
            break;
        case 1:
        case 4:
            u[i]->state = 4;
            u[i]->work[0] = 0x28;
            pk_object_spawn(g, fx_update, OBJ_FX, 0x31, u[i]->arg0, u[i]->arg1, u[i]->x, u[i]->y - 0x1e);
            break;
        case 2:
        case 5:
            u[i]->state = 4;
            u[i]->work[0] = 0xd;
            pk_object_spawn(g, fx_update, OBJ_FX, 0x31, u[i]->arg0, u[i]->arg1, u[i]->x, u[i]->y - 0x1e);
            break;
        case 8:
            pk_object_spawn(g, fx_update, OBJ_FX, 0x34, u[i]->arg0, u[i]->arg1, u[i]->x, u[i]->y - 0x14);
            break;
        default:
            break;
        }
    }
    if (pk_b.trap == 3) { /* 0x10002030 state 3: both sides stand mixed up */
        for (i = 0; i < g->objects->count; i++) {
            pk_object *e = g->objects->items[i];
            int sl;
            if (e->dead || !IS_UNIT(e)) {
                continue;
            }
            sl = e->arg1 & 3;
            if (e->arg0 == 1) {
                e->x = (int)pk_app_i32(0x1007d9b4u + (Uint32)sl * 4u);
                if (sl < 2) {
                    e->flags |= PK_OBJ_FLIP_X;
                } else {
                    e->flags &= ~(Uint32)PK_OBJ_FLIP_X;
                }
            } else {
                e->x = (int)pk_app_i32(0x1007d9c4u + (Uint32)sl * 4u);
                if (sl < 2) {
                    e->flags &= ~(Uint32)PK_OBJ_FLIP_X;
                } else {
                    e->flags |= PK_OBJ_FLIP_X;
                }
            }
        }
    }
    if (pk_b.trap == 7) {
        trap_swap(u, n);
    }
    if (pk_b.trap == 8) {
        pk_b.sealed[attacker_rside()] = 1; /* status 8: no special moves */
    }
    PK_DEBUG("trap %d went off (%d units)", pk_b.trap, n);
}

static void trap_reveal(pk_game *g)
{
    int i;
    pk_b.trap_done = 2; /* cards on screen; trap_fire follows game+0x4f8 */
    pk_b.trap_revealed = 0;
    for (i = 0; i < 8; i++) {
        pk_object_spawn(g, trap_card_update, OBJ_CONTROLLER, 0, 0, i, 0, 0);
    }
    pk_audio_play_sfx(4);
}

/* trap 0 (0x10002030 state 2): the units fall from above (y -20, speed
 * 8, +4 a tick) and those that do not fly take the blow on landing
 * (FUN_10005684, burst effect 1) */
static void trap_drop(pk_game *g)
{
    pk_object *u[4];
    int n = trap_targets(g, u), i, landed = 0;
    for (i = 0; i < n; i++) {
        if (u[i]->work[5] == 0) {
            u[i]->work[5] = 1;
            u[i]->y = -20;
            u[i]->work[4] = 8;
        }
        if (u[i]->work[5] == 2) {
            landed++;
            continue;
        }
        u[i]->y += u[i]->work[4];
        u[i]->work[4] += 4;
        if (u[i]->y >= GROUND_Y) {
            int *r = urec(u[i]);
            u[i]->y = GROUND_Y;
            u[i]->work[5] = 2;
            landed++;
            if (r[16] != 1) {
                r[7] -= r[7] / 5;
                if (r[7] < 1) {
                    r[7] = 1;
                }
                pk_object_spawn(g, fx_update, OBJ_FX, 1, u[i]->arg0, u[i]->arg1, u[i]->x, u[i]->y - 0x14);
            }
        }
    }
    if (landed == n) {
        for (i = 0; i < n; i++) {
            u[i]->work[5] = 0;
        }
        pk_b.trap_done = 1;
        trap_speech();
    }
}

static void trap_tick(pk_game *g)
{
    int t = pk_b.trap;
    if (pk_b.trap_done == 3) {
        trap_drop(g);
        return;
    }
    if (pk_b.trap_done == 2) {
        if (pk_b.trap_revealed && t == 0) {
            pk_b.trap_done = 3;
        } else if (pk_b.trap_revealed) {
            trap_fire(g);
        }
        return;
    }
    if (pk_b.trap_done || t < 0 || t > 8) {
        return;
    }
    if (t == 0 || t == 3 || t == 8) {
        trap_reveal(g);
    } else if (t == 1 || t == 2 || t == 6 || t == 7) {
        if (pk_b.time < 0xa6) {
            trap_reveal(g);
        }
    } else { /* 4, 5: someone walks into the middle */
        pk_object *u[4];
        int n = trap_targets(g, u), i;
        for (i = 0; i < n; i++) {
            if (urec(u[i])[16] != 1 && u[i]->x >= 0xa0 && u[i]->x <= 0xc0) {
                trap_reveal(g);
                break;
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* HUD (0x10004154 gauges, 0x10003d28 unit markers). Each side's gauge
 * is five rows of filled bars (colours 0x1007e7cc: rows 0/4, 1/3, 2;
 * row 2 is four pixels tall) at y 0x15..0x1c, as long as 40 * the
 * side's HP as a percentage of its HP at the start / 100 (FUN_10004e80); the player's grows left
 * from x 0x4a, the other side's right from 0x66. Over every unit still
 * standing floats its marker: image 0x5e, frames 0x1007da64, 2 + slot
 * on the player's side, 6 + slot on the other. */

static int side_hp_pct(int rs) /* FUN_10004e80: against the HP at the start (+0x490/+0x494) */
{
    int i, cur = 0, pct;
    for (i = 0; i < 4; i++) {
        int *r = pk_b.rec[rs][i];
        if (r[0] != 0xff) {
            cur += r[7];
        }
    }
    if (pk_b.hp_total[rs] <= 0) {
        return 0;
    }
    pct = cur * 100 / pk_b.hp_total[rs];
    return SDL_clamp(pct, 0, 100);
}

#define TBL_LEADER_FACE 0x1007d9d4u /* portrait frames: 3 / 4 the small faces */

static void hud_update(pk_object *o, pk_game *g)
{
    if (pk_b.over > 1) {
        pk_object_kill(o);
        return;
    }
    if (o->state == 0x30) { /* 0x10003a1c: arg1 0 top bar, 1 bottom panel */
        o->image = pk_game_image_slot(g, IMG_BANNER);
        if (!o->frames) {
            pk_object_set_frames(o, TBL_BANNER);
        }
        o->flags = 0x4c1;
        o->layer = o->arg1 != 1;
        o->frame = o->arg1;
        if (o->arg1 == 1 && pk_b.special_msg > 0) {
            /* game+0x464: the side of the unit whose special move runs */
            int screen = pk_b.special_rs == 0 ? pk_b.player_side : 1 - pk_b.player_side;
            o->frame = screen == 0 ? 0x66 : 0x67;
        }
        return;
    }
    if (o->state == 0x31) { /* 0x100044b4: timer digits, game+0x49c / 110 and / 11 % 10 */
        int t = pk_b.time;
        o->image = pk_game_image_slot(g, IMG_BANNER);
        if (!o->frames) {
            pk_object_set_frames(o, TBL_BANNER);
        }
        o->flags = 0x4e1;
        o->layer = 1;
        o->frame = 0x2c + (o->arg1 == 0 ? (t / 110) % 10 : (t / 11) % 10);
        return;
    }
    if (o->state == 0x32) { /* 0x100055b8: the leaders' faces; arg0 record side */
        int t = pk_b.rec[o->arg0][pk_b.leader[o->arg0] & 3][0];
        if (!o->frames && t != 0xff) {
            o->image = pk_game_load_image(g, PK_PORTRAIT_BASE + t * 2, 1);
            pk_object_set_frames(o, TBL_LEADER_FACE);
        }
        o->flags = t == 0xff ? 0x400 : 0x4c1;
        o->layer = 1;
        o->frame = o->arg1;
        return;
    }
    if (o->state == 0x34) { /* 0x10003a1c arg1 2/3: the speaker's portrait, 0x6a: the strip */
        const int *r = pk_b.rec[pk_b.special_rs][pk_b.special_slot];
        if (pk_b.special_msg <= 0 || r[0] == 0xff) {
            o->flags = 0x400;
            return;
        }
        o->layer = 1;
        o->flags = 0x4c1;
        if (o->arg1 == 0x6a) {
            o->image = pk_game_image_slot(g, IMG_BANNER);
            if (!o->frames) {
                pk_object_set_frames(o, TBL_BANNER);
            }
            o->frame = g->boss_battle == 1 ? 0x6b : 0x6a;
        } else {
            if (o->work[2] != r[0] + 1) {
                o->image = pk_game_load_image(g, PK_PORTRAIT_BASE + r[0] * 2, 1);
                pk_object_set_frames(o, TBL_LEADER_FACE);
                o->work[2] = r[0] + 1;
            }
            o->frame = o->arg1 - 2;
        }
        return;
    }
    if (o->state == 0x33) { /* 0x10003c74: where the camera is */
        o->image = pk_game_image_slot(g, IMG_BANNER);
        if (!o->frames) {
            pk_object_set_frames(o, TBL_BANNER);
        }
        o->flags = 0x4c1;
        o->layer = 1;
        o->frame = 0x11;
        o->x = g->cam_x / 2 + 0x2c;
        o->y = 4;
        return;
    }
    if (o->state == 0x10) { /* gauge row: arg0 screen side, arg1 row */
        static const int k_row_y[5] = { 0x15, 0x16, 0x17, 0x1b, 0x1c };
        static const int k_row_col[5] = { 0, 1, 2, 1, 0 };
        int rs = o->arg0 == pk_b.player_side ? 0 : 1, w = side_hp_pct(rs) * 40 / 100, c = k_row_col[o->arg1];
        o->image = pk_game_image_slot(g, IMG_BANNER);
        o->flags = 0x400 | PK_OBJ_RECT | PK_OBJ_SCREEN;
        o->layer = 1;
        o->y = k_row_y[o->arg1];
        o->h = o->arg1 == 2 ? 4 : 1;
        o->w = w;
        o->x = o->arg0 == 0 ? 0x4a - w : 0x66;
        o->work[0] = (int)pk_app_i32(0x1007e7ccu + (Uint32)(c * 12));
        o->work[1] = (int)pk_app_i32(0x1007e7ccu + (Uint32)(c * 12 + 4));
        o->work[2] = (int)pk_app_i32(0x1007e7ccu + (Uint32)(c * 12 + 8));
        if (w <= 0) {
            o->flags &= ~(Uint32)PK_OBJ_RECT;
        }
    } else if (o->state == 0x20) { /* marker: arg0 screen side, arg1 slot */
        int i;
        pk_object *u = NULL;
        for (i = 0; i < g->objects->count; i++) {
            pk_object *e = g->objects->items[i];
            if (!e->dead && IS_UNIT(e) && e->arg0 == o->arg0 && (e->arg1 & 3) == o->arg1) {
                u = e;
                break;
            }
        }
        if (!o->frames) {
            o->image = pk_game_image_slot(g, IMG_BANNER);
            pk_object_set_frames(o, 0x1007da64u);
            o->frame = (o->arg0 == 0 ? 2 : 6) + o->arg1;
            o->layer = 1;
        }
        if (!u || urec(u)[22] || u->state == 7 || u->state == 0x20 || !pk_b.started) {
            o->flags = 0x400;
            return;
        }
        o->flags = 0x481; /* as the original (whole bitmap) */
        o->x = SDL_clamp(u->x, g->cam_x + 8, g->cam_x + 0xa8);
        o->y = u->y - 0x40;
        if (o->work[1]) { /* state 8 of 0x10003d28: HP in six steps, frames 10..15 */
            int *r = urec(u), q = r[8] / 6;
            o->frame = r[7] >= r[8] ? 0xf : (q > 0 && r[7] / q < 5 ? r[7] / q + 10 : (q > 0 ? 0xf : 10));
            o->y = u->y - 0x38;
        }
    }
}

static void hud_spawn(pk_game *g)
{
    int s, i, left = pk_b.player_side == 0 ? 0 : 1; /* record side on the left */
    pk_object *o;
    /* controller state 1 (0x10000824): bars, timer, faces, markers, gauges */
    pk_object_spawn(g, hud_update, OBJ_HUD, 0x30, 1, 0, 0x58, 0x15);
    pk_object_spawn(g, hud_update, OBJ_HUD, 0x30, 1, 1, 0x58, 0xb8);
    /* in the original's order (0x10000824 state 1): the strip, then the
     * portrait over it */
    pk_object_spawn(g, hud_update, OBJ_HUD, 0x34, 1, 0x6a, 0x4c, 0x9e);
    pk_object_spawn(g, hud_update, OBJ_HUD, 0x34, 1, 2, 0x98, 0x9c);
    pk_object_spawn(g, hud_update, OBJ_HUD, 0x34, 1, 3, 0x98, 0xb4);
    pk_object_spawn(g, hud_update, OBJ_HUD, 0x33, 2, 0, 0x58, 4);
    pk_object_spawn(g, hud_update, OBJ_HUD, 0x32, left, 3, 0x0e, 0x19);
    pk_object_spawn(g, hud_update, OBJ_HUD, 0x32, 1 - left, 4, 0xa2, 0x19);
    for (s = 0; s < 2; s++) {
        for (i = 0; i < 4; i++) {
            pk_object_spawn(g, hud_update, OBJ_HUD, 0x20, s, i, 0, 0);
            o = pk_object_spawn(g, hud_update, OBJ_HUD, 0x20, s, i, 0, 0);
            if (o) {
                o->work[1] = 1; /* the HP icon */
            }
        }
    }
    for (s = 0; s < 2; s++) {
        for (i = 0; i < 5; i++) {
            pk_object_spawn(g, hud_update, OBJ_HUD, 0x10, s, i, 0, 0);
        }
    }
    pk_object_spawn(g, hud_update, OBJ_HUD, 0x31, 0, 0, 0x53, 0x18);
    pk_object_spawn(g, hud_update, OBJ_HUD, 0x31, 0, 1, 0x5c, 0x18);
}

static void controller_update(pk_object *o, pk_game *g)
{
    int d;
    switch (o->state) {
    case 0:
        pk_b.over = 0;
        pk_b.started = 0;
        pk_audio_play_bgm(g->boss_battle == 1 ? 5 : 4);
        hud_spawn(g);
        o->flags = 0x440;
        o->counter = 0;
        o->state = 1;
        break;
    case 1:
        if (g->curtain == PK_CURTAIN_HELD) {
            pk_game_set_curtain(g, PK_CURTAIN_OPEN);
            o->counter = 0;
            o->work[1] = 0;
            o->state = 2;
        }
        break;
    case 2: /* intro (0x10000824 state 2): the camera jumps to the other
             * side's leader (mode 7), who speaks once the curtain is open
             * (FUN_10009c2c group 0, 6 for a boss); the fight starts after
             * 0x20 ticks or a key */
        if (o->work[1] == 0 || o->work[1] == 1) {
            int rs = 1, sl = pk_b.leader[1] % 6;
            pk_object *ld;
            if (sl < 0 || pk_b.rec[rs][sl][0] == 0xff || pk_b.rec[rs][sl][22]) {
                for (sl = 0; sl < 4 && (pk_b.rec[rs][sl][0] == 0xff || pk_b.rec[rs][sl][22]); sl++) {
                }
            }
            ld = sl < 4 ? pk_object_find(g, pk_b.rec[rs][sl][0], 1 - pk_b.player_side, sl) : NULL;
            if (!ld) {
                o->work[1] = 2;
                o->counter = 0;
            } else if (o->work[1] == 0) {
                pk_b.cam[2] = ld->x;
                pk_b.cam[3] = ld->y;
                pk_b.cam[1] = 7;
                o->work[1] = 1;
                o->counter = 0;
            } else if (g->curtain == PK_CURTAIN_NONE) {
                pk_battle_panel(g, ld, g->boss_battle == 1 ? 6 : 0);
                o->work[1] = 2;
                o->counter = 0;
            }
            break;
        }
        if (++o->counter > 0x20 || g->key_code == PK_SC_5 || g->key_code == PK_SC_BACK) {
            pk_b.cam[1] = 0;
            pk_b.started = 1;
            pk_b.special_msg = 0;
            pk_b.over = 0;
            o->counter = 0;
            o->work[1] = 0;
            o->state = 3;
        }
        break;
    case 3: /* fighting */
        trap_tick(g);
        if (g->boss_battle == 1 && pk_b.rec[1][pk_b.leader[1] & 3][0] != 0xff &&
            pk_b.rec[1][pk_b.leader[1] & 3][22] != 0) {
            pk_b.over = 1; /* the area boss is down */
            o->counter = 0;
            o->state = 4;
            break;
        }
        d = alive_all_down();
        if (d != 0) {
            o->counter = 0;
            o->state = 4;
            break;
        }
        if (pk_b.trap_done >= 2) {
            break;
        }
        if (--pk_b.time < 1) {
            pk_b.time = 0;
            pk_b.over = 1;
            pk_object_spawn(g, banner_update, OBJ_CONTROLLER, 0, 0, 2, 0x58, 100);
            o->counter = 0;
            o->state = 5;
        }
        break;
    case 4:
        if (++o->counter < 0x10) {
            break;
        }
        pk_b.over = 1;
        d = alive_all_down();
        if (g->boss_battle == 1) {
            if (pk_b.rec[1][pk_b.leader[1] & 3][22] != 0) {
                boss_beaten(g);
                o->state = 6;
            } else {
                o->state = 7;
            }
        } else if (d & 0x10) {
            o->state = 6;
        } else {
            int me = hp_percent(1), them = hp_percent(0);
            o->state = (me > them || (me == them && pk_b.player_side == 1)) ? 6 : 7;
        }
        break;
    case 5: /* time over */
        if (++o->counter < 0x32) {
            break;
        }
        o->counter = 0;
        o->state = 4;
        break;
    case 6:
    case 7:
        pk_b.won = o->state == 6;
        pk_object_spawn(g, banner_update, OBJ_CONTROLLER, 0, 0, o->state == 6 ? 0 : 1, 0x58, 0x68);
        o->counter = 0;
        o->state = 8;
        break;
    case 8:
        if (++o->counter > 0x15) {
            pk_game_set_curtain(g, PK_CURTAIN_CLOSE);
            o->state = 10;
        }
        break;
    case 10:
        if (g->curtain == PK_CURTAIN_HELD) {
            pk_game_set_mode(g, PK_MODE_KINGDOM);
        }
        break;
    default:
        break;
    }
    if (o->state >= 2) {
        camera_follow(g);
        draw_hud(g);
    }
}

/* ------------------------------------------------------------------ */
/* mode 0 (FUN_10068db0) */

void pk_mode_battle(pk_game *g)
{
    int s, i;
    switch (g->phase) {
    case 0: /* FUN_10068778 */
        pk_game_set_curtain(g, PK_CURTAIN_HELD);
        pk_game_release_images(g);
        pk_objects_clear(g);
        g->cam_x = g->cam_y = 0;
        pk_game_fill(g, 0, 0, PK_SCREEN_W, PK_SCREEN_H, 0xFF000000u);
        pk_game_mestext(g, 0, 0x58, 100, 0xFFFFFFu, PK_ALIGN_CENTER, 0, PK_FONT_LEGEND);
        pk_game_set_phase(g, 1);
        break;
    case 1: /* FUN_10068a34 */
        pk_game_set_bg(g, 0, pk_b.field + 0x86);
        g->bg_visible = 1;
        pk_game_load_image(g, IMG_BANNER, 1);
        pk_game_load_image(g, 0x17, 1);
        pk_object_spawn(g, controller_update, OBJ_CONTROLLER, 0, 0, 0, 0, 0);
        for (s = 0; s < 2; s++) {
            int screen_side = s == 0 ? pk_b.player_side : 1 - pk_b.player_side;
            for (i = 0; i < 4; i++) {
                int t = pk_b.rec[s][i][0];
                if (t == 0xff || pk_b.rec[s][i][22] != 0) {
                    continue;
                }
                pk_game_load_image(g, t * 2 + 0x102, 1);
                pk_bu_spawn(g, t, screen_side, i, 0, 0);
            }
        }
        pk_game_set_phase(g, 2);
        break;
    default:
        break;
    }
}

/* Mode 2: the results (root 0x1000a6dc, rewards FUN_1000d5ac). The
 * original shows a results screen with the loot, the experience and the
 * dropped materials; the port applies the same rewards and returns to
 * the kingdom. */
static int reward_loot(pk_game *g)
{
    int i, ranks = 0, down = 0, n = 0, bonus = 0, loot;
    (void)g;
    for (i = 0; i < 4; i++) {
        if (pk_b.rec[1][i][0] == 0xff) {
            continue;
        }
        n++;
        pk_unit_template(pk_b.rec[1][i][0], 10);
        ranks += pk_r.unit[10].b[1];
        if (pk_b.rec[1][i][22] != 0) {
            down++;
        }
    }
    loot = down * 200 + ranks * 100;
    if (down == n) {
        loot += 1000;
    }
    for (i = 0; i < 4; i++) { /* equipment traits 0x31..0x33 raise the loot */
        const int *e = pk_b.eq[0][i];
        if (e[3] == 0x31 || e[4] == 0x31) {
            if (!bonus) {
                bonus = 1;
            }
        } else if (e[3] == 0x32 || e[4] == 0x32) {
            if (!bonus) {
                bonus = 2;
            }
        } else if (e[3] == 0x33 || e[4] == 0x33) {
            bonus = 3;
        }
    }
    if (bonus == 1) {
        loot = loot * 6 / 5;
    } else if (bonus == 2) {
        loot = loot * 3 / 2;
    } else if (bonus == 3) {
        loot *= 2;
    }
    return loot;
}

/* FUN_1000d5ac: every downed enemy leaves one material, one of the
 * first three entries of its class in dropitem.dat (16-byte header,
 * 16 bytes per unit type: six kind/id pairs). A spot's last fight won
 * adds a clear item: clearitem.dat row = the spot's fights - 1, one of
 * five kind/id pairs (FUN_100639b0). Items stop at 250. */
static void name_line(char *out, const char *prefix, const Uint16 *nm)
{
    int o = SDL_snprintf(out, 48, "%s", prefix), j = 0;
    while (nm[j] && nm[j] != 0x1b && o < 44) {
        Uint16 c = nm[j++];
        if (c < 0x80) {
            out[o++] = (char)c;
        } else if (c < 0x800) {
            out[o++] = (char)(0xc0 | (c >> 6));
            out[o++] = (char)(0x80 | (c & 0x3f));
        } else {
            out[o++] = (char)(0xe0 | (c >> 12));
            out[o++] = (char)(0x80 | ((c >> 6) & 0x3f));
            out[o++] = (char)(0x80 | (c & 0x3f));
        }
    }
    out[o] = 0;
}

static int battle_drops(pk_game *g, char lines[][48], int max)
{
    size_t n = 0;
    Uint8 *d = (Uint8 *)pk_fs_load("dropitem.dat", &n);
    int i, k = 0;
    for (i = 0; i < 4 && k < max; i++) {
        int *r = pk_b.rec[1][i], pick, kind, id, idx;
        size_t off;
        if (r[0] == 0xff || r[22] == 0) {
            continue;
        }
        off = (size_t)r[0] * 16 + 16;
        if (!d || off + 12 > n || g->world.item_count >= PK_ITEM_MAX) {
            continue;
        }
        pick = pk_rng_range(&g->rng, 3);
        kind = d[off + pick * 2];
        id = d[off + pick * 2 + 1];
        idx = pk_item_add(g, kind, id);
        if (idx >= 0) {
            const Uint16 *nm = kind >= PK_KIND_EQUIP ? pk_r.equip[10].name : pk_r.item[10].name;
            name_line(lines[k], "", nm);
            k++;
        }
    }
    SDL_free(d);
    if (pk_b.won && pk_b.mode > 1 && pk_b.spot >= 0 && pk_b.spot < 5 && k < max &&
        g->world.item_count < PK_ITEM_MAX) {
        int fights = (int)pk_app_i32(0x100ac7dcu + (Uint32)(pk_b.area * 0x28 + pk_b.spot * 8));
        if (fights > 0 && g->world.areas[pk_b.area][30 + pk_b.spot] >= fights) {
            Uint8 *c = (Uint8 *)pk_fs_load("clearitem.dat", &n);
            size_t off = (size_t)(fights - 1) * 16 + 16;
            int col = pk_rng_range(&g->rng, 5);
            if (c && off + 10 <= n && pk_item_add(g, c[off + col * 2], c[off + col * 2 + 1]) >= 0) {
                const Uint16 *nm = c[off + col * 2] >= PK_KIND_EQUIP ? pk_r.equip[10].name : pk_r.item[10].name;
                name_line(lines[k], "", nm);
                k++;
            }
            SDL_free(c);
        }
    }
    return k;
}

/* 0x1000b6cc state 0x10: a row of the results window (image 0x17,
 * frames 0x1007ea68: 0x40 top, 0x41 middle, 0x42 bottom, unmasked) */
static void result_row_update(pk_object *o, pk_game *g)
{
    if (o->state == 0x10) {
        o->image = pk_game_load_image(g, 0x17, 1);
        o->flags = 0x4e1;
        pk_object_set_frames(o, 0x1007ea68u);
        o->frame = o->arg1 + 0x40;
        o->layer = 0;
        pk_object_clear_work(o);
        o->state = 0x11;
    }
}

static struct
{
    int won, lost_kingdom, loot, vp, n;
    char item[7][48];
} s_res;

/* FUN_1000b8e0: the results' texts (mestext 1 won / 2 lost at 0x58,0x76;
 * 3 loot and 5 victory points with their numbers; 4 the items, one a
 * line, or 6 none; 0x195 for an undefended kingdom). Record 3 goes with
 * the victory points (+0x95c), record 5 with the loot (+0x960). */
static void result_texts(pk_game *g)
{
    char num[16];
    int i;
    pk_game_mestext(g, s_res.won ? 1 : 2, 0x58, 0x76, s_res.won ? 0x2266DDu : 0xE60000u, PK_ALIGN_CENTER, 1,
                    PK_FONT_LEGEND);
    if (s_res.lost_kingdom) {
        pk_game_mestext(g, 0x195, 0x58, 0x92, 0, PK_ALIGN_CENTER, 1, PK_FONT_DENSE);
        return;
    }
    pk_game_mestext(g, 3, 100, 0x87, 0, PK_ALIGN_RIGHT, 1, PK_FONT_DENSE);
    SDL_snprintf(num, sizeof(num), "%d", s_res.vp); /* +0x95c */
    pk_drawq_text_utf8(&g->drawq, num, 100, 0x87, 0, PK_ALIGN_LEFT, 1, PK_FONT_DENSE);
    pk_game_mestext(g, 5, 0x32, 0x92, 0, PK_ALIGN_RIGHT, 1, PK_FONT_DENSE);
    SDL_snprintf(num, sizeof(num), "%d", s_res.loot); /* +0x960 */
    pk_drawq_text_utf8(&g->drawq, num, 0x32, 0x92, 0, PK_ALIGN_LEFT, 1, PK_FONT_DENSE);
    if (s_res.n == 0) {
        pk_game_mestext(g, 6, 0x32, 0x9d, 0, PK_ALIGN_LEFT, 1, PK_FONT_DENSE);
        return;
    }
    pk_game_mestext(g, 4, 0x32, 0x9d, 0, PK_ALIGN_RIGHT, 1, PK_FONT_DENSE);
    for (i = 0; i < s_res.n; i++) {
        pk_drawq_text_utf8(&g->drawq, s_res.item[i], 0x32, 0x9d + i * 0xb, 0, PK_ALIGN_LEFT, 1, PK_FONT_DENSE);
    }
}

/* FUN_1007b0f8: the team's units after the battle. Each unit takes its
 * rank, HP and fallen flag (+0x58) back. A fallen unit leaves the team
 * (its worth, f17, goes to team f11); if it was only hurt ("out",
 * battle +0x984) it stays with 1 HP, otherwise ("owned") it is gone for
 * good. Either way its equipment is lost, unless a survivor carries the
 * piece with effect 0x30: then each piece is kept, unequipped, on a coin
 * toss. */
static void battle_write_back(pk_game *g)
{
    pk_world *w = &g->world;
    int t = pk_b.team & 7, *tm = w->teams[t], i, best = 0, any = 0;
    bool salvage = false;
    for (i = 0; i < 4; i++) {
        int u = tm[i], *r = pk_b.rec[0][i];
        if (u == 0xff || u >= PK_UNITS) {
            continue;
        }
        w->units[u][7] = r[7];
        w->units[u][22] = r[22];
        w->units[u][1] = r[1];
        if (r[22] == 0 && (pk_b.eq[0][i][3] == 0x30 || pk_b.eq[0][i][4] == 0x30)) {
            salvage = true;
        }
    }
    for (i = 0; i < 4; i++) {
        int u = tm[i], *ur, e;
        if (u == 0xff || u >= PK_UNITS) {
            continue;
        }
        ur = w->units[u];
        if (ur[22] == 0) {
            any = 1;
            continue;
        }
        tm[i] = 0xff;
        tm[11] += ur[17];
        e = ur[2];
        if (e != 0xff && e < PK_EQUIPS) {
            if (salvage && (pk_rng_next(&g->rng) & 1)) {
                w->equip[e][10] = 0xff;
                w->equip[e][11] = 0;
            } else {
                pk_equip_remove(g, e);
            }
        }
        if (pk_b.r97c[2 + i] == 0) {
            pk_unit_remove(g, u); /* owned (FUN_1007ae48) */
        } else {
            ur[7] = 1; /* out */
            ur[3] = 0xff;
            ur[4] = 0;
            ur[22] = 0;
            ur[2] = 0xff;
        }
    }
    for (i = 0; i < 4; i++) { /* FUN_10051a18 */
        if (tm[i] != 0xff && tm[i] < PK_UNITS && w->units[tm[i]][14] >= best) {
            best = w->units[tm[i]][14];
        }
    }
    tm[10] = best;
    if (!any) { /* FUN_100693a4: the team is empty again */
        for (i = 0; i < 4; i++) {
            tm[i] = 0xff;
            tm[4 + i] = 5;
        }
        tm[8] = 0;
        tm[9] = 0xff;
        tm[10] = 0;
        tm[11] = 6;
        tm[12] = 0;
        tm[13] = 0;
    } else {
        for (i = 0; i < 4; i++) {
            if (tm[i] != 0xff) {
                tm[9] = i;
                break;
            }
        }
    }
    pk_units_sort(g, false); /* FUN_1007a018 */
    pk_equip_sort(g, false); /* FUN_10061b4c */
}

void pk_mode_result(pk_game *g)
{
    int i, loot = 0, vp = 0, ndrop = 0;
    char lines[10][48];
    if (g->phase == 1) { /* 0x1000a6dc state 4: until 5 */
        result_texts(g);
        if (g->curtain == PK_CURTAIN_NONE && ++g->timer > 10 &&
            (g->key_code == PK_SC_5 || g->key_code == PK_SC_BACK)) {
            pk_audio_play_sfx(0);
            pk_game_set_curtain(g, PK_CURTAIN_CLOSE);
            pk_game_set_phase(g, 2);
        }
        return;
    }
    if (g->phase == 2) {
        if (g->curtain == PK_CURTAIN_HELD) {
            pk_objects_clear(g);
            pk_game_set_mode(g, g->ending_pending ? PK_MODE_ENDING : PK_MODE_FIELD);
        }
        return;
    }
    if (g->phase != 0) {
        return;
    }
    if (pk_b.team == 0xff && pk_b.player_side == 1) {
        /* undefended kingdom: the invaders take loot and fame */
        vp = g->fame < 0x1e ? -g->fame : -0x1e;
        loot = -500;
    } else if (pk_b.won) {
        loot = reward_loot(g);
        if (pk_b.mode > 1 && pk_b.spot >= 0 && pk_b.spot < 5) {
            g->world.areas[pk_b.area][30 + pk_b.spot]++; /* spot searched once more */
        }
        if (g->boss_battle == 0 && pk_b.formation < 0xc && g->world.formations[pk_b.formation][43] != 0xffff) {
            /* an enemy base is destroyed (FUN_1000d5ac -> FUN_1000ca84) */
            int *f = g->world.formations[pk_b.formation];
            f[43] = 0xffff;
            f[45] = 0xff;
            f[46] = f[47] = 0;
            if (g->world.h_450 > 0) {
                g->world.h_450--;
            }
            if (g->world.areas[pk_b.area][2] > 0) {
                g->world.areas[pk_b.area][2]--;
            }
            g->world.areas[pk_b.area][3]++;
        }
        if (g->boss_battle == 1 || g->world.formations[pk_b.formation & 15][47] == 1) {
            vp = 10;
        }
    } else if (g->world.formations[pk_b.formation & 15][47] == 1) {
        vp = -10;
    }
    for (i = 0; i < 4; i++) { /* survivors keep at least 1 HP; everyone gains a level */
        int *r = pk_b.rec[0][i];
        if (r[0] == 0xff) {
            continue;
        }
        if (r[22] == 0 && r[7] < 1) {
            r[7] = 1;
        }
        if (r[1] < 0x14) {
            r[1]++;
        }
    }
    if (pk_b.team != 0xff) {
        battle_write_back(g);
        g->world.teams[pk_b.team & 7][12] = 0;
    }
    if (!(pk_b.team == 0xff && pk_b.player_side == 1)) {
        ndrop = battle_drops(g, lines + 3, 7);
    }
    pk_loot_add(g, loot);
    g->fame += vp;
    if (g->fame < 0) {
        g->fame = 0;
    }
    PK_DEBUG("battle %s: loot %+d, VP %+d, %d items", pk_b.won ? "won" : "lost", loot, vp, ndrop);
    /* the results window over the battlefield (0x1000a6dc state 1) */
    SDL_zero(s_res);
    s_res.won = pk_b.won;
    s_res.lost_kingdom = pk_b.team == 0xff && pk_b.player_side == 1;
    s_res.loot = loot;
    s_res.vp = vp;
    for (i = 0; i < ndrop && i < 7; i++) {
        SDL_strlcpy(s_res.item[i], lines[3 + i], sizeof(s_res.item[i]));
    }
    s_res.n = ndrop < 7 ? ndrop : 7;
    pk_objects_clear(g);
    g->cam_x = 0;
    pk_object_spawn(g, result_row_update, OBJ_CONTROLLER, 0x10, 0x10, 0, 0, 0x68);
    for (i = 0x6c; i < 0xcc; i += 4) {
        pk_object_spawn(g, result_row_update, OBJ_CONTROLLER, 0x10, 0x10, 1, 0, i);
    }
    pk_object_spawn(g, result_row_update, OBJ_CONTROLLER, 0x10, 0x10, 2, 0, 0xcc);
    pk_game_set_curtain(g, PK_CURTAIN_OPEN);
    g->timer = 0;
    if (pk_b.formation >= 0 && pk_b.formation < 0xc) {
        g->world.formations[pk_b.formation][46] = 0;
    }
    g->boss_battle = 0;
    g->battle_result = pk_b.won ? 1 : 2;
    pk_items_sort(g);
    pk_world_save(g);
    pk_game_set_phase(g, 1);
}
