/* SPDX-License-Identifier: MIT */
/* Global Auction (0x1002a544; screens 0x1002bd20 exhibit, 0x1002cd38
 * browse, 0x1002dba8 collect, 0x1002d574 own list), reached from the
 * Shop while online. Messages (PhantomLink docs/ARENA.md):
 *   13 exhibit {name @4, item bytes @20..26, u32 price @28}  -> 141
 *   14 browse  {[2..4], [8] filters (0xff any), u32 page @12, name @16}
 *              -> 142 {u16 page @2, u32 matches @4, 10 x 24 from @8}
 *   15 bid     {u32 auction @4, u32 amount @8, name @12}      -> 143
 *   16 own list {[3] mode 0..3, name @4}                       -> 144
 * List entry: u32 id, [4] status, [5..11] item bytes, u32 seconds left
 * @12, u32 price @16, u32 own bid @20. Numbers big endian.
 * Item bytes (port's reading): [0] 0 item / 1 equipment, [1] kind,
 * [2] id, [3..5] values, [6] rank; a won item is rebuilt from kind/id.
 * The menu is the generic dialog type 0x10 (mestext 341..344) over the
 * shop, as in the original (state 6, FUN_1006ad98); a request shows the
 * window frame 0xa9 (frames 0x1008d3e4) at 0x5a, 0x5d with mestext
 * 354 / 355 and the cursor animation under it (states 9 / 0xb / 0xc).
 * Exhibiting picks the kind (dialog 0xb) and the item (0x10039b34) and
 * takes the starting price on the number entry; the lists follow the
 * list screen 0x1002df64 (see auc_list_draw). */
#include "audio/pk_audio.h"
#include "core/pk_app.h"
#include "core/pk_fs.h"
#include "core/pk_log.h"
#include "game/pk_arena_game.h"
#include "game/pk_help.h"
#include "game/pk_rules.h"
#include "game/pk_scenes.h"
#include "game/pk_world.h"
#include "game/ui_dialog.h"
#include "gfx/pk_font.h"
#include "input/pk_haptics.h"
#include "input/pk_input.h"
#include "net/pk_online.h"
#include <SDL3/SDL.h>

#define ENTRIES 10

typedef struct
{
    Uint32 id, left, price, mine;
    int status;
    Uint8 item[7];
} entry;

static struct
{
    int menu, cursor, top;
    int wait; /* expected answer type, 0 none */
    Uint8 ans[PK_ONLINE_MSG_MAX];
    int ans_len;
    entry e[ENTRIES];
    int n;
    int sel_idx; /* exhibit: owned item / equipment index */
    int sel_kind;
    Uint32 price;
    int collect_step;
    int booked_loot, booked_items, refunds;
    Uint64 t0;
} A;

static Uint32 be32(const Uint8 *p) { return ((Uint32)p[0] << 24) | ((Uint32)p[1] << 16) | ((Uint32)p[2] << 8) | p[3]; }
static void put32(Uint8 *p, Uint32 v)
{
    p[0] = (Uint8)(v >> 24);
    p[1] = (Uint8)(v >> 16);
    p[2] = (Uint8)(v >> 8);
    p[3] = (Uint8)v;
}

void pk_auction_message(const Uint8 *m, int n)
{
    if (A.wait && m[0] == A.wait && n <= (int)sizeof(A.ans)) {
        SDL_memcpy(A.ans, m, (size_t)n);
        A.ans_len = n;
        A.wait = 0;
    }
}

static void send_req(Uint8 *m, int len, int answer)
{
    A.wait = answer;
    A.ans_len = 0;
    A.t0 = SDL_GetTicks();
    pk_online_send(m, len);
}

static void parse_list(const Uint8 *m, int n, int off)
{
    int i;
    A.n = 0;
    for (i = 0; i < ENTRIES && off + (i + 1) * 24 <= n; i++) {
        const Uint8 *e = m + off + i * 24;
        if (e[4] == 0) {
            break;
        }
        A.e[A.n].id = be32(e);
        A.e[A.n].status = e[4];
        SDL_memcpy(A.e[A.n].item, e + 5, 7);
        A.e[A.n].left = be32(e + 12);
        A.e[A.n].price = be32(e + 16);
        A.e[A.n].mine = be32(e + 20);
        A.n++;
    }
}

/* owned things that can be exhibited: items (kinds 0..4) and unused equipment */

/* FUN_10063a54: the mold (row of the recipe table 0x100abee8, 11 x 7
 * blocks) whose recipes make the weapon; -1 when none does */
static int weapon_mold(int kind, int id)
{
    size_t len = 0;
    Uint8 *d = (Uint8 *)pk_fs_load_localized("itemdata.dat", &len);
    int r, c, i, found = -1;
    for (r = 0; d && r < 11 && found < 0; r++) {
        for (c = 0; c < 7 && found < 0; c++) {
            Uint32 off = pk_app_i32(0x100abee8u + (Uint32)(r * 7 + c) * 4u);
            int n = off < len ? d[off] : 0;
            for (i = 0; i < n && off + 32 + (Uint32)i * 16 <= len; i++) {
                const Uint8 *e = d + off + 16 + i * 16;
                if (e[4] == kind && e[5] == id) {
                    found = r;
                    break;
                }
            }
        }
    }
    SDL_free(d);
    return found;
}

/* the item bytes 20..26 of an exhibit (FUN_10067250): kind, the weapon's
 * mold (kind 5, else 0xff), id, then items: word 3, 0, word 2, rank;
 * equipment: words 3, 4, 2 (the properties) and 5 */
static void owned_bytes(pk_game *g, int code, Uint8 *it)
{
    SDL_memset(it, 0, 7);
    if (code & 0x1000) {
        int *e = g->world.equip[code & 0xfff], m = e[0] == 5 ? weapon_mold(5, e[1]) : -1;
        it[0] = (Uint8)e[0];
        it[1] = (Uint8)(m < 0 ? 0xff : m);
        it[2] = (Uint8)e[1];
        it[3] = (Uint8)e[3];
        it[4] = (Uint8)e[4];
        it[5] = (Uint8)e[2];
        it[6] = (Uint8)e[5];
    } else {
        int *t = g->world.items[code];
        it[0] = (Uint8)t[0];
        it[1] = 0xff;
        it[2] = (Uint8)t[1];
        it[3] = (Uint8)t[3];
        it[4] = 0;
        it[5] = (Uint8)t[2];
        it[6] = (Uint8)t[6];
    }
}

