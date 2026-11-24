/* SPDX-License-Identifier: MIT */
/* Battle classes with behaviour of their own (reimplementation),
 * included by pk_battle_unit.c. Each function is named after the class
 * behaviour it follows; the plain ones run cls_melee from the table.
 *
 * A special move usually: pins the camera on the unit (game+0xa8 mode 7),
 * stops everyone else (game+0x44c = 1; the unit itself carries 0x200000 so
 * it keeps moving), shows the special-move panel, plays the move once the
 * camera has settled (mode 6), pans to the target (mode 5) and ends with
 * the camera back in the player's hands (mode 0 → 8). */

#define CAM_MODE pk_b.cam[1]
#define W2 work[2]

static void cam_on(const pk_object *u, int mode)
{
    pk_b.cam[2] = u->x;
    pk_b.cam[3] = u->y;
    pk_b.cam[1] = mode;
}
static bool cam_ready(void) { return CAM_MODE == 6; }
static bool anim_done(const pk_object *o) { return !(o->flags & PK_OBJ_ANIMATING); }
static bool tick(int *v, int n) { return ++*v >= n; }
static void face(pk_object *o, const pk_object *t)
{
    if (o->x < t->x) o->flags &= ~FLIP; else o->flags |= FLIP;
}
static void sp_start(pk_object *o)
{
    o->flags |= F_OVERRUN | 0x80000u;
    pk_b.over = 1;
    cam_on(o, 7);
}
static void sp_release(pk_object *o) { o->flags &= ~(F_OVERRUN | 0x80000u); to_think(o); }
static void sp_abort(pk_object *o) { pk_b.over = 0; sp_release(o); }
static void sp_end(pk_object *o) { CAM_MODE = 0; pk_b.over = 0; sp_release(o); }
static void panel(pk_game *g, pk_object *o) { pk_battle_panel(g, o, 2); }

/* FUN_1000a4c8: the unit at a slot of the object's own side (down or not) */
static pk_object *ally_any(pk_game *g, const pk_object *o, int i)
{
    if (i < 0 || i > 5) return NULL;
    return find(g, pk_b.rec[rs(o)][i][0], i, o->arg0);
}

/* the luck roll of several special moves */
static bool sp_roll(pk_game *g, const pk_object *o) { return roll_luck(g, o); }

/* a roll that failed shows "miss" and lets the ending wait less */
static void sp_missed(pk_game *g, pk_object *o, const pk_object *t)
{
    o->W2 = 5;
    miss(g, o, t);
}

/* ------------------------------------------------------------------ */
/* attack variants */

static void melee_crit(pk_game *g, pk_object *o, pk_object *t, const unit_class *c, int chance)
{
    if (!melee(g, o, t, c, chance)) common_states(g, o, t, c);
}

/* spells and shots: face the target for `wait` ticks, then the box (a
 * projectile state) at the target's position */
typedef struct cast_def {
    Uint8 wait, at, box, snd, end;
    bool timeout, timeout_late, sleep_check;
} cast_def;

static bool cast(pk_game *g, pk_object *o, pk_object *t, const unit_class *c, const cast_def *d, int box)
{
    if (o->state != 2 && o->state != 3) return false;
    if (!t) { to_think(o); return true; }
    if (d->sleep_check && (status(o) & 0x10)) { o->state = 4; return true; }
    switch (o->work[1]) {
    case 0:
        anim(o, c->walk);
        o->work[1] = 1;
        o->W2 = 0;
        /* fall through */
    case 1:
        face(o, t);
        if (++o->W2 > d->wait) {
            o->work[1] = 2;
            o->W2 = 0;
        }
        if (d->timeout && --o->work[0] < 1) to_think(o);
        break;
    case 2:
        anim(o, c->swing);
        o->work[1] = 3;
        o->counter = 0;
        break;
    case 3:
        if (d->timeout_late && --o->work[0] < 1) to_think(o);
        if (++o->counter == d->at) {
            sfx(d->snd);
            spawn_attack(g, box, o, t->x, t->y);
        }
        attack_end(o, d->end);
        break;
    default:
        break;
    }
    return true;
}

/* ------------------------------------------------------------------ */
/* 0x100117f0: shoots after 10 ticks of aiming */
static void cls_100117f0(pk_game *g, pk_object *o, pk_object *t, const unit_class *c)
{
    static const cast_def d = { 10, 7, 8, 6, 10, true, false, false };
    if (!cast(g, o, t, c, &d, d.box)) common_states(g, o, t, c);
}

/* 0x10011b08: a caster; its special move summons a helper (type 0x37) */
static void cls_10011b08(pk_game *g, pk_object *o, pk_object *t, const unit_class *c)
{
    if (o->state == 2 || o->state == 3) {
        if (!t) { to_think(o); return; }
        if (status(o) & 0x10) { o->state = 4; return; }
        if (o->work[1] == 0) {
            anim(o, 0x100815dcu);
            o->work[1] = 1;
            if (o->x < t->x) o->flags &= ~FLIP;
            else if (t->x < o->x) o->flags |= FLIP;
            return;
        }
        if (o->work[1] != 1) return;
        if (--o->work[0] < 1) to_think(o);
        if (++o->counter == 8) {
            sfx(7);
            spawn_attack(g, 4, o, t->x, t->y);
        }
        attack_end(o, 0xc);
        return;
    }
    if (o->state == 6) {
        if (!t) { to_think(o); return; }
        if (status(o) & 0x10) { o->state = 4; return; }
        /* (sub-states 0..2 run on into each other, as in the original) */
        switch (o->work[1]) {
        case 0:
            if (free_summon_slot(o) == 0xff) {
                to_think(o);
            } else {
                face(o, t);
                sfx(0x1e);
                anim(o, 0x1008161cu);
                sp_start(o);
                o->work[1] = 1;
            }
            /* fall through */
        case 1:
            if (cam_ready() && anim_done(o)) {
                panel(g, o);
                o->work[1] = 2;
                o->W2 = 0;
            }
            /* fall through */
        case 2:
            if (++o->W2 > 10) {
                summon_imp(g, o);
                o->work[1] = 3;
                o->W2 = 0;
            }
            /* fall through */
        case 3:
            if (++o->W2 < 0x12) return;
            sp_end(o);
            return;
        default:
            return;
        }
    }
    common_states(g, o, t, c);
}

