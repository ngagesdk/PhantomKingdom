# Third-party components

| Component | Use | License | Where |
|---|---|---|---|
| SDL 3 | windowing, rendering, audio, input, files | zlib | fetched at build time by CMake (FetchContent) |
| SDL3_net | networking (SNAP over UDP, SOAP over HTTP) | zlib | fetched at build time by CMake (FetchContent) |
| SDL 3 Android Java sources (`org.libsdl.app`) | the Android activity | zlib | fetched at build time by Gradle (`android/app/build.gradle.kts`) |
| Gradle wrapper (`android/gradlew*`, `android/gradle/wrapper/`) | Android build | Apache-2.0 | in this repository |
| Android Gradle plugin, NDK | Android build | Apache-2.0 and others | downloaded by Gradle / the Android SDK |

No part of Pocket Kingdom (code, graphics, sound, text, data tables) is
included in this repository. See [LEGAL.md](LEGAL.md).
