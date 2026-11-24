/* SPDX-License-Identifier: MIT */
#include "core/pk_fs.h"
#include "core/pk_log.h"

#include <SDL3/SDL.h>

static char g_data_dir[PK_PATH_MAX];
static char g_pref_dir[PK_PATH_MAX];
static pk_language g_lang = PK_LANG_ENGLISH;

static const char *const k_lang_dirs[PK_LANG_COUNT] = {
    "english", "french", "german", "italian", "spanish"
};

/* The minimum a data set needs before the engine will start. The list
 * only names files; it carries none of their content. */
static const char *const k_required[] = {
    "6rah.app",
    "pokekin.mbm",
    "pokeport.mbm",
    "mestext.dat",
    "unit.dat",
    "itemdata.dat",
    "mapinfo.dat",
    "partydata.dat",
    "unitcol.dat",
    "name.dat",
    "bgm01.pcm",
    "sys01.pcm",
    "english/mestext.dat",
};

/* ------------------------------------------------------------------ */

typedef struct find_ctx
{
    const char *want;
    char found[256];
    bool ok;
} find_ctx;

static SDL_EnumerationResult SDLCALL find_cb(void *ud, const char *dirname, const char *fname)
{
    find_ctx *c = (find_ctx *)ud;
    (void)dirname;
    if (SDL_strcasecmp(fname, c->want) == 0) {
        SDL_strlcpy(c->found, fname, sizeof(c->found));
        c->ok = true;
        return SDL_ENUM_SUCCESS;
    }
    return SDL_ENUM_CONTINUE;
}

static void join(char *out, size_t cap, const char *a, const char *b)
{
    size_t n = SDL_strlen(a);
    if (n && (a[n - 1] == '/' || a[n - 1] == '\\')) {
        SDL_snprintf(out, cap, "%s%s", a, b);
    } else {
        SDL_snprintf(out, cap, "%s/%s", a, b);
    }
}

/* Resolves rel (with '/' or '\\' separators) below base, matching every
 * component case-insensitively so data copied from a FAT card works on
 * case-sensitive file systems. */
static bool resolve_in(const char *base, const char *rel, char *out, size_t cap)
{
    char cur[PK_PATH_MAX], part[256];
    const char *p = rel;

    SDL_strlcpy(cur, base, sizeof(cur));
    while (*p) {
        size_t n = 0;
        SDL_PathInfo info;
        char direct[PK_PATH_MAX];

        while (*p == '/' || *p == '\\') {
            p++;
        }
        while (p[n] && p[n] != '/' && p[n] != '\\' && n < sizeof(part) - 1) {
            part[n] = p[n];
            n++;
        }
        part[n] = '\0';
        p += n;
        if (!n) {
            break;
        }
        join(direct, sizeof(direct), cur, part);
        if (SDL_GetPathInfo(direct, &info)) {
            SDL_strlcpy(cur, direct, sizeof(cur));
        } else {
            find_ctx c;
            SDL_zero(c);
            c.want = part;
            SDL_EnumerateDirectory(cur, find_cb, &c);
            if (!c.ok) {
                return false;
            }
            join(direct, sizeof(direct), cur, c.found);
            SDL_strlcpy(cur, direct, sizeof(cur));
        }
    }
    SDL_strlcpy(out, cur, cap);
    return true;
}

/* Strips "e:\System\Apps\6RAH\" style prefixes of the original paths. */
static const char *strip_symbian(const char *rel)
{
    const char *k = "\\system\\apps\\6rah\\";
    const char *p = rel;
    size_t i, kn = SDL_strlen(k);

    if (p[0] && p[1] == ':') {
        p += 2;
    }
    for (i = 0; i < kn && p[i]; i++) {
        char a = (char)SDL_tolower((unsigned char)p[i]);
        char b = k[i];
        if (a == '/') {
            a = '\\';
        }
        if (a != b) {
            return rel;
        }
    }
    return i == kn ? p + kn : rel;
}

int pk_fs_check_dir(const char *dir, char *out, size_t cap)
{
    size_t i;
    int missing = 0;
    char tmp[PK_PATH_MAX];

    if (out && cap) {
        out[0] = '\0';
    }
    for (i = 0; i < SDL_arraysize(k_required); i++) {
        if (!resolve_in(dir, k_required[i], tmp, sizeof(tmp))) {
            missing++;
            if (out && cap) {
                SDL_strlcat(out, "  ", cap);
                SDL_strlcat(out, k_required[i], cap);
                SDL_strlcat(out, "\n", cap);
            }
        }
    }
    return missing;
}

