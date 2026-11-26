/* SPDX-License-Identifier: MIT */
/* Battle unit effects (reimplementation), included by pk_battle_unit.c:
 * statuses, special-move damage, equipment properties, the order's
 * action weights, falling in battle and summoning. */

/* the unit object on a record side at a slot (only units still up) */
static pk_object *unit_at(pk_game *g, int r, int slot)
{
    const int *e = pk_b.rec[r][slot];
    int screen = r == 0 ? pk_b.player_side : 1 - pk_b.player_side;
    if (e[0] == 0xff || e[22] != 0) return NULL;
    return find(g, e[0], slot, screen);
}
static pk_object *foe(pk_game *g, const pk_object *o, int i) { return unit_at(g, rs(o) ^ 1, i); }
static pk_object *ally(pk_game *g, const pk_object *o, int i) { return unit_at(g, rs(o), i); }

/* special-move damage: `base` less defense, at least 1, halved while
 * the target is staggered (status 1) */
static void blow(pk_game *g, pk_object *t, int base)
{
    int d = base - def(t);
    if (d < 1) d = 1;
    if (status(t) & 1) d /= 2;
    damage(g, t, d);
}

static void miss(pk_game *g, const pk_object *o, const pk_object *t) /* fx 2 */
{
    FX(g, 2, o->arg0, o->arg1, t->x, t->y - 0x14);
}

/* ---- statuses ---- */

static void st_sleep(pk_game *g, pk_object *t) /* FUN_10007a3c */
{
    if (!t) return;
    if (t->flags & 0x2000) { miss(g, t, t); return; }
    if (!(status(t) & 0x1000)) FX(g, 0x2d, t->arg0, t->arg1, t->x, t->y - 0x2d);
    status_set(t, 0x10);
}

static void st_poison(pk_game *g, pk_object *t, int bit) /* FUN_10007b28 (0x20) / FUN_10007c94 (0x40) */
{
    if (!t) return;
    if (t->flags & 0x1000) { miss(g, t, t); return; }
    t->flags |= F_KNOCKDOWN;
    if (!(status(t) & 0x1000)) {
        FX(g, 0x2f, t->arg0, t->arg1, t->x, t->y - 0x1e);
        FX(g, 0x31, t->arg0, t->arg1, t->x, t->y - 0x14);
    }
    status_set(t, bit);
    *cnt(t, 0x748) = 0x28;
}

static void st_bind(pk_game *g, pk_object *t) /* FUN_10007e00 */
{
    if (!t) return;
    if (t->flags & 0x8000) { miss(g, t, t); return; }
    t->flags |= F_KNOCKDOWN;
    if (!(status(t) & 0x1000)) {
        FX(g, 0x32, t->arg0, t->arg1, t->x, t->y);
        FX(g, 0, t->arg0, t->arg1, t->x, t->y - 0x14);
    }
    status_set(t, 0x400);
    t->state = 0xd;
    clear(t);
}

static void st_seal(pk_game *g, pk_object *t) /* FUN_10007f48 */
{
    if (!t) return;
    if (t->flags & 0x4000) { miss(g, t, t); return; }
    if (!(status(t) & 0x100)) FX(g, 0x36, t->arg0, t->arg1, t->x, t->y - 0x2d);
    status_set(t, 0x100);
    t->state = 0xd;
    clear(t);
}

static void st_freeze(pk_game *g, pk_object *t) /* FUN_10008044 */
{
    if (!t) return;
    t->flags |= F_KNOCKDOWN;
    if (!(status(t) & 0x2000)) {
        FX(g, 0x3b, t->arg0, t->arg1, t->x, t->y);
        FX(g, 0, t->arg0, t->arg1, t->x, t->y - 0x14);
    }
    status_set(t, 0x2000);
    t->state = 0xc;
    clear(t);
    *cnt(t, 0x778) = 0x37;
}