/* an item that comes (back) from the auction keeps its properties
 * (FUN_10029e64) */
static int item_back(pk_game *g, const Uint8 *it)
{
    int i = pk_item_add(g, it[0], it[2]);
    if (i < 0) {
        return i;
    }
    if (it[0] < PK_KIND_EQUIP) {
        g->world.items[i][2] = it[5];
        g->world.items[i][3] = it[3];
    } else {
        g->world.equip[i][2] = it[5];
        g->world.equip[i][3] = it[3];
        g->world.equip[i][4] = it[4];
    }
    return i;
}

/* a frame of an interface image as a queued sprite */
static void auc_spr(pk_game *g, int image, Uint32 table, int frame, int x, int y)
{
    int slot = pk_game_load_image(g, image, 1);
    const pk_image *img = pk_game_slot_image(g, slot);
    pk_frame f;
    pk_rect r;
    if (!img || !pk_app_frame(table, frame, &f)) {
        return;
    }
    r.x = f.sx;
    r.y = f.sy;
    r.w = f.w;
    r.h = f.h;
    pk_drawq_sprite(&g->drawq, img, &r, x + f.ox, y + f.oy, 1, 0);
}

/* the width of a number in the digit sprites (FUN_10065990) */
static int auc_digits_w(const char *s)
{
    int w = 0;
    pk_frame f;
    for (; *s; s++) {
        int fr = *s == '0' ? 0x87 : *s >= '1' && *s <= '9' ? 0x7d + (*s - '0')
                                                           : 0;
        if (fr && pk_app_frame(0x100ad638u, fr, &f)) {
            w += f.w + 1;
        }
    }
    return w;
}

static void auc_digits(pk_game *g, const char *s, int x, int y)
{
    Uint16 t[16];
    int i;
    for (i = 0; s[i] && i < 15; i++) {
        t[i] = (Uint16)s[i];
    }
    t[i] = 0;
    pk_drawq_text(&g->drawq, t, x, y, 0, PK_ALIGN_LEFT, 0x21, 0);
}

static void auc_mes(pk_game *g, int id, int x, int y, pk_color c, pk_align a)
{
    pk_game_mestext(g, id, x + 1, y + 1, 0, a, 1, PK_FONT_LEGEND);
    pk_game_mestext(g, id, x, y, c, a, 1, PK_FONT_LEGEND);
}

static void auc_text16(pk_game *g, const Uint16 *t, int x, int y, pk_align a)
{
    pk_drawq_text(&g->drawq, t, x + 1, y + 1, 0, a, 1, PK_FONT_LEGEND);
    pk_drawq_text(&g->drawq, t, x, y, 0xFFFFFFu, a, 1, PK_FONT_LEGEND);
}

/* FUN_10065670: up to three mestext lines centred in the panel around y */
static void auc_panel3(pk_game *g, pk_color c, int a, int b, int d, int y)
{
    if (b == 0x11c) {
        auc_mes(g, a, 0x58, y, c, PK_ALIGN_CENTER);
        return;
    }
    if (d == 0x11c) {
        auc_mes(g, a, 0x58, y - 7, c, PK_ALIGN_CENTER);
        auc_mes(g, b, 0x58, y + 7, c, PK_ALIGN_CENTER);
        return;
    }
    auc_mes(g, a, 0x58, y - 0xe, c, PK_ALIGN_CENTER);
    auc_mes(g, b, 0x58, y, c, PK_ALIGN_CENTER);
    auc_mes(g, d, 0x58, y + 0xe, c, PK_ALIGN_CENTER);
}

/* the status of an entry as the original's index (icons 0x1008d340,
 * panel texts 0x1008d3a8): bids 1 outbid 3, 2 high 4, 3 won 7, 4 lost 8;
 * exhibits 1 no bids 9, 2 bids 4, 3 sold 11, 4 no sale 12; browsing: our
 * high bid 4 */
static int auc_status(int mode, int st)
{
    static const int k_bid[5] = { 0, 3, 4, 7, 8 }, k_ex[5] = { 0, 9, 4, 11, 12 };
    if (st < 0 || st > 4) {
        return 0;
    }
    if (mode == 3 || mode == 1) {
        return k_bid[st];
    }
    if (mode == 2) {
        return k_ex[st];
    }
    return st == 2 ? 4 : 0;
}

/* the list screen (0x1002df64, three rows of plates 0xac / 0xad at
 * 8, 0x1e + 0x21 * row): the kind's mark (0x1008d308[kind]) at +4, +2,
 * the name at +0x17, +0xf, the rarity bar (0xad + rank) at +0x5e, +5, the
 * price in digit sprites at +0x19, +0x12 followed by the loot mark
 * (frame 3), the time left (hours:minutes) right at +0x9e, +0x1c and the
 * status mark at +0x94, +0xa; the arrows 0x31 / 0x32 at 0x58, 0x19 /
 * 0x87; the panel (FUN_100310ac) tells kind, status, rarity and, for
 * equipment (panel frame 0x93), its properties. */
/* Touch (port): a tap on one of the three plates opens its page,
 * scrolling moves the list. */
static void list_touch(pk_game *g)
{
    int i, d;
    for (i = 0; i < 3 && A.top + i < A.n; i++) {
        if (pk_tap(g, 8, 0x1e + 0x21 * i, 0xa0, 0x21)) {
            A.cursor = A.top + i;
            g->key_code = PK_SC_5;
        }
    }
    d = pk_swipe_y(g);
    if (d && A.n > 3) {
        int t = SDL_clamp(A.top + d, 0, A.n - 3);
        if (t != A.top) {
            A.cursor = SDL_clamp(A.cursor, t, t + 2);
            A.top = t;
            pk_audio_play_sfx(2);
            pk_haptics_play(PK_HAPTIC_STEP);
        }
    }
}