/* special moves that charge for a while, then strike every foe */
typedef struct charge_def {
    Uint32 anim;
    Uint8 wait, rest;
    bool count_from_zero;
    void (*effect)(pk_game *g, pk_object *o);
} charge_def;

static void special_charge(pk_game *g, pk_object *o, pk_object *t, const unit_class *c, const charge_def *d)
{
    if (!t) { sp_abort(o); return; }
    switch (o->work[1]) {
    case 0:
        sp_start(o);
        o->work[1] = 1;
        break;
    case 1:
        if (!cam_ready()) return;
        face(o, t);
        anim(o, d->anim);
        panel(g, o);
        o->work[1] = 2;
        if (d->count_from_zero) o->counter = 0;
        break;
    case 2:
        if (!tick(&o->counter, d->wait)) return;
        d->effect(g, o);
        anim(o, c->idle);
        o->work[1] = 3;
        return;
    case 3:
        if (!tick(&o->W2, d->rest)) return;
        sp_end(o);
        return;
    default:
        return;
    }
    if (!d->count_from_zero) o->W2 = 0;
}

static void eff_miasma_fx(pk_game *g, pk_object *o) { eff_miasma(g, o); }
static void eff_wave_fx(pk_game *g, pk_object *o) { eff_wave(g, o); }

/* 0x10012d68: special move: poison cloud */
static void cls_10012d68(pk_game *g, pk_object *o, pk_object *t, const unit_class *c)
{
    static const charge_def d = { 0x10081b3cu, 0x13, 10, true, eff_miasma_fx };
    if (o->state == 6) { special_charge(g, o, t, c, &d); return; }
    cls_melee(g, o, t, c);
}

/* 0x100131d8: plain fighter, turns magic back */
static void cls_100131d8(pk_game *g, pk_object *o, pk_object *t, const unit_class *c) { cls_melee(g, o, t, c); }

/* 0x10013540: special move: crystals fall on every foe */
static void cls_10013540(pk_game *g, pk_object *o, pk_object *t, const unit_class *c)
{
    if (o->state != 6) { cls_melee(g, o, t, c); return; }
    if (!t) { sp_abort(o); return; }
    switch (o->work[1]) {
    case 0: sp_start(o); o->work[1] = 1; return;
    case 1:
        if (!cam_ready()) return;
        face(o, t);
        anim(o, 0x100821acu);
        panel(g, o);
        o->work[1] = 2;
        o->counter = 0;
        return;
    case 2:
        if (!tick(&o->counter, 10)) return;
        cam_on(t, 5);
        anim(o, c->idle);
        o->work[1] = 3;
        return;
    case 3:
        if (!cam_ready()) return;
        eff_crystals(g, o);
        o->work[1] = 4;
        o->W2 = 0;
        return;
    case 4:
        if (!tick(&o->W2, 0x1e)) return;
        sp_end(o);
        return;
    default:
        return;
    }
}

/* flyers (0x100139e8, 0x1001f74c): rise to the top, then blast */
static void special_flyer(pk_game *g, pk_object *o, pk_object *t, const unit_class *c, Uint32 cast_anim, int wait, bool all)
{
    if (!t) { sp_abort(o); return; }
    switch (o->work[1]) {
    case 0:
        anim(o, c->walk);
        cam_on(o, 7);
        o->work[1] = 1;
        return;
    case 1:
        if (cam_ready()) o->work[1] = 2;
        return;
    case 2:
        if (o->y < 0x6a) {
            o->y = 0x69;
            cam_on(o, 5);
            o->work[1] = 3;
            return;
        }
        o->y -= 4;
        if (!(o->flags & FLIP)) {
            o->x -= 4;
            if (o->x < 0x15) o->x = 0x14;
        } else {
            o->x += 4;
            /* 0x1001f74c compares the other way round: facing left it
             * lands on the right edge at once (as in the original) */
            if (all ? o->x <= 0x14c : o->x > 0x14b) o->x = 0x14c;
        }
        return;
    case 3:
        face(o, t);
        panel(g, o);
        pk_b.over = 1;
        anim(o, cast_anim);
        o->flags |= F_OVERRUN | 0x80000u;
        o->work[1] = 4;
        o->counter = 0;
        return;
    case 4:
        if (!tick(&o->counter, wait)) return;
        o->work[1] = 5;
        cam_on(t, 5);
        return;
    case 5:
        if (!cam_ready()) return;
        if (all) eff_blast_all(g, o); else eff_blast(g, o, t);
        o->work[1] = 6;
        o->W2 = 0;
        return;
    case 6:
        if (!tick(&o->W2, 0xf)) return;
        sp_end(o);
        return;
    default:
        return;
    }
}

static void cls_100139e8(pk_game *g, pk_object *o, pk_object *t, const unit_class *c)
{
    if (o->state == 6) { special_flyer(g, o, t, c, 0x10082468u, 10, false); return; }
    cls_melee(g, o, t, c);
}

/* special moves that put foes to sleep (0x10013f10 one, 0x10014434 all) */
static void special_sleep(pk_game *g, pk_object *o, pk_object *t, Uint32 a, int wait, bool all)
{
    if (!t) { sp_abort(o); return; }
    switch (o->work[1]) {
    case 0:
        pk_b.over = 1;
        cam_on(o, 7);
        face(o, t);
        o->flags |= F_OVERRUN | 0x80000u;
        anim(o, a);
        o->work[1] = 1;
        o->W2 = 0;
        panel(g, o);
        return;
    case 1:
        if (!tick(&o->W2, wait)) return;
        cam_on(t, 5);
        o->work[1] = 2;
        return;
    case 2:
        if (!cam_ready()) return;
        if (all) {
            eff_magic_all(g, o, false);
        } else {
            int lv = level(o) > 4 ? 5 : level(o), lk = luck(o);
            if (pct(g) < lk + lv * 4) {
                sfx(0x1d);
                st_sleep(g, t);
            } else {
                miss(g, o, t);
            }
        }
        o->work[1] = 3;
        o->W2 = 0;
        return;
    case 3:
        if (!tick(&o->W2, 5)) return;
        sp_end(o);
        return;
    default:
        return;
    }
}

