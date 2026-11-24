/* SPDX-License-Identifier: MIT */
#ifndef PK_GAME_H
#define PK_GAME_H

#include "core/pk_rand.h"
#include "core/pk_types.h"
#include "engine/pk_object.h"
#include "engine/pk_textq.h"
#include "game/pk_world.h"
#include "gfx/pk_gfx.h"

/* The game object. In the original this is the application's main
 * control (more than 0x1b000 bytes of state). Only the fields the port
 * uses so far are reconstructed; the comment gives the original offset.
 *
 * Frame structure (FUN_1006f3c4, called by a 30 ms CPeriodic):
 *   tick_phase 0: logic. Mode handler, then every object's update.
 *   tick_phase 1: render pass 0 and 1 into the back buffer, curtain, present.
 *   tick_phase 2: present again, advance the curtain (FUN_10070944).
 * The back buffer is persistent: when bg_visible is 0 nothing clears it,
 * and screens such as "Now Loading" are drawn once and stay. */

/* Top level modes (game+0x88), dispatched by FUN_1006f1f0. */
typedef enum pk_mode
{
    PK_MODE_BATTLE = 0,  /* FUN_10068db0: loads both sides' units */
    PK_MODE_FIELD = 1,   /* FUN_10069334: the kingdom screen and its menus */
    PK_MODE_KINGDOM = 2, /* FUN_10068ff8: home screen with the secretary */
    PK_MODE_BOOT = 3,    /* FUN_1006b5c4: copyright, logo, loading, title */
    /* Port only: modes the original runs inside others. */
    PK_MODE_ARENA_PENDING = 60,
    PK_MODE_CREDITS_PENDING = 61,
    PK_MODE_ENDING = 63, /* port: 0x10038208 run as its own mode (game+0x58 == 3) */
    PK_MODE_VIEWER = 64, /* port only: asset viewer (PK_ENABLE_ASSET_VIEWER) */
    PK_MODE_COUNT_MAX = 65
} pk_mode;

/* Black curtain used for screen transitions (game+0x98 mode, +0x9c width). */
typedef enum pk_curtain
{
    PK_CURTAIN_NONE = 0,
    PK_CURTAIN_CLOSE = 1,     /* 0 -> full width, then HELD */
    PK_CURTAIN_OPEN = 2,      /* full width -> 0, then NONE */
    PK_CURTAIN_CLOSE_ALT = 3, /* like CLOSE */
    PK_CURTAIN_BLACK = 4,     /* static black */
    PK_CURTAIN_HELD = 5       /* closed, waiting */
} pk_curtain;

/* Symbian standard scan codes as stored in game+0x68. */
enum
{
    PK_SC_NONE = 0x00,
    PK_SC_BACKSPACE = 0x01, /* clear key */
    PK_SC_LEFT = 0x0e,
    PK_SC_RIGHT = 0x0f,
    PK_SC_UP = 0x10,
    PK_SC_DOWN = 0x11,
    PK_SC_STAR = 0x2a,
    PK_SC_0 = 0x30, /* '0'..'9' = 0x30..0x39 */
    PK_SC_5 = 0x35, /* select */
    PK_SC_7 = 0x37,
    PK_SC_BACK = 0x37, /* the key handler (FUN_10070100) turns the right soft key into 0x37 */
    PK_SC_HASH = 0x7f,
    PK_SC_LSK = 0xa4,
    PK_SC_RSK = 0xa5
};

#define PK_MAX_SLOTS      256
#define PK_BATTLE_FIELD_Y 0x20  /* port assumption: top of the battlefield bitmap */
#define PK_PORTRAIT_BASE  0x1e8 /* slot ids of pokeport.mbm bitmaps */

typedef struct pk_slot
{
    int id;       /* index in pokekin.mbm */
    int mask_off; /* mask bitmap id - id, 0 = none */
    const pk_image *img;
    Uint32 halves_frames[4]; /* frame tables the layout was checked with */
    Uint8 halves_swapped[4]; /* port: plain copy on top, mirrored copy below */
} pk_slot;

typedef void (*pk_mode_fn)(pk_game *g);

struct pk_game
{
    pk_object_list list_a, list_b;
    pk_object_list *objects; /* +0x654 */
    pk_object_list *saved;   /* +0x658 (pause overlay swaps lists) */
    pk_drawq drawq;          /* +0x65c text queue */