static void auc_list_draw(pk_game *g, int mode)
{
    static const int k_icon[12] = { 0, 191, 193, 192, 193, 192, 196, 194, 0, 0, 197, 195 };
    static const int k_stat[12] = { 0xffff, 0x194, 0x193, 0x192, 0x193, 0x192, 0x190, 0x191, 0x18f, 0xffff, 0x190, 0x18e };
    int i;
    char num[24];
    if (A.cursor < A.top) {
        A.top = A.cursor;
    }
    if (A.cursor >= A.top + 3) {
        A.top = A.cursor - 2;
    }
    if (A.n == 0) {
        auc_spr(g, 0x17, 0x1008d3e4u, 0xad, 8, 0x1e);
        pk_game_mestext(g, 0xf, 8 + 0x17, 0x1e + 0xf, 0, PK_ALIGN_LEFT, 1, PK_FONT_DENSE);
        return;
    }
    for (i = 0; i < 3 && A.top + i < A.n; i++) {
        entry *e = &A.e[A.top + i];
        int x = 8, y = 0x1e + 0x21 * i, kind = e->item[0], st = auc_status(mode, e->status);
        const Uint16 *nm;
        auc_spr(g, 0x17, 0x1008d3e4u, A.top + i == A.cursor ? 0xad : 0xac, x, y);
        if (kind < 8) {
            auc_spr(g, 0x17, 0x1008d3e4u, (int)pk_app_i32(0x1008d308u + (Uint32)kind * 4u), x + 4, y + 2);
        }
        pk_item_preview(kind, e->item[2], 9);
        nm = kind >= PK_KIND_EQUIP ? pk_r.equip[9].name : pk_r.item[9].name;
        pk_drawq_text(&g->drawq, nm, x + 0x17, y + 0xf, 0, PK_ALIGN_LEFT, 1, PK_FONT_DENSE);
        if (e->item[6] >= 1 && e->item[6] <= 10) {
            auc_spr(g, 0x17, 0x1008d3e4u, 0xad + e->item[6], x + 0x5e, y + 5);
        }
        SDL_snprintf(num, sizeof(num), "%u", (unsigned)e->price);
        auc_digits(g, num, x + 0x19, y + 0x12);
        auc_spr(g, 0x17, 0x1008d3e4u, 3, x + 0x1b + auc_digits_w(num), y + 0x12);
        if (mode == 0 || e->status < 3) {
            SDL_snprintf(num, sizeof(num), "%02u:%02u", (unsigned)(e->left / 3600), (unsigned)(e->left / 60 % 60));
            pk_drawq_text_utf8(&g->drawq, num, x + 0x9e, y + 0x1c, 0, PK_ALIGN_RIGHT, 1, PK_FONT_DENSE);
        }
        if (st >= 1 && st <= 12 && k_icon[st - 1]) {
            auc_spr(g, 0x17, 0x1008d3e4u, k_icon[st - 1], x + 0x94, y + 10);
        }
    }
    if (A.top > 0) {
        auc_spr(g, 0x17, 0x1008d3e4u, 0x31, 0x58, 0x19);
    }
    if (A.top + 3 < A.n) {
        auc_spr(g, 0x17, 0x1008d3e4u, 0x32, 0x58, 0x87);
    }
    { /* the panel */
        entry *e = &A.e[A.cursor];
        int kind = e->item[0], st = auc_status(mode, e->status), y, step;
        static const int k_kind[8] = { 127, 128, 125, 126, 44, 122, 123, 124 };
        Uint16 t[25];
        if (kind >= PK_KIND_EQUIP) {
            auc_spr(g, 0x17, 0x1008d3e4u, 0x93, 0, 0x9a);
            y = 0xa6;
            step = 0xd;
        } else {
            y = 0xab;
            step = 0x12;
        }
        auc_mes(g, 0x17, 0x33, y, 0xFFFFFFu, PK_ALIGN_RIGHT);
        if (kind < 8) {
            auc_mes(g, k_kind[kind], 0x37, y, 0xFFFFFFu, PK_ALIGN_LEFT);
        }
        y += step;
        if (st >= 1 && st <= 12 && k_stat[st - 1] != 0xffff) {
            auc_mes(g, k_stat[st - 1], 0xb0, y, 0xFFDC00u, PK_ALIGN_RIGHT);
        }
        auc_mes(g, 0x11, 0x33, y, 0xFFFFFFu, PK_ALIGN_RIGHT);
        if (e->item[6] >= 1 && e->item[6] <= 10) {
            auc_spr(g, 0x17, 0x1008d3e4u, 0xad + e->item[6], 0x34, kind >= PK_KIND_EQUIP ? 0xaa : 0xb4);
        }
        if (kind >= PK_KIND_EQUIP) {
            y += step;
            auc_mes(g, 0x15, 0x33, y, 0xFFFFFFu, PK_ALIGN_RIGHT);
            pk_item_attr_name(1, e->item[3], t, 25);
            auc_text16(g, t, 0x37, y, PK_ALIGN_LEFT);
            y += step;
            if (kind == 7) { /* a second property instead of the mode-0 one */
                pk_item_attr_name(1, e->item[4], t, 25);
                auc_text16(g, t, 0x37, y, PK_ALIGN_LEFT);
            } else {
                auc_mes(g, 0x16, 0x42, y, 0xFFFFFFu, PK_ALIGN_RIGHT);
                pk_item_attr_name(0, e->item[5], t, 25);
                auc_text16(g, t, 0x46, y, PK_ALIGN_LEFT);
            }
        }
    }
}

