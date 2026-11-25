/* SPDX-License-Identifier: MIT */
/* Battle units (reimplementation; addresses name the original functions
 * the behaviour follows).
 *
 * A unit object (type = unit type, arg0 = screen side, arg1 = slot,
 * arg2 = record side) is set up by the dispatcher 0x10020f3c and then run
 * by the behaviour of its class: 49 classes, each with its own animations,
 * timing, reach, attack boxes and special move, sharing the helpers at
 * 0x10022000..0x10023044. Attack boxes (type 0xc0, 0x100122f0) carry the
 * attacker's side and slot; FUN_1000dfcc tests them against every unit.
 *
 * Object fields as in the original: work[0] (+0x18) the action timer,
 * work[1] (+0x1c) the sub-state, counter (+0x3c), x2 / y2 (+0x4c) the
 * box offset, w / h (+0x54) its half size, target (+0x64).
 * Unit flags (+0x0c) above the renderer's:
 *   0x10000 hit (knocked down), 0x20000 hit (staggered), 0x40000 just hit
 *   0x80000, 0x200000 acts while the battle is over, 0x400000 reflected
 *   box, 0x800000, 0x1000000 / 0x2000000 / 0x4000000 small / medium / large,
 *   0x8000000 special move used last, 0x10000000 critical hit next. */
#include "game/pk_battle_unit.h"
#include "audio/pk_audio.h"
#include "core/pk_log.h"
#include "game/pk_battle.h"
#include "game/pk_rules.h"
#include "game/pk_world.h"

#include <SDL3/SDL.h>

#define F_KNOCKDOWN 0x10000u
#define F_STAGGER   0x20000u
#define F_STRUCK    0x40000u
#define F_OVERRUN   0x200000u
#define F_REFLECTED 0x400000u
#define F_SMALL     0x1000000u
#define F_MEDIUM    0x2000000u
#define F_LARGE     0x4000000u
#define F_SPECIAL   0x8000000u
#define F_CRITICAL  0x10000000u
#define FLIP        PK_OBJ_FLIP_X

#define OBJ_CONTROLLER          0xc3
#define FX(g, st, a0, a1, x, y) pk_battle_fx((g), (st), (a0), (a1), (x), (y))

static void unit_update(pk_object *o, pk_game *g);
static void unit_dispatch(pk_object *o, pk_game *g);
static void attack_update(pk_object *o, pk_game *g);

/* ------------------------------------------------------------------ */
/* records */

static int rs(const pk_object *o) { return o->arg0 == pk_b.player_side ? 0 : 1; }
static int sl(const pk_object *o) { return o->arg1 % 6; }
static int *rec(const pk_object *o) { return pk_b.rec[rs(o)][sl(o)]; }
static int *eqp(const pk_object *o) { return pk_b.eq[rs(o)][sl(o)]; }
static int *stp(const pk_object *o) { return &pk_b.status[rs(o)][sl(o)]; }
static int status(const pk_object *o) { return *stp(o); }            /* FUN_100082e8 */
static void status_set(const pk_object *o, int b) { *stp(o) |= b; }  /* FUN_100081f0 */
static void status_clr(const pk_object *o, int b) { *stp(o) &= ~b; } /* FUN_1000826c */
/* per-unit counters at +0x718.. [record side * 6 + slot] */
static int *cnt(const pk_object *o, int off) { return &pk_b.r718[(off - 0x718) / 4 + o->arg2 * 6 + sl(o)]; }

static int level(const pk_object *o) { return rec(o)[1]; }   /* FUN_10008978 */
static int atk(const pk_object *o) { return rec(o)[5]; }     /* FUN_10008490 */
static int def(const pk_object *o) { return rec(o)[6]; }     /* FUN_100084ec */
static int hp(const pk_object *o) { return rec(o)[7]; }      /* FUN_10008c98 */
static int hpmax(const pk_object *o) { return rec(o)[8]; }   /* FUN_10008cf4 */
static int agi(const pk_object *o) { return rec(o)[9]; }     /* FUN_100085a4 */
static int luck(const pk_object *o) { return rec(o)[10]; }   /* FUN_10008714 */
static int res(const pk_object *o) { return rec(o)[11]; }    /* FUN_100086b8 */
static int mag(const pk_object *o) { return rec(o)[12]; }    /* FUN_10008600 */
static int acc(const pk_object *o) { return rec(o)[13]; }    /* FUN_10008548 */
static int rank(const pk_object *o) { return rec(o)[14]; }   /* FUN_100097cc */
static int flying(const pk_object *o) { return rec(o)[16]; } /* FUN_10008a3c */
static int down(const pk_object *o) { return rec(o)[22]; }   /* FUN_10009250 */
/* FUN_1000865c: the magic of the unit's target */
static int target_mag(const pk_object *o) { return pk_b.rec[1 - rs(o)][o->target % 6][12]; }

static void stat_sub(const pk_object *o, int f, int v) /* FUN_10009304 .. FUN_10009700 */
{
    int *r = rec(o);
    r[f] -= v;
    if (r[f] < 0) {
        r[f] = 0;
    }
}

