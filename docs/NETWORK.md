# Online play with PhantomLink

The original game reaches N-Gage Arena through the Arena DLLs: a SOAP
login (Community service, TCP 80), SegaChat (Jabber, TCP 5222) and SNAP
(UDP 9090) with a Pocket Kingdom game server behind the room. PhantomLink
(https://github.com/ngagesdk/PhantomLink) emulates all of them; with
`direct = 1` its `gamecommsd` accepts these connections from any client,
which is how the port connects.

## Status

| Part | State |
|---|---|
| Synthetic IMEI (`net/pk_identity.c`) | done |
| Community login, `authenticateUser2` (`net/pk_arena.c`) | done, `--arena-test` |
| SNAP transport: framing, reliability, login (`net/pk_snap.c`) | done |
| SNAP lobby/room flow: Play Now "Scrimmage" or "Ranked" (lobby and room of that name) (`net/pk_online.c`) | done |
| Title "N-Gage Arena": connect screen, kingdom played online (`game/scene_arena.c`) | done |
| Game messages: online (1), keep alive (2), offline (3), moved (5), area search (6 / 134) | done |
| Rival kingdoms: rival bases from the area search (6 / 134), scout (7 / 135), attack (9 / 137, 10 and its echo, seeded battle against the defender's party), "nobody home" / "moved" reports | done |
| Answering other players' scouting and attacks with the defense party | done |
| Chat (18 / 146): Map > Search > Chat, typed on the keyboard; incoming messages pop up | done |
| Global Auction: Shop > Global Auction while online; bid (14 / 142, 15 / 143), exhibit (13 / 141), check exhibits and bids with collection of sales, won items and refunds (16 / 144) (`game/scene_auction.c`) | done |
| Defence: an attack (9) is answered with the defense party (137) and kept; the attacker's 10 starts the battle in the defender's game against the attacker's units, announced by trigger034 | done |
| SegaChat presence | to do |

## Identity

PhantomLink binds an account to the IMEI sent with the first Community
login and afterwards accepts the account only from that IMEI (one account
per IMEI). The port generates one IMEI per installation:

- 15 digits with a valid Luhn check digit;
- prefix `00` (reporting body 00 is reserved for test IMEIs), so it can
  never equal a real N-Gage's IMEI; real phones and PCs share the server's
  account store;
- 12 digits from the operating system's cryptographic random source
  (BCryptGenRandom, arc4random or /dev/urandom), so collisions are
  practically impossible;
- generated once, stored in `identity.cfg` in the preference folder
  (`SDL_GetPrefPath("ngagesdk", "PocketKingdom")`); never derived from
  hardware identifiers.

Copying `identity.cfg` moves the account binding to another computer.
`--imei <15 digits>` uses a given IMEI, for example the one of the N-Gage
an account was created with.

## Playing online

    pocketkingdom --data <dir> --arena-host <server> --arena-user <name> --arena-pass <password>

The account is kept in `arena.cfg` next to the save, so later starts need
no options. Found a kingdom offline first; the title's N-Gage Arena entry
then logs in, enters the room Scrimmage and continues that kingdom
online. An area search also lists the other players' kingdoms in the
area; they appear under Map > Attack and Map > Search > Scout. Their games
answer scouting and attacks themselves, so a rival must be online. While
you are online your own defense party (Party > Defense) answers attacks.

## Testing against a server

    gamecommsd --direct=1            # on the server, see PhantomLink docs
    pocketkingdom --data <dir> --arena-host <server> --arena-test <user> <password>

The log reports the Community login, the SNAP login and the room.
`--frames N --realtime` runs a client headless (each tick about 10 ms
apart), and `--pref DIR` gives each client its own folder for
`save.dat`, `arena.cfg` and `identity.cfg`, so two clients can play
against each other on one machine:

    pocketkingdom --data <dir> --pref alice --arena-host <server> --arena-user alice --arena-pass pw1 --frames 5000 --realtime --keys ...
    pocketkingdom --data <dir> --pref bob --arena-host <server> --arena-user bob --arena-pass pw2 --frames 5000 --realtime --keys ...

## Rival kingdoms

As in the original (details in [RE_NOTES.md](RE_NOTES.md), "Rival bases
and attacks"):

- An online area search sends 6 and ends when the answer 134 is in. Its
  report turns the kingdoms of the answer into rival bases: formation
  slots with word 47 = 1, counted in area word 1, which replace the rival
  bases of the last search.
- Scout sends 7 and Attack sends 9 (with the battle seed in byte 3); the
  mission ends as soon as the answer is in, or after 110 frames. An attack
  answered with the defender's party sends 10 at once and waits for the
  server to send it back.
- Without an answer, or with result 253 / 254, the report says the
  kingdom has moved and its base goes; result 255 ("nobody home") leaves
  the base.
- Both games seed the generator with the attacker's seed before the
  battle. As in the original, the two games can still play the battle out
  differently.
- Going offline (or losing the connection) drops the rival bases and the
  online missions and frees every party; loading a save drops them, too.