static void cls_10013f10(pk_game *g, pk_object *o, pk_object *t, const unit_class *c)
{
    if (o->state == 6) { special_sleep(g, o, t, 0x10082794u, 0x10, false); return; }
    cls_melee(g, o, t, c);
}

static void cls_10014434(pk_game *g, pk_object *o, pk_object *t, const unit_class *c)
{
    if (o->state == 6) { special_sleep(g, o, t, 0x10082b08u, 0x19, true); return; }
    cls_melee(g, o, t, c);
}

/* 0x100148a4: plain fighter; types 0x5e / 0x5f counter when hit */
static void cls_100148a4(pk_game *g, pk_object *o, pk_object *t, const unit_class *c)
{
    if (o->state == 0 && (o->type == 0x5e || o->type == 0x5f)) status_set(o, 0x200000);
    cls_melee(g, o, t, c);
}

/* 0x10014c08 / 0x10020280: special move: shock wave */
static void cls_10014c08(pk_game *g, pk_object *o, pk_object *t, const unit_class *c)
{
    static const charge_def d = { 0x100830f0u, 0xb, 10, false, eff_wave_fx };
    if (o->state == 6) { special_charge(g, o, t, c, &d); return; }
    cls_melee(g, o, t, c);
}

static void cls_10020280(pk_game *g, pk_object *o, pk_object *t, const unit_class *c)
{
    static const charge_def d = { 0x1008a84cu, 0xb, 10, false, eff_wave_fx };
    if (o->state == 6) { special_charge(g, o, t, c, &d); return; }
    cls_melee(g, o, t, c);
}

/* 0x10015064: special move: a chance to fell the target at once */
static void cls_10015064(pk_game *g, pk_object *o, pk_object *t, const unit_class *c)
{
    if (o->state != 6) { cls_melee(g, o, t, c); return; }
    if (!t) { sp_abort(o); return; }
    switch (o->work[1]) {
    case 0: sp_start(o); o->work[1] = 1; return;
    case 1:
        if (!cam_ready()) return;
        face(o, t);
        anim(o, 0x1008344cu);
        panel(g, o);
        o->work[1] = 2;
        o->counter = 0;
        return;
    case 2:
        if (!tick(&o->counter, 0x11)) return;
        anim(o, c->idle);
        cam_on(t, 5);
        o->work[1] = 3;
        return;
    case 3:
        if (!cam_ready()) return;
        if (pct(g) < (10 - rank(t)) * 5) {
            o->W2 = 0;
            sfx(0x15);
            FX(g, 0x47, o->arg0, o->arg1, t->x, t->y - 0x14);
            t->flags |= F_KNOCKDOWN;
            damage(g, t, 500);
        } else {
            sp_missed(g, o, t);
        }
        o->work[1] = 4;
        return;
    case 4:
        if (!tick(&o->W2, 10)) return;
        sp_end(o);
        return;
    default:
        return;
    }
}

/* 0x1001724c: special move: a spike under the target */
static void cls_1001724c(pk_game *g, pk_object *o, pk_object *t, const unit_class *c)
{
    int next;
    if (o->state != 6) { cls_melee(g, o, t, c); return; }
    if (!t) { sp_abort(o); return; }
    if (status(o) & 0x10) { to_think(o); return; }
    if (o->type == 0x4b) to_think(o); /* this type has none (and runs on, as written) */
    switch (o->work[1]) {
    case 0:
        anim(o, 0x10085644u);
        next = 1;
        break;
    case 1:
        face(o, t);
        if (!tick(&o->W2, 0x16)) return;
        pk_b.over = 1;
        cam_on(o, 7);
        o->flags |= F_OVERRUN | 0x80000u;
        o->work[1] = 2;
        o->W2 = 0;
        panel(g, o);
        return;
    case 2:
        if (!tick(&o->W2, 0xb)) return;
        cam_on(t, 5);
        next = 3;
        break;
    case 3:
        if (!cam_ready()) return;
        sfx(0x12);
        FX(g, 0x38, o->arg0, o->arg1, t->x, 0x98);
        next = 4;
        break;
    case 4:
        if (!tick(&o->W2, 6)) return;
        eff_type_blow(g, o, t);
        FX(g, 1, o->arg0, o->arg1, t->x, t->y - 0x14);
        next = 5;
        break;
    case 5:
        if (!tick(&o->W2, 8)) return;
        sp_end(o);
        return;
    default:
        return;
    }
    o->work[1] = next;
    o->W2 = 0;
}

/* 0x100177c4: special move: binds every foe */
static void cls_100177c4(pk_game *g, pk_object *o, pk_object *t, const unit_class *c)
{
    if (o->state != 6) { cls_melee(g, o, t, c); return; }
    if (!t) { sp_abort(o); return; }
    switch (o->work[1]) {
    case 0: sp_start(o); o->work[1] = 1; return;
    case 1:
        if (!cam_ready()) return;
        face(o, t);
        anim(o, 0x10085938u);
        panel(g, o);
        o->work[1] = 2;
        o->counter = 0;
        return;
    case 2:
        if (++o->counter == 6) { sfx(0x14); return; }
        if (o->counter < 0xf) return;
        cam_on(t, 5);
        anim(o, c->idle);
        o->work[1] = 3;
        return;
    case 3:
        if (cam_ready()) {
            eff_bind_all(g, o);
            o->work[1] = 4;
            o->W2 = 0;
        }
        /* fall through - the wait counts from here, as written */
    case 4:
        if (!tick(&o->W2, 10)) return;
        sp_end(o);
        return;
    default:
        return;
    }
}

/* 0x10017f9c: plain fighter, counters often */
static void cls_10017f9c(pk_game *g, pk_object *o, pk_object *t, const unit_class *c) { cls_melee(g, o, t, c); }

