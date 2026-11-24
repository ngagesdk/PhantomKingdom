/* SPDX-License-Identifier: MIT */
/* The battle effect object 0x100155f4 (type 0xc1), included by
 * pk_battle.c. Its state picks the effect; arg0 / arg1 name the unit
 * (screen side, slot) it belongs to. Images and frame / animation tables
 * are the original's (addresses into the user's executable):
 *   0       hit spark (image 0x60), mirrored with its unit
 *   1       burst (0x5e)
 *   2       "MISS" rising (0x5e frame 0x6c)
 *   4 / 5   a thrown item (frames 0x12..0x16, x = kind on spawn)
 *   6..0x1d stat labels rising (frames 0x4d..0x58)
 *   0x1e/0x1f  image 0x62 animations
 *   0x21    image 0x72 burst spawning three 0x23 sparks
 *   0x23    image 0x60 frame 0xe, 12 ticks
 *   0x25    image 0x64
 *   0x26    a sweep over the field spawning 0x28 every tick (five)
 *   0x28    small burst (0x5e)
 *   0x29.. 0x3c  status marks that follow their unit while the status
 *           bit holds (0x1000, 0x10, 0x60, 0x400, 8 timed, 0x100, 0x2000)
 *   0x38 / 0x39 / 0x3d   images 0x6c / 0x6a / 0x70
 *   0x3f    a falling crystal (image 0x5c)
 *   0x41..0x4c  images 0x68..0x84
 *   0x4d / 0x4b  rising frames 0x10 / 0x11 of image 0x84
 * Shared ends: "rise" states move up 3 px in 4 ticks and end after 10;
 * "anim" states (0x4f) end with their animation. */

#define OBJ_FX 0xc1

#define FX_FR_5E  0x100834e8u /* frame tables (DAT values) */
#define FX_FR_60  0x10083f20u

static int fx_status(const pk_object *o) { return pk_b.status[rside(o->arg0)][o->arg1 % 6]; }

static int fx_unit_down(const pk_object *o) /* FUN_10009250 */
{
    const int *r = pk_b.rec[rside(o->arg0)][o->arg1 & 3];
    return r[0] == 0xff || r[22] != 0;
}

static void fx_setup(pk_object *o, pk_game *g, int img, Uint32 frames_dat, Uint32 flags)
{
    o->image = pk_game_load_image(g, img, 1);
    pk_object_set_frames(o, frames_dat);
    o->flags = flags;
}

static void fx_anim(pk_object *o, Uint32 anim_dat, int layer)
{
    pk_object_set_anim(o, anim_dat);
    if (layer >= 0) o->layer = layer;
    pk_object_clear_work(o);
    o->state = 0x4f;
}

static void fx_rise_start(pk_object *o, int next)
{
    o->layer = 1;
    o->counter = 0;
    pk_object_clear_work(o);
    o->state = next;
}

static void fx_rise(pk_object *o)
{
    if ((++o->counter & 3) != 0) o->y--;
    if (o->counter > 9) pk_object_kill(o);
}

/* the status marks: follow the unit, end with the status or the unit */
static void fx_follow(pk_object *o, pk_game *g, int dy, int bit)
{
    pk_object *u = unit_object(g, o->arg0, o->arg1 & 3);
    pk_object_animate(o);
    if (!u) { pk_object_kill(o); return; }
    o->x = u->x;
    o->y = u->y + dy;
    if (bit && !(fx_status(o) & bit)) pk_object_kill(o);
    if (fx_unit_down(o)) pk_object_kill(o);
}

