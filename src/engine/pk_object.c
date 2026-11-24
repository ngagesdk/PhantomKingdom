/* SPDX-License-Identifier: MIT */
#include "engine/pk_object.h"
#include "core/pk_app.h"
#include "core/pk_log.h"
#include "engine/pk_game.h"
#include "gfx/pk_gfx.h"

#include <SDL3/SDL.h>

static void destroy_obj(pk_object *o)
{
    if (o->destroy) {
        o->destroy(o);
    }
    SDL_free(o);
}

void pk_object_list_free(pk_object_list *l)
{
    int i;
    for (i = 0; i < l->count; i++) {
        destroy_obj(l->items[i]);
    }
    SDL_free(l->items);
    SDL_zerop(l);
}

pk_object *pk_object_spawn(pk_game *g, pk_object_fn update, int type, int state, int arg0, int arg1, int x, int y)
{
    pk_object_list *l = g->objects;
    pk_object *o = (pk_object *)SDL_calloc(1, sizeof(*o));
    if (!o) {
        return NULL;
    }
    if (l->count == l->cap) {
        int cap = l->cap ? l->cap * 2 : 32;
        pk_object **n = (pk_object **)SDL_realloc(l->items, (size_t)cap * sizeof(*n));
        if (!n) {
            SDL_free(o);
            return NULL;
        }
        l->items = n;
        l->cap = cap;
    }
    o->update = update;
    o->image = PK_IMAGE_NONE;
    o->type = type;
    o->state = state;
    o->arg0 = arg0;
    o->arg1 = arg1;
    o->x = x;
    o->y = y;
    l->items[l->count++] = o;
    return o;
}

void pk_object_kill(pk_object *o) { o->dead = true; }

void pk_object_set_update(pk_object *o, pk_object_fn update)
{
    o->update = update;
}

void pk_object_clear_work(pk_object *o)
{
    SDL_memset(o->work, 0, sizeof(o->work));
}

static int anim_word(Uint32 table, int index, int field)
{
    return pk_app_i32(table + (Uint32)(index * 8 + field * 4));
}

void pk_object_set_anim(pk_object *o, Uint32 anim)
{
    o->anim = anim;
    o->frame = anim ? anim_word(anim, 0, 0) : 0;
    o->anim_timer = 0;
    o->anim_index = 0;
    if (anim) {
        o->flags |= PK_OBJ_ANIMATING;
    }
}

void pk_object_set_frames(pk_object *o, Uint32 frames)
{
    o->frames = frames;
}

void pk_object_animate(pk_object *o)
{
    int next;
    if (!(o->flags & PK_OBJ_ANIMATING) || !o->anim) {
        return;
    }
    if (++o->anim_timer < anim_word(o->anim, o->anim_index, 1)) {
        return;
    }
    o->anim_timer = 0;
    o->anim_index++;
    next = anim_word(o->anim, o->anim_index, 0);
    if (next == PK_ANIM_LOOP) {
        o->anim_index = 0;
        o->frame = anim_word(o->anim, 0, 0);
    } else if (next == PK_ANIM_STOP) {
        o->anim_index = 0;
        o->flags &= ~(Uint32)PK_OBJ_ANIMATING;
    } else {
        o->frame = next;
    }
}

pk_object *pk_object_find(pk_game *g, int type, int arg0, int arg1)
{
    int i;
    for (i = 0; i < g->objects->count; i++) {
        pk_object *o = g->objects->items[i];
        if ((type == PK_ANY || o->type == type) && (arg0 == PK_ANY || o->arg0 == arg0) &&
            (arg1 == PK_ANY || o->arg1 == arg1)) {
            return o;
        }
    }
    return NULL;
}

void pk_objects_update(pk_game *g)
{
    int i, n = g->objects->count;
    int mode = g->mode;
    pk_object_list *list = g->objects;
    for (i = 0; i < n && i < list->count; i++) {
        pk_object *o = list->items[i];
        if (o->dead) {
            continue;
        }
        if (o->update) {
            o->update(o, g);
        }
        /* Off-screen objects are removed unless flagged (FUN_1006ad3c):
         * x outside -20 .. 372. */
        if (!o->dead && !(o->flags & PK_OBJ_KEEP_OFFSCREEN) && (Uint32)(o->x + 20) > 0x188u) {
            o->dead = true;
        }
        if (g->mode != mode || g->objects != list) {
            break;
        }
    }
}