/* 0x100182e0: special move: charges across the field from the edge */
static void cls_100182e0(pk_game *g, pk_object *o, pk_object *t, const unit_class *c)
{
    int step = o->arg0 == 0 ? 1 : -1;
    if (o->state != 6) { cls_melee(g, o, t, c); return; }
    if (!t) { sp_abort(o); return; }
    switch (o->work[1]) {
    case 0:
        panel(g, o);
        pk_b.over = 1;
        cam_on(t, 7);
        o->work[1] = 1;
        o->flags |= F_OVERRUN | 0x80000u;
        return;
    case 1:
        if (!cam_ready()) return;
        if (o->arg0 == 0) {
            o->flags &= ~FLIP;
            o->x = g->cam_x;
        } else {
            o->flags |= FLIP;
            o->x = g->cam_x + 0xb0;
        }
        anim(o, 0x1008610cu);
        o->work[1] = 2;
        return;
    case 2:
        o->x += step * 3;
        if (anim_done(o)) {
            o->flags &= ~(Uint32)PK_OBJ_SPRITE;
            o->work[1] = 3;
            o->W2 = 0;
        }
        return;
    case 3:
        o->x += step * 0xf;
        if (!tick(&o->W2, 6)) return;
        sfx(0x1b);
        FX(g, 0x26, o->arg0, 0, g->cam_x, 0);
        anim(o, 0x1008616cu);
        o->flags |= PK_OBJ_SPRITE;
        o->work[1] = 4;
        o->W2 = 0;
        return;
    case 4:
        o->x += step * 3;
        if (!anim_done(o)) return;
        eff_all_fresh(g, o, 0x5a);
        CAM_MODE = 0;
        sp_abort(o);
        return;
    default:
        return;
    }
}

/* 0x10018814: closes in to 6 px, the box close by */
static void cls_10018814(pk_game *g, pk_object *o, pk_object *t, const unit_class *c)
{
    if (o->state != 2 && o->state != 3) { common_states(g, o, t, c); return; }
    if (!t) { to_think(o); return; }
    switch (o->work[1]) {
    case 0:
        anim(o, c->walk);
        o->work[1] = 1;
        /* fall through */
    case 1: {
        int x;
        if (approach(o, t, 2, 10)) o->work[1] = 2;
        face(o, t);
        x = o->x;
        if (!(o->flags & FLIP)) {
            o->x = x + 2;
            if (x - 6 <= t->x && t->x <= x + 0x22) o->work[1] = 2;
        } else {
            o->x = x - 2;
            if (x - 0x22 <= t->x && t->x <= x + 6) o->work[1] = 2;
        }
        if (--o->work[0] < 1) to_think(o);
        break;
    }
    case 2:
        anim(o, c->swing);
        o->work[1] = 3;
        o->counter = 0;
        break;
    case 3:
        if (++o->counter == 5) spawn_attack(g, 1, o, o->x + dir(o, 10), o->y - 0x14);
        attack_end(o, 0x14);
        break;
    default:
        break;
    }
}

/* 0x10018f18: special move: walks up and numbs the target */
static void cls_10018f18(pk_game *g, pk_object *o, pk_object *t, const unit_class *c)
{
    if (o->state != 6) { cls_melee(g, o, t, c); return; }
    if (!t) { sp_abort(o); return; }
    switch (o->work[1]) {
    case 0:
        anim(o, c->walk);
        o->work[1] = 1;
        /* fall through */
    case 1:
        if (approach(o, t, 2, 0x19)) o->work[1] = 2;
        if (--o->work[0] < 1) to_think(o);
        return;
    case 2:
        panel(g, o);
        anim(o, 0x100868b0u);
        o->flags |= F_OVERRUN | 0x80000u;
        pk_b.over = 1;
        o->work[1] = 3;
        o->counter = 0;
        return;
    case 3:
        if (++o->counter != 4) return;
        FX(g, 0, o->arg0, o->arg1, o->x + dir(o, 0x19), o->y - 0x14);
        st_numb(g, o, t);
        o->work[1] = 4;
        o->counter = 0;
        return;
    case 4:
        if (!tick(&o->counter, 7)) return;
        sp_end(o);
        return;
    default:
        return;
    }
}

/* 0x100193f8: special move: pillars of light, then a blow to every foe */
static void cls_100193f8(pk_game *g, pk_object *o, pk_object *t, const unit_class *c)
{
    int next;
    if (o->state != 6) { cls_melee(g, o, t, c); return; }
    if (!t) { sp_abort(o); return; }
    switch (o->work[1]) {
    case 0: sp_start(o); o->work[1] = 1; o->W2 = 0; return;
    case 1:
        if (!cam_ready()) return;
        face(o, t);
        panel(g, o);
        anim(o, 0x10086b7cu);
        next = 2;
        break;
    case 2:
        if (!tick(&o->W2, 9)) return;
        cam_on(t, 5);
        next = 3;
        break;
    case 3:
        if (!cam_ready()) return;
        eff_pillars(g, o);
        next = 4;
        break;
    case 4:
        if (!tick(&o->W2, 8)) return;
        eff_quake(g, o, 0x50, 0);
        next = 5;
        break;
    case 5:
        if (!tick(&o->W2, 5)) return;
        sp_end(o);
        return;
    default:
        return;
    }
    o->work[1] = next;
    o->W2 = 0;
}

/* healers (0x100199bc one ally, 0x1001c860 one or all) */
static void cls_100199bc(pk_game *g, pk_object *o, pk_object *t, const unit_class *c)
{
    pk_object *u;
    if (o->state != 6) { cls_melee(g, o, t, c); return; }
    if (!t) { sp_abort(o); return; }
    if (status(o) & 0x10) { to_think(o); return; }
    switch (o->work[1]) {
    case 0:
        o->target = most_hurt_ally(o);
        if (o->target == 0xff) { to_think(o); return; }
        panel(g, o);
        sp_start(o);
        anim(o, 0x10086ed8u);
        o->work[1] = 1;
        return;
    case 1:
        if (!anim_done(o)) return;
        o->work[1] = 2;
        u = ally_any(g, o, o->target);
        if (u) cam_on(u, 5);
        return;
    case 2:
        if (!cam_ready()) return;
        sfx(10);
        u = ally_any(g, o, o->target);
        if (u) FX(g, 0x2b, 0, 0, u->x, u->y - 0x14);
        o->work[1] = 3;
        o->W2 = 0;
        return;
    case 3:
        if (!tick(&o->W2, 0x11)) return;
        u = ally_any(g, o, o->target);
        if (u) heal(u, res(o));
        o->target = 0xff;
        sp_end(o);
        return;
    default:
        return;
    }
}