static void st_numb(pk_game *g, pk_object *o, pk_object *t) /* FUN_100064f4 */
{
    int d;
    if (!t) return;
    t->flags |= F_KNOCKDOWN;
    sfx(0xe);
    d = rank(o);
    if (status(t) & 1) d /= 2;
    damage(g, t, d);
    if (!(status(t) & 0x1000)) FX(g, 0x29, t->arg0, t->arg1, t->x, t->y - 0x2d);
    if (!(status(t) & 0x10000000)) {
        status_set(t, 0x1000);
        t->state = 10;
        clear(t);
    }
}

/* the chance of a status or property: level (up to 5) * 4 + luck / 2 */
static bool roll_luck(pk_game *g, const pk_object *a)
{
    int lv = level(a) > 4 ? 5 : level(a);
    return pct(g) < lv * 4 + luck(a) / 2;
}

/* ---- special-move damage ---- */

static void drain(pk_game *g, pk_object *o, pk_object *t, int part) /* FUN_10005e98 / FUN_10005fdc */
{
    int d = field_mod(o, atk(o), 0) - field_mod(t, def(t), 0);
    if (status(t) & 1) d /= 2;
    if (d < 3) d = 3;
    damage(g, t, d);
    heal(o, d * part / 3);
    FX(g, 0x48, o->arg0, o->arg1, t->x, t->y - 0x50);
    FX(g, 0x49, o->arg0, o->arg1, t->x, t->y - 10);
}

static void eff_quake(pk_game *g, pk_object *o, int base, int fx) /* FUN_10005c40 (100, fx 1) / FUN_10006260 (0x50, fx 0) */
{
    int i;
    for (i = 0; i < 6; i++) {
        pk_object *t = foe(g, o, i);
        if (!t) continue;
        t->flags |= F_KNOCKDOWN;
        blow(g, t, base);
        FX(g, fx, o->arg0, o->arg1, t->x, t->y - 0x14);
    }
}

static void eff_drain_all(pk_game *g, pk_object *o) /* FUN_10005db4 */
{
    int i;
    sfx(0x1d);
    for (i = 0; i < 6; i++) {
        pk_object *t = foe(g, o, i);
        if (t) drain(g, o, t, 1);
    }
}

static void eff_heal_all(pk_game *g, pk_object *o) /* FUN_10006160 with FUN_10006124 */
{
    int i;
    for (i = 0; i < 6; i++) {
        const int *r = pk_b.rec[rs(o)][i];
        pk_object *u;
        if (r[0] == 0xff || r[22] != 0) continue;
        u = find(g, r[0], i, o->arg0);
        if (u) heal(u, res(o));
    }
}

static void eff_rank_blow(pk_game *g, pk_object *o, pk_object *t, int k) /* FUN_100061f8 (8) */
{
    t->flags |= F_KNOCKDOWN;
    blow(g, t, rank(o) * k);
}

static void eff_all_fresh(pk_game *g, pk_object *o, int base) /* FUN_100063d4: foes not already down */
{
    int i;
    for (i = 0; i < 6; i++) {
        pk_object *t = foe(g, o, i);
        if (!t || (t->flags & F_KNOCKDOWN)) continue;
        t->flags |= F_KNOCKDOWN;
        blow(g, t, base);
    }
}

static void eff_type_blow(pk_game *g, pk_object *o, pk_object *t) /* FUN_10006600 */
{
    if (!t) return;
    t->flags |= F_KNOCKDOWN;
    blow(g, t, o->type == 0x4c ? 0x15 : o->type == 0x4d ? 0x23 : 0x38);
}

static void eff_spikes(pk_game *g, pk_object *o) /* FUN_10006684 */
{
    int i;
    sfx(0x12);
    for (i = 0; i < 6; i++) {
        pk_object *t = foe(g, o, i);
        if (!t) continue;
        t->flags |= F_KNOCKDOWN;
        blow(g, t, o->type == 0x53 ? 0x38 : 0x5a);
        FX(g, 0x38, o->arg0, o->arg1, t->x, 0x98);
    }
}

