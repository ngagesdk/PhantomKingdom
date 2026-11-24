/* SPDX-License-Identifier: MIT */
#include "net/pk_snap.h"
#include "core/pk_log.h"

#include <SDL3/SDL.h>
#include <SDL3_net/SDL_net.h>

#define MAX_RETRIES   17
#define RESEND_MS     1000
#define LOGIN_TIMEOUT 15000
#define MAX_PENDING   64

static const Uint8 k_trailer[4] = { 0xBA, 0x47, 0x66, 0x11 };

typedef struct pending
{
    Uint8 buf[PK_SNAP_MAX_DGRAM];
    int len;
    Uint32 seq;
    Uint64 sent_at;
    int tries;
} pending;

struct pk_snap
{
    NET_DatagramSocket *sock;
    NET_Address *addr;
    Uint16 port;
    pk_snap_state state;
    pk_snap_handler handler;
    void *ud;
    Uint32 user_id;
    Uint32 next_seq;   /* our next reliable sequence number */
    Uint32 expect_seq; /* next reliable sequence number we accept */
    Uint8 cmd_seq;
    pending queue[MAX_PENDING];
    int queued;
    char user[41];
    Uint32 game_class;
    Uint64 login_at;
    bool login_pending;
};

static void put16(Uint8 *p, Uint16 v)
{
    p[0] = (Uint8)(v >> 8);
    p[1] = (Uint8)v;
}
static void put32(Uint8 *p, Uint32 v)
{
    p[0] = (Uint8)(v >> 24);
    p[1] = (Uint8)(v >> 16);
    p[2] = (Uint8)(v >> 8);
    p[3] = (Uint8)v;
}
static Uint16 get16(const Uint8 *p) { return (Uint16)((p[0] << 8) | p[1]); }
static Uint32 get32(const Uint8 *p) { return ((Uint32)p[0] << 24) | ((Uint32)p[1] << 16) | ((Uint32)p[2] << 8) | p[3]; }

int pk_snap_encode(Uint8 *out, int cap, const pk_snap_msg *m)
{
    int total = PK_SNAP_HEADER + m->len;
    if (m->len < 0 || total > 0x3ff || total > cap) {
        return -1;
    }
    put16(out, (Uint16)((m->flags & 0xFC00) | (total & 0x3ff)));
    out[2] = m->cmd_seq;
    out[3] = m->op;
    put32(out + 4, m->user_id);
    put32(out + 8, m->seq);
    put32(out + 12, m->ack);
    if (m->len) {
        SDL_memcpy(out + PK_SNAP_HEADER, m->payload, (size_t)m->len);
    }
    return total;
}

int pk_snap_decode(const Uint8 *in, int len, pk_snap_msg *out, int max)
{
    int n = 0, off = 0;
    if (len < 4 || SDL_memcmp(in + len - 4, k_trailer, 3) != 0) {
        return -1;
    }
    len -= 4;
    while (off + PK_SNAP_HEADER <= len && n < max) {
        Uint16 fl = get16(in + off);
        int total = fl & 0x3ff;
        if (total < PK_SNAP_HEADER || off + total > len) {
            return -1;
        }
        out[n].flags = (Uint16)(fl & 0xFC00);
        out[n].cmd_seq = in[off + 2];
        out[n].op = in[off + 3];
        out[n].user_id = get32(in + off + 4);
        out[n].seq = get32(in + off + 8);
        out[n].ack = get32(in + off + 12);
        out[n].payload = in + off + PK_SNAP_HEADER;
        out[n].len = total - PK_SNAP_HEADER;
        n++;
        off += total;
        if (!(fl & PK_SNAP_F_MORE)) {
            break;
        }
    }
    return n;
}

