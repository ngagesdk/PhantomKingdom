# Usage

## Your game data

Copy the contents of `\System\Apps\6RAH\` from your copy of the game (the
folder with `6rah.app`, `pokekin.mbm`, `unit.dat`, `bgm01.pcm`, ...) to a
folder of your choice. Optionally copy `game.id` from the card root too.
Supported build: Pocket Kingdom v. 110804 (`6rah.app` uid3 0x101FD3DD).

Optional: `ceurope.gdr`, the Series 60 font store (from the phone's
`Z:\System\Fonts\` or the N-Gage SDK emulator). When it is in the data
folder the game's text is drawn from it; otherwise the font files in
`assets/fonts` are used.

The program looks for the data in this order: `--data <folder>`, the
`PK_DATA` environment variable, then next to the executable and in the
working directory (`.`, `data/`, `mmc/system/apps/6rah/`, ...). Without a
complete data set it shows which files are missing and exits.

On Android the data goes into the app's folder, see
[android/README.md](../android/README.md).

## Save games

Save games use the original `save.dat` layout with its CRC check
(unencrypted). The game saves when the kingdom screen opens. A `save.dat`
copied from the phone is imported when the program is started with that
phone's IMEI: `--imei <15 digits>`.

## Running

    pocketkingdom --data /path/to/6rah

Options: `--lang en|fr|de|it|es`, `--scale N`, `--fullscreen`,
`--audio-rate HZ`, `--no-audio`, `--no-haptics`, `--imei DIGITS`,
`--arena-host HOST --arena-test USER PASSWORD`; `--help` lists all.

## Keyboard

| Key | N-Gage |
|---|---|
| Arrows / WASD | joypad |
| Enter, Space, 5 | 5 / select |
| F1, Q | left soft key (select, like the original) |
| F2, E, Esc | right soft key (back) |
| Backspace | C (clear) |
| 0-9, *, # | keypad |
| F11 / F12 | fullscreen / screenshot |

In battles the Left and Right keys pan the camera, as in the original.

## Gamepad

A select, B right soft key, X/Start left soft key.

## Touch and mouse

| Gesture | Does |
|---|---|
| Tap / left click on a menu entry, list row, button or map cell | moves the cursor there and selects it |
| Tap / click anywhere on a screen without choices (texts, pop-ups) | 5 (continue) |
| Swipe up / down, mouse wheel | scrolls lists (short menus: moves the cursor) |
| Swipe left / right | left / right (pages, values); in battle pans the battlefield |
| Long press, two-finger tap, right click, the phone's back button | right soft key (back) |
| Tap on the letter's paper / on an amount | opens the on-screen keyboard (numbers: a numeric one) |

On the travel map a tap chooses a neighbouring area and a second tap (or a
tap on the panel below) travels there. A tap elsewhere on the map chooses
the neighbour in that direction from the kingdom, also one that is out of
view or on another page.

With touch input the phone gives a short vibration when the game takes a
tap, a swipe step or a back gesture (long press, two-finger tap), so you
know the touch counted; a tap that hits nothing gives none. On Android
this is the system's touch feedback (it follows the "Touch feedback"
setting). Mouse input never vibrates. `--no-haptics` turns it off (Android: start screen,
Settings, "Vibrate on touch").

## Online

See [NETWORK.md](NETWORK.md). Each installation gets a synthetic,
persistent, Luhn-valid IMEI with the test prefix `00`, because PhantomLink
binds accounts to the IMEI.