static int rnd(pk_game *g) { return (int)(pk_rng_next(&g->rng) & 0x7fff); } /* FUN_100714e8 */
static int pct(pk_game *g) { return rnd(g) % 100; }
static int div10(int v) { return v / 10; }

static void sfx(int id) { pk_audio_play_sfx(id); } /* FUN_10069d58 */

/* FUN_100707a4: arguments in the original order (type, arg1, arg0) */
static pk_object *find(pk_game *g, int type, int a1, int a0) { return pk_object_find(g, type, a0, a1); }

pk_object *pk_bu_find(pk_game *g, int screen_side, int slot) /* FUN_10009a10 */
{
    int r = screen_side == pk_b.player_side ? 0 : 1;
    return find(g, pk_b.rec[r][slot % 6][0], slot, screen_side);
}

static pk_object *owner(pk_game *g, const pk_object *o) { return pk_bu_find(g, o->arg0, o->arg1); }

bool pk_bu_is_unit(const pk_object *o) { return o->update == unit_update || o->update == unit_dispatch; }

static void clear(pk_object *o) { pk_object_clear_work(o); }
static void to_think(pk_object *o)
{
    o->state = 1;
    clear(o);
}
static void anim(pk_object *o, Uint32 a) { pk_object_set_anim(o, a); }
static void anim_stop(pk_object *o) { o->flags &= ~(Uint32)PK_OBJ_ANIMATING; } /* FUN_1006ac34 */
static void box(pk_object *o, int x, int y, int w, int h)
{
    o->x2 = x;
    o->y2 = y;
    o->w = w;
    o->h = h;
}
static int dir(const pk_object *o, int v) { return (o->flags & FLIP) ? -v : v; }

static void damage(pk_game *g, pk_object *u, int d);
void pk_bu_damage(pk_game *g, pk_object *u, int d) { damage(g, u, d); }
void pk_bu_status_set(const pk_object *o, int b) { status_set(o, b); }
void pk_bu_status_clr(const pk_object *o, int b) { status_clr(o, b); }

/* FUN_10008af4 with FUN_10010174: the damage number, then the HP */
static void damage(pk_game *g, pk_object *u, int d)
{
    int m = hpmax(u), k;
    if (d == 1) {
        k = 0;
    } else if (d < m / 5) {
        k = 1;
    } else if (d < m / 2) {
        k = 2;
    } else if (hp(u) < d) {
        k = 4;
    } else {
        k = 3;
    }
    FX(g, 4, u->arg0, u->arg1, k, u->y - 0x1e);
    rec(u)[7] -= d;
    if (rec(u)[7] < 0) {
        rec(u)[7] = 0;
    }
}

static void heal(const pk_object *u, int v) /* FUN_10008bc4 */
{
    int *r = rec(u);
    r[7] += v;
    if (r[8] < r[7]) {
        r[7] = r[8];
    }
}

static bool has_prop(const pk_object *o, int p) /* FUN_1000a53c: equipment property */
{
    const int *e = eqp(o);
    if (e[0] == 0xff) {
        return false;
    }
    return e[3] == p || e[4] == p;
}

/* FUN_10009f94: the battlefield favours or hinders the equipment's
 * element (eq f2); `of_target` takes the equipment of the object's target */
static int field_mod(const pk_object *o, int v, int of_target)
{
    const int *e = of_target ? pk_b.eq[1 - rs(o)][o->target % 6] : eqp(o);
    int el, f = pk_b.field, k = 0;
    if (e[0] == 0xff || e[2] == 0xff) {
        return v;
    }
    el = e[2];
    switch (f) {
    case 4:
        k = (unsigned)(el - 3) < 2;
        break;
    case 1:
        if (el == 0) {
            k = 2;
        }
        break;
    case 5:
        if (el == 3) {
            k = 2;
        }
        break;
    case 3:
        if (el == 4) {
            k = 2;
        }
        break;
    case 7:
        if (el == 1) {
            k = 2;
        }
        break;
    case 9:
        if (el == 2) {
            k = 2;
        }
        break;
    case 10:
        k = el == 2 || el == 0;
        break;
    case 8:
        k = (unsigned)(el - 1) < 2;
        break;
    case 2:
        k = el == 4 || el == 0;
        break;
    case 6:
        k = el == 3 || el == 1;
        break;
    default:
        break;
    }
    if (k == 2) {
        return v * 15 / 10;
    }
    if (k == 1) {
        return v * 12 / 10;
    }
    return v;
}

/* FUN_10009df8: a weapon (kind 5) adds its value, of the object's own or
 * its target's equipment */
static int weapon_bonus(const pk_object *o, int v, int of_target)
{
    const int *e = of_target ? pk_b.eq[1 - rs(o)][o->target % 6] : eqp(o);
    return e[0] == 5 ? v + e[7] : v;
}

static int armour_bonus(const pk_object *o, int v) /* FUN_10009ef8 (kind 6) */
{
    const int *e = eqp(o);
    return e[0] == 6 ? v + e[7] : v;
}

