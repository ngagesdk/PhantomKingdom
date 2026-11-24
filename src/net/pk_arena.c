/* SPDX-License-Identifier: MIT */
#include "net/pk_arena.h"
#include "core/pk_log.h"

#include <SDL3/SDL.h>
#include <SDL3_net/SDL_net.h>

#define TIMEOUT_MS 15000

struct pk_arena
{
    NET_Address *addr;
    NET_StreamSocket *sock;
    Uint16 port;
    pk_arena_state state;
    char host[128];
    char req[2048];
    int req_len;
    char resp[8192];
    int resp_len;
    char user[64];
    Uint64 started;
};

static void xml_escape(char *out, size_t cap, const char *s)
{
    size_t o = 0;
    for (; *s && o + 7 < cap; s++) {
        const char *r = NULL;
        switch (*s) {
        case '<':
            r = "&lt;";
            break;
        case '>':
            r = "&gt;";
            break;
        case '&':
            r = "&amp;";
            break;
        case '"':
            r = "&quot;";
            break;
        case '\'':
            r = "&apos;";
            break;
        default:
            break;
        }
        if (r) {
            size_t n = SDL_strlen(r);
            SDL_memcpy(out + o, r, n);
            o += n;
        } else {
            out[o++] = *s;
        }
    }
    out[o] = '\0';
}

int pk_arena_build_request(char *out, int cap, const char *host, const char *user, const char *password,
                           const char *imei)
{
    char u[256], p[256], body[1200];
    int blen, n;
    xml_escape(u, sizeof(u), user);
    xml_escape(p, sizeof(p), password);
    blen = SDL_snprintf(body, sizeof(body),
                        "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
                        "<soapenv:Envelope xmlns:soapenv=\"http://schemas.xmlsoap.org/soap/envelope/\" "
                        "xmlns:xsd=\"http://www.w3.org/2001/XMLSchema\" xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\">"
                        "<soapenv:Body><ns1:authenticateUser2 soapenv:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\" "
                        "xmlns:ns1=\"urn:CommunityApp\">"
                        "<username xsi:type=\"xsd:string\">%s</username>"
                        "<password xsi:type=\"xsd:string\">%s</password>"
                        "<imei xsi:type=\"xsd:string\">%s</imei>"
                        "</ns1:authenticateUser2></soapenv:Body></soapenv:Envelope>",
                        u, p, imei);
    if (blen < 0 || blen >= (int)sizeof(body)) {
        return -1;
    }
    n = SDL_snprintf(out, (size_t)cap,
                     "POST /services/Community HTTP/1.1\r\n"
                     "Host: %s\r\n"
                     "Content-Type: text/xml; charset=utf-8\r\n"
                     "SOAPAction: \"authenticateUser2\"\r\n"
                     "Content-Length: %d\r\n"
                     "Connection: close\r\n\r\n%s",
                     host, blen, body);
    return (n < 0 || n >= cap) ? -1 : n;
}

pk_arena *pk_arena_login(const char *host, Uint16 port, const char *user, const char *password, const char *imei)
{
    pk_arena *a = (pk_arena *)SDL_calloc(1, sizeof(*a));
    if (!a) {
        return NULL;
    }
    SDL_strlcpy(a->host, host, sizeof(a->host));
    SDL_strlcpy(a->user, user, sizeof(a->user));
    a->port = port ? port : 80;
    a->req_len = pk_arena_build_request(a->req, (int)sizeof(a->req), host, user, password, imei);
    a->addr = NET_ResolveHostname(host);
    a->started = SDL_GetTicks();
    a->state = (a->req_len > 0 && a->addr) ? PK_ARENA_CONNECTING : PK_ARENA_ERROR;
    return a;
}

static void finish(pk_arena *a)
{
    const char *ret;
    a->resp[a->resp_len] = '\0';
    ret = SDL_strstr(a->resp, "authenticateUser2Return");
    if (!ret) {
        PK_WARN("arena: unexpected answer from the Community service");
        a->state = PK_ARENA_ERROR;
        return;
    }
    if (SDL_strstr(ret, ">true<")) {
        const char *u = SDL_strstr(ret, "<username");
        a->state = PK_ARENA_OK;
        if (u && (u = SDL_strchr(u, '>')) != NULL) {
            const char *e = SDL_strchr(++u, '<');
            if (e && e > u && (size_t)(e - u) < sizeof(a->user)) {
                SDL_memcpy(a->user, u, (size_t)(e - u));
                a->user[e - u] = '\0';
            }
        }
        PK_INFO("arena: logged in as '%s'", a->user);
    } else {
        a->state = PK_ARENA_REFUSED;
        PK_WARN("arena: login refused (wrong password, or the name is bound to another IMEI)");
    }
}

void pk_arena_update(pk_arena *a)
{
    if (!a) {
        return;
    }
    if ((a->state == PK_ARENA_CONNECTING || a->state == PK_ARENA_WAITING) &&
        SDL_GetTicks() - a->started > TIMEOUT_MS) {
        PK_WARN("arena: timed out");
        a->state = PK_ARENA_ERROR;
        return;
    }
    if (a->state == PK_ARENA_CONNECTING) {
        if (!a->sock) {
            NET_Status st = NET_GetAddressStatus(a->addr);
            if (st == NET_FAILURE) {
                a->state = PK_ARENA_ERROR;
                return;
            }
            if (st != NET_SUCCESS) {
                return;
            }
            a->sock = NET_CreateClient(a->addr, a->port, 0);
            if (!a->sock) {
                a->state = PK_ARENA_ERROR;
                return;
            }
        }
        switch (NET_GetConnectionStatus(a->sock)) {
        case NET_SUCCESS:
            if (!NET_WriteToStreamSocket(a->sock, a->req, a->req_len)) {
                a->state = PK_ARENA_ERROR;
                return;
            }
            a->state = PK_ARENA_WAITING;
            break;
        case NET_FAILURE:
            a->state = PK_ARENA_ERROR;
            return;
        default:
            return;
        }
    }
    if (a->state == PK_ARENA_WAITING) {
        int n = NET_ReadFromStreamSocket(a->sock, a->resp + a->resp_len, (int)sizeof(a->resp) - 1 - a->resp_len);
        if (n > 0) {
            a->resp_len += n;
            if (a->resp_len >= (int)sizeof(a->resp) - 1) {
                finish(a);
            }
        } else if (n < 0) {
            /* connection closed: the answer is complete */
            finish(a);
        } else {
            a->resp[a->resp_len] = '\0';
            if (SDL_strstr(a->resp, "</soapenv:Envelope>")) {
                finish(a);
            }
        }
    }
}

pk_arena_state pk_arena_get_state(const pk_arena *a) { return a ? a->state : PK_ARENA_IDLE; }
const char *pk_arena_user(const pk_arena *a) { return a ? a->user : ""; }

void pk_arena_destroy(pk_arena *a)
{
    if (!a) {
        return;
    }
    if (a->sock) {
        NET_DestroyStreamSocket(a->sock);
    }
    if (a->addr) {
        NET_UnrefAddress(a->addr);
    }
    SDL_free(a);
}