/* ---- the item page (0x1002fba8) -------------------------------------
 * window rows 0x40..0x42 (frames 0x1008d3e4) at y 0xe..0x82, the kind's
 * mark at 0xa, 0x13, the name at 0x1e, 0x23, rank (mestext 0x11 and the
 * bar 0xad + rank at 0x48, 0x28), the kind (items) or the values and
 * properties (equipment, FUN_10031428), the time left at 0xac, 0x23, the
 * price (mestext 0x15c, digit sprites and the loot mark at 0x95, 0x78),
 * one button (0xb / 0xc at 0x32, 0x8b) or two (0xa and 0x5e, the second
 * mestext 8) with the label at +0x26, 0x98 and two panel lines at 0xbd */
static struct
{
    Uint8 item[7];
    int attr[3];
    int secs, two, label, l1, l2, focus, ret_ok, ret_cancel;
    Sint64 price;
} D;

static void det_start(pk_object *o, const Uint8 *item, Sint64 price, int secs, int two, int label, int l1, int l2,
                      int ok, int cancel)
{
    SDL_memcpy(D.item, item, 7);
    D.attr[0] = item[0] >= PK_KIND_EQUIP ? item[5] : 0xff;
    D.attr[1] = item[0] >= PK_KIND_EQUIP ? item[3] : 0xff;
    D.attr[2] = item[0] >= PK_KIND_EQUIP ? item[4] : 0xff;
    D.price = price;
    D.secs = secs;
    D.two = two;
    D.label = label;
    D.l1 = l1;
    D.l2 = l2;
    D.focus = 0;
    D.ret_ok = ok;
    D.ret_cancel = cancel;
    o->counter = 0;
    o->state = 50;
}

static void det_draw(pk_game *g)
{
    static const int k_kind[8] = { 127, 128, 125, 126, 44, 122, 123, 124 };
    int i, kind = D.item[0], x = 0xa, y = 0x13;
    char num[24];
    Uint16 t[25];
    const Uint16 *nm;
    for (i = 0; i <= 0x1d; i++) {
        auc_spr(g, 0x17, 0x1008d3e4u, 0x40 + (i == 0 ? 0 : i == 0x1d ? 2
                                                                     : 1),
                0, 0xe + 4 * i);
    }
    if (kind < 8) {
        auc_spr(g, 0x17, 0x1008d3e4u, (int)pk_app_i32(0x1008d308u + (Uint32)kind * 4u), x, y);
    }
    pk_item_preview(kind, D.item[2], 9);
    nm = kind >= PK_KIND_EQUIP ? pk_r.equip[9].name : pk_r.item[9].name;
    pk_drawq_text(&g->drawq, nm, x + 0x14, y + 0x10, 0, PK_ALIGN_LEFT, 1, PK_FONT_DENSE);
    pk_game_mestext(g, 0x11, x + 0x39, y + 0x1f, 0, PK_ALIGN_RIGHT, 1, PK_FONT_DENSE);
    if (D.item[6] >= 1 && D.item[6] <= 10) {
        auc_spr(g, 0x17, 0x1008d3e4u, 0xad + D.item[6], 0x48, 0x28);
    }
    if (kind < PK_KIND_EQUIP) {
        pk_game_mestext(g, 0x17, x + 0x39, y + 0x30, 0, PK_ALIGN_RIGHT, 1, PK_FONT_DENSE);
        if (kind < 8) {
            pk_game_mestext(g, k_kind[kind], x + 0x3e, y + 0x30, 0, PK_ALIGN_LEFT, 1, PK_FONT_DENSE);
        }
    } else {
        pk_game_mestext(g, 0x1a, x + 0x39, y + 0x2c, 0, PK_ALIGN_RIGHT, 1, PK_FONT_DENSE);
        SDL_snprintf(num, sizeof(num), "%d%%", pk_r.equip[9].rec[9]);
        pk_drawq_text_utf8(&g->drawq, num, x + 0x3e, y + 0x2c, 0, PK_ALIGN_LEFT, 1, PK_FONT_DENSE);
        if (kind == 5 || kind == 6) {
            pk_game_mestext(g, kind == 5 ? 0x1b : 0x1c, x + 0x39, y + 0x37, 0, PK_ALIGN_RIGHT, 1, PK_FONT_DENSE);
            SDL_snprintf(num, sizeof(num), "%d", kind == 5 ? pk_r.equip[9].rec[7] : pk_r.equip[9].rec[8]);
            pk_drawq_text_utf8(&g->drawq, num, x + 0x3e, y + 0x37, 0, PK_ALIGN_LEFT, 1, PK_FONT_DENSE);
        }
        pk_game_mestext(g, 0x15, x + 0x39, y + 0x42, 0, PK_ALIGN_RIGHT, 1, PK_FONT_DENSE);
        pk_item_attr_name(1, D.attr[1], t, 25);
        pk_drawq_text(&g->drawq, t, x + 0x3e, y + 0x42, 0, PK_ALIGN_LEFT, 1, PK_FONT_DENSE);
        pk_item_attr_name(1, D.attr[2], t, 25);
        pk_drawq_text(&g->drawq, t, x + 0x3e, y + 0x4d, 0, PK_ALIGN_LEFT, 1, PK_FONT_DENSE);
        pk_game_mestext(g, 0x16, x + 0x39, y + 0x58, 0, PK_ALIGN_RIGHT, 1, PK_FONT_DENSE);
        pk_item_attr_name(0, D.attr[0], t, 25);
        pk_drawq_text(&g->drawq, t, x + 0x3e, y + 0x58, 0, PK_ALIGN_LEFT, 1, PK_FONT_DENSE);
    }
    if (D.secs >= 0) {
        SDL_snprintf(num, sizeof(num), "%02d:%02d", D.secs / 3600, D.secs / 60 % 60);
        pk_drawq_text_utf8(&g->drawq, num, x + 0xa2, 0x23, 0, PK_ALIGN_RIGHT, 1, PK_FONT_DENSE);
    }
    if (D.price >= 0) {
        int w;
        SDL_snprintf(num, sizeof(num), "%d", (int)D.price);
        w = auc_digits_w(num);
        pk_game_mestext(g, 0x15c, x + 0x85 - w, 0x81, 0, PK_ALIGN_RIGHT, 1, PK_FONT_DENSE);
        auc_digits(g, num, x + 0x89 - w, 0x78);
        auc_spr(g, 0x17, 0x1008d3e4u, 3, 0x95, 0x78);
    }
    if (D.two) {
        auc_spr(g, 0x17, 0x1008d3e4u, D.focus == 0 ? 0xc : 0xb, 0xa, 0x8b);
        auc_spr(g, 0x17, 0x1008d3e4u, D.focus == 1 ? 0xc : 0xb, 0x5e, 0x8b);
        pk_game_mestext(g, D.label, 0xa + 0x26, 0x98, 0, PK_ALIGN_CENTER, 1, PK_FONT_DENSE);
        pk_game_mestext(g, 8, 0x84, 0x98, 0, PK_ALIGN_CENTER, 1, PK_FONT_DENSE);
        auc_panel3(g, 0xFFFFFFu, D.l1, D.l2, 0x11c, 0xbd);
    } else {
        auc_spr(g, 0x17, 0x1008d3e4u, 0xc, 0x32, 0x8b);
        pk_game_mestext(g, D.label, 0x32 + 0x26, 0x98, 0, PK_ALIGN_CENTER, 1, PK_FONT_DENSE);
        if (D.l1 != 0x11c) {
            auc_panel3(g, 0xFFFFFFu, D.l1, D.l2, 0x11c, 0xbd);
        }
    }
}

