# Implementation status

Status as of November 2026. The complete offline game is playable, and
N-Gage Arena through PhantomLink works (tested with two players).

| Area | State |
|---|---|
| Data loading: MBM bitmaps and masks, PCM audio, UTF-16 text tables, layout tables from `6RAH.APP` | done |
| Engine: 30 ms timer with the original 3-tick frame, task objects, sprite/line/rect renderer, text queue, curtain transitions, persistent back buffer | done |
| Start-up: Nokia copyright (per language), SEGA logo, Now Loading, title screen with its original intro animation and music | done |
| Title menus as in the original: Offline Play, N-Gage Arena, Options (volume, credits, language), Quit; New Game / Continue / Delete Save File sub-menus and confirmations | done |
| Text in bold and plain 12 px fonts, placed like the original | done |
| New game: kingdom set-up (name, loot, points) and the scrolling opening story | done |
| Kingdom screen: castle background, top bar (level, loot, points in the game's bitmap digits), sliding 2x2 menu with icons, Party and Lab sub-menus, story-based locking of entries | done |
| Save games: the original save.dat layout with CRC check (unencrypted), Continue, Delete Save File, damaged-save message, import of a save from the phone | done |
| Shop: Buy / Sell menu (Arena-only cells blank offline); buying and selling items, equipment and units | done |
| Party > Form: team list, team editor (units, equipment, targeting, order, positions), unit and item lists | done |
| Party > Medical (heal all / party / unit) and Defense (defense party, strategy, battlefield); enemy invasions | done |
| Map: areas, travel (also by tapping the map), Area Search, Scout, quests, attacks on spots and bases, mission events with timers and reports, area bosses, area unlocking | done |
| Lab: Combine, Vault, Rank Up, Upgrade; Party > List with item and unit pages | done |
| Battles: setup from partydata.dat, all 49 unit classes with their own movement, attacks, projectiles, special moves and camera work, summons, statuses, equipment properties, targeting, orders, hit chance, damage with field and element bonuses, time limit, rewards (loot, VP, levels, drops) | done |
| Help pop-ups (the secretary's letters), ending, staff roll, attract mode | done |
| Online: synthetic IMEI, Community SOAP login, SNAP transport/login, room, keep-alive, lost-connection notice | done |
| Online: rival kingdoms, scouting and attacks against other players, defending against them, letters (chat), Global Auction (bid, exhibit, collect) | done |
| Asset viewer (port-only tool, off by default: `-DPK_ENABLE_ASSET_VIEWER=ON`) | done |

## Known differences

- Quest spots: trade quests work; the online-only quest kinds are not used.
- Event kinds 7..11 (N-Gage Arena) are not created: the defender's game
  answers scouting and attacks without its own kinds 9 / 7, and kind 8
  (the "False alarm" report) is not started.
- Online message 22 (a battle result the original builds after a battle
  against a rival) is not sent; PhantomLink does not expect it.
- In Lab > Upgrade, touch-selecting an upgrade asks for Yes/No confirmation
  before spending loot; the original does not. Keyboard selection retains the
  original immediate-purchase behavior.
- The Upgrade screen and the detail screens 0x1003c224 / 0x1005fc0c are
  drawn as text.
- The pop-up function does not check game+0x5c / +0x140, and area pop-ups
  are remembered in popups.dat instead of +0x174 / +0x178.
- Several screens (the Global Auction among them) are drawn as text panels
  instead of the original artwork.

See [NETWORK.md](NETWORK.md) for the online parts in detail and
[RE_NOTES.md](RE_NOTES.md) for what is known about the original program.
