/* SPDX-License-Identifier: MIT */
/* Unit tests that need no game data. */
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "core/pk_text.h"
#include "engine/pk_game.h"
#include "game/pk_scenes.h"
#include "game/pk_world.h"
#include "game/ui_dialog.h"
#include "gfx/pk_font.h"
#include "gfx/pk_mbm.h"
#include "input/pk_input.h"
#include "net/pk_arena.h"
#include "net/pk_identity.h"
#include "net/pk_online.h"
#include "net/pk_snap.h"

static int g_fail, g_run;

#define CHECK(c)                                               \
    do {                                                       \
        g_run++;                                               \
        if (!(c)) {                                            \
            g_fail++;                                          \
            SDL_Log("FAIL %s:%d: %s", __FILE__, __LINE__, #c); \
        }                                                      \
    } while (0)

static void test_ranked_server(void)
{
    CHECK(pk_online_is_ranked_server(true, "188.68.43.183", "Ranked"));
    CHECK(pk_online_is_ranked_server(true, "188.68.43.183", "ranked"));
    CHECK(!pk_online_is_ranked_server(false, "188.68.43.183", "Ranked"));
    CHECK(!pk_online_is_ranked_server(true, "188.68.43.183", "Scrimmage"));
    CHECK(!pk_online_is_ranked_server(true, "localhost", "Ranked"));
    CHECK(!pk_online_is_ranked_server(true, NULL, "Ranked"));
    CHECK(!pk_online_is_ranked_server(true, "188.68.43.183", NULL));
}

static void test_luhn(void)
{
    /* 490154203237518 is the textbook IMEI example */
    CHECK(pk_luhn_digit("49015420323751") == '8');
    CHECK(pk_imei_is_valid("490154203237518"));
    CHECK(!pk_imei_is_valid("490154203237519"));
    CHECK(!pk_imei_is_valid("49015420323751"));
    CHECK(!pk_imei_is_valid("49015420323751a"));
}

static void put32(Uint8 *p, Uint32 v)
{
    p[0] = (Uint8)v;
    p[1] = (Uint8)(v >> 8);
    p[2] = (Uint8)(v >> 16);
    p[3] = (Uint8)(v >> 24);
}

static void test_mbm(void)
{
    /* One 3x2 12 bpp bitmap with 12-bit RLE: row stride is 8 bytes
     * (4 pixels incl. padding), so 8 pixels in total:
     * 3 x red (0xF00), 1 pad, 3 x blue (0x00F), 1 pad. */
    Uint8 *f = (Uint8 *)SDL_calloc(1, 256);
    Uint8 *bm = f + 20;
    Uint16 rle[] = { 0x2F00, 0x0000, 0x200F, 0x0000 };
    pk_mbm m;
    int w = 0, h = 0, i;
    Uint32 *px = NULL;
    size_t hdr = 40, data = sizeof(rle), total = 20 + hdr + data;

    put32(f, 0x10000037);
    put32(f + 4, 0x10000042);
    put32(f + 16, (Uint32)total);
    put32(bm, (Uint32)(hdr + data));
    put32(bm + 4, (Uint32)hdr);
    put32(bm + 8, 3);
    put32(bm + 12, 2);
    put32(bm + 24, 12);
    put32(bm + 28, 1);
    put32(bm + 36, 2);
    for (i = 0; i < 4; i++) {
        bm[hdr + 2 * i] = (Uint8)(rle[i] & 0xff);
        bm[hdr + 2 * i + 1] = (Uint8)(rle[i] >> 8);
    }
    put32(f + total, 1);
    put32(f + total + 4, 20);
    CHECK(pk_mbm_open_mem(&m, f, total + 8));
    CHECK(m.count == 1);
    CHECK(pk_mbm_decode(&m, 0, &w, &h, &px));
    CHECK(w == 3 && h == 2);
    if (px) {
        CHECK(px[0] == 0xFFFF0000u && px[2] == 0xFFFF0000u);
        CHECK(px[3] == 0xFF0000FFu && px[5] == 0xFF0000FFu);
        SDL_free(px);
    }
    pk_mbm_close(&m);
}

static void test_text(void)
{
    const Uint8 rec[] = { 'H', 0, 'i', 0, '\n', 0, 0xE9, 0, 0x1B, 0, 'X', 0 };
    Uint16 out[16];
    char u8[32];
    CHECK(pk_text_decode(rec, 6, out, 16) == 4);
    CHECK(out[0] == 'H' && out[2] == '\n' && out[3] == 0xE9);
    pk_text_to_utf8(out, u8, sizeof(u8));
    CHECK(SDL_strcmp(u8, "Hi\n\xC3\xA9") == 0);
}

static void test_snap(void)
{
    Uint8 buf[64], dg[68];
    const Uint8 payload[3] = { 1, 2, 3 };
    pk_snap_msg m, out[4];
    int n, k;
    SDL_zero(m);
    m.flags = PK_SNAP_F_RELIABLE | PK_SNAP_F_SINGLE;
    m.cmd_seq = 7;
    m.op = PK_SNAP_OP_DATA_SERVER;
    m.user_id = 0x01020304;
    m.seq = 5;
    m.ack = 9;
    m.payload = payload;
    m.len = 3;
    n = pk_snap_encode(buf, sizeof(buf), &m);
    CHECK(n == 19);
    CHECK(buf[0] == 0xA0 && buf[1] == 19 && buf[3] == 17 && buf[4] == 1 && buf[7] == 4);
    SDL_memcpy(dg, buf, (size_t)n);
    dg[n] = 0xBA;
    dg[n + 1] = 0x47;
    dg[n + 2] = 0x66;
    dg[n + 3] = 0x11;
    k = pk_snap_decode(dg, n + 4, out, 4);
    CHECK(k == 1);
    CHECK(out[0].op == 17 && out[0].seq == 5 && out[0].ack == 9 && out[0].len == 3 && out[0].payload[2] == 3);
    CHECK(out[0].flags == (PK_SNAP_F_RELIABLE | PK_SNAP_F_SINGLE));
    dg[n] = 0; /* bad trailer */
    CHECK(pk_snap_decode(dg, n + 4, out, 4) < 0);
}

static void test_arena(void)
{
    char req[2048];
    int n = pk_arena_build_request(req, sizeof(req), "arena.n-gage.com", "a<b", "pw", "001234567890128");
    CHECK(n > 0);
    CHECK(SDL_strstr(req, "POST /services/Community HTTP/1.1") == req);
    CHECK(SDL_strstr(req, "<username xsi:type=\"xsd:string\">a&lt;b</username>") != NULL);
    CHECK(SDL_strstr(req, "<imei xsi:type=\"xsd:string\">001234567890128</imei>") != NULL);
    CHECK(SDL_strstr(req, "SOAPAction: \"authenticateUser2\"") != NULL);
}

static void test_font(void)
{
    const Uint16 quit[] = { 'Q', 'u', 'i', 't', 0 };
    const pk_glyph *g;
    /* no data folder: the shipped font files are used */
    CHECK(pk_font_init());
    CHECK(pk_font_height(PK_FONT_LEGEND) == 12 && pk_font_ascent(PK_FONT_LEGEND) == 10);
    CHECK(pk_font_height(PK_FONT_DENSE) == 12 && pk_font_ascent(PK_FONT_DENSE) == 10);
    g = pk_font_glyph(PK_FONT_LEGEND, 'A');
    CHECK(g->cp == 'A' && g->width > 0 && g->height > 0 && g->height <= 12);
    CHECK(pk_font_glyph(PK_FONT_LEGEND, 0x2122)->cp == 0x2122); /* trade mark sign */
    CHECK(pk_font_glyph(PK_FONT_DENSE, 0xE9)->cp == 0xE9);      /* e acute */
    CHECK(pk_font_glyph(PK_FONT_DENSE, 0x4E00)->cp == '?');     /* fallback */
    CHECK(pk_font_text_width(PK_FONT_LEGEND, quit, -1) > pk_font_text_width(PK_FONT_DENSE, quit, -1) - 4);
    CHECK(!pk_font_load_gdr_mem((const Uint8 *)"not a font store", 16));
    pk_font_quit();
}

static void test_world(void)
{
    static pk_game a, b;
    static Uint8 buf[PK_SAVE_SIZE];
    CHECK(pk_crc32((const Uint8 *)"123456789", 9) == 0xCBF43926u);
    pk_game_init(&a);
    a.fame = 0x012345;
    a.loot = 9876;
    a.progress = 3;
    a.name_index = 0x123;
    a.field_bg = 9;
    a.world.units[7][19] = 0x1a2;
    a.world.areas[5][4] = 0x2b3c;
    a.world.areas[5][33] = 17;
    a.world.formations[2][43] = 0x4d5e;
    a.world.formations[2][39 + 3] = 77;
    a.world.equip[99][12] = 4;
    a.world.help_seen[57] = 1;
    pk_world_serialize(&a, buf);
    CHECK(buf[9] == 0x01 && buf[10] == 0x23 && buf[11] == 0x45);
    CHECK(buf[0] == 0 && buf[52 + 32 * 7 + 19] == 0xa2 && buf[52 + 32 * 7 + 23] == 0x01);
    SDL_memset(&b, 0, sizeof(b));
    CHECK(pk_world_deserialize(&b, buf, sizeof(buf)));
    CHECK(b.fame == a.fame && b.loot == a.loot && b.progress == 3 && b.name_index == 0x123 && b.field_bg == 9);
    CHECK(b.world.units[7][19] == 0x1a2 && b.world.areas[5][4] == 0x2b3c && b.world.areas[5][33] == 17);
    CHECK(b.world.formations[2][43] == 0x4d5e && b.world.formations[2][42] == 77);
    CHECK(b.world.equip[99][12] == 4 && b.world.help_seen[57] == 1);
    CHECK(b.world.player_name[0] == 'd' && b.world.player_name[8] == 'y' && b.world.player_name[9] == 0);
    buf[100] ^= 1;
    CHECK(!pk_world_deserialize(&b, buf, sizeof(buf)));
    pk_game_shutdown(&a);
}

static void test_touch(void)
{
    static pk_game g;
    int x = 0, y = 0, cur = 0, top = 0;
    pk_input_init();
    /* a short touch is a tap where it began */
    pk_input_test_touch(40, 50, 41, 51, 50);
    pk_input_frame();
    CHECK(pk_input_tap(&x, &y) && x == 40 && y == 50);
    CHECK(!pk_input_tap(NULL, NULL));
    /* a drag up by 3.5 rows: three steps on, no tap */
    pk_input_test_touch(80, 120, 80, 120 - PK_TOUCH_STEP_Y * 7 / 2, 100);
    pk_input_frame();
    CHECK(!pk_input_tap(NULL, NULL));
    CHECK(pk_input_scroll_y() == 3 && pk_input_scroll_x() == 0);
    /* sideways: one step back */
    pk_input_test_touch(100, 50, 100 + PK_TOUCH_STEP_X + 2, 52, 100);
    pk_input_frame();
    CHECK(pk_input_scroll_x() == -1 && pk_input_scroll_y() == 0);
    /* a long press is the right soft key */
    pk_input_test_touch(60, 60, 60, 60, 1000);
    pk_input_frame();
    CHECK(pk_input_pressed(PK_KEY_RSK) && !pk_input_tap(NULL, NULL));
    pk_input_frame();
    /* game side: targets take the tap, the list scrolls */
    g.tap = 1;
    g.tap_x = 20;
    g.tap_y = 0x30;
    CHECK(pk_tap_rows(&g, 8, 0x1e, 0xa0, 0x14, 5, &cur) && cur == 0 && g.key_code == PK_SC_5);
    g.tap = 1;
    g.tap_x = 20;
    g.tap_y = 0x60;
    CHECK(pk_tap_rows(&g, 8, 0x1e, 0xa0, 0x14, 5, &cur) && cur == 3);
    g.tap = 1;
    g.tap_x = 0xb0;
    g.tap_y = 0x60;
    CHECK(!pk_tap_rows(&g, 8, 0x1e, 0xa0, 0x14, 5, &cur) && g.tap);
    g.swipe_y = 4;
    CHECK(pk_swipe_list(&g, &top, &cur, 12, 5) && top == 4 && cur == 0);
    g.swipe_y = 9;
    CHECK(pk_swipe_list(&g, &top, &cur, 12, 5) && top == 7);
}

static void upgrade_test_tick(pk_game *g)
{
    pk_drawq_reset(&g->drawq);
    pk_objects_update(g);
    pk_objects_reap(g);
    g->key_code = PK_SC_NONE;
}

static int upgrade_test_count(pk_game *g, int state)
{
    int i, n = 0;
    for (i = 0; i < g->objects->count; i++) {
        if (g->objects->items[i]->state == state) {
            n++;
        }
    }
    return n;
}

static void test_upgrade_confirmation(void)
{
    static pk_game g;
    pk_object *root, *row;
    int i, price_found;
    pk_game_init(&g);
    root = pk_object_spawn(&g, pk_upgrade_update, 0xc2, 0xc, 0, 0, 0, 0);
    CHECK(root != NULL);
    if (!root) {
        pk_game_shutdown(&g);
        return;
    }
    g.curtain = PK_CURTAIN_HELD;
    root->counter = 3;
    upgrade_test_tick(&g);
    g.curtain = PK_CURTAIN_NONE;
    g.loot = 10000;
    g.world.h_4e4 = 0;
    upgrade_test_tick(&g);
    g.tap = 1;
    g.tap_x = 0x20;
    g.tap_y = 0x28 + 3 * 0x14;
    upgrade_test_tick(&g);
    upgrade_test_tick(&g);
    CHECK(upgrade_test_count(&g, 0x26) == 5);
    CHECK(upgrade_test_count(&g, 0x2a) == 9);

    /* The bottom of the second displayed row must still select that row. */
    g.tap = 1;
    g.tap_x = 0x20;
    g.tap_y = 0x41 + 0x10 + 0xd;
    upgrade_test_tick(&g);
    CHECK(root->update == pk_dialog_update && g.loot == 10000);
    CHECK(upgrade_test_count(&g, 0x26) == 0 && upgrade_test_count(&g, 0x2a) == 0);
    root->state = 2;
    g.dlg_count = 2;
    upgrade_test_tick(&g);
    price_found = 0;
    for (i = 0; i < g.drawq.text_count; i++) {
        const Uint16 *text = g.drawq.text[i].text;
        if (text[0] == '2' && text[1] == '0' && text[2] == '0' && text[3] == '0' && text[4] == 0) {
            price_found = 1;
        }
    }
    CHECK(price_found);
    g.cursor = 1;
    g.key_code = PK_SC_5;
    upgrade_test_tick(&g);
    upgrade_test_tick(&g);
    upgrade_test_tick(&g);
    upgrade_test_tick(&g);
    row = pk_object_find(&g, 0, 5, 1);
    CHECK(root->update == pk_upgrade_update && g.loot == 10000 && g.world.h_4e4 == 0);
    CHECK(upgrade_test_count(&g, 0x26) == 5 && upgrade_test_count(&g, 0x2a) == 9);
    CHECK(row && row->y == 0x51 && row->frame == 0x3d);
    CHECK(g.dlg_hint == NULL);

    /* Back also restores the menu; confirming buys the selected row once. */
    for (i = 0; i < 2; i++) {
        g.tap = 1;
        g.tap_x = 0x20;
        g.tap_y = 0x5e;
        upgrade_test_tick(&g);
        CHECK(root->update == pk_dialog_update && g.loot == 10000);
        root->state = 2;
        g.dlg_count = 2;
        g.cursor = 0;
        g.key_code = i == 0 ? PK_SC_BACK : PK_SC_5;
        upgrade_test_tick(&g);
        upgrade_test_tick(&g);
        upgrade_test_tick(&g);
        upgrade_test_tick(&g);
        CHECK(upgrade_test_count(&g, 0x26) == 5 && upgrade_test_count(&g, 0x2a) == 9);
        CHECK(g.loot == (i == 0 ? 10000 : 8000));
        CHECK(g.world.h_4e4 == (i == 0 ? 0 : 4));
    }
    g.key_code = PK_SC_DOWN;
    upgrade_test_tick(&g);
    g.key_code = PK_SC_5;
    upgrade_test_tick(&g);
    CHECK(root->update == pk_upgrade_update && g.loot == 6000 && g.world.h_4e4 == 8);
    pk_game_shutdown(&g);
}

static void test_smooth(void)
{
    static pk_game g;
    static Uint32 frame[PK_SCREEN_W * PK_SCREEN_H];
    Uint32 green = 0xFF00FF00u;
    pk_image image = { 1, 1, &green };
    pk_object *o;
    Uint32 *fb = pk_gfx_framebuffer();
    pk_game_init(&g);
    pk_gfx_set_clip(NULL);
    pk_gfx_clear(0xFF000000u);
    g.bg_visible = 1;
    g.bg_id[0] = 0;
    o = pk_object_spawn(&g, NULL, 0, 0, 0, 0, 10, 10);
    CHECK(o != NULL);
    if (!o) {
        pk_game_shutdown(&g);
        return;
    }
    o->image = 0;
    o->flags = PK_OBJ_RECT | PK_OBJ_SCREEN;
    o->w = o->h = 1;
    o->work[0] = 255;
    pk_drawq_sprite(&g.drawq, &image, NULL, 10, 20, 0, 0);
    pk_game_smooth_begin(&g);
    o->x = 22;
    CHECK(pk_game_smooth_capture(&g));
    pk_game_smooth_render(&g, frame, 45000000ull);
    CHECK(frame[10 * PK_SCREEN_W + 16] == 0xFFFF0000u);
    CHECK(o->x == 22 && fb[10 * PK_SCREEN_W + 16] == 0xFF000000u);
    pk_game_smooth_render(&g, frame, 22500000ull);
    CHECK(frame[10 * PK_SCREEN_W + 13] == 0xFFFF0000u);
    pk_game_smooth_render(&g, frame, 90000000ull);
    CHECK(frame[10 * PK_SCREEN_W + 22] == 0xFFFF0000u && o->x == 22);

    pk_drawq_reset(&g.drawq);
    pk_game_smooth_begin(&g);
    o->x = 34;
    pk_drawq_sprite(&g.drawq, &image, NULL, 22, 20, 0, 0);
    g.curtain = PK_CURTAIN_CLOSE;
    g.curtain_w = 32;
    pk_gfx_clear(0xFFFFFFFFu);
    CHECK(pk_game_smooth_capture(&g));
    pk_game_smooth_render(&g, frame, 45000000ull);
    CHECK(frame[10 * PK_SCREEN_W + 28] == 0xFFFF0000u);
    CHECK(frame[20 * PK_SCREEN_W + 16] == 0xFF00FF00u);
    CHECK(frame[0 * PK_SCREEN_W + 15] == 0xFF000000u);
    CHECK(frame[0 * PK_SCREEN_W + 16] == 0xFFFFFFFFu);
    CHECK(o->x == 34 && g.drawq.sprite[0].x == 22 && fb[0] == 0xFFFFFFFFu);
    pk_drawq_reset(&g.drawq);
    pk_game_smooth_render(&g, frame, 45000000ull);
    CHECK(frame[20 * PK_SCREEN_W + 16] == 0xFF00FF00u && g.drawq.sprite_count == 0);

    pk_game_smooth_begin(&g);
    g.phase++;
    CHECK(!pk_game_smooth_capture(&g));
    pk_game_smooth_begin(&g);
    g.bg_visible = 0;
    CHECK(!pk_game_smooth_capture(&g));
    pk_game_shutdown(&g);
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    test_ranked_server();
    test_luhn();
    test_mbm();
    test_text();
    test_snap();
    test_arena();
    test_font();
    test_world();
    test_touch();
    test_upgrade_confirmation();
    test_smooth();
    SDL_Log("%d checks, %d failed", g_run, g_fail);
    return g_fail ? 1 : 0;
}
