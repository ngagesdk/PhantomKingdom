# Reverse engineering notes

Target: `6RAH.APP` of Pocket Kingdom v. 110804 (E32 uid3 0x101FD3DD, code
checksum 0x19024F41), loaded in Ghidra as raw ARM v5T little endian at
0x10000000. `tools/re` repeats the setup on your own copy. Function
names below are Ghidra's default `FUN_<address>`; offsets are relative to
the object passed as the first argument.

## Program structure

The application's main control ("game object", 0x1b000+ bytes) holds all
state; Symbian applications have no writable static data. Important fields:

| Offset | Meaning | Port |
|---|---|---|
| +0x38/+0x3c/+0x40 | background bitmaps (FUN_10070a10) | `bg_id[3]` |
| +0x58 | entry reason (0 cold start, 3 back from Arena, 10, 0x65 ...) | `entry_reason` |
| +0x60 | frame counter | `frame_count` |
| +0x68 | scan code of the key pressed this frame | `key_code` |
| +0x74/+0x78 | camera | `cam_x/cam_y` |
| +0x7c | tick phase 0..2 | `tick_phase` |
| +0x88 | mode | `mode` |
| +0x8c | phase within the mode | `phase` |
| +0x90 | mode timer | `mode_timer` |
| +0x94 | render pass | `render_pass` |
| +0x98/+0x9c | curtain mode / width | `curtain/curtain_w` |
| +0xa4 | title intro finished | `title_ready` |
| +0xb8 | draw the background | `bg_visible` |
| +0xf4 | title menu cursor | `title_cursor` |
| +0x5cc | language 0..4 | `language` |
| +0x624 | graphics context of the back buffer (+0x61c) | frame buffer |
| +0x628 | CPeriodic, 30 ms (FUN_1006f27c) | main loop |
| +0x64c | sound manager | `pk_audio` |
| +0x650 | bitmap list (FUN_10070250 / FUN_100705c4) | `slots` |
| +0x654/+0x658 | object list / saved list | `objects/saved` |
| +0x65c, +0xc4 | text queue, 64 x 0x84 bytes, count | `pk_drawq` |
| +0x1a590 | pointer to the save game / kingdom data | - |
| +0x1aa04 | CArenaFramework | `net/` |

## Main loop (FUN_1006f3c4, timer callback 0x1006fe18)

Three ticks per frame: 0 logic (mode handler FUN_1006f1f0, then every
object's update through FUN_1006ad3c, removal FUN_1006d5f4), 1 render
(FUN_1006d6ac: two passes of background FUN_1006f1a8, objects
FUN_1006de4c and text FUN_10065a20, then curtain FUN_100708e0), 2 present
again and advance the curtain (FUN_10070944). Logic therefore runs every
90 ms. The back buffer is not cleared between frames.

## Objects (constructor FUN_1006ab10, 0x9c bytes)

FUN_1006d544(game, update, type, state, a, b, x, y) creates and appends.

| Offset | Field |
|---|---|
| +0x04 | frame |
| +0x08 | bitmap list index, 0xff = hidden |
| +0x0c | flags: 1 sprite, 2 animating, 4 mirrored, 0x10 OR blend, 0x20 unmasked, 0x40 screen fixed, 0x80 no half-height source offset, 0x100 line, 0x200 rectangle, 0x400 never culled |
| +0x10/+0x14 | animation timer / index |
| +0x18..+0x34 | work (cleared by FUN_1006ad14); +0x18..+0x20 colour of lines/rects |
| +0x3c | counter |
| +0x40 | render pass |
| +0x4c/+0x50 | line end; +0x54/+0x58 rectangle size |
| +0x60 | dead |
| +0x68 | update function (FUN_1006ad90 changes it) |
| +0x6c, +0x74, +0x78 | type and two arguments (searched by FUN_100707a4) |
| +0x70 | state |
| +0x80/+0x84 | position |
| +0x94 | animation table (FUN_1006ac04, stepped by FUN_1006ac44) |
| +0x98 | sprite frame table (FUN_1006ac2c) |

Objects with x outside -20..372 are removed unless flag 0x400 is set.

Drawing (FUN_1006de4c, per render pass): flag 4 draws with FUN_1006d91c,
else flag 0x20 with FUN_1006dd14, else FUN_1006db1c; flag 0x10 sets the
device's OR mode around the call. Sheets drawn both ways (unit sprites,
effects) hold two copies: FUN_1006d91c reads the frame at x = width - sx -
w (the mirrored copy in the upper half, no mirroring at draw time), the
others read (sx, sy + height / 2) unless flag 0x80 is set. A frame read
from the wrong copy shows parts of other poses. Three unit sheets in the
game data (types 0x1e, 0x35, 0x54) keep the copies the other way round
while sharing frame tables with sheets that do not; the port checks each
unit sheet's layout against its frame table (pk_game_slot_halves_swapped;
only unit sheets 0x102..0x1e7: effect sheets such as 0x60 spread their
frames too evenly for the check).

## Modes (FUN_1006f1f0)

| Mode | Handler | Content | Port |
|---|---|---|---|
| 0 | FUN_10068db0 | battle: loads four units per side | pending |
| 1 | FUN_10069334 | not identified | pending |
| 2 | FUN_10068ff8 | kingdom screen (background 0x58, secretary portraits 0x4e..0x56) | pending |
| 3 | FUN_1006b5c4 | start-up and title | done up to the title menu |

## Mode 3 and the title

Phases: 0 FUN_1006aeec (Nokia copyright, bitmap 2 + language), 1
FUN_1006b0cc (after more than 0x20 frames: SEGA logo), 2 FUN_1006b128
(black, mestext record 0 at (0x58,100), game.id at (0x58,0x82)), 3 Arena
path, 4 FUN_1006b490 (14 load jobs, title object type 0xc4 with update
0x10078580), 5 idle. The ESRB screen (bitmap 0) is not shown by this path.

Title object 0x10078580: root states 0..3 (close curtain, spawn two
children, BGM 0, show frame 0 of table 0x100aff80 from bitmap 7, open
curtain, wait for +0xa4), 5 menu (cursor +0xf4, keys 0x10/0x11/0x35,
sounds 2/0/1, 0x1ad idle frames start the attract mode FUN_10036e68).
Children: 0x17..0x19 logo flash (frames 3, 2, 1), 0x1a..0x1e banner with
animation 0x100b0130, 0x1b/0x1f second banner (animation 0x100b0158),
0x20 menu items (FUN_10079b18), 0x21 slide out.

## Title menus (0x10078580, FUN_10079b18)

Menu layout table 0x100b0198 (per menu: first y, line spacing) and item
table 0x100b01d8 (per menu five mestext ids; -1 ends, 0x11c is an empty
line, 0x127 + language is the language name). Items are drawn through
the text queue with font 0, centred: a shadow at (0x59, y+1) in black,
then the text at (0x58, y) in white when selected, gold (TRgb 0x00dcff)
otherwise, grey (0xb4b4b4) for an unavailable Arena entry.

