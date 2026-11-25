/* SPDX-License-Identifier: MIT */
#ifndef PK_OBJECT_H
#define PK_OBJECT_H

#include <SDL3/SDL_stdinc.h>
#include <stdbool.h>

/* Task objects. In the original everything on screen and most of the
 * game flow is an object of 0x9c bytes (constructor FUN_1006ab10,
 * created through FUN_1006d544(game, update, type, state, a, b, x, y)).
 * Each logic frame the game calls the update function of every object in
 * list order (FUN_1006ad3c); an update is a state machine on `state`.
 * Objects flagged dead are deleted after the pass (FUN_1006d5f4).
 * Offsets in comments refer to the original object. */

#ifndef PK_GAME_TYPEDEF
#define PK_GAME_TYPEDEF
typedef struct pk_game pk_game;
#endif
typedef struct pk_object pk_object;
typedef void (*pk_object_fn)(pk_object *self, pk_game *g);

/* Animation tables (+0x94) are pairs of words {frame, duration in logic
 * frames}, ended by PK_ANIM_LOOP (restart) or PK_ANIM_STOP (stop). Like
 * the sprite frame tables (+0x98) they live in the game executable and
 * are referenced by their original address (see core/pk_app.h). */
#define PK_ANIM_LOOP  0xffff
#define PK_ANIM_STOP  0xfffe
#define PK_IMAGE_NONE 0xff

/* Object flags (+0x0c), as tested by the object renderer FUN_1006de4c. */
enum
{
    PK_OBJ_SPRITE = 1u << 0,         /* draw frame `frame` of the frame table */
    PK_OBJ_ANIMATING = 1u << 1,      /* animation table running */
    PK_OBJ_FLIP_X = 1u << 2,         /* mirrored (FUN_1006d91c) */
    PK_OBJ_ADDITIVE = 1u << 4,       /* OR drawing (FUN_1006de4c) */
    PK_OBJ_ALT_DRAW = 1u << 5,       /* unmasked blit (FUN_1006dd14) */
    PK_OBJ_SCREEN = 1u << 6,         /* ignore the camera (game+0x74/+0x78) */
    PK_OBJ_FULL_ADDITIVE = 1u << 7,  /* unmirrored draws read the whole bitmap, not its lower half */
    PK_OBJ_LINE = 1u << 8,           /* line to (x2, y2) in work[0..2] colour */
    PK_OBJ_RECT = 1u << 9,           /* filled w x h rectangle in work[0..2] colour */
    PK_OBJ_KEEP_OFFSCREEN = 1u << 10 /* not culled off screen */
};

#define PK_OBJ_WORK 8
#define PK_ANY      0xff /* wildcard for pk_object_find */

struct pk_object
{
    int frame;             /* +0x04 current animation / sprite frame */
    int image;             /* +0x08 image slot (pk_game_image_slot), PK_IMAGE_NONE hides */
    Uint32 flags;          /* +0x0c */
    int anim_timer;        /* +0x10 */
    int anim_index;        /* +0x14 */
    int work[PK_OBJ_WORK]; /* +0x18..+0x34, cleared by FUN_1006ad14 */
    int counter;           /* +0x3c general frame counter */
    int layer;             /* +0x40 render pass 0 or 1 */
    int dir;               /* +0x44 slide direction given to children */
    int x2, y2;            /* +0x4c/+0x50 line end point */
    int w, h;              /* +0x54/+0x58 rectangle size */
    bool dead;             /* +0x60 */
    int target;            /* +0x64 (battle units: the target's slot, 0xff none) */
    pk_object_fn update;   /* +0x68 */
    int type;              /* +0x6c object class id (0xc2, 0xc3, ...) */
    int state;             /* +0x70 state of the update state machine */
    int arg0;              /* +0x74 */
    int arg1;              /* +0x78 */
    int arg2;              /* +0x7c (battle units: the record side) */
    int x;                 /* +0x80 */
    int y;                 /* +0x84 */
    Uint32 anim;           /* +0x94 animation table (original address) */
    Uint32 frames;         /* +0x98 sprite frame table (original address) */
    void *user;            /* engine extension: per-object data owned by the object */
    void (*destroy)(pk_object *self);

    int smooth_x, smooth_y, smooth_current_x, smooth_current_y, smooth_image;
    Uint32 smooth_frames, smooth_flags;
    bool smooth_valid;
};

typedef struct pk_object_list
{
    pk_object **items;
    int count;
    int cap;
} pk_object_list;

void pk_object_list_free(pk_object_list *l);

pk_object *pk_object_spawn(pk_game *g, pk_object_fn update, int type, int state, int arg0, int arg1, int x, int y);
void pk_object_kill(pk_object *o);
void pk_object_set_update(pk_object *o, pk_object_fn update); /* FUN_1006ad90 */
void pk_object_clear_work(pk_object *o);                      /* FUN_1006ad14 */
void pk_object_set_anim(pk_object *o, Uint32 anim);           /* FUN_1006ac04 */
void pk_object_set_frames(pk_object *o, Uint32 frames);       /* FUN_1006ac2c */
void pk_object_animate(pk_object *o);                         /* FUN_1006ac44 */

/* FUN_100707a4: first object matching type/arg0/arg1 (PK_ANY = any). */
pk_object *pk_object_find(pk_game *g, int type, int arg0, int arg1);

/* One logic pass over the current list. Returns early when the scene
 * changes during the pass, like the original loop. */
void pk_objects_update(pk_game *g);
void pk_objects_reap(pk_game *g);
void pk_objects_clear(pk_game *g);

/* Draws all objects of one render pass (FUN_1006de4c). */
void pk_objects_draw(pk_game *g, int layer);

#endif