static void cls_1001c860(pk_game *g, pk_object *o, pk_object *t, const unit_class *c)
{
    pk_object *u;
    int i;
    if (o->state != 6) { cls_melee(g, o, t, c); return; }
    if (status(o) & 0x10) { to_think(o); return; }
    switch (o->work[1]) {
    case 0:
        o->target = most_hurt_ally(o);
        if (o->target == 0xff) { to_think(o); return; }
        panel(g, o);
        sp_start(o);
        anim(o, 0x10088d14u);
        o->work[1] = 1;
        return;
    case 1:
        if (anim_done(o)) {
            o->work[1] = 2;
            u = ally_any(g, o, o->target);
            if (u) cam_on(u, 5);
        }
        return;
    case 2:
        if (!cam_ready()) return;
        sfx(10);
        if (o->type == 0x26) {
            for (i = 0; i < 6; i++) {
                u = ally_any(g, o, i);
                /* (as written: up-and-about is read from record side 0) */
                if (u && pk_b.rec[0][i][0] != 0xff && pk_b.rec[0][i][22] == 0)
                    FX(g, 0x2b, 0, 0, u->x, u->y - 0x14);
            }
        } else {
            u = ally_any(g, o, o->target);
            if (u) FX(g, 0x2b, 0, 0, u->x, u->y - 0x14);
        }
        o->work[1] = 3;
        o->W2 = 0;
        return;
    case 3:
        if (!tick(&o->W2, 0x13)) return;
        if (o->type == 0x26) {
            eff_heal_all(g, o);
        } else {
            u = ally_any(g, o, o->target);
            if (u) heal(u, res(o));
        }
        o->target = 0xff;
        sp_end(o);
        return;
    default:
        return;
    }
}

/* 0x10019eb4: special move: blizzard */
static void cls_10019eb4(pk_game *g, pk_object *o, pk_object *t, const unit_class *c)
{
    if (o->state != 6) { cls_melee(g, o, t, c); return; }
    if (!t) { sp_abort(o); return; }
    switch (o->work[1]) {
    case 0: sp_start(o); o->work[1] = 1; return;
    case 1:
        if (!cam_ready()) return;
        face(o, t);
        panel(g, o);
        anim(o, 0x100871bcu);
        o->work[1] = 2;
        return;
    case 2:
        if (!anim_done(o)) return;
        eff_blizzard(g, o);
        anim(o, c->idle);
        o->work[1] = 3;
        return;
    case 3:
        if (!tick(&o->W2, 10)) return;
        sp_end(o);
        return;
    default:
        return;
    }
}

/* casters whose special move aims at the target after a long stare
 * (0x1001b180, 0x1001b718, 0x1001ddd0); `act` plays it and returns
 * whether it worked */
typedef bool (*stare_fn)(pk_game *g, pk_object *o, pk_object *t);

static void special_stare(pk_game *g, pk_object *o, pk_object *t, const unit_class *c, Uint32 a, bool sleep_check, stare_fn act)
{
    if (!t) { sp_abort(o); return; }
    if (sleep_check && (status(o) & 0x10)) { to_think(o); return; }
    switch (o->work[1]) {
    case 0:
        anim(o, c->walk);
        o->work[1] = 1;
        o->W2 = 0;
        /* fall through */
    case 1:
        face(o, t);
        if (++o->W2 > 0x20) {
            o->work[1] = 2;
            o->W2 = 0;
        }
        return;
    case 2: sp_start(o); o->work[1] = 3; return;
    case 3:
        if (cam_ready()) {
            panel(g, o);
            anim(o, a);
            o->work[1] = 4;
        }
        return;
    case 4:
        if (anim_done(o)) {
            cam_on(t, 5);
            o->work[1] = 5;
        }
        return;
    case 5:
        if (cam_ready()) {
            if (act(g, o, t)) o->W2 = 0; else sp_missed(g, o, t);
            o->work[1] = 6;
        }
        return;
    case 6:
        if (!tick(&o->W2, 10)) return;
        sp_end(o);
        return;
    default:
        return;
    }
}

static bool stare_seal(pk_game *g, pk_object *o, pk_object *t)
{
    if (o->type == 0x29) { eff_magic_all(g, o, true); return true; }
    if (!sp_roll(g, o)) return false;
    sfx(0x1d);
    st_seal(g, t);
    return true;
}

static bool stare_drain_attack(pk_game *g, pk_object *o, pk_object *t)
{
    if (equip_hit(g, t, o, 1)) return false;
    sfx(0x1d);
    return true;
}

static bool stare_by_type(pk_game *g, pk_object *o, pk_object *t)
{
    if (!sp_roll(g, o)) return false;
    switch (o->type) {
    case 0x50: sfx(0x1d); st_bind(g, t); break;
    case 0x51: sfx(0x1d); st_seal(g, t); break;
    case 0x52: sfx(0x1d); drain(g, o, t, 1); break;
    default: eff_spikes(g, o); break;
    }
    return true;
}

/* 0x1001a634: caster; special move: weakens every foe */
static void cls_1001a634(pk_game *g, pk_object *o, pk_object *t, const unit_class *c)
{
    static const cast_def d = { 2, 3, 4, 7, 9, false, false, true };
    if (cast(g, o, t, c, &d, d.box)) return;
    if (o->state != 6) { common_states(g, o, t, c); return; }
    if (!t) { sp_abort(o); return; }
    if (status(o) & 0x10) { to_think(o); return; }
    switch (o->work[1]) {
    case 0:
        anim(o, c->walk);
        o->work[1] = 1;
        o->W2 = 0;
        /* fall through */
    case 1:
        face(o, t);
        if (!tick(&o->W2, 0xb)) return;
        o->work[1] = 2;
        o->W2 = 0;
        return;
    case 2:
        panel(g, o);
        anim(o, c->swing);
        sp_start(o);
        o->work[1] = 3;
        return;
    case 3:
        if (anim_done(o)) {
            cam_on(t, 5);
            o->work[1] = 4;
        }
        return;
    case 4:
        if (!cam_ready()) return;
        eff_weaken_all(g, o);
        o->work[1] = 5;
        o->W2 = 0;
        return;
    case 5:
        if (!tick(&o->W2, 10)) return;
        sp_end(o);
        return;
    default:
        return;
    }
}

/* 0x1001aad4: critical hits (30 %) */
static void cls_1001aad4(pk_game *g, pk_object *o, pk_object *t, const unit_class *c) { melee_crit(g, o, t, c, 0x1e); }