| Menu | Items (mestext) | Root states |
|---|---|---|
| 0 | Offline Play 271, N-Gage Arena 272, Options 273, Quit 274 | 4, 5 |
| 1 / 6 | New Game 281 (no save) | 6, 7 |
| 5 | Continue 280, Delete Save File 395 | 6, 8 |
| 7 | Delete Save File 395, Yes 65, No 66 | 0x12, 0x13 |
| 3 | VOLUME 283, Credits 285, language 295+ | 0xc, 0xd |
| 4 | Yes 65, No 66 ("No" preselected) | 0x10, 0x11 |
| 2 | Game, User Account, Auction, Ranking, Chat (Arena) | - |

Changing menus: the root sets work[6]=1 and work[5]=menu; the banner child
(state 0x20) switches to state 0x1b (animation 0x100b0158, 14 frames) and
draws the new menu afterwards; the root waits 14 frames in state 0x14.
The cursor does not wrap. Sounds: 0 confirm, 1 back, 2 cursor. Quit sets
+0x58 = 5. Continue loads save.dat and enters mode 1; New Game switches
the root to update 0x1004597c; Credits to 0x1003632c; the Arena entry
starts the Arena framework (FUN_10071fd0). The volume gauge is two
children (state 0xe/0xf, frames 0xc and 0xc+volume of table 0x100aff80).

## New game and opening (title state 7, 0x1004597c)

Confirming "New Game" resets the kingdom (+0x434 points 0, +0x438 loot
10000, +0x440 progress 0), picks +0x138 = rand % 5 and +0x13c = a free
name record in 200..399 (FUN_1007b018), reads the kingdom name from
name.dat (16 byte header, 24 byte records, FUN_10064dc8) into +0x544, sets
background +0x4f0 = 0xf and switches the title root to 0x1004597c. That
object scrolls opening.dat (64 byte records, all of them) up from y 0xdc,
1 px per frame, lines 0x1a apart, a ring of ten lines (game+0x9064),
drawn twice in black at (0x57, y-1) and (0x59, y+1) and once in white at
(0x58, y). Backgrounds 0x92, then 0x93 at 1/3 and 0x94 at 2/3 of
(lines+8)*0x1a frames; music 3; 5 skips; then mode 1.

## Mode 1: kingdom screen (FUN_10069334, 0x1002479c, 0x10025354)

Phase 0 clears everything and shows "Now Loading"; phase 1 picks the
background (FUN_1007163c: 0xf below 0x97 points, 9 up to 300, 0xe above),
runs 20 load jobs and spawns the kingdom root (type 0xc2) and the help
object 0x1005ad94 (type 0xc5); phase 2 triggers idle help pop-ups.

Root states: 1 spawns the background decoration (child 0xd0) and the top
bar/panel (FUN_10024604: icons 2, 3, 4 at y 3 and panel 0x7a at (0,0x92)),
opens the curtain, music 2; 2 spawns four buttons (child 0x90, arg1 6) at
x -0x54/0xb0, y 0x9a/0xb2, their icons (arg1 7) and the pointer (0xa0);
4 handles keys. work[0] (+0x18) is the menu, work[1] (+0x1c) the cursor
(bit 1 column, bit 0 row; FUN_10024740 flips a bit and skips cells
disabled in +0x134). Labels: table 0x1008b7bc, icons 0x1008b80c:

| Menu | Cells 0..3 |
|---|---|
| 0 | Party, Shop (0x1005b754), Map (0x1003db80), Lab |
| 1 Party | Form 0x100493c4, Medical 0x10045630, Defense 0x10036b8c, List 0x1003d8a0 |
| 2 Shop | Buy, Global Auction, Sell, None |
| 3 Map | Search, Attack, Move, Info |
| 4 Lab | Combine 0x100329a0, Vault 0x1003b9a8, RankUp 0x10053a70, Upgrade 0x10045dc8 |

Buttons slide by 0x16 per frame, frame 6 selected / 7 enabled / 8
disabled, label at (x+0x32, y+0xe) black and (x+0x33, y+0xf) white or
gold. Lab is disabled at progress 0, Defense below progress 2, Medical
without injured units. Changing menus: root state 5/6 waits 4 frames
while the buttons slide out (0x94) and in again (0x91). Top bar numbers
use the bitmap digits (FUN_10065814): level (thresholds 0x1008d280) at
(2,3), loot right-aligned at (0x60,3), points right-aligned at (0xa1,3).

## Shop (0x1005b754) and sub-routine calls

Entered from the kingdom menu with the curtain closed: state 2 opens the
curtain, sets menu 2 and background +0x4f0 = 0xc, spawns the top bar;
state 7 spawns the buttons; state 4: left/right toggles Buy (0) and Sell
(2); up/down and the cells Global Auction (1) / empty (3) work only with
the Arena connected (+0x1aa0c == 2). Buy -> state 0x10, Sell -> 8, back ->
0x17 (kill children, recompute the background, kingdom root state 1).

FUN_1006ad98(obj, game, fn, state, ret_fn, ret_state) stores (ret_fn,
ret_state) at *(game+0x3b14) and switches the object to (fn, state): a
sub-routine call between update functions. Buy and Sell call the generic
dialog 0x1002637c (dialog type in +0x100: 0xd, 0xb, 0xc; result in +0x104,
0 = confirmed, 1 = cancelled; list position +0xf0 + cursor +0xf4), then
the unit list 0x1005c7b4, the item list 0x10039b34 or the kingdom list
0x1004a4a0, returning to shop states 0xa..0x16. Buying writes the unit
roster (+0x5e40) and areas (+0x7810); to be reconstructed.

## Generic dialog (0x1002637c), lists and game data

The dialog type in +0x100 selects labels from 0x1008b93c (11 mestext ids
per type; row counts 0x1008b87c, first row y 0x1008b8c0). Types: 0
targeting (High HP ... Front Position), 1 orders, 2/5 Select/Clear, 3
Defense Party/Strategy/Battlefield, 4 All/Party/Unit, 6 battlefields, 7
traps, 8 Area Search/Scout/Chat, 9 Weapons/Armor/Accessories, 0xa
Jewels/Others/Clear, 0xb material kinds, 0xc Yes/No, 0xd Item/Unit, 0xe
pause menu, 0x10 auction. The line under the rows depends on the caller
(switch on *(game+0x3b14)); the port lets the caller install it
(g->dlg_hint). The list position lives in +0xf0 (top) and +0xf4
(cursor); FUN_10029bfc/FUN_10029c10 save/restore it in +0xf8/+0xfc.

Every child that calls FUN_1006ac2c continues after the call; Ghidra
had split these tails off as separate functions because the callee
looked non-returning. tools/re/ghidra/PKTail.java clears the 562 flow
overrides and re-joins the tails.

