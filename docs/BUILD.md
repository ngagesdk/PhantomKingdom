# Building

Requirements: CMake 3.24+, a C99 compiler, Git. SDL3 and SDL3_net are
downloaded and built automatically (FetchContent).

    cmake -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build --config Release
    ctest --test-dir build -C Release

## Platforms

- Windows: Visual Studio 2019+ or MinGW-w64.
- macOS: Xcode command line tools.
- Linux: SDL3's build dependencies for your desktop
  (https://wiki.libsdl.org/SDL3/README-linux#build-dependencies), e.g. on
  Debian/Ubuntu `libx11-dev libxext-dev libxcursor-dev libxi-dev
  libxrandr-dev libxss-dev libwayland-dev libxkbcommon-dev
  libasound2-dev libpulse-dev`.
- Android: a Gradle project in `android/` builds an APK from the same
  sources, see [android/README.md](../android/README.md).

## Options

- `-DPK_USE_SYSTEM_SDL=ON` uses installed SDL3/SDL3_net packages instead.
- `-DPK_ENABLE_ASSET_VIEWER=ON` adds the port-only asset viewer (`--viewer`).
- Debug builds (also the Android debug APK) unlock the N-Gage Arena entry
  on the title screen without the progress the original asks for, to
  test online play; Release builds keep the original rule.
  `-DPK_DEBUG_UNLOCK_ARENA=OFF` turns this off.

The build copies `assets/` next to the executable; `cmake --install` puts
the executable, `assets/` and the license files into one folder.

## Continuous integration

`.github/workflows/continuous-integration.yml` builds and tests Linux and
Windows and builds the Android APKs; the packages and APKs are attached to
each run as artifacts.

## Source layout

    src/core      logging, files, text, random numbers, 6RAH.APP tables
    src/gfx       MBM decoder, frame buffer and image cache, bitmap font
    src/audio     music and sound effects
    src/input     keypad mapping (keyboard, gamepad), touch and mouse gestures
    src/engine    game object, main loop, task objects, text queue
    src/game      modes and objects of the game, data tables, asset viewer
    src/net       identity, Arena login, SNAP
    src/platform  window and presentation
    tests         unit tests (no game data needed)
    android       Android app (Gradle; SDL's Java activity + the CMake project)
    tools/re      reverse engineering scripts (work on your own copy)
    assets/fonts  the two built-in fonts as PNG atlases + BMFont .fnt files
    docs          documentation
