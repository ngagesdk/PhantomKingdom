/* SPDX-License-Identifier: MIT */
#ifndef PK_IDENTITY_H
#define PK_IDENTITY_H

#include <stdbool.h>

/* The Arena (and PhantomLink, which emulates it) binds each account to the
 * IMEI of the N-Gage that created it: the Community SOAP login carries
 * <imei>, the server stores it on first login and afterwards only accepts
 * that account from the same IMEI (and one account per IMEI).
 *
 * A PC has no IMEI, so the port makes one per installation:
 *   - 15 digits, the last one a Luhn check digit, so it is well formed
 *     wherever an IMEI is parsed.
 *   - Prefix "00" (reporting body identifier 00 is reserved for test
 *     IMEIs): it can never equal the IMEI of a real N-Gage, which matters
 *     because real phones and the port share PhantomLink's account store.
 *   - The other 12 digits come from the OS's cryptographic random source,
 *     so two installations practically never collide (10^12 values).
 *   - It is generated once and stored in the per-user preference folder
 *     (identity.cfg). It is not derived from hardware (MAC addresses,
 *     disk serials, ...), which would be a privacy problem, can change,
 *     and is not portable.
 * Moving identity.cfg to another computer moves the account binding with
 * it, just like moving the SIM/phone did. --imei <15 digits> overrides it,
 * for example to keep using an account bound to a real N-Gage. */

#define PK_IMEI_LEN 15

bool pk_identity_init(const char *override_imei);
const char *pk_identity_imei(void);

/* Helpers exposed for tests. */
bool pk_imei_is_valid(const char *imei);
char pk_luhn_digit(const char *digits14);

#endif