static void eff_blast(pk_game *g, pk_object *o, pk_object *t) /* FUN_10006810 */
{
    if (!t) return;
    sfx(0x1a);
    t->flags |= F_KNOCKDOWN;
    blow(g, t, rank(o) * 10);
    FX(g, 0x21, o->arg0, o->arg1, t->x, t->y);
}

static void eff_blast_all(pk_game *g, pk_object *o) /* FUN_100068ec */
{
    int i;
    sfx(0x1a);
    for (i = 0; i < 6; i++) {
        pk_object *t = foe(g, o, i);
        if (!t) continue;
        t->flags |= F_KNOCKDOWN;
        blow(g, t, 0x5a);
        FX(g, 0x21, o->arg0, o->arg1, t->x, t->y);
    }
}

static void eff_crystals(pk_game *g, pk_object *o) /* FUN_10006a68 */
{
    int i;
    sfx(0x18);
    for (i = 0; i < 6; i++) {
        pk_object *t = foe(g, o, i);
        if (!t) continue;
        t->flags |= F_KNOCKDOWN;
        blow(g, t, o->type == 0x69 ? 0x5a : 0x50);
        FX(g, 0x3f, o->arg0, o->arg1, t->x, t->y - 0x78 - rnd(g) % 0x28);
    }
}

static void eff_wave(pk_game *g, pk_object *o) /* FUN_10006c20 */
{
    sfx(0x1b);
    FX(g, 0x44, o->arg0, o->arg1, o->x, o->y - 0x3c);
    eff_all_fresh(g, o, o->type == 0x34 ? 0x5a : 0x3c);
}

static void eff_blizzard(pk_game *g, pk_object *o) /* FUN_10006db4 */
{
    int i, lv = level(o) > 4 ? 5 : level(o), lk = luck(o);
    sfx(0x16);
    FX(g, 0x45, o->arg0, o->arg1, o->x, o->y - 0x3c);
    for (i = 0; i < 6; i++) {
        pk_object *t = foe(g, o, i);
        if (!t || (t->flags & F_KNOCKDOWN)) continue;
        t->flags |= F_KNOCKDOWN;
        blow(g, t, 0x32);
        if (pct(g) < lk + lv * 4) st_freeze(g, t);
        else miss(g, t, t);
    }
}

static void eff_miasma(pk_game *g, pk_object *o) /* FUN_10007004 */
{
    int i;
    sfx(0x13);
    FX(g, 0x46, o->arg0, o->arg1, o->x, o->y - 0x3c);
    for (i = 0; i < 6; i++) {
        pk_object *t = foe(g, o, i);
        if (!t || (t->flags & F_KNOCKDOWN)) continue;
        t->flags |= F_KNOCKDOWN;
        blow(g, t, 0x32);
        st_poison(g, t, 0x20);
    }
}

static void eff_bind_all(pk_game *g, pk_object *o) /* FUN_10007194 */
{
    int i;
    for (i = 0; i < 6; i++) st_bind(g, foe(g, o, i));
}

static bool equip_hit(pk_game *g, pk_object *u, pk_object *a, int prop);

static void eff_weaken_all(pk_game *g, pk_object *o) /* FUN_10007264 */
{
    int i;
    sfx(0x1d);
    for (i = 0; i < 6; i++) {
        pk_object *t = foe(g, o, i);
        if (t && equip_hit(g, t, o, 5)) miss(g, o, t);
    }
}

static void eff_magic_all(pk_game *g, pk_object *o, bool seal) /* FUN_100073b0 (sleep) / FUN_10007558 (seal) */
{
    int i;
    sfx(0x1d);
    for (i = 0; i < 6; i++) {
        pk_object *t = foe(g, o, i);
        int lv;
        if (!t) continue;
        lv = level(o) > 4 ? 5 : level(o);
        if (pct(g) < mag(o) + lv * 4) {
            if (seal) st_seal(g, t); else st_sleep(g, t);
        } else {
            miss(g, o, t);
        }
    }
}

static void eff_pillars(pk_game *g, pk_object *o) /* FUN_1001989c */
{
    int i;
    sfx(0x17);
    for (i = 0; i < 6; i++) {
        pk_object *t = foe(g, o, i);
        if (t) FX(g, 0x3d, o->arg0, o->arg1, t->x, 0x99);
    }
}