void pk_objects_reap(pk_game *g)
{
    pk_object_list *l = g->objects;
    int i, j = 0;
    for (i = 0; i < l->count; i++) {
        if (l->items[i]->dead) {
            destroy_obj(l->items[i]);
        } else {
            l->items[j++] = l->items[i];
        }
    }
    l->count = j;
}

void pk_objects_clear(pk_game *g)
{
    pk_object_list *l = g->objects;
    int i;
    for (i = 0; i < l->count; i++) {
        destroy_obj(l->items[i]);
    }
    l->count = 0;
}

static pk_color work_color(const pk_object *o)
{
    return 0xFF000000u | ((Uint32)(o->work[0] & 0xff) << 16) | ((Uint32)(o->work[1] & 0xff) << 8) |
           (Uint32)(o->work[2] & 0xff);
}

static void draw_line(int x0, int y0, int x1, int y1, pk_color c)
{
    int dx = SDL_abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -SDL_abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        pk_gfx_fill(x0, y0, 1, 1, c);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        if (2 * err >= dy) {
            err += dy;
            x0 += sx;
        }
        if (2 * err <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void pk_objects_draw(pk_game *g, int layer)
{
    int i;
    for (i = 0; i < g->objects->count; i++) {
        pk_object *o = g->objects->items[i];
        int cx = 0, cy = 0;
        const pk_image *img;
        pk_frame f;

        if (o->dead || o->layer != layer || o->image == PK_IMAGE_NONE) {
            continue;
        }
        if (!(o->flags & PK_OBJ_SCREEN)) {
            cx = g->cam_x;
            cy = g->cam_y;
        }
        if ((o->flags & PK_OBJ_SPRITE) && o->frames) {
            img = pk_game_slot_image(g, o->image);
            if (!img || !pk_app_frame(o->frames, o->frame, &f)) {
                continue;
            }
            /* FUN_1006de4c: flag 4 draws with FUN_1006d91c, otherwise flag 0x20
             * with FUN_1006dd14, else FUN_1006db1c; flag 0x10 switches the
             * device to OR drawing around the call. Sheets drawn both ways hold
             * two copies: the mirrored one on top, read at x = width - sx - w,
             * and the plain one in the lower half (flag 0x80 clear) */
            /* (port: a sheet with its halves the other way round is read
             * from the other half, see pk_game_slot_halves_swapped) */
            bool swap = !(o->flags & PK_OBJ_FULL_ADDITIVE) && pk_game_slot_halves_swapped(g, o->image, o->frames);
            if (o->flags & PK_OBJ_FLIP_X) {
                int sx = img->w - f.sx - f.w, sy = f.sy + (swap ? img->h / 2 : 0);
                int dx = o->x - cx - (f.w + f.ox), dy = o->y - cy + f.oy;
                if (o->flags & PK_OBJ_ADDITIVE) {
                    pk_gfx_blit_or(img, sx, sy, f.w, f.h, dx, dy);
                } else {
                    pk_gfx_blit(img, sx, sy, f.w, f.h, dx, dy, 0);
                }
            } else {
                int half = ((o->flags & PK_OBJ_FULL_ADDITIVE) || swap) ? 0 : img->h / 2;
                int dx = o->x - cx + f.ox, dy = o->y - cy + f.oy;
                if (o->flags & PK_OBJ_ALT_DRAW) {
                    if (o->flags & PK_OBJ_ADDITIVE) {
                        pk_gfx_blit_or(img, f.sx, f.sy + half, f.w, f.h, dx, dy);
                    } else {
                        pk_gfx_blit(img, f.sx, f.sy + half, f.w, f.h, dx, dy, PK_BLIT_OPAQUE);
                    }
                } else if (o->flags & PK_OBJ_ADDITIVE) {
                    pk_gfx_blit_or(img, f.sx, f.sy + half, f.w, f.h, dx, dy);
                } else {
                    pk_gfx_blit(img, f.sx, f.sy + half, f.w, f.h, dx, dy, 0);
                }
            }
        } else if (o->flags & PK_OBJ_LINE) {
            draw_line(o->x - cx, o->y - cy, o->x2 - cx, o->y2 - cy, work_color(o));
        } else if (o->flags & PK_OBJ_RECT) {
            pk_gfx_fill(o->x - cx, o->y - cy, o->w, o->h, work_color(o));
        }
    }
}