/* FUN_1000a1b4: element against element (attacker box a, defender d) */
static int element_mod(const pk_object *a, const pk_object *d, int v)
{
    const int *ea = eqp(a), *ed = pk_b.eq[rs(a) ^ 1][sl(d)];
    int x, y, k = 0;
    if (ea[0] == 0xff && ed[0] == 0xff) {
        return v;
    }
    if (ea[2] == 0xff && ed[0] == 0xff) {
        return v;
    }
    x = ea[2];
    y = ed[2];
    switch (x) {
    case 0:
        k = y == 1 ? 1 : y == 3  ? 2
                     : y == 0xff ? 3
                                 : 0;
        break;
    case 1:
        k = y == 0 ? 2 : y == 4  ? 1
                     : y == 0xff ? 3
                                 : 0;
        break;
    case 2:
        k = y == 3 ? 1 : y == 4  ? 2
                     : y == 0xff ? 3
                                 : 0;
        break;
    case 3:
        k = y == 0 ? 1 : y == 2  ? 2
                     : y == 0xff ? 3
                                 : 0;
        break;
    case 4:
        k = y == 1 ? 2 : y == 2  ? 1
                     : y == 0xff ? 3
                                 : 0;
        break;
    case 0xff:
        k = 4;
        break;
    default:
        break;
    }
    switch (k) {
    case 1:
        return v * 5 / 10;
    case 2:
        return v * 2;
    case 3:
        return v * 12 / 10;
    case 4:
        return v * 8 / 10;
    default:
        return v;
    }
}

#include "game/pk_battle_effects.inc"

/* ------------------------------------------------------------------ */
/* FUN_1000dfcc: attack boxes of the other side against the unit */

static void rect_of(const pk_object *o, SDL_Rect *r)
{
    r->x = o->x + o->x2 - o->w;
    r->y = o->y + o->y2 - o->h;
    r->w = o->w * 2;
    r->h = o->h * 2;
}

static bool rects_meet(const SDL_Rect *a, const SDL_Rect *b) /* TRect::Intersects */
{
    if (a->w <= 0 || a->h <= 0 || b->w <= 0 || b->h <= 0) {
        return false;
    }
    return a->x < b->x + b->w && b->x < a->x + a->w && a->y < b->y + b->h && b->y < a->y + a->h;
}

static bool hit_check(pk_game *g, pk_object *u)
{
    int i, n = g->objects->count;
    for (i = 0; i < n; i++) {
        pk_object *e = g->objects->items[i];
        SDL_Rect a, b;
        int a4, lv, atkv = 0, defv = 0, d;
        bool hit = false;
        if (e->type != PK_OBJ_TYPE_ATTACK || u->arg0 == e->arg0) {
            continue;
        }
        rect_of(u, &a);
        rect_of(e, &b);
        if (!rects_meet(&a, &b)) {
            continue;
        }
        lv = level(e) * 3;
        if (lv >= 0x10) {
            lv = 0xf;
        }
        a4 = agi(u) / 4;
        if (pct(g) >= acc(e) + lv - a4) {
            continue;
        }
        equip_on_hit(g, u, e);
        if (!(e->flags & 0x800000)) {
            if (e->flags & FLIP) {
                u->flags &= ~FLIP;
            } else {
                u->flags |= FLIP;
            }
        } else {
            if (e->flags & FLIP) {
                u->flags |= FLIP;
            } else {
                u->flags &= ~FLIP;
            }
        }
        u->target = e->arg1;
        if (!has_prop(u, 0x2c)) {
            if (!(status(e) & 2)) { /* weapons */
                int v = atk(e);
                if ((status(e) & 4) && flying(u) == 1) {
                    v *= 2;
                }
                atkv = field_mod(e, v, 0);
                defv = field_mod(u, def(u), 0);
                hit = true;
            } else if (!(status(u) & 0x8000)) { /* magic */
                bool own = !(e->flags & F_REFLECTED);
                int m = own ? field_mod(e, mag(e), 0) : field_mod(e, target_mag(e), 1);
                atkv = weapon_bonus(e, m, !own);
                defv = field_mod(u, mag(u), 0);
                atkv = armour_bonus(u, atkv);
                hit = true;
            } else { /* the unit turns magic back */
                if (!(e->flags & F_REFLECTED)) {
                    Uint32 f = e->flags;
                    e->flags = f | F_REFLECTED;
                    e->target = e->arg1;
                    e->arg0 = u->arg0;
                    e->arg1 = u->arg1;
                    e->flags = (f & FLIP) ? ((f & ~FLIP) | F_REFLECTED) : (e->flags | FLIP);
                    sfx(0x1c);
                } else {
                    e->flags |= F_KNOCKDOWN;
                }
                FX(g, 0x25, u->arg0, u->arg1, e->x, e->y);
            }
        } else { /* the unit bursts */
            FX(g, 0x28, u->arg0, u->arg1, u->x, u->y - 0x1e);
            FX(g, 0x28, u->arg0, u->arg1, u->x + 10, u->y - 0x14);
            FX(g, 0x28, u->arg0, u->arg1, u->x - 10, u->y - 0x14);
            eff_burst(g, u);
            atkv = 1000;
            defv = 0;
            hit = true;
        }
        if (!hit) {
            continue;
        }
        e->flags |= F_KNOCKDOWN;
        u->flags |= F_STRUCK;
        *cnt(u, 0x7d8) = 0x16;
        d = element_mod(e, u, atkv - defv);
        if (e->flags & F_CRITICAL) {
            d *= 2;
            e->flags &= ~F_CRITICAL;
            sfx(8);
        } else {
            sfx((status(e) & 2) ? 0xc : 0xb);
        }
        if (!(status(u) & 0x3500) && pct(g) < a4) {
            d /= 2;
            u->flags |= F_STAGGER;
        } else {
            u->flags |= F_KNOCKDOWN;
        }
        if (!(status(e) & 2)) {
            int s = status(u);
            if (s & 0x70000) {
                FX(g, 0x42, u->arg0, u->arg1, u->x, u->y - 0x14);
                if (s & 0x40000) {
                    d /= 2;
                } else if (s & 0x20000) {
                    d = d * 3 / 5;
                } else {
                    d -= d / 5;
                }
            }
        } else if (status(u) & 0x80000) {
            FX(g, 0x43, u->arg0, u->arg1, u->x, u->y - 0x14);
            d -= d / 4;
        }
        if (d < 1) {
            d = 1;
        }
        damage(g, u, d);
        return true;
    }
    if (status(u) & 0x60) { /* poison */
        if (--*cnt(u, 0x718) == 0) {
            *cnt(u, 0x748) = 0x28;
            damage(g, u, (status(u) & 0x20) ? 5 : 10);
            FX(g, 0x31, u->arg0, u->arg1, u->x, u->y - 0xf);
            return true;
        }
    }
    return false;
}