/* FUN_10007700: the unit bursts, hurting everyone within 60 px by its HP */
static void eff_burst(pk_game *g, pk_object *o)
{
    int pass, i, v;
    sfx(0x1b);
    v = hp(o);
    for (pass = 0; pass < 2; pass++) {
        for (i = 0; i < 6; i++) {
            pk_object *t = pass == 0 ? foe(g, o, i) : ally(g, o, i);
            int dx;
            if (!t || t == o) continue;
            dx = o->x < t->x ? t->x - o->x : o->x - t->x;
            if (dx >= 0x3c) continue;
            t->flags |= F_KNOCKDOWN;
            blow(g, t, v);
            FX(g, 0x28, t->arg0, t->arg1, t->x, t->y - 0x14);
        }
    }
}

/* ---- equipment properties ---- */

static void stat_boost(pk_game *g, pk_object *o, int f, int label) /* FUN_1000f3a0: a quarter more */
{
    int dx = (o->flags & FLIP) ? 4 : -4;
    FX(g, label, o->arg0, o->arg1, o->x + dx, o->y - 0x14);
    FX(g, 0x1f, o->arg0, o->arg1, o->x + dx, o->y - 0x14);
    stat_sub(o, f, -(rec(o)[f] / 4));
}

static void stat_more(pk_object *o, int f, int tenths) { rec(o)[f] += rec(o)[f] * tenths / 10; }

/* FUN_1000f3a0: a property that applies from the start of the battle */
static void equip_start(pk_game *g, pk_object *o, int p)
{
    switch (p) {
    case 0: stat_boost(g, o, 5, 0x12); break;
    case 2: stat_boost(g, o, 6, 0x14); break;
    case 4: stat_boost(g, o, 9, 0x1c); break;
    case 6: stat_boost(g, o, 10, 0x1a); break;
    case 8: stat_boost(g, o, 12, 0x16); break;
    case 10: stat_boost(g, o, 11, 0x18); break;
    case 0xe: o->flags |= 0x4000; break;   /* no seal */
    case 0x11: o->flags |= 0x8000; break;  /* no bind */
    case 0x14: o->flags |= 0x1000; break;  /* no poison */
    case 0x17: o->flags |= 0x2000; break;  /* no sleep */
    case 0x18: *cnt(o, 0x7a8) = 0x21; status_set(o, 0x1000000); break;
    case 0x19: *cnt(o, 0x7a8) = 0x21; status_set(o, 0x2000000); break;
    case 0x1a: *cnt(o, 0x7a8) = 0x21; status_set(o, 0x4000000); break;
    case 0x20: status_set(o, 0x200000); break;
    case 0x21: status_set(o, 0x400000); break;
    case 0x22: status_set(o, 0x800000); break;
    case 0x23: status_set(o, 0x10000); break;
    case 0x24: status_set(o, 0x20000); break;
    case 0x25: status_set(o, 0x40000); break;
    case 0x26: case 0x27: status_set(o, 0x80000); break;
    case 0x28: rec(o)[12] += rec(o)[12] / 2; break;
    case 0x29: stat_more(o, 5, 3); stat_more(o, 6, 3); break;
    case 0x2a: stat_more(o, 12, 3); stat_more(o, 11, 3); break;
    case 0x2b:
        stat_more(o, 5, 3); stat_more(o, 6, 3); stat_more(o, 12, 3);
        stat_more(o, 11, 3); stat_more(o, 9, 3); stat_more(o, 10, 3);
        break;
    default: break;
    }
}

/* FUN_1000eda8: a property of attacker `a`'s equipment as its blow lands
 * on `u`; true when the chance failed */