Data files (all read at run time from the user's installation):
itemdata.dat 0x40 byte records in eight sections (kind 0..7 at 0xab0,
0xd70, 0xf30, 0x1430, 0x23b0, 0x2b70, 0x4770, 0x4af0), unit.dat 0xa0 byte
records (name, race, ability, 16 values: icon, rank, -, cost, HP, ...,
price hi/lo, -), itemshop.dat / unitshop.dat 16 byte header then 16 / 8
bytes per area. Prices are doubled by the shop. Owned units (+0x291c,
0x5c each): f0 type, f2 weapon, f3 team << 4 | slot, f7/f8 HP, f14 rank,
f15 icon, f17 cost, f19 name.dat record. Teams (+0x275c, 0x38 each):
f0..f3 unit per slot, f4..f7 targeting per slot, f8 order, f9 leader
slot, f11 cost left (6 at start), f12 busy, f13 has units.

Save games are read field by field from save.dat (big endian 24-bit
values, around line 108468 of the listing); to be reconstructed.

## Keys (FUN_10070100)

game+0x68 receives the scan code; the left soft key (0xa4) and the centre
key (0xa7) become 0x35 ("5", select), the right soft key (0xa5) becomes
0x37 (back). In modes 0 and 1 a soft key opens the pause menu instead
(+0x630). game+0x70 counts idle frames and is reset by key presses.

## Fonts

Text uses game+0x5e8[font]: font 0 is CEikonEnv::LegendFont()
(EIKCORE ordinal 243), font 1 CEikonEnv::DenseFont() (ordinal 241), both
re-fetched for the back buffer device. On Series 60 1.x these resolve to
the bold and plain 12 px Latin faces (uids 0x10005910, 0x10005914; the
mapping is the platform default and was not read from the phone's LAF
library). FUN_10065a20 draws with CFbsBitGc::DrawText into the box
(x, y-height+2, x+width+5, y+2) for left alignment (centred: x-(w+5)/2 ..
x+(w+5)/2; right: x-(w+5) .. x), baseline offset = ascent, margin 2, so
y is the baseline.

## Curtain (FUN_1007088c, FUN_10070944)

Mode 1/3 closes: width 0 grows by 0x20 per frame; past 0xcf it becomes
mode 5 (held). Mode 2 opens: width 0xb0 shrinks by 0x20; below 1 mode 0.
Mode 4 is static black. The curtain is a black rectangle (0, 0, width, 208).

## Sound (sound thread FUN_10069f70)

Commands through a 32-entry ring buffer (FUN_10069a80/FUN_10069ac8):
0 play BGM (+0xa8), 1 stop, 2 pause, 3 effect, 4 resume, 5 volume.
BGM ids 0..7 = BGM01..08.pcm (FUN_10077fd4; the same id does not
restart). Effects (FUN_100780e0): 0..4 SYS01..05, 5..0x1e ATK01..26,
0x1f grow, 0x20 whiskaway1, 0x21 neon_light.

## Pitfalls

- Ghidra marks FUN_1006ac2c (a one-line setter) as non-returning; code
  after its call sites is cut off. `tools/re/ghidra/PKFix.java` clears the
  flag and disassembles the call sites again; the remaining tails can be
  read with `tools/re/armtail.py`.
- Many functions use literal pools; `tools/re/annotate.py` resolves them
  to values and strings.

## Missions, events and the start of a battle

Teams act through events (+0x7394, 22 records of 13 words): f0 timer in
logic frames, f1 status (0 free, 1 running, 2 just finished, 3 done), f2
kind, f3 team, f4 sequence number, f6 area, f7 target type (+0x128), f8
target index (+0x12c), f9, f10 random byte. FUN_10070c78(game, kind, team)
starts one: timer = base[kind] (table 0x100aecf8: 5, 11, 11, 11, 11, 5,
330, 5, 5, 660, 5, 5) + (rand % 5) * 22 for kinds below 5. FUN_10070b20
runs every logic frame and counts running events down only in mode 1;
FUN_10070e6c returns the finished event with the lowest sequence number
(kind 0xb is only a message). FUN_10071588 (mode 1, every frame) counts
+0x4d8 down and then starts an enemy invasion (kind 0, team 0xff) when
the player is past area 1 and none is running; the defense team (+0xdc)
is marked busy.

The kingdom and the map hand a finished event to 0x10028538: kind 0
invasion (defense team or lost by default), 1 attack on a base, 2 area
search (random battle against partydata.dat formations), 3 search result
(0x100583bc), 8, 10 online. A battle is prepared by FUN_1000c738(game,
area, level, slot) - partydata.dat record area * 0x120 + level * 0x20 +
0x10 into formation slot (+0x19ad8 + slot * 0xc4: battlefield, order,
trap, four units of type/equipment kind/id/.../targeting, HP) - and
FUN_1000cc40(game, team, formation, side, mode), which fills the battle
record at *(game+0x1a590): two sides of four unit records (+0x0 and
+0x228, 0x5c each, copies of owned units / templates), targeting
(+0x450 / +0x468), orders (+0x480 / +0x484), team and formation
(+0x488 / +0x48c), total HP (+0x490 / +0x494), player side (+0x498),
time limit (+0x49c, 0x14a or 0x441), leaders (+0x4a0 / +0x4a4),
equipment (+0x4a8 / +0x5e0, 0x34 each), names (+0x86c / +0x8e4, 0x14
each), battlefield (+0x964), area (+0x96c), trap (+0x978). Then
FUN_10070844(game, 0) enters mode 0: FUN_10068a34 loads the battlefield
bitmap (+0x964 + 0x86), the unit sprite sheets (type * 2 + 0x102) and
portraits, the battle controller object 0x10000824 (type 0xc3) and one
0x10020f3c object per unit.

Map travel cost (FUN_1003dafc): highest rank in the team times
0x10096344[area * 4]. mapinfo.dat: 0xf0 bytes per area, five 0x30 byte
names; their kind and value come from 0x100ac7d8 (FUN_10063cc0).

## Map: Search, Attack and the mission events (corrected)

- Search > Area Search starts event kind 4 (not a battle). When it ends,
  0x10028538 hands it to the report 0x10059514, which applies
  FUN_10063f40: the party's summed AGI ("Search Power") picks a tier
  (<31, <61, <91, <121, more); k = word at 0x100ac688 + area*8, / 100;
  r = rand % (k+1) - k. Bases = r + tier, materials = r + tier + 1.
  Materials come from product.dat (16-byte header, 24 bytes per area,
  kind/id byte pairs), weighted by rank {70,60,50,40,30} (0x100ac30c).
  Area words (areas[a], +0x7810, 0x94 per area): 0 spots discovered,
  1 rival bases, 2 enemy bases, 3 bases destroyed, 4 base serial,
  30..34 visits per spot. New spots need two destroyed bases each;
  each area knows at most twelve places. Enemy bases take formation
  slots 0..11 (word 43 = serial, 0xffff free; 46 = under attack;
  47 = rival).
- Search > Scout starts kind 3 (report 0x100583bc, not reconstructed).
- Map > Attack (0x10043060) picks a spot (g+0x128 = 0) or a base (1);
  then the team list. Spot -> kind 2 (FUN_100740e0): battle, area boss
  on the last visit, treasure for spot kind 4. Base -> kind 1:
  FUN_1000cc40(team, slot, trap, 0). Winning destroys the base (area
  word 2 -1, word 3 +1).
- Scout (kind 3, FUN_10064494): team AGI level <41/<81/<121/<161/more.
  Base: level 0 nothing, 1-2 partial (word 45 = 1), 3+ full (2).
  Spot: level > 0 reveals the fights left before the boss.

## Secretary pop-ups (FUN_10071c74, window 0x1005ad94)

Texts are UTF-16 files in Fabbis/<language>/ of the installation:
popupa/b###.txt (kind 0), trigger###.txt (kind 1, once-only flag per id
at 0x100aefc0, seen flags +0x2c4), info###.txt (kind 2, Map > Info:
FUN_10071c74(g, 2, area)), followed by Ingr###a/b.txt. A missing file
means no pop-up. Implemented in src/game/pk_help.c (plain window).