/* 0x1001f3b4: critical hits by type (7: 10 %, 8: 20 %; the roll always runs) */
static void cls_1001f3b4(pk_game *g, pk_object *o, pk_object *t, const unit_class *c)
{
    melee_crit(g, o, t, c, o->type == 7 ? 10 : o->type == 8 ? 0x14 : 0);
}

static void cls_1001b180(pk_game *g, pk_object *o, pk_object *t, const unit_class *c)
{
    static const cast_def d = { 10, 7, 4, 7, 10, false, false, true };
    if (cast(g, o, t, c, &d, d.box)) return;
    if (o->state == 6) { special_stare(g, o, t, c, 0x10087f60u, true, stare_seal); return; }
    common_states(g, o, t, c);
}

static void cls_1001b718(pk_game *g, pk_object *o, pk_object *t, const unit_class *c)
{
    static const cast_def d = { 10, 8, 4, 7, 10, false, true, true };
    if (cast(g, o, t, c, &d, d.box)) return;
    if (o->state == 6) { special_stare(g, o, t, c, c->swing, true, stare_drain_attack); return; }
    common_states(g, o, t, c);
}

static void cls_1001ddd0(pk_game *g, pk_object *o, pk_object *t, const unit_class *c)
{
    static const cast_def d = { 10, 6, 10, 7, 8, false, false, false };
    if (cast(g, o, t, c, &d, o->type == 0x51 ? 0xc : 10)) return;
    if (o->state == 6) { special_stare(g, o, t, c, 0x10089580u, false, stare_by_type); return; }
    common_states(g, o, t, c);
}

/* 0x1001bc64: special move: drains every foe */
static void eff_drain_fx(pk_game *g, pk_object *o) { eff_drain_all(g, o); }
static void cls_1001bc64(pk_game *g, pk_object *o, pk_object *t, const unit_class *c)
{
    static const charge_def d = { 0x10088550u, 10, 10, true, eff_drain_fx };
    if (o->state == 6) { special_charge(g, o, t, c, &d); return; }
    cls_melee(g, o, t, c);
}

/* 0x1001c3f0: special move: crystals (without turning) */
static void cls_1001c3f0(pk_game *g, pk_object *o, pk_object *t, const unit_class *c)
{
    if (o->state != 6) { cls_melee(g, o, t, c); return; }
    if (!t) { sp_abort(o); return; }
    switch (o->work[1]) {
    case 0: sp_start(o); o->work[1] = 1; return;
    case 1:
        if (!cam_ready()) return;
        anim(o, 0x10088a68u);
        panel(g, o);
        o->work[1] = 2;
        o->counter = 0;
        return;
    case 2:
        if (!tick(&o->counter, 0xd)) return;
        cam_on(t, 5);
        anim(o, c->idle);
        o->work[1] = 3;
        return;
    case 3:
        if (!cam_ready()) return;
        eff_crystals(g, o);
        o->work[1] = 4;
        o->W2 = 0;
        return;
    case 4:
        if (!tick(&o->W2, 0x1e)) return;
        sp_end(o);
        return;
    default:
        return;
    }
}

/* knocked down, with the class's own recovery (0x1001ce2c / 0x1001e8a0):
 * `revive` runs once the unit is up again; true when it took over */
typedef bool (*recover_fn)(pk_game *g, pk_object *o);