/* ------------------------------------------------------------------ */
/* attack boxes and projectiles (0x100122f0) */

static void shot_from(pk_object *o, const pk_object *u, int dx, int dy, bool flip_left)
{
    int ax = o->x < u->x ? u->x - o->x : o->x - u->x, t = ax / 6, vy;
    if (t < 1) {
        t = 1;
    }
    vy = (o->y - u->y) / t;
    o->work[0] = vy > 6 ? 6 : vy;
    if (!(u->flags & FLIP)) {
        o->x = u->x + dx;
        o->flags |= FLIP;
    } else {
        o->x = u->x - dx;
        if (flip_left) {
            o->flags &= ~FLIP;
        }
    }
    o->y = u->y - dy;
}

static void attack_update(pk_object *o, pk_game *g)
{
    pk_object *u;
    if (pk_b.over && o->frames && !(o->flags & F_OVERRUN)) {
        return;
    }
    u = owner(g, o);
    switch (o->state) {
    case 0:
    case 1:
    case 2:
        /* (box 1 runs on into box 2 in the original: both are 0x19 wide) */
        o->flags = 0x100400 | (u ? (u->flags & FLIP) : 0);
        box(o, 0, -0xf, o->state == 0 ? 0xf : 0x19, 0xf);
        clear(o);
        o->state = 3;
        return;
    case 3:
        if (u && !(u->flags & F_KNOCKDOWN)) {
            if (!(o->flags & F_KNOCKDOWN)) {
                if (++o->work[0] < 5) {
                    return;
                }
            } else {
                FX(g, 0, o->arg0, o->arg1, o->x, o->y);
            }
        }
        break;
    case 4:
    case 6:
    case 8:
    case 10:
    case 12:
        if (!u) {
            break;
        }
        o->image = pk_game_image_slot(g, 0x60);
        pk_object_set_frames(o, 0x100816e0u);
        switch (o->state) {
        case 4:
            o->flags = 0x900411;
            status_set(o, 2);
            anim(o, 0x10081878u);
            box(o, 0, 0, 10, 10);
            clear(o);
            shot_from(o, u, 0x23, 0x19, false);
            break;
        case 6:
            o->flags = 0x900401;
            o->frame = 1;
            box(o, 0, 0, 10, 4);
            clear(o);
            shot_from(o, u, 0x10, 0x10, false);
            break;
        case 8:
            o->flags = 0x900401;
            o->frame = 4;
            box(o, 0, 0, 10, 10);
            clear(o);
            status_set(o, 4);
            shot_from(o, u, 0x19, 0x17, false);
            break;
        case 10:
            o->flags = 0x900401;
            anim(o, 0x10081890u);
            o->layer = 1;
            box(o, 0, 0, 10, 10);
            shot_from(o, u, 0x10, 0x1e, true);
            break;
        default:
            o->flags = 0x900401;
            o->frame = 0;
            box(o, 0, 0, 10, 10);
            clear(o);
            shot_from(o, u, 0x10, 0x10, false);
            break;
        }
        o->state++;
        return;
    case 5:
    case 7:
    case 9:
    case 11:
    case 13:
        pk_object_animate(o);
        o->x += (o->flags & FLIP) ? 6 : -6;
        o->y += o->work[0];
        if (o->flags & F_KNOCKDOWN) {
            FX(g, 0, o->arg0, o->arg1, o->x, o->y);
            break;
        }
        if ((unsigned)(o->x + 10) < 0x175u) {
            return;
        }
        break;
    default:
        return;
    }
    pk_object_kill(o);
}