static bool try_dir(const char *dir)
{
    char missing[512];
    if (!dir || !*dir) {
        return false;
    }
    if (pk_fs_check_dir(dir, missing, sizeof(missing)) == 0) {
        SDL_strlcpy(g_data_dir, dir, sizeof(g_data_dir));
        PK_INFO("data: using '%s'", dir);
        return true;
    }
    PK_DEBUG("data: '%s' is not a complete data set", dir);
    return false;
}

bool pk_fs_init(const char *explicit_dir)
{
    static const char *const k_sub[] = {
        "",
        "data",
        "6rah",
        "6RAH",
        "mmc/system/apps/6rah",
        "e/System/Apps/6RAH",
        "../mmc/system/apps/6rah",
        "../data",
        "../../mmc/system/apps/6rah",
    };
    const char *bases[4];
    char path[PK_PATH_MAX];
    char *pref;
    size_t b, s;

    g_data_dir[0] = '\0';
    pref = SDL_GetPrefPath("ngagesdk", "PocketKingdom");
    if (pref) {
        SDL_strlcpy(g_pref_dir, pref, sizeof(g_pref_dir));
        SDL_free(pref);
    }

    if (explicit_dir) {
        if (try_dir(explicit_dir)) {
            return true;
        }
        for (s = 1; s < SDL_arraysize(k_sub); s++) {
            join(path, sizeof(path), explicit_dir, k_sub[s]);
            if (resolve_in(explicit_dir, k_sub[s], path, sizeof(path)) && try_dir(path)) {
                return true;
            }
        }
        return false;
    }
    if (try_dir(SDL_getenv("PK_DATA"))) {
        return true;
    }
    bases[0] = SDL_GetBasePath();
    bases[1] = ".";
    bases[2] = g_pref_dir;
#ifdef SDL_PLATFORM_ANDROID
    /* the app's folder on the shared storage, reachable over USB:
     * Android/data/<package>/files */
    bases[3] = SDL_GetAndroidExternalStoragePath();
#else
    bases[3] = NULL;
#endif
    for (b = 0; b < SDL_arraysize(bases); b++) {
        if (!bases[b] || !*bases[b]) {
            continue;
        }
        for (s = 0; s < SDL_arraysize(k_sub); s++) {
            if (!*k_sub[s]) {
                if (try_dir(bases[b])) {
                    return true;
                }
            } else if (resolve_in(bases[b], k_sub[s], path, sizeof(path)) && try_dir(path)) {
                return true;
            }
        }
    }
    return false;
}

void pk_fs_quit(void)
{
    g_data_dir[0] = '\0';
}

const char *pk_fs_data_dir(void) { return g_data_dir; }
const char *pk_fs_pref_dir(void) { return g_pref_dir; }
void pk_fs_set_language(pk_language lang)
{
    if (lang < PK_LANG_COUNT) {
        g_lang = lang;
    }
}
pk_language pk_fs_language(void) { return g_lang; }
const char *pk_fs_language_dir(pk_language lang) { return lang < PK_LANG_COUNT ? k_lang_dirs[lang] : k_lang_dirs[0]; }

bool pk_fs_resolve(const char *rel, char *out, size_t cap)
{
    if (!g_data_dir[0]) {
        return false;
    }
    return resolve_in(g_data_dir, strip_symbian(rel), out, cap);
}

static void *load_abs(const char *path, size_t *len)
{
    size_t n = 0;
    void *p = SDL_LoadFile(path, &n);
    if (len) {
        *len = p ? n : 0;
    }
    return p;
}

void *pk_fs_load(const char *rel, size_t *len)
{
    char path[PK_PATH_MAX];
    if (!pk_fs_resolve(rel, path, sizeof(path))) {
        PK_WARN("data: '%s' not found", rel);
        if (len) {
            *len = 0;
        }
        return NULL;
    }
    return load_abs(path, len);
}

void *pk_fs_load_localized(const char *name, size_t *len)
{
    char rel[PK_PATH_MAX], path[PK_PATH_MAX];
    SDL_snprintf(rel, sizeof(rel), "%s/%s", k_lang_dirs[g_lang], name);
    if (pk_fs_resolve(rel, path, sizeof(path))) {
        return load_abs(path, len);
    }
    return pk_fs_load(name, len);
}

void *pk_fs_load_pref(const char *name, size_t *len)
{
    char path[PK_PATH_MAX];
    if (!g_pref_dir[0]) {
        return NULL;
    }
    join(path, sizeof(path), g_pref_dir, name);
    return load_abs(path, len);
}

bool pk_fs_save_pref(const char *name, const void *data, size_t len)
{
    char path[PK_PATH_MAX], tmp[PK_PATH_MAX];
    if (!g_pref_dir[0]) {
        return false;
    }
    join(path, sizeof(path), g_pref_dir, name);
    SDL_snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    if (!SDL_SaveFile(tmp, data, len)) {
        return false;
    }
    SDL_RemovePath(path);
    return SDL_RenamePath(tmp, path);
}