## Party > List (0x1003d8a0)

Dialog 0xd (Item / Unit). Item: dialog 0xb kinds -> item list
0x10039b34 -> detail 0x1003c224 (hub 0x1003bf3c). Unit: unit list
0x1004a4a0 with +0x120 = +0x124 = 1 -> detail (hub 0x1005fc0c). In
the List any unit / item can be picked (Form only takes free ones).
Port: details drawn as a text window (src/game/scene_list.c).

## Lab > Combine (0x100329a0 / 0x10032ee0 / 0x100342b8)

Slots at game+0x17914 (pointer struct): mold, ore, extra A, extra B
(owned item indices, 0xff empty); result kind/id/rate at +0x10/+0x14/
+0x18. FUN_10062960: itemdata.dat offset from the table at 0x100abee8
[mold id * 7 + ore id]; block = 16-byte header (count in byte 0),
16-byte entries {A kind, A id, B kind, B id, result kind, id, rate}
(A/B as kind<<8|id, 0xffff = empty; either order). No match: first
entry. Cost = result rank * 200 (FUN_10063570). Success: rand%100 <
rate; materials replaced by the result. Failure: materials lost, no
cost. Full: items > 249 (unless the result is kind 4), equipment > 99.
Port: scene_lab.c, text panel.

## Lab > Vault, Rank Up, Upgrade

- Vault 0x1003b9a8: background 0x47, nine pictures (image 0x48, frames
  0x10092e34) shown per bit of +0x444 (masks 0x10092ffc); the ninth
  when +0x43c > 0 or +0x440 > 2. Pop-up trigger 0x20.
- Rank Up 0x10053a70 (confirm 0x10054f8c, chart 0x10056738):
  rankup.dat, 16-byte header (count), 8-byte entries {new type, type,
  emblem id, min level, fame (BE16)}; fame counts as 10 when 0 and
  +0x440 > 1 (FUN_1007aa30). FUN_10057dd4: the unit becomes the new
  type at level 0 (name kept, weapon dropped, leaves its team); costs
  new rank * 200; emblem used. Pop-ups 0x18 (no loot), 0x1a (done,
  "XXX" = the class from +0x574).
- Upgrade 0x10045dc8: castle tier (+0x4e4 & 3; 0x1009863c), +0x4e8 and
  +0x4ec (5 choices each, 5 = none; 0x1009864c / 0x10098664), castle
  style (+0x4e4 >> 2, 2000). Shown on the world map. Pop-up 0x21.

## Battle drops (FUN_1000d5ac)

Each downed enemy: dropitem.dat (16-byte header; 16 bytes per unit
type, six kind/id pairs), one of the first three (rand % 3). Winning a
spot's last fight: clearitem.dat row (fights - 1), one of five pairs
(FUN_100639b0). Item cap 250. The results screen (0x1000a6dc) lists
loot, VP, drops and the clear item; the port shows them in a window.

## Area pop-ups and event kinds

- Map state 9 calls FUN_10071c74(g, 0, area): popupa###.txt the first
  time (+0x174 + area*8), popupb### later. Port: shown once per area,
  flags in popups.dat beside the save (save layout unchanged).
- Event kinds: 0 invasion, 1 base attack, 2 spot attack, 3 scout,
  4 area search, 6 started when an area pop-up first closes (exact
  meaning unclear; not reconstructed), 7-11 N-Gage Arena (online),
  5 never started.

## Ending (0x10038208) and treasures

Area-boss win (battle controller state 4): +0x440 progress + 1 (max
10), area word 35 = 1, Vault bits of +0x444 from the bosses of areas
13, 19, 36, 9, 18, 32, 14, 39 (bits 0..7). Area 0x17's boss sets
+0x58 = 3: after the results the title mode runs the ending: background
0xb7, a 70-frame quake (cam offsets), ending.dat lines rolling up
(26 px apart, <= 10 on screen) until frame 0x42a or 5; then +0x43c + 1
(times cleared; higher enemy levels), +0x448 = 1, back to the kingdom.
Port: PK_MODE_ENDING (scene_ending.c); the title's Credits run the same
roll over staff.dat (the original credits screen 0x1003632c differs).

## Quest spots (reconstructed: trade quests)

A spot of kind 4 (fight count 0) runs a quest (event kind 2 -> 0x100525ac, FUN_100740e0 /
FUN_10074378 / FUN_10073a50): per-spot state in area words 5..9
(+0x7824), 10..14 (+0x7838), 15..29 (+0x784c, 3 per spot), texts in
questmes.dat, rewards from quest_weapon/armor/accessories.dat and
questitem.dat. The port reports that the quest is not available and
brings the party home.

Port (scene_quest.c): offline quests are 0 or 1 (both trades). New
quest: pages 0, Yes/No (0x10053208), accept -> page 1 and the state
is stored (stage 3), decline -> page 2. Later visit: item A owned ->
page 5, A traded for B, state cleared; else page 4. Quest kinds 1..15
and 0x10 (quests 2, 4..18) are only dealt online and are not used.

## Traps (0x10002030 start, 0x10002910 later)

The defender's trap (formation byte 2 / +0x4f4) hits the attacker's
units that are not down. Start: 0 pit (non-flying lose HP/5,
FUN_10005684), 3 positions changed, 8 status 8 (unused by any rule).
When time < 0xa6: 1 status 0x20 for 40 ticks (FUN_10007b28), 2 status
0x100 for 13 (FUN_10007f48), 6 blast HP/5 around a point (FUN_10005a9c),
7 units swapped (FUN_10009930/998c). 4/5 when a non-flying attacker is
at x 0xa0..0xc0 (effects not fully decoded; the port stuns 40 / 13).
Status timers: game+0x1a590 + 0x748 + side*0x18 + slot*4. Statuses in
0x570 stop a unit acting; 1 halves damage taken. Port: stun = idle with
the timer; "Trap!" line instead of the effect sprites.

