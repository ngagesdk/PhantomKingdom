# Data formats of Pocket Kingdom

All multi-byte values are little endian unless stated otherwise.

## Installation folder (`\System\Apps\6RAH\`)

| File | Content |
|---|---|
| `6rah.app` | Symbian E32 executable (uncompressed, code base 0x10000000, no data section). The port reads layout tables from it. |
| `pokekin.mbm` | 488 bitmaps: screens, sprite sheets, masks |
| `pokeport.mbm` | portraits |
| `bgm01..08.pcm` | music, ids 0..7 |
| `sys01..05.pcm`, `atk01..26.pcm`, `grow.pcm`, `whiskaway1.pcm`, `neon_light.pcm`, `whoot.pcm`, `yeeaht.pcm` | sound effects |
| `*.dat` in the root and in `english/ french/ german/ italian/ spanish/` | tables; text tables exist per language |
| `fabbis/<language>/*.txt` | UTF-16 (BOM) help texts: info, ingredients, pop-ups, triggers |
| `save.dat` | save game, see below; `options.dat` (volume, language) is replaced by the port's `options.cfg` |
| `e:\game.id` (card root) | ASCII version string shown under "Now Loading" |

## MBM (Symbian multi-bitmap)

    0x00 u32 uid1 0x10000037
    0x04 u32 uid2 0x10000042
    0x08 u32 uid3, 0x0c u32 checksum
    0x10 u32 offset of the trailer
    trailer: u32 count, count x u32 offset of a bitmap
    bitmap: 40 byte header, then data
      0x00 size of header + data   0x04 header size (40)
      0x08 width                   0x0c height
      0x10/0x14 size in twips      0x18 bits per pixel
      0x1c colour flag             0x20 palette entries
      0x24 compression: 0 none, 1 byte RLE, 2 12-bit RLE, 3 16-bit RLE, 4 24-bit RLE

Scan lines are padded to 32 bits; 12 bpp uses 16 bits a pixel (`0x0RGB`).
Byte RLE: a byte `n < 0x80` repeats the next byte `n+1` times, otherwise
`256-n` literal bytes follow. 12-bit RLE: 16-bit words, the top nibble is
the repeat count minus one, the low 12 bits the colour.

Pocket Kingdom stores colour bitmaps as 12 bpp with 12-bit RLE and their
masks as 1 bpp with byte RLE directly after them (mask id = id + 1). The
original blits with `BitBltMasked(..., invertMask = ETrue)`: a clear mask
bit (black) is opaque.

Unit sprite sheets are bitmaps 0x102 + type * 2 (mask + 1). Four of them
(types 0x11, 0x1e, 0x35, 0x54), the only ones with a size in twips, are
stored mirrored relative to their frame tables; the port flips them
horizontally on load.

Known bitmaps of `pokekin.mbm`: 0 ESRB rating, 1 SEGA logo, 2..6 Nokia
copyright (en, fr, de, it, es), 7/8 title sheet 176x1060 and mask,
0x16 "Game Over", 0x17/0x18 user interface sheet, 0x58 kingdom background.

## PCM

Raw signed 16-bit mono samples, no header. The original plays them through
`CMdaAudioOutputStream`; the rate is not confirmed yet (8000 Hz assumed).

## Text tables

UTF-16LE records of fixed size; a string ends with ESC (0x001B) or at the
end of the record, 0x000A breaks the line.

| File | Record size |
|---|---|
| `mestext.dat`, `opening.dat`, `ending.dat`, `staff.dat` | 64 |
| `area.dat`, `move.dat` | 48 |
| `battlemes.dat` | 128 |
| `pausemess.dat` | 256 |
| `questmes.dat` | 320 |
| `unit.dat`, `itemdata.dat` | name records mixed with binary fields (to be documented) |
| `name.dat` | 24 byte ASCII records (kingdom names, preceded by a header) |

## Tables in `6RAH.APP`

Referenced by original address (see `src/core/pk_app.h`):

- Sprite frame table: records of six s32 `{sx, sy, w, h, ox, oy}`. An
  object draws record `frame` at `(x + ox, y + oy)` minus the camera.
- Animation table: pairs of s32 `{frame, duration}`, ended by 0xffff
  (loop) or 0xfffe (stop). Durations count logic frames (90 ms).

## Symbian font store (`.gdr`)