static void knockdown_own(pk_game *g, pk_object *o, int frame, recover_fn revive)
{
    pk_object *t;
    int s;
    if (o->work[1] == 0) {
        *cnt(o, 0x748) = 0;
        status_clr(o, 0x1100);
        o->flags &= ~F_CRITICAL;
        anim_stop(o);
        o->frame = frame;
        o->work[0] = 0;
        o->work[1] = 1;
        return;
    }
    if (o->work[1] != 1) return;
    if (++o->work[0] < 5 && !(status(o) & 0x10006000) && !pk_b.over) {
        if (!(o->flags & FLIP)) {
            o->x -= 2;
            if (o->x < 0x15) o->x = 0x14;
        } else {
            o->x += 2;
            if (o->x > 0x14b) o->x = 0x14c;
        }
    }
    if (o->work[0] < 10) return;
    if (hp(o) > 0) {
        if (revive(g, o)) return;
        clear(o);
        o->flags &= ~F_KNOCKDOWN;
        if (status(o) & 0x400) { o->state = 0xb; return; }
        if (status(o) & 0x2000) { o->state = 0xc; return; }
        t = target(g, o);
        s = status(o);
        if ((s & 0xe00000) && t) {
            int chance = (s & 0x800000) ? 0x3c : (s & 0x400000) ? 0x28 : 0x14;
            if (pct(g) < chance) {
                face(o, t);
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
        o->state = 1;
        return;
    }
    sfx(0xd);
    if (o->arg1 > 5) return;
    if (unit_dies(g, o)) {
        if (o->arg1 < 4) pk_battle_panel(g, o, 3);
        pk_object_kill(o);
        return;
    }
    clear(o);
    o->flags &= ~F_KNOCKDOWN;
    o->work[0] = 0x14;
    o->state = 4;
}

/* 0x1001ce2c: splits in two when it gets up (property 0x2e, 10 %) */
static bool split_up(pk_game *g, pk_object *o)
{
    if (has_prop(o, 0x2e) && pct(g) < 10) summon_double(g, o);
    return false;
}

static void cls_1001ce2c(pk_game *g, pk_object *o, pk_object *t, const unit_class *c)
{
    switch (o->state) {
    case 0:
        pk_object_set_frames(o, c->frames);
        o->flags |= PK_OBJ_SPRITE;
        box(o, 0, c->by, c->bw, c->bh);
        if (o->work[0] == 1) { /* a copy: comes out of its original */
            pk_object *src = find(g, o->type, o->work[5], o->arg0);
            o->frame = 4;
            if (!src) {
                anim(o, c->idle);
            } else {
                if (src->flags & FLIP) o->flags |= FLIP;
                o->flags |= F_KNOCKDOWN;
            }
            clear(o);
        }
        o->state = 9; /* (as written, also for copies) */
        return;
    case 2: case 3:
        if (melee(g, o, t, c, -1)) return;
        break;
    case 7:
        knockdown_own(g, o, 4, split_up);
        return;
    case 0xe:
        if (++o->work[1] < 5) {
            o->x += (o->flags & FLIP) ? 2 : -2;
            return;
        }
        o->flags &= ~F_KNOCKDOWN;
        to_think(o);
        return;
    default:
        break;
    }
    common_states(g, o, t, c);
}

/* 0x1001d8e0: special move: a pillar of light on the target */
static void cls_1001d8e0(pk_game *g, pk_object *o, pk_object *t, const unit_class *c)
{
    int next;
    if (o->state != 6) { cls_melee(g, o, t, c); return; }
    if (!t) { sp_abort(o); return; }
    switch (o->work[1]) {
    case 0: sp_start(o); o->work[1] = 1; o->W2 = 0; return;
    case 1:
        if (!cam_ready()) return;
        face(o, t);
        panel(g, o);
        anim(o, 0x100892bcu);
        next = 2;
        break;
    case 2:
        if (!tick(&o->W2, 0xc)) return;
        cam_on(t, 5);
        next = 3;
        break;
    case 3:
        if (!cam_ready()) return;
        sfx(0x17);
        FX(g, 0x3d, o->arg0, o->arg1, t->x, 0x99);
        next = 4;
        break;
    case 4:
        if (!tick(&o->W2, 8)) return;
        eff_rank_blow(g, o, t, 8);
        next = 5;
        break;
    case 5:
        if (!tick(&o->W2, 5)) return;
        sp_end(o);
        return;
    default:
        return;
    }
    o->work[1] = next;
    o->W2 = 0;
}

/* 0x1001e3dc: caster; special move: a quake */
static void cls_1001e3dc(pk_game *g, pk_object *o, pk_object *t, const unit_class *c)
{
    int next;
    if (o->state == 2 || o->state == 3) {
        if (!t) { to_think(o); return; }
        if (status(o) & 0x10) { o->state = 4; return; }
        if (o->work[1] == 0) {
            face(o, t);
            anim(o, 0x1008986cu);
            o->work[1] = 1;
            o->counter = 0;
            return;
        }
        if (o->work[1] != 1) return;
        if (--o->work[0] < 1) to_think(o);
        if (++o->counter == 4) {
            sfx(7);
            spawn_attack(g, 4, o, t->x, t->y);
        }
        attack_end(o, 8);
        return;
    }
    if (o->state != 6) { common_states(g, o, t, c); return; }
    if (!t) { sp_abort(o); return; }
    if (status(o) & 0x10) { to_think(o); return; }
    switch (o->work[1]) {
    case 0:
        face(o, t);
        sp_start(o);
        o->work[1] = 1;
        return;
    case 1:
        if (cam_ready()) {
            panel(g, o);
            anim(o, 0x100898bcu);
            o->work[1] = 2;
        }
        return;
    case 2:
        if (anim_done(o)) {
            anim(o, c->idle);
            o->work[1] = 3;
            cam_on(t, 5);
        }
        return;
    case 3:
        if (!cam_ready()) return;
        FX(g, 0x39, o->arg0, o->arg1, t->x + 10, 0x32);
        next = 4;
        break;
    case 4:
        if (!tick(&o->W2, 0x10)) return;
        eff_quake(g, o, 100, 1);
        next = 5;
        break;
    case 5:
        if (!tick(&o->W2, 10)) return;
        sp_end(o);
        return;
    default:
        return;
    }
    o->work[1] = next;
    o->W2 = 0;
}

/* 0x1001e8a0: may get up healed (property 0x2d, 10 %) */
static bool second_wind(pk_game *g, pk_object *o)
{
    int r = pct(g);
    if (status(o) < 0 && r < 10) {
        FX(g, 0x2c, 0, 0, o->x, o->y - 0x14);
        heal(o, hpmax(o) / 2);
        to_think(o);
        o->flags &= ~F_KNOCKDOWN;
        return true;
    }
    return false;
}

static void cls_1001e8a0(pk_game *g, pk_object *o, pk_object *t, const unit_class *c)
{
    switch (o->state) {
    case 0:
        pk_object_set_frames(o, c->frames);
        o->flags |= PK_OBJ_SPRITE;
        box(o, 0, c->by, c->bw, c->bh);
        if (o->work[0] == 1) { /* summoned: appears next to its summoner */
            pk_object *src;
            anim(o, 0x10089c38u);
            src = find(g, o->type, o->work[5], o->arg0);
            if (src && (src->flags & FLIP)) o->flags |= FLIP;
            o->state = 0xe;
        } else {
            anim(o, c->rest);
            o->state = 9;
        }
        clear(o);
        if (has_prop(o, 0x2d)) status_set(o, (int)0x80000000u);
        return;
    case 2: case 3:
        if (melee(g, o, t, c, -1)) return;
        break;
    case 7:
        knockdown_own(g, o, 8, second_wind);
        return;
    case 0xe:
        if (!anim_done(o)) return;
        o->flags &= ~0x290000u;
        to_think(o);
        return;
    default:
        break;
    }
    common_states(g, o, t, c);
}

/* 0x1001f74c: flyer; special move: blasts every foe */
static void cls_1001f74c(pk_game *g, pk_object *o, pk_object *t, const unit_class *c)
{
    if (o->state == 6) { special_flyer(g, o, t, c, 0x1008a304u, 9, true); return; }
    if ((o->state == 2 || o->state == 3) && o->work[1] == 0 && t) {
        anim(o, c->walk); /* (this one waits a tick before walking) */
        o->work[1] = 1;
        return;
    }
    cls_melee(g, o, t, c);
}

/* 0x1001fc6c: shoots; special move: dashes through the target */
static void cls_1001fc6c(pk_game *g, pk_object *o, pk_object *t, const unit_class *c)
{
    if (o->state == 2 || o->state == 3) {
        if (!t) { to_think(o); return; }
        switch (o->work[1]) {
        case 0:
            anim(o, 0x1008a5a0u);
            o->work[1] = 1;
            o->W2 = 0;
            /* fall through */
        case 1:
            if (approach(o, t, 2, 10)) o->work[1] = 2;
            if (++o->W2 >= 6) o->work[1] = 2;
            return;
        case 2:
            anim(o, c->swing);
            o->work[1] = 3;
            o->counter = 0;
            return;
        case 3:
            if (++o->counter == 4) {
                sfx(6);
                spawn_attack(g, 6, o, t->x, t->y);
            }
            attack_end(o, 6);
            return;
        default:
            return;
        }
    }
    if (o->state != 6) { common_states(g, o, t, c); return; }
    if ((o->work[1] != 5 && !t) || (t && (t->x > 0x124 || t->x < 0x3c))) {
        sp_end(o);
        return;
    }
    switch (o->work[1]) {
    case 0:
        anim(o, 0x1008a5a0u);
        o->work[1] = 1;
        /* fall through */
    case 1:
        if (approach(o, t, 3, 10)) o->work[1] = 2;
        if (--o->work[0] < 1) to_think(o);
        return;
    case 2:
        panel(g, o);
        o->flags |= F_OVERRUN | 0x80000u;
        pk_b.over = 1;
        if (t) { pk_b.cam[2] = t->x; pk_b.cam[3] = t->y; }
        CAM_MODE = 7;
        o->work[1] = 3;
        o->counter = 0;
        if (t && o->x < t->x) o->flags &= ~FLIP; else o->flags |= FLIP;
        return;
    case 3: {
        int x = o->x;
        if (!t) return;
        if (!(o->flags & FLIP)) {
            o->x = x + 6;
            if (x - 0x1d < t->x) return;
        } else {
            o->x = x - 6;
            if (t->x < x + 0x1d) return;
        }
        o->work[1] = 4;
        return;
    }
    case 4:
        anim(o, 0x1008a600u);
        o->work[1] = 5;
        o->counter = 0;
        if (t && o->x < t->x) o->flags &= ~FLIP; else o->flags |= FLIP;
        return;
    case 5:
        if (++o->counter == 4) {
            int d = o->type == 0x16 ? 0x2c : o->type == 0x17 ? 0x3c : 0x60;
            if (t) t->flags |= F_KNOCKDOWN;
            sfx(8);
            if (!t) return;
            if (status(t) & 1) d /= 2;
            damage(g, t, d);
            FX(g, 0, t->arg0, t->arg1, t->x, t->y - 0x14);
            FX(g, 1, t->arg0, t->arg1, t->x, t->y - 0x14);
            return;
        }
        if (o->counter != 10) return;
        sp_end(o);
        return;
    default:
        return;
    }
}

/* 0x100206e4: special move: spikes under every foe */
static void cls_100206e4(pk_game *g, pk_object *o, pk_object *t, const unit_class *c)
{
    int next;
    if (o->state != 6) { cls_melee(g, o, t, c); return; }
    if (!t) { sp_abort(o); return; }
    if (status(o) & 0x10) { to_think(o); return; }
    switch (o->work[1]) {
    case 0:
        cam_on(o, 7);
        next = 1;
        break;
    case 1:
        if (!cam_ready()) return;
        face(o, t);
        panel(g, o);
        pk_b.over = 1;
        anim(o, 0x1008aae0u);
        o->flags |= F_OVERRUN | 0x80000u;
        next = 2;
        break;
    case 2:
        if (!anim_done(o)) return;
        anim(o, c->idle);
        cam_on(t, 5);
        o->work[1] = 3;
        return;
    case 3:
        if (!cam_ready()) return;
        eff_spikes(g, o);
        next = 4;
        break;
    case 4:
        if (!tick(&o->W2, 8)) return;
        sp_end(o);
        return;
    default:
        return;
    }
    o->work[1] = next;
    o->W2 = 0;
}

/* 0x10020b84: regenerates by type (0x5a / 0x5b / 0x5c) */
static void cls_10020b84(pk_game *g, pk_object *o, pk_object *t, const unit_class *c)
{
    if (o->state == 0) {
        if (o->type == 0x5a) status_set(o, 0x1000000);
        else if (o->type == 0x5b) status_set(o, 0x2000000);
        else if (o->type == 0x5c) status_set(o, 0x4000000);
        *cnt(o, 0x7a8) = 0x21;
    }
    cls_melee(g, o, t, c);
}

/* 0x10023114: walks up; special move: drains the target */
static void cls_10023114(pk_game *g, pk_object *o, pk_object *t, const unit_class *c)
{
    if (o->state != 6) { cls_melee(g, o, t, c); return; }
    if (!t) { sp_abort(o); return; }
    switch (o->work[1]) {
    case 0:
        anim(o, c->idle);
        o->work[1] = 1;
        o->W2 = 0;
        /* fall through */
    case 1:
        if (approach(o, t, 2, 0x14)) o->work[1] = 2;
        if (--o->work[0] < 1) to_think(o);
        return;
    case 2:
        panel(g, o);
        anim(o, 0x1008b014u);
        sp_start(o);
        o->work[1] = 3;
        o->counter = 0;
        return;
    case 3:
        if (++o->counter != 3) return;
        if (sp_roll(g, o)) {
            o->W2 = 0;
            sfx(0x1d);
            drain(g, o, t, o->type == 0x13 ? 1 : 2);
        } else {
            sp_missed(g, o, t);
        }
        o->work[1] = 4;
        return;
    case 4:
        if (!tick(&o->W2, 10)) return;
        sp_end(o);
        return;
    default:
        return;
    }
}

/* 0x1002367c: does not fight; falls once the rest of its side has */
static void idol_check(pk_game *g, pk_object *o)
{
    pk_object *ctl = find(g, OBJ_CONTROLLER, 0, 0);
    int i, n = 0, *r;
    if (!ctl || ctl->state != 3) return;
    for (i = 0; i < 6; i++) {
        r = pk_b.rec[rs(o)][i];
        if (r[0] != 0xff && r[22] != 1 && r[15] != 0x30) n++;
    }
    if (n) return;
    r = rec(o);
    if (r[7] < 1) return;
    r[7] = 0;
    o->state = 7;
    clear(o);
}

static void cls_1002367c(pk_game *g, pk_object *o, pk_object *t, const unit_class *c)
{
    switch (o->state) {
    case 0:
        common_states(g, o, t, c);
        status_set(o, o->type == 0x70 ? 0x10000 : 0x20000);
        return;
    case 2: case 3: case 6:
        to_think(o);
        return;
    default:
        common_states(g, o, t, c);
        return;
    }
}
