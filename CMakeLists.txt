cmake_minimum_required(VERSION 3.24)

project(PhantomLink
    VERSION 1.1.0
    DESCRIPTION "Clean-room style C99 reimplementation of the Pocket Kingdom engine (bring your own game data)"
    LANGUAGES C)

# C99 is applied to this project's targets only (pk_set_c99 below), never
# globally: SDL3_net needs POSIX/GNU declarations that -std=c99 would hide.
function(pk_set_c99 target)
    set_target_properties(${target} PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
endfunction()

option(PK_BUILD_TESTS     "Build the unit tests"                    ON)
option(PK_USE_SYSTEM_SDL  "Use an installed SDL3/SDL3_net instead of FetchContent" OFF)
option(PK_WARNINGS_AS_ERRORS "Treat compiler warnings as errors"    OFF)
option(PK_ENABLE_ASSET_VIEWER "Build the port-only asset viewer (--viewer)" OFF)
# Debug builds (also the Android debug APK) unlock the N-Gage Arena entry on
# the title screen without the progress the original asks for, for testing
# online play. Release builds keep the original rule.
option(PK_DEBUG_UNLOCK_ARENA "Unlock N-Gage Arena in Debug builds"     ON)

set(PK_SDL3_TAG     "release-3.4.8" CACHE STRING "SDL3 git tag fetched by FetchContent")
set(PK_SDL3NET_TAG  "release-3.2.0"  CACHE STRING "SDL3_net git tag fetched by FetchContent")

include(FetchContent)

if(PK_USE_SYSTEM_SDL)
    find_package(SDL3 REQUIRED CONFIG)
    find_package(SDL3_net REQUIRED CONFIG)
else()
    if(ANDROID)
        # SDL3 as libSDL3.so next to libmain.so: the two libraries SDL's
        # Java activity loads (android/)
        set(SDL_SHARED ON  CACHE BOOL "" FORCE)
        set(SDL_STATIC OFF CACHE BOOL "" FORCE)
    else()
        set(SDL_SHARED OFF CACHE BOOL "" FORCE)
        set(SDL_STATIC ON  CACHE BOOL "" FORCE)
    endif()
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    set(SDL_TESTS OFF CACHE BOOL "" FORCE)
    set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(SDL3
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG        ${PK_SDL3_TAG}
        GIT_SHALLOW    TRUE
        GIT_PROGRESS   TRUE)
    FetchContent_MakeAvailable(SDL3)

    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(SDLNET_INSTALL OFF CACHE BOOL "" FORCE)
    set(SDLNET_SAMPLES OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(SDL3_net
        GIT_REPOSITORY https://github.com/libsdl-org/SDL_net.git
        GIT_TAG        ${PK_SDL3NET_TAG}
        GIT_SHALLOW    TRUE
        GIT_PROGRESS   TRUE)
    FetchContent_MakeAvailable(SDL3_net)
endif()

# ---------------------------------------------------------------------------
# Engine library: everything except main(), so tests can link it.
# ---------------------------------------------------------------------------
set(PK_CORE_SOURCES
    src/core/pk_log.c
    src/core/pk_fs.c
    src/core/pk_blowfish.c
    src/core/pk_text.c
    src/core/pk_rand.c
    src/core/pk_app.c
    src/gfx/pk_mbm.c
    src/gfx/pk_gfx.c
    src/gfx/pk_font.c
    src/audio/pk_audio.c
    src/input/pk_input.c
    src/input/pk_haptics.c
    src/engine/pk_object.c
    src/engine/pk_textq.c
    src/engine/pk_game.c
    src/game/pk_data.c
    src/game/pk_world.c
    src/game/pk_scenes.c
    src/game/scene_boot.c
    src/game/scene_opening.c
    src/game/scene_field.c
    src/game/scene_shop.c
    src/game/scene_form.c
    src/game/scene_lists.c
    src/game/scene_map.c
    src/game/pk_battle.c
    src/game/pk_battle_unit.c
    src/game/ui_dialog.c
    src/game/pk_rules.c
    src/game/pk_help.c
    src/game/pk_pause.c
    src/game/scene_list.c
    src/game/scene_lab.c
    src/game/scene_ending.c
    src/game/scene_quest.c
    src/game/scene_attract.c
    src/game/scene_arena.c
    src/game/scene_auction.c
    src/game/scene_viewer.c
    src/net/pk_identity.c
    src/net/pk_snap.c
    src/net/pk_online.c
    src/net/pk_arena.c
    src/platform/pk_platform.c
)

add_library(pkcore STATIC ${PK_CORE_SOURCES})
target_include_directories(pkcore PUBLIC src)
pk_set_c99(pkcore)
if(PK_ENABLE_ASSET_VIEWER)
    target_compile_definitions(pkcore PUBLIC PK_ENABLE_ASSET_VIEWER=1)
endif()
# Fallback for runs from an IDE or the build tree: the font files in the
# source tree. Release packages carry assets/ next to the executable.
target_compile_definitions(pkcore PRIVATE PK_SOURCE_ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets")
target_link_libraries(pkcore PUBLIC SDL3::SDL3 SDL3_net::SDL3_net)
if(PK_DEBUG_UNLOCK_ARENA)
    target_compile_definitions(pkcore PRIVATE $<$<CONFIG:Debug>:PK_DEBUG_UNLOCK_ARENA=1>)
endif()

if(MSVC)
    target_compile_options(pkcore PRIVATE /W4)
    target_compile_definitions(pkcore PUBLIC _CRT_SECURE_NO_WARNINGS)
    if(PK_WARNINGS_AS_ERRORS)
        target_compile_options(pkcore PRIVATE /WX)
    endif()
else()
    target_compile_options(pkcore PRIVATE -Wall -Wextra -Wshadow -Wstrict-prototypes -Wno-unused-parameter)
    if(PK_WARNINGS_AS_ERRORS)
        target_compile_options(pkcore PRIVATE -Werror)
    endif()
endif()

if(WIN32)
    target_link_libraries(pkcore PUBLIC bcrypt)
endif()

if(ANDROID)
    # SDL's Java activity loads libmain.so and calls SDL_main (android/);
    # the font files go into the APK's assets.
    add_library(main SHARED src/main.c)
    target_link_libraries(main PRIVATE pkcore)
    pk_set_c99(main)
    return()
endif()

add_executable(PhantomLink src/main.c src/icon.rc)
target_link_libraries(PhantomLink PRIVATE pkcore)
pk_set_c99(PhantomLink)
if(WIN32)
    set_target_properties(PhantomLink PROPERTIES WIN32_EXECUTABLE ON)
endif()

# Ship assets/ (the font files) next to the executable.
add_custom_command(TARGET PhantomLink POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:PhantomLink>/assets"
    COMMENT "Copying assets")

include(GNUInstallDirs)
install(TARGETS PhantomLink RUNTIME DESTINATION .)
install(DIRECTORY assets/ DESTINATION assets)
install(FILES README.md LICENSE.md docs/THIRD_PARTY.md DESTINATION .)

if(PK_BUILD_TESTS)
    enable_testing()
    add_executable(pk_tests tests/test_main.c)
    target_link_libraries(pk_tests PRIVATE pkcore)
    pk_set_c99(pk_tests)
    add_test(NAME pk_tests COMMAND pk_tests)
endif()