/* ---- the number entry (0x1002a544 states 0xd..0x14) ----------------
 * the field (frame 0xab focused / 0xaa) at 0x25, 0x32 with the number,
 * two plates (0x3c / selected 0x3d) at 0x18, 0x50 (label) and 0x18, 0x60
 * (mestext 8), the cursor at the field or a plate and the panel's lines
 * in gold; a number under the minimum or over 9999999 shows help 0x35 */
static struct
{
    int focus, label, l1, l2, l3, ret_ok, ret_cancel;
    Uint32 val, min;
} N;

static void num_start(pk_object *o, int label, int l1, int l2, int l3, Uint32 price, Uint32 min, int ok, int cancel)
{
    /* FUN_1002a4a0(price, 25): a quarter more, rounded up to two figures */
    Uint32 v = price + price * 25 / 100, m = 1, step;
    while (m <= v && m <= 9999999u) {
        m *= 10;
    }
    step = m / 100 ? m / 100 : 1;
    v = v - v % step + step;
    if (v > 9999999u) {
        v = 9999999u;
    }
    N.focus = 0;
    N.label = label;
    N.l1 = l1;
    N.l2 = l2;
    N.l3 = l3;
    N.val = v;
    N.min = min;
    N.ret_ok = ok;
    N.ret_cancel = cancel;
    o->counter = 0;
    o->state = 40;
}

/* Touch (port): a tap on the number opens a numeric keyboard, scrolling
 * there changes the number by 100s; the two buttons below. */
static void num_touch(pk_game *g)
{
    int d;
    if (pk_tap(g, 0x25, 0x32, 0x60, 0x1a)) {
        N.focus = 0;
        if (!pk_input_text_active()) {
            char b[12];
            pk_input_text_begin_number(7);
            SDL_snprintf(b, sizeof(b), "%u", (unsigned)N.val);
            pk_input_text_append(b);
        } else {
            pk_input_text_show();
        }
    }
    if (pk_tap(g, 0x18, 0x50, 0x84, 0x10)) {
        N.focus = 1;
        g->key_code = PK_SC_5;
    }
    if (pk_tap(g, 0x18, 0x60, 0x84, 0x10)) {
        N.focus = 2;
        g->key_code = PK_SC_5;
    }
    if (pk_input_text_active()) {
        N.val = (Uint32)SDL_strtoul(pk_input_text(), NULL, 10);
    }
    if ((d = pk_swipe_y(g)) != 0) {
        Sint64 v = (Sint64)N.val + (Sint64)d * 100;
        if (pk_input_text_active()) {
            pk_input_text_end();
        }
        N.focus = 0;
        N.val = (Uint32)(v < 0 ? 0 : v > 9999999 ? 9999999
                                                 : v);
        pk_audio_play_sfx(2);
        pk_haptics_play(PK_HAPTIC_STEP);
    }
}

static void num_draw(pk_game *g)
{
    char buf[16];
    auc_spr(g, 0x17, 0x1008d3e4u, N.focus == 0 ? 0xab : 0xaa, 0x25, 0x32);
    auc_spr(g, 0x17, 0x1008d3e4u, N.focus == 1 ? 0x3d : 0x3c, 0x18, 0x50);
    auc_spr(g, 0x17, 0x1008d3e4u, N.focus == 2 ? 0x3d : 0x3c, 0x18, 0x60);
    pk_game_mestext(g, N.label, 0x18 + 0x42, 0x50 + 0xc, 0, PK_ALIGN_CENTER, 1, PK_FONT_DENSE);
    pk_game_mestext(g, 8, 0x18 + 0x42, 0x60 + 0xc, 0, PK_ALIGN_CENTER, 1, PK_FONT_DENSE);
    if (N.val) {
        SDL_snprintf(buf, sizeof(buf), "%u", (unsigned)N.val);
    } else {
        buf[0] = '\0';
    }
    pk_drawq_text_utf8(&g->drawq, buf, 0x3c + 0x3c, 0x32 + 0xd, 0, PK_ALIGN_RIGHT, 1, PK_FONT_DENSE);
    if (N.focus == 0 && (SDL_GetTicks() / 400) % 2 == 0) {
        pk_drawq_text_utf8(&g->drawq, "|", 0x3c + 0x3d, 0x32 + 0xd, 0, PK_ALIGN_LEFT, 1, PK_FONT_DENSE);
    }
    auc_panel3(g, 0xFFDC00u, N.l1, N.l2, N.l3, 0xb6);
}