## Battle units (dispatcher 0x10020f3c, 49 class behaviours)

The dispatcher runs once per unit: image type*2+0x102, position (side 0
at (3-slot)*0x24+0x20, side 1 mirrored at slot*0x24+200, y 0x99),
equipment start properties (FUN_1000f3a0), the order's action weights
(FUN_10021e78), +0x7c = record side, a size flag (0x1000000 / 0x2000000 /
0x4000000, which widens the reach in FUN_10022f44) and the class
behaviour for the type. Summoned units (slots 4 / 5) come with
arg1 = slot | summoner << 8 and keep the summoner's slot in +0x2c.

A class behaviour: FUN_10022230 (hits, regeneration; everyone stops while
game+0x44c is set unless the object carries 0x200000), FUN_100218b8
(target), animate, then the state: 0 set-up, 1 think (FUN_10022000),
2/3 attack, 4 idle, 5 back off, 6 special move, 7 knocked down, 8
staggered, 9 wait for the start, 10..13 statuses. Classes differ in
frames, boxes, timings (the counter tick that spawns the attack box, the
end of the swing), reach, sounds, critical rolls, casters that shoot
projectiles (box states 4..13) after staring at the target, and their
special moves, each with its own camera choreography: game+0xa8 mode 7
jumps to +0xac / +0xb0, mode 5 pans there 12 px a tick, both report 6;
mode 0 hands the camera back (8).

Details worth knowing:

- FUN_10069764 empties all six slots of both sides (type 0xff, targeting
  5, equipment 0xff). Slots 4 and 5 must stay empty for summons
  (0x10011f6c: type 0x37 from the template; 0x1001d5a8: a copy).
- 0x10011b08 jumps to "think" after its switch, so a missing target ends
  its attack and its special move. Its special move runs its sub-states
  into each other: the summon comes after six ticks.
- 0x1001f74c (flyer) compares the wrong way round while rising facing
  left (x <= 0x14c gives 0x14c): it lands on the right edge at once.
- FUN_1000ff6c (types with a property of their own) passes when
  rand % 100 <= level * 4 + luck / 2; equipment properties (FUN_1000eda8)
  when it is below. FUN_1000eda8 lowers the attacker's attack or defense
  by a quarter of the target's magic for the stat properties, as written.
- 0x1001ce2c overwrites its own state with 9 after setting up a copy.

Port: game/pk_battle_unit.c (dispatcher, shared helpers, attack boxes,
hit detection), pk_battle_classes.inc (the per-class numbers and the type
table), pk_battle_special.inc (classes with behaviour of their own),
pk_battle_effects.inc (statuses, special-move damage, equipment
properties, falling, summons). Written by hand from the behaviour; checked
tick by tick against a local reference (not shipped) over every class,
special moves, summons and equipment properties with fixed seeds.

## Battle HUD and unit sheets

- Gauges (0x10004154, spawned 5 per side): rows at y 0x15, 0x16,
  0x17 (4 px), 0x1b, 0x1c; colours 0x1007e7cc (rows 0/4, 1/3, 2);
  length 40 * HP% / 100 (FUN_10004e80); player side right-aligned at
  0x4a, the other from 0x66. No HP numbers on screen.
- Unit markers (0x10003d28): image 0x5e, frames 0x1007da64, 2 + slot
  (player side) / 6 + slot, 0x40 above the unit, kept on screen.
- Unit sheets (0x102 + type * 2) have white frame margins that their
  masks mark opaque; the port makes connected white areas transparent
  (src/gfx/pk_gfx.c) so no white bars show beside units.

## Attract mode (0x10036e68)

After 0x1ad idle ticks on the title menu. Captions: demo.dat (64-byte
records; 0..0x12 and 0x13..0x24 are growing copies shown as typing,
then 0x25..0x2c). Background 0x95 scrolls in from camera y -0xd0;
picture objects (type 7) 0..0x10 per step: backgrounds 0x96/0x9b/
0xa1/0xa2, images 0x97/0x99 (frames 0x10091154), 0xa5..0xb5 sliding
(frames 0x1009119c), 0x9c/0x9e rising; each lives 0x28 ticks. Any key
or the end returns to the title (counter +0x1b048 + 1). Port:
scene_attract.c (records of step 3 assumed 0x26/0x27).

## Credits (0x1003632c)

Background 0xba, music 7, preloads pictures 0xbd..0xd0. staff.dat line
every 10 ticks (0xb7 lines), ring of 13 lines 0x14 apart, scroll
2 px/tick (+0x12 when a line leaves); a picture (type 5, image 0xbd +
n, frames 0x100910f4, flags 0x4e1) per tenth line ((line + 5) % 10 ==
0) from x 0x108, y 0x68: -8 px for 0x16 ticks, rest 100, -8 px for
0x18. Key 5 or the end: back to the title. Port: scene_ending.c.

## N-Gage Arena (online)

Rooms and messages: PhantomLink docs/ARENA.md. Port details: the
defender's answer 137 (FUN_100669a8): [2] battlefield (+0xd4) or 0xff
(no defense party / busy), [3] party order (+0x277c), [4] trap (+0x4f4),
to @8, from @24, units @40 x 12: type, weapon kind/id/w3/w4/w2 (0xff
without), level, targeting (+0x276c), 0, HP. Scout answer 135
(FUN_100661e0): [2] battlefield, [3] order, units @36 x 4: type, HP,
word 17. The defender's game also starts event kinds 9 and 7 for the
defense; the port only answers. Port: net/pk_online.c,
game/scene_arena.c, rival targets in scene_map.c.

## Global Auction (0x1002a544)

Shop cell 1 while online. Dialog 0x10, rows mestext 341..344: Bid Item
(0x1002bd20, browse 14 -> 142 then bid 15 -> 143; [2] of 143 is the
result, triggers 0x2c.., u32 @4 the earlier bid the game gets back),
Exhibit Item (0x1002cd38, 13 -> 141, [3] result), Check Exhibits
(0x1002dba8, 16 mode 2: sold -> loot, no sale -> item back) and Auction
Bids (0x1002d574, 16 mode 3: won -> item, lost -> bid back; mode 1 and
trigger054 without room for the items). Port: game/scene_auction.c.

## Event announcement (0x10028538 state 2)