pk_snap *pk_snap_create(const char *host, Uint16 port, pk_snap_handler h, void *ud)
{
    pk_snap *s = (pk_snap *)SDL_calloc(1, sizeof(*s));
    if (!s) {
        return NULL;
    }
    s->handler = h;
    s->ud = ud;
    s->port = port ? port : PK_SNAP_PORT;
    s->sock = NET_CreateDatagramSocket(NULL, 0, 0);
    if (!s->sock) {
        /* no IPv6 on this machine: bind the IPv4 any-address instead */
        NET_Address *any = NET_ResolveHostname("0.0.0.0");
        if (any && NET_WaitUntilResolved(any, 2000) == NET_SUCCESS) {
            s->sock = NET_CreateDatagramSocket(any, 0, 0);
        }
        if (any) {
            NET_UnrefAddress(any);
        }
    }
    s->addr = NET_ResolveHostname(host);
    if (!s->sock || !s->addr) {
        PK_ERROR("snap: cannot create socket or resolve '%s': %s", host, SDL_GetError());
        pk_snap_destroy(s);
        return NULL;
    }
    s->state = PK_SNAP_RESOLVING;
    return s;
}

void pk_snap_destroy(pk_snap *s)
{
    if (!s) {
        return;
    }
    if (s->sock) {
        NET_DestroyDatagramSocket(s->sock);
    }
    if (s->addr) {
        NET_UnrefAddress(s->addr);
    }
    SDL_free(s);
}

static bool raw_send(pk_snap *s, const Uint8 *msg, int len)
{
    Uint8 dg[PK_SNAP_MAX_DGRAM];
    if (len + 4 > (int)sizeof(dg)) {
        return false;
    }
    SDL_memcpy(dg, msg, (size_t)len);
    SDL_memcpy(dg + len, k_trailer, 4);
    return NET_SendDatagram(s->sock, s->addr, s->port, dg, len + 4);
}

bool pk_snap_send(pk_snap *s, Uint8 op, Uint16 flags, const void *payload, int len)
{
    pk_snap_msg m;
    Uint8 buf[PK_SNAP_MAX_DGRAM];
    int n;

    SDL_zero(m);
    m.flags = (Uint16)(flags | PK_SNAP_F_SINGLE);
    m.cmd_seq = ++s->cmd_seq;
    m.op = op;
    m.user_id = s->user_id;
    m.payload = (const Uint8 *)payload;
    m.len = len;
    if (flags & PK_SNAP_F_RELIABLE) {
        if (s->queued >= MAX_PENDING) {
            PK_WARN("snap: send queue full");
            return false;
        }
        m.seq = s->next_seq++;
    }
    n = pk_snap_encode(buf, (int)sizeof(buf) - 4, &m);
    if (n < 0) {
        return false;
    }
    if (flags & PK_SNAP_F_RELIABLE) {
        pending *p = &s->queue[s->queued++];
        SDL_memcpy(p->buf, buf, (size_t)n);
        p->len = n;
        p->seq = m.seq;
        p->tries = 1;
        p->sent_at = SDL_GetTicks();
    }
    return raw_send(s, buf, n);
}

bool pk_snap_send_game_data(pk_snap *s, const void *data, int len)
{
    return pk_snap_send(s, PK_SNAP_OP_DATA_SERVER, PK_SNAP_F_RELIABLE, data, len);
}

bool pk_snap_login(pk_snap *s, const char *user, Uint32 game_class)
{
    SDL_strlcpy(s->user, user, sizeof(s->user));
    s->game_class = game_class;
    s->login_pending = true;
    return true;
}

static void send_login(pk_snap *s)
{
    /* op 44 payload: user name [40], text [60], game class (BE) at 100,
     * then TLVs 'LC' (latency) and 'MR' (max retries). */
    Uint8 p[128];
    SDL_zero(p);
    SDL_memcpy(p, s->user, SDL_strlen(s->user));
    put32(p + 100, s->game_class);
    p[104] = 'L';
    p[105] = 'C';
    put16(p + 106, 4);
    put32(p + 108, 0);
    p[112] = 'M';
    p[113] = 'R';
    put16(p + 114, 4);
    put32(p + 116, MAX_RETRIES);
    s->next_seq = 0;
    s->expect_seq = 0;
    s->queued = 0;
    s->login_at = SDL_GetTicks();
    s->state = PK_SNAP_LOGGING_IN;
    pk_snap_send(s, PK_SNAP_OP_LOGIN, PK_SNAP_F_LOBBY, p, 120);
}

