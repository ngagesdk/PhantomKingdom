/* SPDX-License-Identifier: MIT */
#include "core/pk_blowfish.h"
#include "core/pk_app.h"

static Uint32 f(const pk_blowfish *bf, Uint32 x)
{
    return ((bf->s[0][x >> 24] + bf->s[1][(x >> 16) & 0xff]) ^ bf->s[2][(x >> 8) & 0xff]) + bf->s[3][x & 0xff];
}

static void enc(const pk_blowfish *bf, Uint32 *l, Uint32 *r)
{
    Uint32 a = *l, b = *r, t;
    int i;
    for (i = 0; i < 16; i++) {
        a ^= bf->p[i];
        b ^= f(bf, a);
        t = a;
        a = b;
        b = t;
    }
    t = a;
    a = b;
    b = t;
    b ^= bf->p[16];
    a ^= bf->p[17];
    *l = a;
    *r = b;
}

static void dec(const pk_blowfish *bf, Uint32 *l, Uint32 *r)
{
    Uint32 a = *l, b = *r, t;
    int i;
    for (i = 17; i > 1; i--) {
        a ^= bf->p[i];
        b ^= f(bf, a);
        t = a;
        a = b;
        b = t;
    }
    t = a;
    a = b;
    b = t;
    b ^= bf->p[1];
    a ^= bf->p[0];
    *l = a;
    *r = b;
}

bool pk_blowfish_init(pk_blowfish *bf, const Uint8 *key, int key_len)
{
    int i, j, k = 0;
    Uint32 l = 0, r = 0;
    if (key_len <= 0) {
        return false;
    }
    for (i = 0; i < 18; i++) {
        bf->p[i] = pk_app_i32(0x100800b4u + (Uint32)i * 4u);
    }
    for (i = 0; i < 4; i++) {
        for (j = 0; j < 256; j++) {
            bf->s[i][j] = pk_app_i32(0x100800fcu + (Uint32)(i * 1024 + j * 4));
        }
    }
    if (bf->p[0] == 0 && bf->p[1] == 0) {
        return false; /* 6rah.app not loaded */
    }
    for (i = 0; i < 18; i++) {
        Uint32 w = 0;
        for (j = 0; j < 4; j++) {
            w = (w << 8) | key[k];
            k = (k + 1) % key_len;
        }
        bf->p[i] ^= w;
    }
    for (i = 0; i < 18; i += 2) {
        enc(bf, &l, &r);
        bf->p[i] = l;
        bf->p[i + 1] = r;
    }
    for (i = 0; i < 4; i++) {
        for (j = 0; j < 256; j += 2) {
            enc(bf, &l, &r);
            bf->s[i][j] = l;
            bf->s[i][j + 1] = r;
        }
    }
    return true;
}

void pk_blowfish_ecb(const pk_blowfish *bf, Uint8 *d, size_t len, bool decrypt)
{
    size_t i;
    for (i = 0; i + 8 <= len; i += 8) {
        Uint32 l = (Uint32)d[i] | ((Uint32)d[i + 1] << 8) | ((Uint32)d[i + 2] << 16) | ((Uint32)d[i + 3] << 24);
        Uint32 r = (Uint32)d[i + 4] | ((Uint32)d[i + 5] << 8) | ((Uint32)d[i + 6] << 16) | ((Uint32)d[i + 7] << 24);
        if (decrypt) {
            dec(bf, &l, &r);
        } else {
            enc(bf, &l, &r);
        }
        d[i] = (Uint8)l;
        d[i + 1] = (Uint8)(l >> 8);
        d[i + 2] = (Uint8)(l >> 16);
        d[i + 3] = (Uint8)(l >> 24);
        d[i + 4] = (Uint8)r;
        d[i + 5] = (Uint8)(r >> 8);
        d[i + 6] = (Uint8)(r >> 16);
        d[i + 7] = (Uint8)(r >> 24);
    }
}