Before a finished event is handled (state 4) a pop-up announces it:
kinds 1..4 and 8 trigger022 with "XXX" = the party's leader's name
(game+0xec = team -> 0x1005b110, FUN_1007aa10 reads the unit's name);
an invasion trigger027; a rival attack on the defense party (e[6] == 2;
the event array starts at game+0x7394, kind at +8 = 0x739c) trigger034
with the attacker's name.

## Defence against a rival (FUN_100669a8, FUN_10066de0)

On 9 the defender answers 137, sets +0xe4 (further attacks are answered
0xff), marks the defense party busy and keeps the 88-byte message. A 10
whose name @4 is not its own clears events 9/7 and starts an event of
kind 0 with e[6] = 2; its handler (0x10028538 state 4) loads formation
0xd from the kept message (units @40 x 12: type, weapon kind, id, w3,
w4, w2, level, targeting, -, HP) and fights it with the defense party
(FUN_1000cc40(h_dc, 0xd, 1, 1)). Port: scene_arena.c, scene_map.c.

## Battle HUD scale

FUN_10004e80 divides a side's HP by its HP at the start of the battle
(+0x490 player, +0x494 enemy, summed in FUN_1000cc40), clamped 0..100;
a damaged party starts with a full gauge.

## Mirrored unit sheets

The sheets of unit types 0x11, 0x1e, 0x35 and 0x54 (the only unit sheets
whose MBM header gives a size in twips) are stored mirrored: their frame
tables fit only the horizontally flipped bitmap. The port flips unit
sheets with a twips size when it loads them (gfx/pk_gfx.c).

## Battle display (controller 0x10000824 state 1)

Interface objects (image 0x5e, frames 0x1007da64):
- 0x10003a1c: arg1 0 top bar (frame 0 at 0x58,0x15), 1 bottom panel
  (frame 1 at 0x58,0xb8; 0x66 / 0x67 while game+0x464 names the side of a
  speaking unit), 2 / 3 the speaker's portrait (portrait frames 0 / 1 at
  0x98,0x9c / 0x98,0xb4) and 0x6a the strip (0x6b for bosses, at
  0x4c,0x9e), these three only while game+0x45c is set.
- 0x10003c74: frame 0x11 at x = camera / 2 + 0x2c, y 4.
- 0x100055b8: the leaders' small faces (portrait frames 3 / 4) at
  (14, 0x19) and (0xa2, 0x19).
- 0x10003d28: per unit a number (frames 2..9) at y - 0x40 and an HP
  step (frames 10..15: HP / (max / 6), full = 15) at y - 0x38.
- 0x10004154: the gauges; 0x100044b4: timer digits (frames 0x2c..0x35)
  at (0x53, 0x18) / (0x5c, 0x18) from game+0x49c / 110 and / 11 % 10.

FUN_10009c2c (unit, group) fills the panel: the unit's name and its line
from battlemes.dat (FUN_1000ea00: record group * 13 + unit word 20).
FUN_100051a8 draws them: the name right-aligned at (0x7e, 0xca), font 0,
white over a black shadow at +1, +1; the lines at x 6 from y 0xab, 13 px
apart, font 1, black (a boss's first line at 6, 0x9e). Text queue entries
(FUN_100654c0): x, y, colour, alignment, render pass, font. game+0x460
counts down every tick while game+0x45c is set; the text shows while it
is above 1.

Top bar (FUN_100051a8): one name in font 0. A normal battle shows the
player's team leader (record side 0, slot +0x4a0; the controller loads its
name, index +0x4c, with FUN_1007aa10): red (TRgb 0x1030f7) at (0xc, 0xc)
when the player fights on the left, blue (0xf7b372) right-aligned at
(0xa4, 0xc) on the right. Against a rival kingdom (formation word 47) a
centred name at (0x58, 0xc): the attacker's in red when defending, the
rival's in blue when attacking.

Intro (controller state 2): the camera jumps to the other side's leader
(mode 7), FUN_10009c2c (group 0, bosses 6) once the curtain is open, the
fight starts after 0x20 ticks or a key (camera mode 0, game+0x458 = 1,
+0x460 = 0, +0x44c = 0). The intro shows the other side's leader (group 0, bosses 6) with
the camera on it; special moves use group 2.

Camera (FUN_100049e0): after the intro game+0xa8 = 8, the held left /
right key pans 8 px a tick between 0 and 0xb0; modes 5 / 7 centre on
game+0xac.

## Effect object 0x100155f4 (type 0xc1)

One object class draws every battle effect; its state selects it (hit
spark 0 on image 0x60 when an attack box lands, bursts, "MISS" 2, stat
labels 6..0x1d, status marks 0x29..0x3c that follow their unit while the
status bit holds, fire 0x44, crystals 0x3f, ...). Port:
game/pk_battle_fx.inc. The class behaviours spawn these effects through
pk_battle_fx().

## Sheet margins

Unit sheets keep their frame margins opaque in the mask; most use white,
some a light colour (0xEEEEFF, 0xEEEEEE, 0xBBFFFF, ...). Frames whose
rectangle reaches into a margin would show it, so the port makes the
sheet's commonest light opaque colour transparent where it forms areas.
Many large sheets hold a mirrored copy of the frames in their lower half.

## Trap reveal (0x10002030, 0x10003670)

A trap first shows eight cards (image 0x5a, frames 0x1007e4e4, 44 x 64,
unmasked): four slide in from the left at y 0x65, four from the right at
y 0x25, they turn (animation 0x1007e74c) and show the trap's picture
(trap 0..9 -> frames 0xe, 0xd, 0xc, 9, 5, 6, 8, 10, 7, 0xb), then
game+0x4f8 = 1 and they slide out. Trap 0 drops the units from y -20
(speed 8, +4 a tick) and hurts those that do not fly on landing (effect
1); trap 3 rearranges both sides (x 270/310/30/70 and 230/190/150/110).
The defending leader then speaks (FUN_10009c2c group 4, 5 after trap 3).

## Battle results (0x1000a6dc)

After the curtain the camera goes to 0 and a window of rows (0x1000b6cc
state 0x10: image 0x17, frames 0x1007ea68, 0x40 at y 0x68, 0x41 every 4
px, 0x42 at 0xcc) opens over the field. FUN_1000b8e0 writes mestext 1 /
2 (won / lost) at 0x58,0x76, record 3 with the victory points (+0x95c) at
100,0x87, record 5 with the loot (+0x960) at 0x32,0x92, record 4 and the
materials (one line each from 0x9d) or record 6; an undefended kingdom
shows 0x195. Key 5 closes it. Port: pk_mode_result.

## Search and scout reports (0x10059514, 0x100583bc)