void pk_snap_logout(pk_snap *s)
{
    if (s->state == PK_SNAP_ONLINE) {
        pk_snap_send(s, PK_SNAP_OP_LOGOUT, PK_SNAP_F_RELIABLE | PK_SNAP_F_LOBBY, NULL, 0);
    }
    s->state = PK_SNAP_IDLE;
}

static void send_ack(pk_snap *s, Uint32 seq)
{
    pk_snap_msg m;
    Uint8 buf[PK_SNAP_HEADER];
    SDL_zero(m);
    m.flags = PK_SNAP_F_ACK | PK_SNAP_F_SINGLE | PK_SNAP_F_LOBBY; /* 0x6010 per the docs */
    m.op = PK_SNAP_OP_ACK;
    m.user_id = s->user_id;
    m.ack = seq;
    if (pk_snap_encode(buf, (int)sizeof(buf), &m) == PK_SNAP_HEADER) {
        raw_send(s, buf, PK_SNAP_HEADER);
    }
}

static void handle(pk_snap *s, const pk_snap_msg *m)
{
    int i;
    if (m->flags & PK_SNAP_F_ACK) {
        for (i = 0; i < s->queued; i++) {
            if (s->queue[i].seq == m->ack) {
                s->queue[i] = s->queue[--s->queued];
                break;
            }
        }
        if (m->op == PK_SNAP_OP_ACK) {
            return;
        }
    }
    if (m->flags & PK_SNAP_F_RELIABLE) {
        send_ack(s, m->seq);
        if (m->seq != s->expect_seq) {
            return; /* repeat or out of order; the server resends in order */
        }
        s->expect_seq++;
    }
    if (m->op == PK_SNAP_OP_LOGIN_OK && m->len >= 12) {
        Uint16 port = (Uint16)get32(m->payload);
        s->user_id = get32(m->payload + 8);
        if (port) {
            s->port = port;
        }
        s->state = PK_SNAP_ONLINE;
        PK_INFO("snap: logged in as user id %u", s->user_id);
    } else if (m->op == PK_SNAP_OP_FAILED && s->state == PK_SNAP_LOGGING_IN) {
        s->state = PK_SNAP_FAILED;
        PK_WARN("snap: login refused");
    }
    if (s->handler) {
        s->handler(s->ud, m);
    }
}

void pk_snap_update(pk_snap *s)
{
    NET_Datagram *dg = NULL;
    Uint64 now = SDL_GetTicks();
    int i;

    if (!s) {
        return;
    }
    if (s->state == PK_SNAP_RESOLVING) {
        NET_Status st = NET_GetAddressStatus(s->addr);
        if (st == NET_FAILURE) {
            PK_WARN("snap: host lookup failed");
            s->state = PK_SNAP_FAILED;
            return;
        }
        if (st == NET_SUCCESS && s->login_pending) {
            s->login_pending = false;
            send_login(s);
        }
        if (st != NET_SUCCESS) {
            return;
        }
    }
    while (NET_ReceiveDatagram(s->sock, &dg) && dg) {
        /* only datagrams from the server address are accepted */
        if (NET_CompareAddresses(dg->addr, s->addr) == 0) {
            pk_snap_msg msgs[16];
            int n = pk_snap_decode(dg->buf, dg->buflen, msgs, 16);
            for (i = 0; i < n; i++) {
                handle(s, &msgs[i]);
            }
        }
        NET_DestroyDatagram(dg);
        dg = NULL;
    }
    for (i = 0; i < s->queued; i++) {
        pending *p = &s->queue[i];
        if (now - p->sent_at >= RESEND_MS) {
            if (p->tries >= MAX_RETRIES) {
                PK_WARN("snap: connection lost (no acknowledgement)");
                s->state = PK_SNAP_FAILED;
                s->queued = 0;
                return;
            }
            p->tries++;
            p->sent_at = now;
            raw_send(s, p->buf, p->len);
        }
    }
    if (s->state == PK_SNAP_LOGGING_IN && now - s->login_at > LOGIN_TIMEOUT) {
        PK_WARN("snap: login timed out");
        s->state = PK_SNAP_FAILED;
    }
}

pk_snap_state pk_snap_get_state(const pk_snap *s) { return s ? s->state : PK_SNAP_IDLE; }
Uint32 pk_snap_user_id(const pk_snap *s) { return s ? s->user_id : 0; }