static pk_object *spawn_attack(pk_game *g, int state, const pk_object *u, int x, int y)
{
    return pk_object_spawn(g, attack_update, PK_OBJ_TYPE_ATTACK, state, u->arg0, u->arg1, x, y);
}

/* ------------------------------------------------------------------ */
/* shared unit behaviour (0x10022000 .. 0x10023044) */

/* FUN_100218b8: the unit's target (object or NULL) */
static pk_object *target(pk_game *g, pk_object *o)
{
    int er = rs(o) ^ 1, mode, best = 0xff, v, i;
    int (*e)[23] = pk_b.rec[er];
    if (o->target != 0xff) {
        int *r = e[o->target % 6];
        if (r[0] == 0xff || r[22] != 0) {
            return NULL;
        }
        return find(g, r[0], o->target, o->arg0 == 0 ? 1 : 0);
    }
    if (o->state == 3) {
        /* support: the most hurt ally decides the rule (as written, the
         * lookup uses the type of the side's last record) */
        int (*a)[23] = pk_b.rec[rs(o)], low = 0xff, who = 0xff;
        pk_object *ally;
        for (i = 0; i < 6; i++) {
            int *r = a[i];
            if (r[0] != 0xff && r[22] == 0 && r[7] < low && r[7] > 0) {
                low = r[7];
                who = i;
            }
        }
        ally = find(g, a[5][0], who, o->arg0);
        if (!ally) {
            mode = 5;
        } else {
            mode = pk_b.target[ally->arg0 == pk_b.player_side ? 0 : 1][ally->arg1 % 6];
        }
    } else {
        mode = pk_b.target[rs(o)][sl(o)];
    }
    switch (mode) {
    case 0:
        v = 0;
        for (i = 0; i < 6; i++) {
            if (e[i][0] != 0xff && e[i][22] == 0 && v < e[i][7]) {
                v = e[i][7];
                best = i;
            }
        }
        break;
    case 1:
        v = 0xff;
        for (i = 0; i < 6; i++) {
            if (e[i][0] != 0xff && e[i][22] == 0 && e[i][7] < v) {
                v = e[i][7];
                best = i;
            }
        }
        break;
    case 2:
        v = 0;
        for (i = 0; i < 6; i++) {
            if (e[i][0] != 0xff && e[i][22] == 0 && v < e[i][14]) {
                v = e[i][14];
                best = i;
            }
        }
        break;
    case 3:
        v = 0xff;
        for (i = 0; i < 6; i++) {
            if (e[i][0] != 0xff && e[i][22] == 0 && e[i][14] < v) {
                v = e[i][14];
                best = i;
            }
        }
        break;
    case 4:
        for (i = 0; i < 6; i++) {
            if (e[i][0] != 0xff && e[i][22] == 0) {
                best = i;
            }
        }
        break;
    default:
        for (i = 5; i >= 0; i--) {
            if (e[i][0] != 0xff && e[i][22] == 0) {
                best = i;
            }
        }
        break;
    }
    if (best == 0xff) {
        return NULL;
    }
    return find(g, e[best][0], best, o->arg0 == 0 ? 1 : 0);
}

/* FUN_1000a5f8: statuses that wear off */
static void status_wear(pk_game *g, pk_object *o)
{
    if (!(status(o) & 0x570)) {
        return;
    }
    if ((status(o) & 0x410) && pct(g) < mag(o) / 2) {
        status_clr(o, 0x410);
    }
    if ((status(o) & 0x160) && pct(g) < res(o) / 2) {
        status_clr(o, 0x160);
    }
}

/* FUN_10022000: the next action from the action weights */
static void think(pk_game *g, pk_object *o)
{
    pk_object *ctl = find(g, OBJ_CONTROLLER, 0, 0);
    int *w = pk_b.weight[rs(o)][sl(o)], acc5[5], sum = 0, i, r, lv;
    o->counter = 0;
    o->work[1] = 0;
    o->target = 0xff;
    if (ctl && ctl->state > 3) {
        o->state = 4;
        o->work[0] = 0xfa;
        return;
    }
    for (i = 0; i < 5; i++) {
        sum += w[i];
        acc5[i] = sum;
    }
    r = sum > 0 ? rnd(g) % sum : 0;
    if (r < acc5[0]) {
        o->state = 2;
    } else if (r < acc5[1]) {
        o->state = 3;
    } else if (r < acc5[2]) {
        o->state = 4;
    } else if (r < acc5[3]) {
        o->state = 5;
    } else if (r < acc5[4] && !(status(o) & 8)) {
        if (!(o->flags & F_SPECIAL)) {
            o->state = 6;
            o->flags |= F_SPECIAL;
        } else {
            o->state = 2;
        }
    } else {
        o->state = 1;
    }
    if (status(o) & 0x4000) {
        o->state = 4;
    }
    lv = level(o) / 3;
    if (lv > 4) {
        lv = 5;
    }
    o->work[0] = (0x1e - lv) - div10(agi(o));
    status_clr(o, 1);
    if (o->state != 6) {
        o->flags &= ~F_SPECIAL;
    }
    status_wear(g, o);
}

