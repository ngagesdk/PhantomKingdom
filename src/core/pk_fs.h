/* SPDX-License-Identifier: MIT */
#ifndef PK_FS_H
#define PK_FS_H

#include <stdbool.h>
#include <stddef.h>

/* Game data is never bundled. The user points the program at the
 * contents of the game's own installation folder (the directory that
 * holds pokekin.mbm, unit.dat, bgm01.pcm, ... on their memory card, in
 * the original \System\Apps\6RAH\). */

#define PK_PATH_MAX 1024

typedef enum pk_language
{
    PK_LANG_ENGLISH,
    PK_LANG_FRENCH,
    PK_LANG_GERMAN,
    PK_LANG_ITALIAN,
    PK_LANG_SPANISH,
    PK_LANG_COUNT
} pk_language;

/* Tries the explicit path first (may be NULL), then the environment
 * variable PK_DATA and a list of conventional locations. Returns true and
 * remembers the directory when a valid data set was found. */
bool pk_fs_init(const char *explicit_dir);
void pk_fs_quit(void);

const char *pk_fs_data_dir(void);
const char *pk_fs_pref_dir(void);

/* Lists files that a valid data set must contain but that are missing.
 * Writes a human-readable list into out. Returns the number missing. */
int pk_fs_check_dir(const char *dir, char *out, size_t cap);

void pk_fs_set_language(pk_language lang);
pk_language pk_fs_language(void);
const char *pk_fs_language_dir(pk_language lang);

/* Loads a file relative to the data directory, case-insensitively.
 * Accepts the original Symbian spelling ("e:\System\Apps\6RAH\BGM01.pcm")
 * too. Returned memory is owned by the caller (SDL_free). */
void *pk_fs_load(const char *rel, size_t *len);

/* Like pk_fs_load, but tries <language>/<name> first, then <name>. */
void *pk_fs_load_localized(const char *name, size_t *len);

bool pk_fs_resolve(const char *rel, char *out, size_t cap);

/* Files in the per-user preference directory (save games, identity). */
void *pk_fs_load_pref(const char *name, size_t *len);
bool pk_fs_save_pref(const char *name, const void *data, size_t len);

#endif