static bool equip_hit(pk_game *g, pk_object *u, pk_object *a, int p)
{
    bool fail;
    int r, dx = (u->flags & FLIP) ? 4 : -4, label = 0, lv;
    if (p == 0xff) return true;
    lv = level(a) > 4 ? 5 : level(a);
    r = pct(g);
    fail = lv * 4 + luck(a) / 2 <= r;
    switch (p) {
    case 1:
        if (!fail) {
            FX(g, 6, a->arg0, a->arg1, u->x + dx, u->y - 0x14);
            FX(g, 0x1e, a->arg0, a->arg1, u->x + dx, u->y - 0x14);
            stat_sub(a, 5, mag(u) / 4); /* (as written: the attacker's attack) */
        }
        break;
    case 3: label = 8; break;
    case 5: label = 0x10; break;
    case 7: label = 0xe; break;
    case 9: label = 10; break;
    case 0xb: label = 0xc; break;
    case 0xc: if (fail) return fail; /* fall through */
    case 0xd: st_seal(g, u); break;
    case 0xf: if (fail) return fail; /* fall through */
    case 0x10: st_bind(g, u); break;
    case 0x12: st_poison(g, u, 0x20); break;
    case 0x13: st_poison(g, u, 0x40); break;
    case 0x15: if (fail) return fail; /* fall through */
    case 0x16: st_sleep(g, u); break;
    case 0x1b: if (!fail) drain(g, a, u, 1); break;
    case 0x1c: if (!fail) drain(g, a, u, 2); break;
    case 0x1d: if (r <= 9) u->flags |= F_CRITICAL; break;
    case 0x1e: if (r <= 0x13) u->flags |= F_CRITICAL; break;
    case 0x1f: if (r <= 0x1d) u->flags |= F_CRITICAL; break;
    default: break;
    }
    if (label) {
        if (fail) return fail;
        FX(g, label, a->arg0, a->arg1, u->x + dx, u->y - 0x14);
        FX(g, 0x1e, a->arg0, a->arg1, u->x + dx, u->y - 0x14);
        stat_sub(a, 6, mag(u) / 4);
    }
    return fail;
}

/* FUN_1000ff6c: some unit types carry a property of their own */
static void type_on_hit(pk_game *g, pk_object *u, pk_object *a)
{
    int p, lv = level(a) > 4 ? 5 : level(a);
    if (lv * 4 + luck(a) / 2 < pct(g)) return;
    switch (a->type) {
    case 0x31: case 0x32: p = 3; break;
    case 0x3b: case 0x3c: case 0x3d: p = 9; break;
    case 0x3f: case 0x40: case 0x41: p = 0xb; break;
    case 0x43: case 0x44: case 0x45: case 0x46: p = 7; break;
    case 0x6b: st_poison(g, u, 0x20); return;
    case 0x6c: st_bind(g, u); return;
    case 0x6d: p = 5; break;
    default: return;
    }
    equip_hit(g, u, a, p);
}

/* FUN_1000ec44: box `e` lands on unit `u` */
static void equip_on_hit(pk_game *g, pk_object *u, pk_object *e)
{
    pk_object *a = owner(g, e);
    const int *q;
    if (!a) return;
    type_on_hit(g, u, a);
    q = eqp(a);
    if (q[0] == 0xff) return;
    equip_hit(g, u, a, q[3]);
    equip_hit(g, u, a, q[4]);
}

/* ---- the order (FUN_10021e78): weights of think, 0 attack .. 4 special ---- */

static void apply_order(pk_object *o)
{
    int *w = pk_b.weight[rs(o)][o->arg1 % 6], lv;
    switch (pk_b.order[rs(o)]) {
    case 0:
        w[0] += 0x14;
        break;
    case 1:
        w[0] -= 10;
        if (w[0] < 0) w[0] = 0;
        w[1] += 0x14;
        w[3] += 10;
        break;
    case 2:
        w[0] -= 10;
        if (w[0] < 0) w[0] = 0;
        w[3] += 0x14;
        w[4] += 10;
        break;
    case 3:
        w[0] += 0x1e;
        w[1] -= 0x14;
        if (w[1] < 0) w[1] = 0;
        w[3] -= 0x14;
        if (w[3] < 0) w[3] = 0;
        break;
    default:
        break;
    }
    lv = level(o) * 5;
    if (lv > 0x1e) lv = 0x1e;
    w[2] -= lv;
    if (w[2] < 0) w[2] = 0;
    w[4] = w[4] ? w[4] + lv : 0;
}