/* the cursor of the number entry and of the lists */
static void auc_cursor_update(pk_object *o, pk_game *g)
{
    pk_object *root = pk_ui_root(g);
    int st = root ? root->state : 0;
    if (!root || root->update != pk_auction_update || st >= 99) {
        pk_object_kill(o);
        return;
    }
    if (!o->work[0]) {
        pk_ui_setup_sprite(g, o, 0x17, 0x1008d3e4u, 0x4c3);
        pk_ui_cursor_anim(g, o);
        o->work[0] = 1;
    }
    o->layer = 1;
    pk_object_animate(o);
    if (st == 50 && !A.wait) {
        o->flags = 0x4c3;
        o->x = !D.two ? 0x32 : D.focus == 0 ? 0xa
                                            : 0x5e;
        o->y = 0x8f;
    } else if (st == 40 && !A.wait) {
        o->flags = 0x4c3;
        o->x = N.focus == 0 ? 0x25 : 0x18;
        o->y = N.focus == 0 ? 0x33 : N.focus == 1 ? 0x51
                                                  : 0x61;
    } else if ((st == 20 || st == 30) && !A.wait && !pk_help_active() && A.n > 0) {
        o->flags = 0x4c3;
        o->x = 8;
        o->y = 0x22 + 0x21 * (A.cursor - A.top);
    } else {
        o->flags = 0x4c2;
    }
}

/* the window rows (shown while a list screen runs) and the request box */
static void auc_obj_update(pk_object *o, pk_game *g)
{
    pk_object *root = pk_ui_root(g);
    int st = root ? root->state : 0;
    bool list = root && root->update == pk_auction_update && st >= 10 && st < 99;
    if (!root || (root->update != pk_auction_update && root->update != pk_dialog_update) ||
        (root->update == pk_auction_update && st >= 99)) {
        pk_object_kill(o);
        return;
    }
    o->layer = 1;
    (void)list;
    if (o->state == 0xb) {
        pk_ui_setup_sprite(g, o, 0x17, 0x1008d3e4u, A.wait ? 0x4c1 : 0x4c0);
        o->frame = 0xa9;
        if (A.wait) {
            pk_game_mestext(g, 354, o->x - 2, o->y - 10, 0, PK_ALIGN_CENTER, 1, PK_FONT_DENSE);
            pk_game_mestext(g, 355, o->x - 2, o->y + 2, 0, PK_ALIGN_CENTER, 1, PK_FONT_DENSE);
        }
    } else if (o->state == 0xc) {
        Uint32 fl = A.wait ? 0x4c1 : 0x4c0;
        if (!o->work[0]) {
            pk_ui_setup_sprite(g, o, 0x17, 0x1008d3e4u, fl);
            pk_ui_cursor_anim(g, o);
            o->work[0] = 1;
        }
        o->flags = fl | PK_OBJ_ANIMATING;
        o->layer = 1;
        pk_object_animate(o);
    }
}

static void auc_spawn(pk_game *g)
{
    int k;
    (void)k;
    pk_object_spawn(g, auc_cursor_update, 0, 8, 0, 0, 8, 0x22);
    pk_object_spawn(g, auc_obj_update, 0, 0xb, 0, 0, 0x5a, 0x5d);
    pk_object_spawn(g, auc_obj_update, 0, 0xc, 0, 0, 0x58, 0x6c);
}

static bool waiting(pk_game *g, pk_object *o)
{
    if (!A.wait) {
        return false;
    }
    if (SDL_GetTicks() - A.t0 > 15000 || !g->arena_online) {
        A.wait = 0;
        pk_help(g, 1, 0x27); /* state 10: the server did not answer */
        o->state = 1;
    }
    return true;
}

