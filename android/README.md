# Android

One app, PhantomKingdom, with two parts:

- **Start screen** (`MainActivity`, Kotlin, Jetpack Compose): imports the
  game data, holds the PhantomLink server and N-Gage Arena account, and
  starts the game. Its look follows the
  [PhantomLink bridge app](https://github.com/ngagesdk/PhantomLink/tree/main/android).
- **Game** (`PocketKingdomActivity`): the same program as the desktop
  build. The top-level CMake project builds `libSDL3.so` and `libmain.so`
  (the game), and SDL's Java activity (`org.libsdl.app`, taken from the SDL
  source of the version in `PK_SDL3_TAG` at build time) starts it.

The APK contains the program, the font files and the artwork, no game data.

## Start screen

Tabs: **Game** (play, status, import), **Log**, **Settings**.

| Setting | Goes to the game as | Default |
|---|---|---|
| Server address | `--arena-host` (every start) | `188.68.43.183`, also used when the field is empty |
| Arena user name | `--arena-user` (when set) | empty: the game keeps what `arena.cfg` has |
| Arena password | `--arena-pass` (when set) | empty: as above |
| Imported game data | `--data <folder with 6rah.app>` | not passed for data copied over USB |
| Vibrate on touch | `--no-haptics` when off | on |

The game saves host, user and password in `arena.cfg` itself
(`pk_arena_set_account`). The Settings tab shows the game's IMEI from
`identity.cfg` (created by the game on its first start). Restoring a save
backup replaces this identity too. The server test opens a TCP connection
to port 80 (the Arena login) and closes it.

The game runs in its own process (`:game`), which ends when the game
closes (`PocketKingdomActivity.onDestroy`), so the native code's static
state is fresh on every start and the start screen is not affected.

## Game data

Either way works:

