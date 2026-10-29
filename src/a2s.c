/* a2s.c - make a game server's info reply acceptable to Steam.
 *
 * When CoFLink asks Steam to connect the friend's game (steam://connect),
 * Steam first queries the server for its info. Cry of Fear's listen server
 * answers without a usable app id, so Steam refuses with "app id specified by
 * server is invalid". CoFLink rewrites only that info reply (old GoldSrc 'm'
 * or Source 'I' format) into a standard 'I' reply carrying Cry of Fear's app
 * id (223710). Game traffic is never touched.
 * Format: https://developer.valvesoftware.com/wiki/Server_queries (A2S_INFO) */
#include "mini.h"

typedef struct { const uint8_t *p; int n, i; bool bad; } Rd;

static int rbyte(Rd *r)
{
    if (r->i >= r->n) { r->bad = true; return 0; }
    return r->p[r->i++];
}

static int rshort(Rd *r) { int a = rbyte(r); int b = rbyte(r); return a | (b << 8); }

static void rskip(Rd *r, int k)
{
    if (r->i + k > r->n) { r->bad = true; r->i = r->n; } else r->i += k;
}

/* returns the string (inside the packet) and its length without the 0 */
static const char *rstr(Rd *r, int *len)
{
    int start = r->i;
    while (r->i < r->n && r->p[r->i]) r->i++;
    if (r->i >= r->n) { r->bad = true; *len = 0; return ""; }
    *len = r->i - start;
    r->i++; /* the 0 */
    return (const char *)r->p + start;
}

typedef struct { uint8_t *p; int n, cap; bool bad; } Wr;

static void wbyte(Wr *w, int v)
{
    if (w->n >= w->cap) { w->bad = true; return; }
    w->p[w->n++] = (uint8_t)v;
}
static void wshort(Wr *w, int v) { wbyte(w, v & 0xFF); wbyte(w, (v >> 8) & 0xFF); }
static void wbytes(Wr *w, const void *s, int len)
{
    if (len < 0 || w->n + len > w->cap) { w->bad = true; return; }
    memcpy(w->p + w->n, s, (size_t)len);
    w->n += len;
}
static void wstr(Wr *w, const char *s, int len) { wbytes(w, s, len); wbyte(w, 0); }
static void w64(Wr *w, u64 v) { for (int k = 0; k < 8; k++) wbyte(w, (int)((v >> (8 * k)) & 0xFF)); }
static int lower(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }

/* Rewrites an info reply in `in` into `out`. Returns the new length, or 0 when
   the packet isn't an info reply, is malformed, or already has the right id. */