/* FUN_10022f44: walks towards the target; true when it is in reach */
static bool approach(pk_object *o, const pk_object *t, int speed, int reach)
{
    reach += (o->flags & F_SMALL) ? 0xf : (o->flags & F_MEDIUM) ? 0x14
                                                                : 0x19;
    reach += (t && (t->flags & F_SMALL)) ? 8 : (t && (t->flags & F_MEDIUM)) ? 0xf
                                                                            : 0x14;
    if (!t || t->x <= o->x) {
        o->flags |= FLIP;
    } else {
        o->flags &= ~FLIP;
    }
    if (!t) {
        return false;
    }
    if (flying(o) == 1) {
        if (o->y < t->y) {
            o->y += speed;
            if (o->y >= t->y) {
                o->y = t->y;
            }
        } else if (o->y > t->y) {
            o->y -= speed;
            if (t->y >= o->y) {
                o->y = t->y;
            }
        }
    }
    if (!(o->flags & FLIP)) {
        o->x += speed;
        if (t->x < o->x) {
            return false;
        }
        if (t->x > o->x + reach) {
            return false;
        }
    } else {
        o->x -= speed;
        if (t->x < o->x - reach) {
            return false;
        }
        if (t->x > o->x) {
            return false;
        }
    }
    return flying(o) != 1 || t->y <= o->y + 10;
}

/* FUN_10022538: backs away from the target */
static void retreat(pk_object *o, const pk_object *t, Uint32 a, int speed)
{
    if (!t) {
        to_think(o);
        return;
    }
    if (o->work[1] == 0) {
        anim(o, a);
        o->work[1] = 1;
        return;
    }
    if (o->work[1] != 1) {
        return;
    }
    if (o->x < t->x) {
        o->flags |= FLIP;
    } else {
        o->flags &= ~FLIP;
    }
    if (flying(o) == 1) {
        o->y = o->y < 0x6a ? 0x69 : o->y - speed;
    }
    if (!(o->flags & FLIP)) {
        o->x += speed;
        if (o->x > 0x14b) {
            o->x = 0x14c;
            o->work[0] = 0;
        }
    } else {
        o->x -= speed;
        if (o->x < 0x15) {
            o->x = 0x14;
            o->work[0] = 0;
        }
    }
    if (--o->work[0] < 1) {
        to_think(o);
    }
}

static void idle(pk_object *o, Uint32 a) /* FUN_100224d8 */
{
    if (o->work[1] == 0) {
        anim(o, a);
        o->work[1] = 1;
    } else if (o->work[1] == 1 && --o->work[0] < 1) {
        to_think(o);
    }
}

static void attack_end(pk_object *o, int n) /* FUN_10022ef0 */
{
    if (n + (0xf - div10(agi(o))) < o->counter) {
        to_think(o);
    }
}

/* FUN_10022638 / FUN_100226e0: staggered (a frame or an animation) */
static void stagger(pk_object *o, int frame, Uint32 a)
{
    if (o->work[1] == 0) {
        anim_stop(o);
        if (a) {
            anim(o, a);
        } else {
            o->frame = frame;
        }
        o->work[0] = 0x19 - agi(o) / 5;
        o->work[1] = 1;
        status_set(o, 1);
        if (!a) {
            sfx(9);
        }
    } else if (o->work[1] == 1 && --o->work[0] < 1) {
        to_think(o);
    }
}

/* FUN_100228c4: knocked back, then up again, revived or gone */
static void knocked(pk_game *g, pk_object *o)
{
    if (o->arg1 > 5) {
        return;
    }
    if (++o->work[0] < 5 && !(status(o) & 0x10006000) && !pk_b.over) {
        if (!(o->flags & FLIP)) {
            o->x -= 2;
            if (o->x < 0x15) {
                o->x = 0x14;
            }
        } else {
            o->x += 2;
            if (o->x > 0x14b) {
                o->x = 0x14c;
            }
        }
    }
    if (o->work[0] < 10) {
        return;
    }
    if (hp(o) > 0) {
        clear(o);
        o->flags &= ~F_KNOCKDOWN;
        if (status(o) & 0x400) {
            o->state = 0xb;
            return;
        }
        if (status(o) & 0x2000) {
            o->state = 0xc;
            return;
        }
        {
            pk_object *t = target(g, o);
            int s = status(o);
            if ((s & 0xe00000) && t) {
                int chance = (s & 0x800000) ? 0x3c : (s & 0x400000) ? 0x28
                                                                    : 0x14;
                if (pct(g) < chance) {
                    if (o->x < t->x) {
                        o->flags &= ~FLIP;
                    } else {
                        o->flags |= FLIP;
                    }
                    if (!(o->flags & FLIP) ? (o->x - 8 <= t->x && t->x <= o->x + 0x28)
                                           : (o->x - 0x28 <= t->x && t->x <= o->x + 8)) {
                        o->flags &= ~F_KNOCKDOWN;
                        o->state = 2;
                        o->work[0] = 0;
                        o->work[1] = 2;
                        return;
                    }
                }
            }
        }
        o->state = 1;
        return;
    }
    sfx(0xd);
    if (unit_dies(g, o)) {
        pk_battle_panel(g, o, 3);
        pk_object_kill(o);
        return;
    }
    clear(o);
    o->flags &= ~F_KNOCKDOWN;
    o->work[0] = 0x14;
    o->state = 4;
}