    pk_slot slots[PK_MAX_SLOTS]; /* +0x650 bitmap list */
    int slot_count;

    int bg_id[3];                 /* +0x38/+0x3c/+0x40 background bitmaps */
    int overlay_id;               /* +0x44 a second picture over the background (FUN_10070ad8), -1 none */
    int overlay_x, overlay_y;     /* +0x48 / +0x4c where it is drawn (shown while +0x50) */
    int bg_visible;               /* +0xb8 */
    int entry_reason;             /* +0x58: 0 cold start, 3 back from Arena, 10 ... */
    int ending_pending;           /* port: +0x58 = 3 after the last boss (FUN_10001xxx state 4) */
    int frame_count;              /* +0x60 */
    int key_code;                 /* +0x68 scan code of this frame's key press */
    int timer;                    /* +0x70 */
    int cam_x, cam_y;             /* +0x74/+0x78 */
    int tick_phase;               /* +0x7c */
    int mode;                     /* +0x88 */
    int phase;                    /* +0x8c */
    int mode_timer;               /* +0x90 */
    int render_pass;              /* +0x94 */
    int curtain;                  /* +0x98 */
    int curtain_w;                /* +0x9c */
    int title_ready;              /* +0xa4 */
    int team_sel;                 /* +0xe8 team being edited / sent */
    int list_top;                 /* +0xf0 first visible list entry */
    int cursor;                   /* +0xf4 cursor within the visible entries */
    int list_f8;                  /* +0xf8 */
    int list_fc;                  /* +0xfc */
    int dlg_type;                 /* +0x100 dialog type (labels 0x1008b93c) / list kind */
    int dlg_result;               /* +0x104 0 confirmed, 1 cancelled */
    int dlg_count;                /* +0x108 entries */
    int dlg_y;                    /* +0x10c y of the first row */
    int slide;                    /* +0x110 slide direction of screen parts: 0 left, 1 right, 2 none */
    int sel;                      /* +0x114 */
    int sel2;                     /* +0x118 */
    int list_120, list_124;       /* +0x120/+0x124 */
    void (*dlg_hint)(pk_game *g); /* port: the caller's line under a dialog (the
                                     original switches on the caller in 0x1002637c) */
    int arena_available;          /* !(+0x43c == 0 && +0x440 < 3) */
    int invasion_timer;           /* +0x4d8 logic frames to the next invasion (FUN_1007154c) */
    int event_seq;                /* +0x780c event sequence counter */
    int boss_battle;              /* +0xe0: 1 in a battle against an area boss */
    int battle_result;            /* port: 1 won, 2 lost, 0 none since the last kingdom visit */
    int arena_online;             /* game+0x1aa0c == 2: logged in to the N-Gage Arena */
    int volume;                   /* +0x4e0, 0..5 */
    int language;                 /* +0x5cc */

    /* Player kingdom (new game: FUN_1007b018/FUN_10064dc8 and the save
     * game reset at 0x1006c02x). */
    Uint32 menu_disabled;     /* +0x134 bit per kingdom menu cell */
    int rival_index;          /* +0x138 */
    int map_page;             /* +0x42c world map page shown (Map > Move) */
    int name_index;           /* +0x13c record of name.dat */
    int fame;                 /* +0x434 points; level thresholds 0x1008d280 */
    int loot;                 /* +0x438 money */
    int progress;             /* +0x440 story progress */
    int field_bg;             /* +0x4f0 kingdom background bitmap */
    Uint16 kingdom_name[25];  /* +0x544 */
    pk_world world;           /* everything else that save.dat holds */
    pk_object_fn call_ret_fn; /* *(game+0x3b14): return address of FUN_1006ad98 */
    int call_ret_state;
    /* port: earlier returns while a called routine calls another one (the
     * original keeps one; its nested screens switch updates by hand) */
    pk_object_fn call_stack_fn[4];
    int call_stack_state[4];
    int call_depth;
    int show_version;        /* +0x1b2fc */
    Uint16 version_text[64]; /* contents of e:\game.id */