static void fx_update(pk_object *o, pk_game *g)
{
    pk_object *u;
    if (pk_b.over > 1) { pk_object_kill(o); return; }
    switch (o->state) {
    case 0:
        u = unit_object(g, o->arg0, o->arg1 & 3);
        if (!u) { pk_object_kill(o); return; }
        fx_setup(o, g, 0x60, FX_FR_60, (u->flags & PK_OBJ_FLIP_X) ? 0x15 : 0x11);
        pk_object_set_anim(o, 0x10084d60u);
        pk_object_clear_work(o);
        o->state = 0x4f;
        break;
    case 1:
        fx_setup(o, g, 0x5e, FX_FR_5E, 0x91);
        pk_object_set_anim(o, 0x10084dc0u);
        pk_object_clear_work(o);
        o->state = 0x4f;
        break;
    case 2:
        fx_setup(o, g, 0x5e, FX_FR_5E, 0x81);
        o->frame = 0x6c;
        fx_rise_start(o, 9);
        break;
    case 4: {
        int kind = o->x;
        u = unit_object(g, o->arg0, o->arg1 & 3);
        if (!u) { pk_object_kill(o); return; }
        fx_setup(o, g, 0x5e, FX_FR_5E, 0x81);
        if (kind >= 0 && kind <= 4) o->frame = 0x12 + kind;
        o->x = u->x;
        pk_object_clear_work(o);
        o->work[1] = (u->flags & PK_OBJ_FLIP_X) ? 3 : -3;
        o->work[2] = -6;
        o->state = 5;
        break;
    }
    case 5:
        o->x += o->work[1];
        o->y += o->work[2]++;
        if (++o->counter > 9) pk_object_kill(o);
        break;
    case 6: case 8: case 10: case 0xc: case 0xe: case 0x10: case 0x12: case 0x14: case 0x16: case 0x18:
    case 0x1a: case 0x1c: {
        static const int k_label[0x1d] = { [6] = 0x57, [8] = 0x58, [10] = 0x55, [0xc] = 0x53, [0xe] = 0x54,
                                           [0x10] = 0x56, [0x12] = 0x51, [0x14] = 0x52, [0x16] = 0x4f,
                                           [0x18] = 0x4d, [0x1a] = 0x4e, [0x1c] = 0x50 };
        fx_setup(o, g, 0x5e, FX_FR_5E, 0x81);
        o->frame = k_label[o->state];
        fx_rise_start(o, o->state + 1);
        break;
    }
    case 3: case 7: case 9: case 0xb: case 0xd: case 0xf: case 0x11: case 0x13: case 0x15: case 0x17:
    case 0x19: case 0x1b: case 0x1d: case 0x4e:
        fx_rise(o);
        break;
    case 0x1e:
    case 0x1f:
        fx_setup(o, g, 0x62, 0x100840b8u, 0x81);
        pk_object_set_anim(o, o->state == 0x1e ? 0x10084da0u : 0x10084d80u);
        o->layer = 1;
        pk_object_clear_work(o);
        o->counter = 0;
        o->state = 0x20;
        break;
    case 0x20:
        pk_object_animate(o);
        if (++o->counter > 9) pk_object_kill(o);
        break;
    case 0x21:
        fx_setup(o, g, 0x72, 0x10084520u, 0x491);
        pk_object_set_anim(o, 0x100850a8u);
        o->y -= 0xc;
        o->counter = 0;
        o->state = 0x22;
        break;
    case 0x22:
        pk_object_animate(o);
        if (o->counter == 0) pk_object_spawn(g, fx_update, OBJ_FX, 0x23, o->arg0, o->arg1, o->x - 1, o->y - 0xe);
        else if (o->counter == 2) pk_object_spawn(g, fx_update, OBJ_FX, 0x23, o->arg0, o->arg1, o->x + 10, o->y - 6);
        else if (o->counter == 4) pk_object_spawn(g, fx_update, OBJ_FX, 0x23, o->arg0, o->arg1, o->x + 4, o->y + 2);
        else if (o->counter == 9) pk_object_kill(o);
        o->counter++;
        break;
    case 0x23: {
        pk_object *src = unit_object(g, o->arg0, o->arg1 & 3);
        fx_setup(o, g, 0x60, FX_FR_60, 0x481);
        o->frame = 0xe;
        if (src && o->x > src->x) o->flags |= PK_OBJ_FLIP_X;
        o->counter = 0;
        o->state = 0x24;
        break;
    }
    case 0x24:
        if (++o->counter >= 0xc) pk_object_kill(o);
        break;
    case 0x25:
        fx_setup(o, g, 100, 0x10084148u, 0x11);
        pk_object_set_anim(o, 0x10084de8u);
        o->layer = 1;
        o->state = 0x4f;
        break;
    case 0x26:
        if (o->arg0 == 1) o->x += 0xb0;
        pk_object_clear_work(o);
        o->state = 0x27;
        break;
    case 0x27:
        o->x += o->arg0 == 0 ? 0x1e : -0x1e;
        pk_object_spawn(g, fx_update, OBJ_FX, 0x28, 0, 0, o->x, 0x85);
        if (++o->counter >= 5) pk_object_kill(o);
        break;
    case 0x28:
        fx_setup(o, g, 0x5e, FX_FR_5E, 0x491);
        pk_object_set_anim(o, 0x10084e20u);
        pk_object_clear_work(o);
        o->state = 0x4f;
        break;
    case 0x29:
        fx_setup(o, g, 0x5e, FX_FR_5E, 0x491);
        pk_object_set_anim(o, 0x10084e58u);
        pk_object_clear_work(o);
        o->state = 0x2a;
        break;
    case 0x2a:
        pk_object_animate(o);
        if (!(fx_status(o) & 0x1000) || fx_unit_down(o)) pk_object_kill(o);
        break;
    case 0x2b: fx_setup(o, g, 0x66, 0x100841d8u, 0x491); fx_anim(o, 0x10084ea0u, 1); break;
    case 0x2c: fx_setup(o, g, 0x68, 0x100842c8u, 0x491); fx_anim(o, 0x10084ef8u, 1); break;
    case 0x2d:
        fx_setup(o, g, 0x5e, FX_FR_5E, 0x491);
        o->frame = 0x42;
        o->layer = 1;
        pk_object_clear_work(o);
        o->state = 0x2e;
        break;
    case 0x2e: fx_follow(o, g, -0x2d, 0x10); break;
    case 0x2f:
        fx_setup(o, g, 0x5e, FX_FR_5E, 0x481);
        pk_object_set_anim(o, 0x10084f68u);
        o->layer = 1;
        pk_object_clear_work(o);
        o->state = 0x30;
        break;
    case 0x30: fx_follow(o, g, -0x1e, 0x60); break;
    case 0x31: fx_setup(o, g, 0x60, FX_FR_60, 0x11); fx_anim(o, 0x10084f30u, 1); break;
    case 0x32:
        fx_setup(o, g, 0x5e, FX_FR_5E, 0x481);
        pk_object_set_anim(o, 0x10084f80u);
        o->layer = 1;
        pk_object_clear_work(o);
        o->state = 0x33;
        break;
    case 0x33: fx_follow(o, g, 0, 0x400); break;
    case 0x34:
        fx_setup(o, g, 0x5e, FX_FR_5E, 0x491);
        pk_object_set_anim(o, 0x10084fa0u);
        o->layer = 1;
        o->counter = 0x96;
        pk_object_clear_work(o);
        o->state = 0x35;
        break;
    case 0x35:
        fx_follow(o, g, -0x14, 0);
        if (!o->dead && --o->counter == 0) { /* the status 8 seal ends */
            pk_b.status[rside(o->arg0)][o->arg1 % 6] &= ~8;
            pk_object_kill(o);
        }
        break;
    case 0x36:
        fx_setup(o, g, 0x5e, FX_FR_5E, 0x491);
        pk_object_set_anim(o, 0x10084fc8u);
        o->layer = 1;
        pk_object_clear_work(o);
        o->state = 0x37;
        break;
    case 0x37: fx_follow(o, g, -0x2d, 0x100); break;
    case 0x38:
        fx_setup(o, g, 0x6c, 0x10084358u, 0x491);
        pk_object_set_anim(o, 0x10084ff0u);
        o->counter = 0;
        pk_object_clear_work(o);
        o->state = 0x4f;
        break;
    case 0x39:
        fx_setup(o, g, 0x6a, 0x10084400u, 0x481);
        pk_object_set_anim(o, 0x10085030u);
        o->counter = 0;
        pk_object_clear_work(o);
        o->state = 0x3a;
        break;
    case 0x3a:
        pk_object_animate(o);
        if (o->work[0] == 0) {
            o->x -= 3;
            o->y += 0x10;
            if (o->y >= 0x8d) {
                o->work[0] = 1;
                o->counter = 0;
                pk_object_set_anim(o, 0x10085048u);
            }
        } else if (++o->counter >= 7) {
            pk_object_kill(o);
        }
        break;
    case 0x3b:
        fx_setup(o, g, 0x5e, FX_FR_5E, 0x481);
        pk_object_set_anim(o, 0x10085088u);
        o->layer = 1;
        pk_object_clear_work(o);
        o->state = 0x3c;
        break;
    case 0x3c: fx_follow(o, g, 0, 0x2000); break;
    case 0x3d: {
        pk_object *src = unit_object(g, o->arg0, o->arg1 & 3);
        fx_setup(o, g, 0x70, 0x10084610u, 1);
        if (src && src->x <= o->x) o->flags = 5;
        pk_object_set_anim(o, 0x10085100u);
        pk_object_clear_work(o);
        o->state = 0x4f;
        break;
    }
    case 0x3f:
        fx_setup(o, g, 0x5c, 0x100844d8u, 0x81);
        o->frame = 2;
        o->layer = 1;
        o->counter = 0;
        pk_object_clear_work(o);
        o->state = 0x40;
        break;
    case 0x40:
        if (o->work[1] == 0) {
            o->y += 10;
            if (o->y >= 0x79) { o->work[1] = 1; o->work[2] = -5; }
        } else if (o->work[1] == 1) {
            o->y += o->work[2];
            if (++o->work[2] == 10) o->work[1] = 2;
        } else {
            o->y += o->work[2];
            if (o->y >= 0x12d) pk_object_kill(o);
        }
        break;
    case 0x41: fx_setup(o, g, 0x68, 0x100842c8u, 0x11); fx_anim(o, 0x10084ef8u, 1); break;
    case 0x42: fx_setup(o, g, 0x76, 0x10084700u, 0x11); fx_anim(o, 0x10085158u, 1); break;
    case 0x43: fx_setup(o, g, 0x74, 0x10084700u, 0x11); fx_anim(o, 0x10085158u, 1); break;
    case 0x44:
        fx_setup(o, g, 0x78, 0x100847f0u, 0x91);
        o->y = 0x5c;
        pk_object_set_anim(o, 0x100851b0u);
        o->layer = 1;
        pk_object_clear_work(o);
        o->state = 0x4f;
        break;
    case 0x45: fx_setup(o, g, 0x7a, 0x100847f0u, 0x91); fx_anim(o, 0x100851b0u, 1); break;
    case 0x46: fx_setup(o, g, 0x7c, 0x100847f0u, 0x91); fx_anim(o, 0x100851b0u, 1); break;
    case 0x47: fx_setup(o, g, 0x82, 0x10084a60u, 0x91); fx_anim(o, 0x10085290u, 1); break;
    case 0x48: fx_setup(o, g, 0x7e, 0x100848e0u, 0x91); fx_anim(o, 0x10085208u, 1); break;
    case 0x49: fx_setup(o, g, 0x80, 0x10084af0u, 0x91); fx_anim(o, 0x100852c8u, 1); break;
    case 0x4a: fx_setup(o, g, 0x84, 0x10084bb0u, 0x81); fx_anim(o, 0x10085308u, 1); break;
    case 0x4c: fx_setup(o, g, 0x84, 0x10084bb0u, 0x81); fx_anim(o, 0x10085388u, 1); break;
    case 0x4b:
    case 0x4d:
        fx_setup(o, g, 0x84, 0x10084bb0u, 0x81);
        o->frame = o->state == 0x4b ? 0x11 : 0x10;
        fx_rise_start(o, 0x4e);
        break;
    case 0x4f:
        pk_object_animate(o);
        if (!(o->flags & PK_OBJ_ANIMATING)) pk_object_kill(o);
        break;
    default:
        pk_object_kill(o);
        break;
    }
}