int a2s_fix_appid(const uint8_t *in, int n, uint8_t *out, int cap, uint32_t appid)
{
    if (n < 6 || in[0] != 0xFF || in[1] != 0xFF || in[2] != 0xFF || in[3] != 0xFF) return 0;
    int kind = in[4];
    if (kind != 'I' && kind != 'm') return 0;
    Rd r = { in, n, 5, false };
    const char *name, *map, *folder, *game, *version = "1.1.2.7", *keywords = 0, *tvname = 0;
    int lname, lmap, lfolder, lgame, lversion = 7, lkeywords = 0, ltvname = 0;
    int proto, players, maxp, bots = 0, type, env, vis, vac;
    int port = -1, tvport = -1;
    u64 steamid = 0;
    bool has_steamid = false;

    if (kind == 'I') {
        proto = rbyte(&r);
        name = rstr(&r, &lname); map = rstr(&r, &lmap);
        folder = rstr(&r, &lfolder); game = rstr(&r, &lgame);
        int shortid = rshort(&r);
        players = rbyte(&r); maxp = rbyte(&r); bots = rbyte(&r);
        type = rbyte(&r); env = rbyte(&r); vis = rbyte(&r); vac = rbyte(&r);
        if (r.bad || shortid == 2400) return 0; /* The Ship has extra fields: leave it */
        if (r.i < r.n) version = rstr(&r, &lversion);
        int edf = r.i < r.n ? rbyte(&r) : 0;
        if (edf & 0x80) port = rshort(&r);
        if (edf & 0x10) { steamid = 0; for (int k = 0; k < 8; k++) steamid |= (u64)rbyte(&r) << (8 * k); has_steamid = true; }
        if (edf & 0x40) { tvport = rshort(&r); tvname = rstr(&r, &ltvname); }
        if (edf & 0x20) keywords = rstr(&r, &lkeywords);
        if (edf & 0x01) {
            u64 gid = 0;
            for (int k = 0; k < 8; k++) gid |= (u64)rbyte(&r) << (8 * k);
            if (!r.bad && (uint32_t)(gid & 0xFFFFFF) == appid) return 0; /* already fine */
        }
        if (r.bad) return 0;
    } else { /* 'm': old GoldSrc reply */
        int laddr;
        rstr(&r, &laddr);
        name = rstr(&r, &lname); map = rstr(&r, &lmap);
        folder = rstr(&r, &lfolder); game = rstr(&r, &lgame);
        players = rbyte(&r); maxp = rbyte(&r); proto = rbyte(&r);
        type = lower(rbyte(&r)); env = lower(rbyte(&r)); vis = rbyte(&r);
        int mod = rbyte(&r);
        if (mod == 1) {
            int l;
            rstr(&r, &l); rstr(&r, &l); /* link, download link */
            rskip(&r, 1 + 4 + 4 + 1 + 1); /* null, version, size, type, dll */
        }
        vac = rbyte(&r);
        if (r.i < r.n) bots = rbyte(&r);
        if (r.bad) return 0;
    }

    Wr w = { out, 0, cap, false };
    wbytes(&w, "\xFF\xFF\xFF\xFFI", 5);
    wbyte(&w, proto);
    wstr(&w, name, lname); wstr(&w, map, lmap);
    wstr(&w, folder, lfolder); wstr(&w, game, lgame);
    wshort(&w, (int)(appid & 0xFFFF));
    wbyte(&w, players); wbyte(&w, maxp); wbyte(&w, bots);
    wbyte(&w, type); wbyte(&w, env); wbyte(&w, vis); wbyte(&w, vac);
    wstr(&w, version, lversion);
    int edf = 0x01 | (port >= 0 ? 0x80 : 0) | (has_steamid ? 0x10 : 0) | (tvport >= 0 ? 0x40 : 0) | (keywords ? 0x20 : 0);
    wbyte(&w, edf);
    if (port >= 0) wshort(&w, port);
    if (has_steamid) w64(&w, steamid);
    if (tvport >= 0) { wshort(&w, tvport); wstr(&w, tvname, ltvname); }
    if (keywords) wstr(&w, keywords, lkeywords);
    w64(&w, (u64)appid);
    return w.bad ? 0 : w.n;
}

/* For the log: the app ids a standard 'I' info reply announces (the 16-bit
   id and, when present, the 64-bit game id's app part). 0 if not an 'I' reply. */
int a2s_appids(const uint8_t *in, int n, int *shortid, long long *gameapp)
{
    *shortid = -1;
    *gameapp = -1;
    if (n < 6 || in[0] != 0xFF || in[1] != 0xFF || in[2] != 0xFF || in[3] != 0xFF || in[4] != 'I') return 0;
    Rd r = { in, n, 5, false };
    int l;
    rbyte(&r);
    rstr(&r, &l); rstr(&r, &l); rstr(&r, &l); rstr(&r, &l);
    *shortid = rshort(&r);
    rskip(&r, 7);
    if (r.i < r.n) rstr(&r, &l);
    int edf = r.i < r.n ? rbyte(&r) : 0;
    if (edf & 0x80) rskip(&r, 2);
    if (edf & 0x10) rskip(&r, 8);
    if (edf & 0x40) { rskip(&r, 2); rstr(&r, &l); }
    if (edf & 0x20) rstr(&r, &l);
    if (edf & 0x01) {
        unsigned long long gid = 0;
        for (int k = 0; k < 8; k++) gid |= (unsigned long long)rbyte(&r) << (8 * k);
        if (!r.bad) *gameapp = (long long)(gid & 0xFFFFFF);
    }
    return r.bad ? 0 : 1;
}