- **Import** on the start screen: a ZIP archive or a folder, chosen with
  Android's file picker (no storage permission). Select the original
  `\System\Apps\6RAH\` folder of your Pocket Kingdom game card, the whole
  card, or a ZIP of either; ZIPs can also be opened from a file manager
  ("Open with"). The app finds the folder that contains `6rah.app`
  (case-insensitive, up to 8 levels deep), checks it against the same list
  as `k_required` in `src/core/pk_fs.c` and stores everything under
  `files/game/` in the app's private storage, so `game.id` at the card's
  root stays where `pk_data.c` looks for it.
- **Over USB**, as before: copy the contents of `\System\Apps\6RAH\` into
  `Android/data/de.ngagesdk.phantomkingdom/files/6rah/`. The start screen
  recognises it; the game finds it without `--data`.

Imported data wins over USB data. An import is unpacked into
`files/game.import/` and replaces `files/game/` only when it holds a
complete data set; paths that leave the folder, more than 50 000 files or
more than 4 GB are refused. Saves, `identity.cfg` and `arena.cfg` (also in
`files/`) are never touched, and are included in Android backups; the
imported game data is not (`res/xml/backup_rules.xml`,
`res/xml/data_extraction_rules.xml`).

## Save backups

In **Settings > Save backup**, close the game completely, then choose
**Export save** and save the ZIP somewhere outside the app. Copy it to
another device or cloud storage before uninstalling. To restore it, choose
**Restore save**, confirm replacement, and select the ZIP. Both `save.dat`
and `identity.cfg` are restored, so the N-Gage ID and its PhantomLink
account binding move with the save. Arena credentials, settings, and game
data are not included. Export the current save first if you want to keep it.

The ZIP contains encrypted save data with its salt appended and the matching
`identity.cfg`. Because the ID is included alongside the save, this does not
provide confidentiality or prevent tampering; keep the backup private. Only
backups containing encrypted saves are accepted. To import an original N-Gage
save, select `save.dat` with **Restore save**, select the matching four-byte
`C:\System\Shareddata\101fd3dd.ini` from the same phone, and enter that
phone's 15-digit IMEI. The app decrypts and checks the save before replacing
the current save and identity. The IMEI cannot be recovered from the files.
A ZIP backup is checked and restored with its saved identity without prompting.

To move a save to a phone, choose **Export for N-Gage**. Extract the ZIP and
copy `save.dat` to the phone's `System\Apps\6RAH` save folder and
`101fd3dd.ini` to `C:\System\Shareddata`. The phone's IMEI must match the
ID stored in the app. Export does not change the app's identity. Keep separate
backups of both original phone files before replacing them. Android backup may
also preserve saves and identity, subject to device backup settings.

## Controls

Touch: tap a menu entry, list row or button to choose it, swipe to scroll,
long press or two-finger tap (or the back button) to go back. Gamepads and
keyboards work as on the desktop. See [docs/USAGE.md](../docs/USAGE.md).

## Building

Requirements: the Android SDK with platform 36, NDK and an SDK CMake 3.24 or
newer, JDK 17 or newer, and network access on the first build (Gradle,
AndroidX/Compose, SDL and SDL_net sources).

    cd android
    ./gradlew assembleDebug -Ppk.cmakeVersion=3.31.6

`-Ppk.cmakeVersion` names the SDK CMake to use (default 3.31.6). With
`-Ppk.sdlSource=/path/to/SDL` the SDL Java files come from a local SDL
checkout of the same version instead of the release archive on GitHub.

The debug APK is in `app/build/outputs/apk/debug/`.

## Release signing

Create `android/keystore.properties` (not committed):

    storeFile=upload.jks
    storePassword=...
    keyAlias=...
    keyPassword=...

The CI workflow writes this file from the repository secrets
`ANDROID_KEYSTORE_BASE64` (the keystore, base64), `ANDROID_KEYSTORE_PASSWORD`,
`ANDROID_KEY_ALIAS` and `ANDROID_KEY_PASSWORD`. Without them the release APK
is unsigned and only the debug APK installs.

For Google Play, build a bundle with `./gradlew bundleRelease` and follow
[store/PLAY_CONSOLE.md](store/PLAY_CONSOLE.md). Privacy policy:
[PRIVACY.md](PRIVACY.md).

## Artwork and store assets

Everything is made from the two provided PhantomKingdom images by
`tools/make_assets.py`; no new artwork:

| Output | From |
|---|---|
| `res/drawable-nodpi/logo.png` (start screen header) | the wordmark; a transparent gap in its outline is filled black |
| adaptive icon foreground | the ghost-and-wordmark sticker, cut out along its outline, sized to the 66 dp safe zone, soft shadow |
| adaptive icon background | a plain patch of the cloth above the sticker |
| themed (monochrome) icon | the sticker's letters, ghost and speed lines |
| `mipmap-*/ic_launcher.png`, `ic_launcher_round.png` | both layers flattened, for Android 7 (minSdk 24) |
| store `icon.png` 512 x 512, `ic_launcher-playstore.png` | the square icon image, scaled |
| store `featureGraphic.png` 1024 x 500 | wordmark and tagline, laid out like PhantomLink's |

    python3 tools/make_assets.py phantom-kingdom-icon.png wordmark.png .
    python3 tools/make_screens.py fastlane/metadata/android/en-US/images/phoneScreenshots

The screenshots are HTML renders of the start screen (same texts, colours,
layout). Replace them with real captures, including the game itself,
before publishing (`adb exec-out screencap -p > shot.png`). Store texts
(en-US, de-DE) are in `fastlane/metadata/android/`.

## Layout

| Folder or file | Content |
|---|---|
| `app/src/main/java/de/ngagesdk/phantomkingdom/PocketKingdomActivity.java` | the game (SDL) |
| `app/src/main/java/de/ngagesdk/phantomkingdom/*.kt` | start screen: import (`GameData`), settings, launch (`GameLauncher`), server test, log |
| `app/src/main/java/de/ngagesdk/phantomkingdom/ui/` | start screen user interface |
| `app/src/main/res/` | icons, logo, theme, backup rules |
| `fastlane/metadata/android/` | store texts, icon, feature graphic, screenshots |
| `store/PLAY_CONSOLE.md` | Play Console walkthrough |
| `tools/` | asset and screenshot generators (Python, Pillow, SciPy, Playwright) |