    /* port: touch and mouse (see pk_tap) */
    int tap, tap_x, tap_y; /* this tick's tap, until a target takes it */
    int tap_targets, tap_targets_prev;
    int swipe_y, swipe_x; /* this tick's scroll steps, until taken */
    int swipe_users_y, swipe_users_y_prev, swipe_users_x, swipe_users_x_prev;
    int swipe_pend_y, swipe_pend_x; /* steps still to be sent as keys */

    pk_rng rng;
    bool quit;
    bool skip_intro;
    pk_mode_fn mode_handlers[PK_MODE_COUNT_MAX];
};

bool pk_game_init(pk_game *g);
void pk_game_shutdown(pk_game *g);

/* Touch (port). Screens call these every tick from their update, also
 * when nothing was touched: the calls tell that the screen has touch
 * targets. A screen without any gets a tap as the select key and the
 * scroll steps as arrow keys.
 * pk_tap: a tap inside the rectangle this tick (taken).
 * pk_tap_any: a tap anywhere (taken).
 * pk_tap_peek: a tap inside the rectangle, left for others; registers no target.
 * pk_swipe_y / pk_swipe_x: this tick's scroll steps (taken; > 0 = on). */
bool pk_tap(pk_game *g, int x, int y, int w, int h);
bool pk_tap_any(pk_game *g);
bool pk_tap_peek(pk_game *g, int x, int y, int w, int h);
int pk_swipe_y(pk_game *g);
/* rows of h pixels from (x, y): a tap on row i sets *cursor = i and
 * makes this tick's key the select key */
bool pk_tap_rows(pk_game *g, int x, int y, int w, int h, int rows, int *cursor);
int pk_swipe_x(pk_game *g);
/* Moves a list's first visible entry by the scroll steps, keeping the
 * cursor on the same entry while it stays visible; returns true if it
 * moved. count = entries, visible = rows shown. */
bool pk_swipe_list(pk_game *g, int *top, int *cursor, int count, int visible);

/* One 30 ms timer tick. Returns true when the frame buffer changed. */
bool pk_game_tick(pk_game *g);

void pk_game_set_mode(pk_game *g, int mode);       /* FUN_10070844 */
void pk_game_set_phase(pk_game *g, int phase);     /* FUN_10070874 */
void pk_game_set_curtain(pk_game *g, int mode);    /* FUN_1007088c */
void pk_game_set_bg(pk_game *g, int slot, int id); /* FUN_10070a10 */

/* Bitmap list (FUN_10070250 adds, FUN_100705c4 finds). */
int pk_game_load_image(pk_game *g, int id, int mask_off);
int pk_game_image_slot(pk_game *g, int id); /* PK_IMAGE_NONE if absent */
const pk_image *pk_game_slot_image(pk_game *g, int slot);
/* port: true when a two-copy sheet keeps its plain copy in the upper half
 * (checked once per frame table against where the frames have pixels) */
bool pk_game_slot_halves_swapped(pk_game *g, int slot, Uint32 frames);
void pk_game_release_images(pk_game *g);

/* Applies g->language: text tables and the language of bitmaps. */
void pk_game_apply_language(pk_game *g);

/* Options (the original's options.dat; the port keeps options.cfg in the
 * preference folder) and save game presence (save.dat). */
void pk_options_load(pk_game *g);
void pk_options_save(const pk_game *g);
bool pk_save_exists(void);
void pk_save_delete(void);

/* FUN_1006ad98: switches o to (fn, state) and remembers where to return. */
void pk_object_call(pk_game *g, pk_object *o, pk_object_fn fn, int state, pk_object_fn ret_fn, int ret_state);
/* Returns o to the remembered update function and state. */
void pk_object_return(pk_game *g, pk_object *o);

/* FUN_10079ab8: removes every object except keep. */
void pk_object_kill_others(pk_game *g, pk_object *keep);

/* Immediate fill into the back buffer (FUN_1006d884). */
void pk_game_fill(pk_game *g, int x, int y, int w, int h, pk_color c);

/* Text queue (FUN_100654c0). */
void pk_game_text(pk_game *g, const Uint16 *s, int x, int y, pk_color c, pk_align a, int layer);
/* FUN_100653f8: queues record `id` of mestext.dat. */
void pk_game_mestext(pk_game *g, int id, int x, int y, pk_color c, pk_align a, int layer, int font);

#endif