/* FUN_1002278c / FUN_10022824: knocked down (a frame or an animation) */
static void knockdown(pk_game *g, pk_object *o, int frame, Uint32 a)
{
    if (o->work[1] == 0) {
        *cnt(o, 0x748) = 0;
        status_clr(o, 0x1100);
        o->flags &= ~F_CRITICAL;
        anim_stop(o);
        if (a) {
            anim(o, a);
        } else {
            o->frame = frame;
        }
        o->work[0] = 0;
        o->work[1] = 1;
    } else if (o->work[1] == 1) {
        knocked(g, o);
    }
}

static void numb(pk_object *o, int frame) /* FUN_10022b90: status 0x1000 */
{
    if (o->work[1] == 0) {
        *cnt(o, 0x748) = 0x21;
        anim_stop(o);
        o->frame = frame;
        o->work[1]++;
    } else if (o->work[1] == 1) {
        if (--*cnt(o, 0x748) < 1) {
            *cnt(o, 0x748) = 0;
            status_clr(o, 0x1000);
            to_think(o);
        }
    }
}

static void frozen(pk_object *o, int frame) /* FUN_10022c98: status 0x2000 */
{
    if (o->work[1] == 0) {
        anim_stop(o);
        o->frame = frame;
        o->work[1]++;
    } else if (o->work[1] == 1 && --*cnt(o, 0x778) < 1) {
        status_clr(o, 0x2000);
        to_think(o);
    }
}

static void held(pk_object *o, int frame, int bit) /* FUN_10022d68 (0x400) / FUN_10022dc0 (0x100) */
{
    if (o->work[0] == 0) {
        anim_stop(o);
        o->frame = frame;
        o->work[0] = 1;
    }
    if (!(status(o) & bit)) {
        to_think(o);
    }
}

/* FUN_10022230: hits, regeneration; true when the unit sits out the tick */
static bool pre_tick(pk_game *g, pk_object *o)
{
    if (!(o->flags & 0xd0000) && o->state != 7) {
        if (!down(o) && hit_check(g, o)) {
            if (o->flags & F_KNOCKDOWN) {
                if ((status(o) & 0x10000000) && hp(o) > 0) {
                    o->flags &= ~F_KNOCKDOWN;
                } else {
                    clear(o);
                    o->state = 7;
                }
            } else {
                clear(o);
                o->state = 8;
            }
        }
    } else if ((o->flags & F_KNOCKDOWN) && o->state != 7) {
        clear(o);
        o->state = 7;
    }
    if (pk_b.over) {
        return !(o->flags & F_OVERRUN);
    }
    if ((o->flags & F_STRUCK) && --*cnt(o, 0x7d8) < 1) {
        o->flags &= ~F_STRUCK;
    }
    if ((status(o) & 0x7000000) && --*cnt(o, 0x7a8) < 1) {
        int s = status(o);
        sfx(10);
        FX(g, 0x41, o->arg0, o->arg1, o->x, o->y - 0x14);
        *cnt(o, 0x7a8) = 0x21;
        heal(o, (s & 0x4000000) ? 10 : (s & 0x2000000) ? 5
                                                       : 2);
    }
    return false;
}

/* ------------------------------------------------------------------ */
/* the classes */

typedef struct unit_class
{
    void (*fn)(pk_game *g, pk_object *o, pk_object *t, const struct unit_class *c);
    Uint32 frames, idle; /* state 0 */
    Sint8 by;
    Uint8 bw, bh;      /* box: offset y, half width / height */
    Uint32 rest, back; /* states 4 / 5 */
    Uint8 back_speed;
    Uint8 down_frame; /* state 7 (or down_anim) */
    Uint32 down_anim;
    Uint8 hit_frame; /* state 8 (or hit_anim) */
    Uint32 hit_anim;
    Uint8 f10, f11, f12, f13;               /* frames of states 10..13 */
    Uint32 walk, swing;                     /* attack: animations */
    Uint8 speed, reach, at, boxst, dx, end; /* approach, box at counter `at`, box state, x offset, end */
    Sint8 dy;
    Uint8 snd;          /* sound with the box (0 none) */
    Uint32 init_status; /* statuses from the start */
    Uint8 fly;          /* starts in the air (y 0x69) */
} unit_class;

#include "game/pk_battle_classes.inc"