void pk_auction_update(pk_object *o, pk_game *g)
{
    int i;
    switch (o->state) {
    case 0:
        if (g->curtain == PK_CURTAIN_NONE) {
            pk_game_set_curtain(g, PK_CURTAIN_CLOSE);
        }
        if (g->curtain != PK_CURTAIN_HELD) {
            break;
        }
        pk_object_kill_others(g, o);
        g->bg_visible = 1;                                                     /* the shop stays behind */
        pk_object_spawn(g, pk_kingdom_ui_update, o->dir, 0x80, 1, 2, 0x10, 3); /* FUN_10024604 */
        pk_object_spawn(g, pk_kingdom_ui_update, o->dir, 0x80, 1, 3, 0x61, 3);
        pk_object_spawn(g, pk_kingdom_ui_update, o->dir, 0x80, 1, 4, 0xa1, 3);
        pk_object_spawn(g, pk_kingdom_ui_update, o->dir, 0x80, 0, 0x7a, 0, 0x92);
        pk_game_set_curtain(g, PK_CURTAIN_OPEN);
        SDL_zero(A);
        o->counter = 0;
        o->work[6] = 0;
        o->state = 1;
        break;
    case 1: /* state 6: the dialog 0x10 (341..344) */
        if (!o->work[6]) {
            auc_spawn(g);
            o->work[6] = 1;
        }
        if (g->curtain != PK_CURTAIN_NONE || pk_help_active()) {
            break;
        }
        g->dlg_hint = NULL;
        g->slide = 0;
        g->dlg_type = 0x10;
        g->list_top = 0;
        g->cursor = A.menu;
        pk_object_call(g, o, pk_dialog_update, 0, pk_auction_update, 2);
        break;
    case 2: /* state 7 */
        if (g->dlg_result != 0) {
            pk_audio_play_sfx(1);
            pk_game_set_curtain(g, PK_CURTAIN_CLOSE);
            o->state = 99;
            break;
        }
        A.menu = g->list_top + g->cursor;
        {
            Uint8 m[32];
            A.cursor = A.top = 0;
            SDL_zeroa(m);
            if (A.menu == 1) { /* Exhibit Item (0x1002cd38): the kind (dialog 0xb), then the item list */
                g->dlg_type = 0xb;
                g->list_top = g->cursor = 0;
                g->slide = 0;
                pk_object_call(g, o, pk_dialog_update, 0, pk_auction_update, 10);
            } else if (A.menu == 0) { /* Bid Item (0x1002bd20) */
                m[0] = 14;
                m[1] = 1;
                m[2] = m[3] = m[4] = m[8] = 0xff;
                put32(m + 12, 0);
                SDL_strlcpy((char *)m + 16, pk_online_user(), 16);
                send_req(m, 32, 142);
                o->state = 20;
            } else {
                /* Check Exhibits (0x1002dba8, mode 2) / Auction Bids (0x1002d574,
                 * mode 3, or mode 1 without room for the items: trigger054) */
                bool room = g->world.item_count <= 0xfa - ENTRIES && g->world.equip_count <= 100 - ENTRIES;
                int mode = A.menu == 2 ? 2 : (room ? 3 : 1);
                if (A.menu == 2 && !room) {
                    pk_help(g, 1, 0x36);
                    o->state = 1;
                    break;
                }
                if (A.menu == 3 && !room) {
                    pk_help(g, 1, 0x36);
                }
                A.collect_step = mode;
                A.booked_loot = A.booked_items = A.refunds = 0;
                m[0] = 16;
                m[1] = 2;
                m[3] = (Uint8)mode;
                SDL_strlcpy((char *)m + 4, pk_online_user(), 16);
                send_req(m, 20, 144);
                o->state = 30;
            }
        }
        break;

    case 10: /* exhibit: the kind chosen */
        if (g->dlg_result != 0) {
            o->state = 1;
            break;
        }
        A.sel_kind = g->list_top + g->cursor;
        g->dlg_type = A.sel_kind;
        g->list_top = g->cursor = 0;
        g->slide = 0;
        pk_object_call(g, o, pk_item_list_update, 0, pk_auction_update, 11); /* 0x10039b34 */
        break;
    case 11:
    { /* exhibit: the item chosen, the starting price (0x1002cd38 state 6) */
        int idx;
        Uint8 it[7];
        if (g->dlg_result != 0) {
            o->state = 1;
            break;
        }
        idx = pk_item_find(g, A.sel_kind, g->list_top + g->cursor);
        if (idx < 0 || idx == 0xff) {
            o->state = 1;
            break;
        }
        A.sel_idx = A.sel_kind >= PK_KIND_EQUIP ? (0x1000 | idx) : idx;
        owned_bytes(g, A.sel_idx, it);
        pk_item_preview(it[0], it[2], 10);
        A.price = (Uint32)(A.sel_kind >= PK_KIND_EQUIP ? pk_r.equip[10].price : pk_r.item[10].price);
        det_start(o, it, -1, -1, 1, 0x15b, 0x181, 0x182, 14, 1); /* 0x1002cd38 state 5 */
        break;
    }
    case 14: /* the starting price (state 6) */
        num_start(o, 0x156, 0x183, 0x184, 0x185, A.price, A.price, 12, 1);
        break;
    case 12:
    { /* exhibit: send (state 7) */
        Uint8 m[32], it[7];
        owned_bytes(g, A.sel_idx, it);
        SDL_zeroa(m);
        m[0] = 13;
        m[1] = 4;
        SDL_strlcpy((char *)m + 4, pk_online_user(), 16);
        SDL_memcpy(m + 20, it, 7);
        put32(m + 28, N.val);
        /* the exhibit leaves the storage when it is sent */
        if (A.sel_idx & 0x1000) {
            pk_equip_remove(g, A.sel_idx & 0xfff);
        } else {
            pk_item_remove(g, A.sel_idx);
        }
        pk_items_sort(g);
        pk_equip_sort(g, false);
        SDL_memcpy(A.e[0].item, it, 7);
        send_req(m, 32, 141);
        o->state = 13;
        break;
    }
    case 13: /* state 8 */
        if (waiting(g, o)) {
            break;
        }
        if (A.ans_len >= 4 && A.ans[3] == 0) {
            pk_help(g, 1, 0x29);
        } else { /* refused: the item comes back */
            item_back(g, A.e[0].item);
            pk_items_sort(g);
            pk_help(g, 1, A.ans_len >= 4 && A.ans[3] == 1 ? 0x28 : 0x2b);
        }
        pk_world_save(g);
        o->state = 1;
        break;

    case 20: /* browse (0x1002df64) */
        if (waiting(g, o)) {
            break;
        }
        if (A.ans_len) {
            parse_list(A.ans, A.ans_len, 8);
            A.ans_len = 0;
            if (A.n == 0) {
                pk_help(g, 1, 0x34);
            }
        }
        auc_list_draw(g, 0);
        if (pk_help_active()) {
            break;
        }
        list_touch(g);
        if (A.n > 0 && (g->key_code == PK_SC_UP || g->key_code == PK_SC_DOWN)) {
            pk_audio_play_sfx(2);
            A.cursor = (A.cursor + (g->key_code == PK_SC_UP ? A.n - 1 : 1)) % A.n;
        } else if (g->key_code == PK_SC_5 && A.n > 0) { /* the page (0x1002bd20 state 0xd) */
            entry *e = &A.e[A.cursor];
            pk_audio_play_sfx(0);
            if (e->status == 2) {
                det_start(o, e->item, e->price, (int)e->left, 0, 0x10, 0x186, 0x187, 20, 20);
            } else {
                det_start(o, e->item, e->price, (int)e->left, 1, 0x15a, 0x17a, 0x11c, 23, 20);
            }
        } else if (g->key_code == PK_SC_BACK || g->key_code == PK_SC_5) {
            pk_audio_play_sfx(1);
            o->state = 1;
        }
        break;
    case 23: /* bid (0x1002b960) */
        num_start(o, 0x159, 0x17b, 0x11c, 0x11c, A.e[A.cursor].price, A.e[A.cursor].price + 1, 21, 20);
        break;
    case 21:
    { /* bid: the loot must stay above 4999 (else help 0x2f) */
        entry *e = &A.e[A.cursor];
        if ((int)(g->loot + (int)e->mine) - (int)N.val <= 0x1387) {
            pk_help(g, 1, 0x2f);
            o->state = 20;
            break;
        }
        {
            Uint8 m[28];
            SDL_zeroa(m);
            m[0] = 15;
            m[1] = 5;
            put32(m + 4, e->id);
            put32(m + 8, N.val);
            SDL_strlcpy((char *)m + 12, pk_online_user(), 16);
            A.price = N.val;
            send_req(m, 28, 143);
            o->state = 22;
        }
        break;
    }
    case 22:
        if (waiting(g, o)) {
            break;
        }
        if (A.ans_len >= 8) {
            static const int k_trig[6] = { 0x2c, 0x2d, 0x2e, 0x2a, 0x32, 0x33 };
            int r = A.ans[2];
            if (r == 0) {
                Uint32 prev = be32(A.ans + 4);
                pk_loot_sub(g, (int)(A.price - prev)); /* the game pays amount minus the earlier bid */
                pk_world_save(g);
            }
            pk_help(g, 1, r < 6 ? k_trig[r] : 0x33);
        }
        o->state = 1;
        break;

    case 30: /* own exhibits / bids; statuses 3 and 4 are booked in modes 2 and 3 */
        if (waiting(g, o)) {
            break;
        }
        if (A.ans_len) {
            parse_list(A.ans, A.ans_len, 4);
            A.ans_len = 0;
            for (i = 0; i < A.n; i++) {
                entry *e = &A.e[i];
                if (A.collect_step == 2) {
                    if (e->status == 3) {
                        A.booked_loot += (int)e->price;                        /* sold */
                    } else if (e->status == 4 && item_back(g, e->item) >= 0) { /* no sale */
                        A.booked_items++;
                    }
                } else if (A.collect_step == 3) {
                    if (e->status == 3 && item_back(g, e->item) >= 0) {
                        A.booked_items++; /* won */
                    } else if (e->status == 4) {
                        A.refunds += (int)e->mine; /* lost: the bid comes back */
                    }
                }
            }
            if (A.booked_loot || A.booked_items || A.refunds) {
                pk_loot_add(g, A.booked_loot + A.refunds);
                pk_items_sort(g);
                pk_equip_sort(g, false);
                pk_world_save(g);
            }
        }
        auc_list_draw(g, A.collect_step);
        if (pk_help_active()) {
            break;
        }
        list_touch(g);
        if (A.n > 0 && (g->key_code == PK_SC_UP || g->key_code == PK_SC_DOWN)) {
            pk_audio_play_sfx(2);
            A.cursor = (A.cursor + (g->key_code == PK_SC_UP ? A.n - 1 : 1)) % A.n;
        } else if (g->key_code == PK_SC_5 && A.n > 0) { /* the page (0x1002dba8 state 4) */
            entry *e = &A.e[A.cursor];
            pk_audio_play_sfx(0);
            det_start(o, e->item, e->price, e->status < 3 ? (int)e->left : -1, 0, 0x10, 0x11c, 0x11c, 31, 31);
        } else if (g->key_code == PK_SC_5 || g->key_code == PK_SC_BACK) {
            pk_audio_play_sfx(1);
            o->state = 1;
        }
        break;
    case 31: /* back to the own list */
        o->state = 30;
        break;

    case 50: /* the item page */
        det_draw(g);
        if (++o->counter < 6 || pk_help_active()) {
            break;
        }
        /* touch: the button(s) */
        if (!D.two) {
            if (pk_tap(g, 0x32, 0x8b, 0x48, 0x1a)) {
                g->key_code = PK_SC_5;
            }
        } else {
            if (pk_tap(g, 0xa, 0x8b, 0x48, 0x1a)) {
                D.focus = 0;
                g->key_code = PK_SC_5;
            }
            if (pk_tap(g, 0x5e, 0x8b, 0x48, 0x1a)) {
                D.focus = 1;
                g->key_code = PK_SC_5;
            }
        }
        if (D.two && (g->key_code == PK_SC_LEFT || g->key_code == PK_SC_RIGHT)) {
            pk_audio_play_sfx(2);
            D.focus ^= 1;
        } else if (g->key_code == PK_SC_BACK || (g->key_code == PK_SC_5 && D.two && D.focus == 1)) {
            pk_audio_play_sfx(1);
            o->state = D.ret_cancel;
        } else if (g->key_code == PK_SC_5) {
            pk_audio_play_sfx(0);
            o->state = D.ret_ok;
        }
        break;

    case 40: /* the number entry */
        num_draw(g);
        if (++o->counter < 6 || pk_help_active()) {
            break;
        }
        num_touch(g);
        if (g->key_code == PK_SC_UP || g->key_code == PK_SC_DOWN) {
            pk_audio_play_sfx(2);
            N.focus = (N.focus + (g->key_code == PK_SC_UP ? 2 : 1)) % 3;
        } else if (N.focus == 0 && g->key_code >= 0x30 && g->key_code <= 0x39) {
            if (N.val < 1000000u) {
                N.val = N.val * 10 + (Uint32)(g->key_code - 0x30);
            }
        } else if (N.focus == 0 && (g->key_code == PK_SC_BACKSPACE || g->key_code == PK_SC_LEFT)) {
            N.val /= 10;
        } else if (N.focus == 0 && g->key_code == PK_SC_RIGHT) {
            N.val = N.val + 100 > 9999999u ? 9999999u : N.val + 100;
        } else if (g->key_code == PK_SC_BACK || (g->key_code == PK_SC_5 && N.focus == 2)) {
            pk_audio_play_sfx(1);
            o->state = N.ret_cancel;
        } else if (g->key_code == PK_SC_5) {
            if (N.focus == 0) {
                N.focus = 1;
                pk_audio_play_sfx(2);
                break;
            }
            if (N.val < N.min || N.val > 9999999u) {
                pk_help(g, 1, 0x35);
                break;
            }
            pk_audio_play_sfx(0);
            o->state = N.ret_ok;
        }
        if ((N.focus != 0 || o->state != 40) && pk_input_text_active()) {
            pk_input_text_end();
        }
        break;

    default: /* back to the shop */
        if (g->curtain != PK_CURTAIN_HELD) {
            break;
        }
        pk_object_kill_others(g, o);
        g->bg_visible = 1;
        g->dlg_result = 1;
        o->counter = 0;
        pk_object_return(g, o);
        break;
    }
}
