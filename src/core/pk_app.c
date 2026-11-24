/* SPDX-License-Identifier: MIT */
#include "core/pk_app.h"
#include "core/pk_fs.h"
#include "core/pk_log.h"

#include <SDL3/SDL.h>

#define E32_SIGNATURE 0x434F5045u /* "EPOC" */

/* Builds the engine knows the addresses of. Others are refused, because
 * every table address in the engine is specific to one build. */
static const pk_app_version k_versions[] = {
    { 0x101FD3DDu, 0x19024F41u, "Pocket Kingdom v. 110804 (multi-language)" },
};

static struct
{
    Uint8 *file;
    size_t size;
    const Uint8 *code;
    Uint32 code_size;
    const pk_app_version *ver;
} P;

static Uint32 rd32(const Uint8 *p)
{
    return (Uint32)p[0] | ((Uint32)p[1] << 8) | ((Uint32)p[2] << 16) | ((Uint32)p[3] << 24);
}

bool pk_app_load(void)
{
    Uint32 uid3, sig, cksum, code_size, code_base, code_off;
    size_t i;

    pk_app_unload();
    P.file = (Uint8 *)pk_fs_load("6rah.app", &P.size);
    if (!P.file || P.size < 0x7c) {
        PK_ERROR("app: 6rah.app not found in the data directory");
        pk_app_unload();
        return false;
    }
    uid3 = rd32(P.file + 0x08);
    sig = rd32(P.file + 0x10);
    cksum = rd32(P.file + 0x18);
    code_size = rd32(P.file + 0x30);
    code_base = rd32(P.file + 0x4c);
    code_off = rd32(P.file + 0x64);
    if (sig != E32_SIGNATURE || code_base != PK_APP_CODE_BASE || (size_t)code_off + code_size > P.size) {
        PK_ERROR("app: 6rah.app is not an uncompressed EPOC executable");
        pk_app_unload();
        return false;
    }
    for (i = 0; i < SDL_arraysize(k_versions); i++) {
        if (k_versions[i].uid3 == uid3 && k_versions[i].code_checksum == cksum) {
            P.ver = &k_versions[i];
        }
    }
    if (!P.ver) {
        PK_ERROR("app: unsupported build (uid3 0x%08x, code checksum 0x%08x)", uid3, cksum);
        pk_app_unload();
        return false;
    }
    P.code = P.file + code_off;
    P.code_size = code_size;
    PK_INFO("app: %s", P.ver->name);
    return true;
}

void pk_app_unload(void)
{
    SDL_free(P.file);
    SDL_zero(P);
}

bool pk_app_loaded(void) { return P.code != NULL; }
const pk_app_version *pk_app_version_info(void) { return P.ver; }

const Uint8 *pk_app_ptr(Uint32 addr, size_t len)
{
    Uint32 off;
    if (!P.code || addr < PK_APP_CODE_BASE) {
        return NULL;
    }
    off = addr - PK_APP_CODE_BASE;
    if (off > P.code_size || len > P.code_size - off) {
        return NULL;
    }
    return P.code + off;
}

Uint32 pk_app_u32(Uint32 addr)
{
    const Uint8 *p = pk_app_ptr(addr, 4);
    return p ? rd32(p) : 0;
}

Sint32 pk_app_i32(Uint32 addr)
{
    return (Sint32)pk_app_u32(addr);
}

bool pk_app_frame(Uint32 table, int index, pk_frame *out)
{
    const Uint8 *p;
    if (!table || index < 0) {
        return false;
    }
    p = pk_app_ptr(table + (Uint32)index * 0x18u, 0x18);
    if (!p) {
        return false;
    }
    out->sx = (Sint32)rd32(p);
    out->sy = (Sint32)rd32(p + 4);
    out->w = (Sint32)rd32(p + 8);
    out->h = (Sint32)rd32(p + 12);
    out->ox = (Sint32)rd32(p + 16);
    out->oy = (Sint32)rd32(p + 20);
    return true;
}