Background 0x58 (the magic circle), object 0x1005f080 (image 0x4c: the
circle, then the beam with animations 0x100a2d18 / 0x100a2d50), the party
leader from its unit sheet (0x1005f254) at 0x58,0x5f and the window rows
of the results screen. Search pages: FUN_1005a400 (mestext 140 + count +
148, then the new spots by move.dat name, mestext 0x38 per enemy base, or
0x3b), FUN_1005a618 (online: rival kingdoms), FUN_1005a7a4 (140 + count +
141 and the materials, or 0x3c; "items/250" at 0xaa,0xcd). Trigger 2 on
the first page. Scout: FUN_10058edc for a spot (its name, 0x35 when
nothing was learned, else 0x36, 0x37, mestext 139 + the spot's fights)
and FUN_100590c8 for a base (0x38, 0x39, 0x3a + the battlefield's name
from the table 0x1009e1d8, 0x106 + the units' template word +0x198 from
scout level 1, 0x29 + their HP from level 2, else 0x1e).

## Map cells (0x1003db80 states 3 / 0xc0)

The map shows the area's targets in the same 4 x 3 grid as the target
screen (0x10043060): spots (image 0x1b, frame kind + 0x1b), rival kingdoms
(image 0x45, castle in four layers from their look), enemy bases (image
0x1b, frame travel cost / 100 + 0x12), from (0x1c, 0x2d) in steps of 0x28.

## Lab panels

Combine (0x10032ee0): plates of image 0x17 (frames 0x1008fe40, 160 x 20)
at 8, 0x19 + 0x18 * slot (0x10, selected 0x11, an empty extra 0x12), the
button at 0x32,0x79 (0xb / 0xc, mestext 0xd), slot icons (mold 0x35, ore
0x34, extras by kind from 0x1008fde4), names at plate + (0x1c, 0xf) or
the empty labels 0x7f / 0x80 / 0x97 in 0x666666, help 0xf6 / 0xf5 / 0xf7 /
0xf8 + 0xf9. Rank Up (0x10053e80): plates (frames 0x1009c18c) for unit
(8,0x23) and emblem (8,0x41), buttons 0x2c and 0x146 (Dictionary) at
0x32,0x5f / 0x32,0x73, the unit's face (image 0x19, frames 0x1009d41c,
template word 14 * 2) and class name or 0x62, the emblem (0x3a) or 0x81,
help 0xfd..0xff / 0x147. Dictionary (0x10056738): 70 rank-ups (table
0x1009dd4c: emblem, unit type, result, name index into the seventh name
block of itemdata.dat), known ones flagged in game+0x5bc by
FUN_1005826c after a rank up; five rows, left / right page by five.

## Lab Upgrade screen (0x10045dc8)

- Option plates: object type 0x26, image 0x17, frames 0x10096ecc (132x14),
  frame 0x3c normal / 0x3d selected, flags 0x4c1, at (0x16, 0x28 + 0x14*row).
- Plate labels (FUN_1004932c): mestext from the int table 0x10096e0c,
  index group*8 + row (group 5 = the four choices, 0..3 = items of each),
  drawn centred at plate + (0x42, 0xc).
- Help line in state 0xf: mestext 0xa3 + cursor at (0x58, 0xb4), centred.
- Castle sub-screen help: 0xab / 0xac + 2*cursor; transporter 0xb9 + cursor.
- Sub-screens: the plates move to (0x16, 0x41 + 0x10*row); a white window
  (rows 0x40..0x42 of image 0x17, y 0xe..0x32) holds two previews built of
  objects type 0x2a: image 0x45, frames 0x1009815c (32x32). Layers:
  transporter back +0x4e8*2+3, castle +0x4e4+0x12 (size + colour*4),
  transporter front +0x4e8*2+2, mark +0x4ec+0xc (5 = none). Owned look at
  (0x2d,0x24), the look under the cursor at (0x83,0x24), arrow frame 0x11
  at (0x58,0x24).

## N-Gage Arena on the title

- The entry is greyed and skipped unless game+0x43c (times cleared) > 0 or
  game+0x440 (area bosses beaten, max 10) > 2, both read from the save
  header (bytes 15 and 16). That is the point the Online Crest is given.

## Shop buttons online, Global Auction look

- Shop (menu 2) online (game+0x1aa0c == 2): cell 1 is one wide button,
  frames 0xba / selected 0xb9 of the interface frames, sliding in at 0x2c
  per tick from x * 2, label 340 at x + 0x5a, icon 187; cell 3 is hidden
  and disabled. Offline both are blank and disabled.
- Auction (0x1002a544): the menu is the generic dialog type 0x10 over the
  shop (state 6). Requests show the window frame 0xa9 (frames 0x1008d3e4,
  160 x 48) at (0x5a, 0x5d) with mestext 354 / 355 at its centre +(-2, -10)
  and +(-2, 2) (object state 0xb), and the cursor animation at (0x58, 0x6c)
  (state 0xc). The port draws the lists on a white window instead of the
  original list screens (0x1002bd20 ...), not decoded yet.

## Screens that replaced the port's text panels

- Help pop-up (0x1005ad94): the letter paper of image 0x4a (frames
  0x1009a3a0 / 0x1008e698: 0x10 and the edges 8 / 3 / 4 / 0xc), the
  secretary (image 0x4e + secretary * 2, frames 0x1009a7a8 / a8c0 / a9b0 /
  aaa0 / ab90) at 0x84, 0xa0, scroll marks (frames 0x100a0a24, 0x1c.. up at
  0x97, 0x1e, 0x19.. down at 0x97, 0x6c), the text box at 0x10, 0x14
  (0x95 x 0x7a, a native rich text editor in the original) and the
  kingdom's name (game+0x544, from name.dat) at 0x40, 0xc1.
- Quest spot (0x100525ac / talk 0x100535ec): background 0x59, window rows
  y 0x68..0xcc (frames 0x1009adc8), the leader at 0x28, 100, the quest giver
  (image 0x1d, frames 0x1009c058, animation by area from 0x1009acf4), the
  talk mark (animation 0x1009ada0) at 0x88, 0x32; FUN_10052ff8 draws eight
  lines at 5, 0x78 + 0xb * i (item A red, item B green).
- Attack / Scout targets (0x10043060): a 4 x 3 grid of cells at
  0x1c + 0x28 * col, 0x2d + 0x28 * row in the order spots, rival kingdoms,
  enemy bases; spots frame kind + 0x1b of image 0x1b (frames 0x10095678),
  castles of image 0x45 (frames 0x10095db0: bases 0x10096344[area*4]/100 +
  0x12, rivals in four layers); panel frame 0x93 at 0, 0x9a; the cursor at
  the cell - (0xd, 0xa); left / right wrap in a row, up / down by four.
  FUN_10044b90 writes the panel (spot name and mestext 139 + fights left;
  base: 0x35 unknown, 0x3a field, 0x106 + power, 0x29 + HP).
- Rival scouting / a failed attack: the scout report page (FUN_100590c8)
  with the rival's name in place of mestext 0x38.
- Letters (FUN_10031a54): image 0x4a paper, text field 0x20 (marks
  0x21..0x24, + 5 while not typing), button 0xb / 0xc of 0x1008eaa0, the
  messenger's portrait (frames 0x1008fd30) with mestext 0x152. Reading
  (0x10032114) shows the sender at 0xd, 0xad and 0x150; writing
  (0x10031e9c) 0x151. Letters arrive in an inbox and open when the kingdom
  screen is idle.
- List > Item (0x1003c224): window rows 0xe..0x86 (frames 0x100930c8), the
  kind mark (0x10093064[kind]) at 0x1e, 0x1d, stars at 0x1e + 0x18 + 6 * i,
  0x33; texts of FUN_1003d280 (properties from itemdata.dat sections 0 and
  1, FUN_100627d8).