Used for the Series 60 system fonts (`ceurope.gdr`). Layout, fnttran
version 39, little endian:

    16 byte uid header (0x10000037, 0x10000039, 0, checksum), u32 root offset
    root: u32 version (39), u32 collection uid, u32 aspect, u32 data offset,
          copyright list
    data: u32 bitmap count, then per bitmap:
          u32 uid, u8 posture, u8 weight, u8 proportional, u8 cell height,
          u8 ascent, u8 max width, u8 max normal width, u32 encoding,
          u32 metrics offset, u32 metrics count, u32 section count,
          sections {u16 first, u16 last, u32 offsets, u32 bitmap}
    metrics: u32 count, 5 byte records
          {s8 ascent, u8 height, s8 left adjust, u8 move, s8 right adjust};
          glyph width = move - left - right
    offsets: u32 count, u16 byte offset per character (0x7fff = none)
    bitmap: u32 size; per character a cardinality-coded metrics index (one
          byte n with n>>1 = index, or two bytes when bit 0 is set), then an
          LSB-first bit stream of row groups: 1 bit kind, 4 bits count;
          kind 0 = one row repeated count times, kind 1 = count rows.

Pocket Kingdom's two fonts are uid 0x10005910 (bold 12, "font 0") and
0x10005914 (plain 12, "font 1"), cell height 12, ascent 10.

## Shipped font files (`assets/fonts`)

`pk_bold12` (font 0) and `pk_plain12` (font 1), each:

- `.png`: 8-bit grey + alpha atlas; an opaque pixel is ink. Glyphs are in a
  16-column grid on a common baseline (for viewing; positions come from
  the .fnt file).
- `.fnt`: AngelCode BMFont text format. `common lineHeight=12 base=10`;
  one `char` line per glyph with `id` (Unicode), `x y width height` (atlas
  rectangle), `xoffset` (left adjust), `yoffset` (rows from the top of
  the line, = base - glyph ascent, may be negative for tall accented
  capitals) and `xadvance`.

A glyph is drawn at pen x + xoffset, top = baseline - base + yoffset.

## save.dat

0x2958 bytes, read and written sequentially (FUN_10075e80 / FUN_100748dc).
Multi-byte values are big endian unless noted. The last 4 bytes are a
CRC-32 (polynomial 0x04c11db7, reflected) of the rest.

On the phone `save.dat` is 0x2958 bytes encrypted with Blowfish (standard
tables, ECB, each 8-byte block read as two little-endian words;
FUN_10011038 key schedule, FUN_100113c4 / FUN_10011600). A separate random
4-byte salt is stored in `C:\System\Shareddata\101fd3dd.ini` (FUN_10077810,
FUN_10077880, FUN_100778c8). The key is the phone's IMEI as text followed
by the salt (FUN_1007507c / FUN_10075e80). The port writes its own save
unencrypted and expects an imported phone save with the salt appended
(0x295c bytes), together with the phone's IMEI (--imei).

| Offset | Size | Content |
|---|---|---|
| 0x00 | 32 | header: +0xc8, +0xcc, +0xd0, +0xd8, +0xdc, rival +0x138, +0x140, current area +0x424, +0x428, points (3 bytes), loot (3 bytes), +0x43c, progress +0x440, +0x444, +0x4e4, +0x4e8, +0x4ec, +0x450, background +0x4f0, name record +0x13c (2 bytes, little endian), +0xd4, +0x4f4, (unused), +0x448 |
| 0x20 | 20 | player name, 8-bit, zero terminated ("dangerboy" by default) |
| 0x34 | 50 x 32 | kingdoms (+0x291c): words 0..22 one byte each, byte 23 = high byte of word 19 (name record) |
| | 8 x 16 | +0x275c: words 0..9, 11, 12, 13 |
| | 250 x 16 | items (+0x3b18): words 0..8 |
| | 100 x 16 | units (+0x5e40): words 0..12 |
| | 42 x 48 | areas (+0x7810): w0, 0, w2, w3, w4 (2 bytes), w5..w14, w15..w29, w30..w34, w35, w36 |
| | 22 x 16 | events (+0x7394): words 0..3, 6..10 |
| | 14 x 48 | +0x19ad8: w43 (2 bytes), w44, w45, w46, w0, w1, w2, then 4 x {w3+i, w11+i, w15+i, w19+i, w23+i, w27+i, w31+i, w35+i, w39+i} |
| | 16 | +0x5bc bytes |
| | 42 x 2 | +0x174/+0x178 pairs |
| | 58 | +0x2c4 help pop-ups shown |
| | 2 | padding |
| 0x2954 | 4 | CRC-32 (IEEE) of all preceding bytes, little endian |

A new game starts with every array empty (FUN_1006c228): no units, no
items; 10000 loot. The first units are bought in the shop.
