/* SPDX-License-Identifier: MIT */
#ifndef PK_BLOWFISH_H
#define PK_BLOWFISH_H
#include <SDL3/SDL.h>
/* Blowfish as used by the phone version for save.dat (FUN_10011038 key
 * schedule, FUN_10010638 rounds, FUN_100113c4 / FUN_10011600 ECB with
 * little-endian block halves). The initial P-array and S-boxes are read
 * from the user's 6rah.app (0x100800b4 / 0x100800fc). */
typedef struct pk_blowfish
{
    Uint32 p[18];
    Uint32 s[4][256];
} pk_blowfish;

bool pk_blowfish_init(pk_blowfish *bf, const Uint8 *key, int key_len);
void pk_blowfish_ecb(const pk_blowfish *bf, Uint8 *data, size_t len, bool decrypt);
#endif