- List > Unit (0x1005fe98): picture 0xd1 + template byte 0 drawn over the
  background at 0, 0xe (game+0x44, FUN_10070ad8), face (image 0x19, frames
  0x100ab310) at 0x9b, 0x11, stars (byte 1) at 0x33 + 6 * i, 0x36, window
  0xa9 at 0x51, 0x97 with the unit's words (battlemes.dat group 0, byte 14,
  FUN_1000ea00), portrait at 0x98, 0x9c / 0xb4, texts of FUN_10060980.
- Template bytes (FUN_1007a614): 0 -> +0x1bc (face / picture), 1 -> +0x1b8
  (rank), 2 -> +0x1c0, 3 -> +0x1c4, 4 -> +0x1a0, 5 -> +0x194, 6 -> +0x198,
  7..11 -> +0x1a4..+0x1b4, 12..13 price (+0x1dc), 14 -> +0x1d0 (words).
  The scout pages' power (+0x198) is byte 6 and HP (+0x194) byte 5.
- Combine confirmation (0x100342b8): see scene_lab.c; no room shows mestext
  300 / 0x12d, too little loot plays sfx 4, success help 0x1a (word = the
  result), failure help 0x17; the result's properties come from the
  extras' words 2 / 3 (FUN_10062d28).
- Auction: exhibit = dialog 0xb + item list 0x10039b34 + number entry
  (0x1002a544 states 0xd..0x14: field 0xab / 0xaa at 0x25, 0x32, plates at
  0x18, 0x50 / 0x60, labels 0x156 / 8, panel 0x183..0x185; bids 0x159 /
  0x17b, minimum price + 1, help 0x2f unless more than 4999 loot stays);
  the starting value is FUN_1002a4a0(price, 25). Lists follow 0x1002df64
  (plates 0xac / 0xad, rarity bars 0xae..0xb7, status marks 0x1008d340,
  status texts 0x1008d3a8, panel FUN_100310ac).
- Auction item bytes (exhibit 13 @20..26, list entries [5..11]), from
  FUN_10067250: kind, the weapon's mold (kind 5 only, FUN_10063a54: the
  row of the recipe table 0x100abee8 whose recipes make it; else 0xff),
  id, then items: word 3, 0, word 2, rank; equipment: words 3, 4, 2 and
  5. List entries keep +0x70 = byte 25, +0x74 = byte 23, +0x78 = byte 24
  (FUN_1002a240); the rank comes from itemdata.dat (FUN_10029f00). A won
  or returned item gets them back (FUN_10029e64: items words 2 / 3,
  equipment words 2 / 3 / 4).
- Auction list panel (FUN_100310ac): equipment shows mestext 0x15 with
  property slot 1 (mode 1, byte 23) at 0x37, then for kind 7 slot 2
  (mode 1, byte 24) at 0x37, otherwise mestext 0x16 at 0x42 with slot 0
  (mode 0, byte 25) at 0x46.
- Auction item page (0x1002fba8, FUN_10031428): the numbers of kinds
  5 / 6 and the "%" line come from the item's itemdata.dat record, not
  from the item bytes.
- The N-Gage Arena connect screen has no original (the phone's Arena
  dialogs); the port draws it on the title picture (frames 0, 1 and 7 of
  bitmap 7).
- Port: pk_object_call keeps a short stack of return points so a called
  screen can call the generic dialog and still return (the quest and the
  auction did).

## Online notices

- Going offline against the player's will (FUN_100723e0: auction
  timeout, message 255, a lost link) sets game+0x3b8; the mode loop
  (0x1006f3c4) then shows trigger 0x24 once a pop-up can open.
- The pop-up window's idle state (0x1005ad94 state 1) shows trigger 0x37,
  the Scrimmage rules, once while online in that room (game+0x3b4).
- FUN_1007b688 (cost of healing everybody) is what enables the Medical
  cell of the Party menu.

## Pause menu (FUN_1006e17c)

Either soft key opens it in modes 0 and 1 (FUN_10070100), and focus loss
does too. While open (game+0x630) the main loop runs only the pause: the
game objects are put aside (+0x658) and the last frame stays underneath.
Mode 1 shows page 0 (resume / volume / save / title; tables 0x100aee88
layout, 0x100aeec0 mestext ids, 0x100aef9c pausemess records); the
other modes only the plate (image 0x4a frame 0x2a, page 6). Save goes
through FUN_100747b8 / FUN_1007709c; quitting closes the curtain with
+0x58 = 2. Window objects 0x10051a8c (frame 0x10), 0x10051ccc (edges),
0x10051e6c (paper: 0x12 at 0x58,0x91 in the pause, 0x11 at 0x58,0x4c
otherwise), 0x10051d84 (volume bar), 0x10051f64 (secretary, eye and
mouth with offsets 0x1009ac80 / 0x1009aca8).
Port: the right soft key is also Back (Escape, Android back), so on the
kingdom screen it opens the menu only from the main menu.

## Secretary pop-ups (0x1005ad94)

Same window objects with arg0 0: the paper frame 0x11 lies behind the
text, which is a rich text editor (font 0, black) in the rectangle
(0x10, 0x14) - (0x95, 0x7a), the paper's white area; longer texts page.

## Map > Move (0x1004017c)

The root itself is the travelling kingdom (x/y in world coordinates).
Children: areas (0x10, frame 0x30 of 0x100943e8), roads (0x20 shadow
0x808000 one pixel down, 0x21 yellow), the kingdom's look (0x30, image
0x45), the arrow to the chosen neighbour (0x50, frame dir + 0x6b,
offsets 0x10096d20), the top bar ground (0x60) and the area picture
(0x40, image 0x44 frames 0x100959c0). All die when the curtain is held.
Each page has three pictures (page * 3 + 0x1f..0x21) that FUN_1006f1a8
shows in turn by frame (game+0x430 & 15) when bg_visible is 2, scrolled
by camera y within the page (FUN_100413e4 clamps to -0x10..0x38).
Travelling takes 0x16 steps; leaving the page closes the curtain and
rebuilds on the next page. Area names: every fifth record of area.dat.

## After a battle (FUN_1007b0f8)

Called when the result screen closes the curtain. The team's units take
rank, HP and the fallen flag (+0x58) back. Fallen units leave the team
(their f17 is added to team f11). The unit code (FUN_10008d50) rolls when
a unit falls: "out" (frames 0x4a/0x4b, battle +0x984[slot] = 1) keeps the
unit with 1 HP, unassigned; "owned" (0x4c/0x4d) deletes it (FUN_1007ae48).
Either way the fallen unit's equipment is deleted (FUN_100634a4), unless a
surviving unit carries effect 0x30: then each piece is unequipped and kept
on a coin toss. An empty team is reset (FUN_100693a4), otherwise f9 is the
first occupied slot and f10 the highest f14 (FUN_10051a18). Units and
equipment are re-sorted (FUN_1007a018, FUN_10061b4c).