/* the states every class shares */
static void common_states(pk_game *g, pk_object *o, pk_object *t, const unit_class *c)
{
    switch (o->state) {
    case 0:
        pk_object_set_frames(o, c->frames);
        anim(o, c->idle);
        o->flags |= PK_OBJ_SPRITE;
        box(o, 0, c->by, c->bw, c->bh);
        clear(o);
        if (c->init_status) {
            status_set(o, (int)c->init_status);
        }
        if (c->fly) {
            o->y = 0x69;
        }
        o->state = 9;
        break;
    case 1:
        think(g, o);
        break;
    case 4:
        idle(o, c->rest);
        break;
    case 5:
        retreat(o, t, c->back, c->back_speed);
        break;
    case 6:
        to_think(o);
        break;
    case 7:
        knockdown(g, o, c->down_frame, c->down_anim);
        break;
    case 8:
        stagger(o, c->hit_frame, c->hit_anim);
        break;
    case 9:
        if (pk_b.started == 1) {
            o->state = 1;
        }
        break;
    case 10:
        numb(o, c->f10);
        break;
    case 11:
        held(o, c->f11, 0x400);
        break;
    case 12:
        frozen(o, c->f12);
        break;
    case 13:
        held(o, c->f13, 0x100);
        break;
    default:
        break;
    }
}

/* the usual close attack: walk up, swing, the box at counter `at`;
 * `crit` >= 0 rolls a critical hit with that chance as the swing starts */
static bool melee(pk_game *g, pk_object *o, pk_object *t, const unit_class *c, int crit)
{
    if (o->state != 2 && o->state != 3) {
        return false;
    }
    if (!t) {
        to_think(o);
        return true;
    }
    switch (o->work[1]) {
    case 0:
        anim(o, c->walk);
        o->work[1] = 1;
        /* fall through */
    case 1:
        if (approach(o, t, c->speed, c->reach)) {
            o->work[1] = 2;
        }
        if (--o->work[0] < 1) {
            to_think(o);
        }
        break;
    case 2:
        anim(o, c->swing);
        o->work[1] = 3;
        o->counter = 0;
        if (crit >= 0 && pct(g) < crit) {
            o->flags |= F_CRITICAL;
        }
        break;
    case 3:
        if (++o->counter == c->at) {
            if (c->snd) {
                sfx(c->snd);
            }
            spawn_attack(g, c->boxst, o, o->x + dir(o, c->dx), o->y + c->dy);
        }
        attack_end(o, c->end);
        break;
    default:
        break;
    }
    return true;
}

static void cls_melee(pk_game *g, pk_object *o, pk_object *t, const unit_class *c)
{
    if (!melee(g, o, t, c, -1)) {
        common_states(g, o, t, c);
    }
}

#include "game/pk_battle_special.inc"

/* ------------------------------------------------------------------ */
/* the dispatcher (0x10020f3c) and the update */

static const unit_class *class_of(int type)
{
    int i = (type >= 0 && type < (int)SDL_arraysize(k_type_class)) ? k_type_class[type] : 46;
    return &k_class[i];
}

static void setup(pk_game *g, pk_object *o)
{
    int slot = o->arg1 & 0xff;
    o->image = pk_game_image_slot(g, o->type * 2 + 0x102);
    o->flags = 0x400;
    if (slot < 4) {
        int *e;
        o->arg1 = slot;
        if (o->arg0 == 0) {
            o->x = (3 - slot) * 0x24 + 0x20;
        } else {
            o->flags |= FLIP;
            o->x = slot * 0x24 + 200;
        }
        o->y = 0x99;
        e = pk_b.eq[o->arg0 == pk_b.player_side ? 0 : 1][slot];
        if (e[0] != 0xff) {
            equip_start(g, o, e[3]);
            equip_start(g, o, e[4]);
        }
    } else { /* summoned: slot | summoner << 8 */
        o->work[0] = 1;
        o->work[5] = (o->arg1 >> 8) & 0xff;
        o->arg1 = slot;
        if (o->type == 0x37) {
            o->flags |= 0x280000;
        }
    }
    apply_order(o);
    o->arg2 = o->arg0 != pk_b.player_side;
    if (o->type >= 0 && o->type < (int)SDL_arraysize(k_type_size)) {
        o->flags |= (Uint32)k_type_size[o->type] << 24;
    }
    o->state = 0;
}

static void unit_update(pk_object *o, pk_game *g)
{
    const unit_class *c = class_of(o->type);
    pk_object *t;
    if (c->fn == cls_1002367c) {
        idol_check(g, o);
    }
    if (pre_tick(g, o)) {
        return;
    }
    t = target(g, o);
    pk_object_animate(o);
    c->fn(g, o, t, c);
}

/* the dispatcher runs once, then the class behaviour takes over */
static void unit_dispatch(pk_object *o, pk_game *g)
{
    setup(g, o);
    pk_object_set_update(o, unit_update);
}

pk_object *pk_bu_spawn(pk_game *g, int type, int screen_side, int slot, int x, int y)
{
    return pk_object_spawn(g, unit_dispatch, type, 0, screen_side, slot, x, y);
}

void pk_battle_unit_look(int type, Uint32 *frames, Uint32 *idle)
{
    const unit_class *c = class_of(type);
    *frames = c->frames;
    *idle = c->rest;
}

/* traps 1 and 2 (FUN_100056d4 / FUN_10005ba4 per attacker unit) */
void pk_bu_trap_status(pk_game *g, pk_object *u, int trap)
{
    if (trap == 1) {
        st_poison(g, u, 0x20); /* FUN_10007b28 */
    } else {
        st_seal(g, u); /* FUN_10007f48 */
    }
}