/* ---- falling (FUN_10008d50): true when the unit leaves the field ---- */

static bool unit_dies(pk_game *g, pk_object *o)
{
    int r = pct(g), *e = eqp(o), s = sl(o);
    if (o->arg1 > 3) { /* summoned: gone */
        rec(o)[22] = 1;
        rec(o)[0] = 0xff;
        FX(g, 0x4c, o->arg0, o->arg1, o->x, o->y - 0x14);
        FX(g, 0x4d, o->arg0, o->arg1, o->x, o->y - 0x14);
        return true;
    }
    if (e[3] == 0x2f || e[4] == 0x2f) { /* a one-time revival */
        if (e[3] == 0x2f) e[3] = 0xff; else e[4] = 0xff;
        rec(o)[7] = hpmax(o) / 2;
        FX(g, 0x2b, 0, 0, o->x, o->y - 0x14);
        if (rs(o) == 0) {
            int u = g->world.teams[pk_b.team & 7][s];
            if (u != 0xff) {
                pk_equip_remove(g, g->world.units[u][2]);
                g->world.units[u][2] = 0xff;
            }
        }
        return false;
    }
    rec(o)[22] = 1;
    if (0x14 - rank(o) < r) { /* only hurt */
        FX(g, 0x4a, o->arg0, o->arg1, o->x, o->y - 0x14);
        FX(g, 0x4b, o->arg0, o->arg1, o->x, o->y - 0x14);
        if (rs(o) == 0) pk_b.r97c[2 + s] = 1;
        return true;
    }
    if (rs(o) == 1) pk_b.opponent_owned[s] = 1;
    FX(g, 0x4c, o->arg0, o->arg1, o->x, o->y - 0x14);
    FX(g, 0x4d, o->arg0, o->arg1, o->x, o->y - 0x14);
    return true;
}

/* ---- summoning ---- */

static int free_summon_slot(const pk_object *o) /* FUN_1000a394 */
{
    int i;
    for (i = 4; i < 6; i++)
        if (pk_b.rec[rs(o)][i][0] == 0xff) return i;
    return 0xff;
}

static int most_hurt_ally(const pk_object *o) /* FUN_1000a414 */
{
    int i, best = 0xff, v = 0;
    for (i = 0; i < 6; i++) {
        const int *r = pk_b.rec[rs(o)][i];
        int d;
        if (r[0] == 0xff || r[22] != 0) continue;
        d = r[8] - r[7];
        if (d != 0 && v < d) { v = d; best = i; }
    }
    return best;
}

static void summon_join(pk_game *g, pk_object *o, int s, int type, int x, int y)
{
    int r = rs(o);
    pk_b.target[r][s] = pk_b.target[r][sl(o)];
    pk_battle_action_weights(type, pk_b.weight[r][s]);
    pk_b.status[r][s] = 0;
    pk_game_load_image(g, type * 2 + 0x102, 1); /* port: the summoned type's sprites */
    pk_bu_spawn(g, type, o->arg0, s | (o->arg1 << 8), x, y);
}

static void summon_imp(pk_game *g, pk_object *o) /* FUN_10011f6c: type 0x37 */
{
    int s = free_summon_slot(o), *r;
    if (s == 0xff) return;
    r = pk_b.rec[rs(o)][s];
    pk_unit_template(0x37, 10);
    pk_battle_template_record(r);
    r[7] = r[8];
    summon_join(g, o, s, 0x37, o->x + dir(o, 0x1e), o->y);
}

static void summon_double(pk_game *g, pk_object *o) /* FUN_1001d5a8: a copy of the unit */
{
    int s = free_summon_slot(o);
    if (s == 0xff) return;
    SDL_memcpy(pk_b.rec[rs(o)][s], rec(o), sizeof(pk_b.rec[0][0]));
    summon_join(g, o, s, o->type, o->x, o->y);
}
