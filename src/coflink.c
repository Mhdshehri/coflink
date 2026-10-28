/* CoFLink - play Cry of Fear co-op with a Steam friend: no VPN, no port forwarding.
 *
 * Host:  creates a friends-only Steam lobby (through Valve's free "Spacewar"
 *        test app, AppID 480) and forwards each friend's
 *        packets into the host's own Cry of Fear co-op server on 127.0.0.1:27015.
 * Join:  finds the host among Steam friends (or by lobby id), joins the lobby,
 *        and opens 127.0.0.1:27115; the friend then connects from Cry of Fear's
 *        own console (connect 127.0.0.1:27115). CoFLink only links; it does not
 *        connect the game itself and does not use Steam invites.
 * Packets travel through Steam's relay network (ISteamNetworkingMessages).
 * The game is not modified. Pattern: coop-anything HXCoop relay
 * (playbook/networking-steam-spacewar.md). MIT license. */
#include "steam.h"
#include "plat.h"
#include "ui.h"

#define VERSION "0.6"
#define PROTO "2"     /* relay protocol; both players need the same */
#define COF_APPID 223710
#define SPACEWAR_APPID 480
#define MAX_PEERS 8
#define BUF_SIZE 65536
#define CH_GAME 0
#define CH_CTL 1
#define KEEPALIVE_MS 1000
#define PEER_TIMEOUT_MS 30000
#define HOST_QUIET_MS 15000
#define MAX_GAME_PKT 8192      /* GoldSrc packets are < 4 KB; anything bigger is dropped */
#define MAX_PKTS_PER_SEC 3000  /* per sender; normal play is ~100-200 */
/* Each game packet carries one leading "slot" byte: one slot per local UDP
   source on the friend's PC (the game, and Steam's own server query when it
   connects the game), so answers go back to the right program. */
#define NSLOT 4
#define SLOT_PROBE 7           /* CoFLink's own "is the host's server up?" check */
#define SLOT_SOCKS 8

static void *U, *F, *MM, *NM, *NU, *AP;
static int32_t g_pipe;
static u64 g_me, g_lobby, g_host, g_joining;
static int g_mode;      /* MODE_IDLE / MODE_HOST / MODE_JOIN */
static bool g_is_host;  /* g_mode == MODE_HOST */
static bool g_steam_ok;
static uint16_t g_game_port = 27015;  /* host: port of the Cry of Fear server */
static uint16_t g_local_port = 27115; /* join: port the friend's game connects to */
static uint16_t g_local_port_base = 27115;
static int g_max_players = 4;
static bool g_no_launch;
static uint8_t g_buf[BUF_SIZE];


/* ---------- output ---------- */

static void say(const char *fmt, ...)
{
    char b[1200];
    va_list ap;
    va_start(ap, fmt);
#ifdef _WIN32
    _vsnprintf(b, sizeof b, fmt, ap);
#else
    vsnprintf(b, sizeof b, fmt, ap);
#endif
    va_end(ap);
    b[sizeof b - 1] = 0;
    ui_log(b);
}

static const char *idstr(u64 v)
{
    static char ring[4][24];
    static int k;
    char *o = ring[k++ & 3];
    u64_to_str(v, o);
    return o;
}

/* Steam names are chosen by other people: print them with control characters
   replaced, so a name can't mess with the console. */
static const char *clean(const char *n)
{
    static char ring[4][72];
    static int k;
    char *o = ring[k++ & 3];
    int i = 0;
    for (; n && n[i] && i < 64; i++) {
        unsigned char c = (unsigned char)n[i];
        o[i] = (c < 0x20 || c == 0x7f) ? '?' : (char)c;
    }
    o[i] = 0;
    return o;
}

static const char *who(u64 id)
{
    if (id == g_me) return clean(S.Friends_GetPersonaName(F));
    const char *n = S.Friends_GetFriendPersonaName(F, id);
    return (n && *n && strcmp(n, "[unknown]") != 0) ? clean(n) : idstr(id);
}


/* ---------- Steam helpers ---------- */

static void ident_of(uint8_t b[IDENT_SIZE], u64 id)
{
    memset(b, 0, IDENT_SIZE);
    S.Ident_SetSteamID64(b, id);
}

static void send_game(u64 to, int slot, const void *p, int n)
{
    static uint8_t out[MAX_GAME_PKT + 1];
    if (n <= 0 || n > MAX_GAME_PKT) return;
    uint8_t id[IDENT_SIZE];
    ident_of(id, to);
    out[0] = (uint8_t)slot;
    memcpy(out + 1, p, (size_t)n);
    /* Cry of Fear (GoldSrc) does its own reliability: send unreliable. */
    S.NM_SendMessageToUser(NM, id, out, (uint32_t)n + 1, SEND_UNRELIABLE | SEND_NONAGLE, CH_GAME);
}

static void send_ctl(u64 to, char c)
{
    uint8_t id[IDENT_SIZE];
    ident_of(id, to);
    char m[2] = { c, PROTO[0] }; /* command + protocol version */
    S.NM_SendMessageToUser(NM, id, m, 2, SEND_RELIABLE | SEND_NONAGLE, CH_CTL);
}

static void on_probe_reply(const uint8_t *b, int len);

static void send_ctl_data(u64 to, const void *p, int n)
{
    uint8_t id[IDENT_SIZE];
    ident_of(id, to);
    S.NM_SendMessageToUser(NM, id, p, (uint32_t)n, SEND_RELIABLE | SEND_NONAGLE, CH_CTL);
}

static void close_session(u64 with)
{
    uint8_t id[IDENT_SIZE];
    ident_of(id, with);
    S.NM_CloseSessionWithUser(NM, id);
}

static bool in_lobby(u64 id)
{
    if (!g_lobby) return false;
    int n = S.MM_GetNumLobbyMembers(MM, g_lobby);
    for (int i = 0; i < n; i++)
        if (S.MM_GetLobbyMemberByIndex(MM, g_lobby, i) == id) return true;
    return false;
}

/* Simple flood guard: at most MAX_PKTS_PER_SEC packets per second. */
typedef struct { u64 window; int count; } Rate;
static bool rate_ok(Rate *r, u64 t)
{
    if (t - r->window >= 1000) { r->window = t; r->count = 0; }
    return ++r->count <= MAX_PKTS_PER_SEC;
}

/* ---------- host: one local socket per friend ---------- */

typedef struct {
    u64 id;
    sock_t s[SLOT_SOCKS]; /* one socket to the server per slot, opened when used */
    u64 last_heard, last_ka, first_up_ms;
    u64 up, down; /* packets friend->server, server->friend */
    bool welcomed, warned_silent_server;
    Rate rate;
} Peer;

static Peer g_peers[MAX_PEERS];
static int g_npeers;
static u64 g_ignored[16];
static int g_nignored;

static Peer *find_peer(u64 id)
{
    for (int i = 0; i < g_npeers; i++)
        if (g_peers[i].id == id) return &g_peers[i];
    return 0;
}

static Peer *add_peer(u64 id)
{
    if (g_npeers >= MAX_PEERS) return 0;
    Peer *p = &g_peers[g_npeers++];
    memset(p, 0, sizeof *p);
    p->id = id;
    for (int k = 0; k < SLOT_SOCKS; k++) p->s[k] = BADSOCK;
    p->last_heard = now_ms();
    say("+ %s connected through Steam (to your server 127.0.0.1:%u).", who(id), (unsigned)g_game_port);
    return p;
}

static sock_t peer_sock(Peer *p, int slot)
{
    if (p->s[slot] != BADSOCK) return p->s[slot];
    sock_t s = udp_open_local(0);
    if (s == BADSOCK || !udp_connect_local(s, g_game_port)) {
        udp_close(s);
        say("! Could not open a local socket for %s.", who(p->id));
        return BADSOCK;
    }
    return p->s[slot] = s;
}

static void close_peer_socks(Peer *p)
{
    for (int k = 0; k < SLOT_SOCKS; k++) { udp_close(p->s[k]); p->s[k] = BADSOCK; }
}

static void drop_peer(u64 id, const char *why)
{
    for (int i = 0; i < g_npeers; i++) {
        if (g_peers[i].id != id) continue;
        say("- %s disconnected (%s).", who(id), why);
        close_peer_socks(&g_peers[i]);
        close_session(id);
        g_peers[i] = g_peers[--g_npeers];
        return;
    }
}

/* Who may reach the host's game server: only people in the lobby who are
   also on the host's own Steam friends list. Everyone else is dropped. */
static bool allowed_peer(u64 id)
{
    return id && id != g_me && in_lobby(id) && S.Friends_HasFriend(F, id, FRIEND_IMMEDIATE);
}

static void note_ignored(u64 id)
{
    for (int i = 0; i < g_nignored; i++)
        if (g_ignored[i] == id) return;
    if (g_nignored < 16) g_ignored[g_nignored++] = id;
    if (in_lobby(id))
        say("! Blocked %s: they are in the lobby but not on your Steam friends list.", who(id));
    else
        say("! Blocked traffic from %s: they are not in your lobby.", who(id));
}

/* ---------- join side state ---------- */

static Rate g_host_rate;
static sock_t g_game_sock = BADSOCK;
static uint32_t g_game_ip;
static uint16_t g_slot_port[NSLOT];  /* local source port using each slot */
static u64 g_slot_used[NSLOT];
static bool g_welcomed, g_host_warned, g_said_down, g_warned_no_answer;
static u64 g_host_last, g_last_hello, g_last_ka, g_up, g_down, g_first_up_ms;
/* CoFLink checks the host's co-op server answers, to tell the friend when to connect */
static bool g_server_ready, g_said_wait_server, g_said_ready;
static u64 g_last_probe, g_welcomed_ms;
static u64 g_rejected[16];
static int g_nrejected;
/* Direct discovery: "are you hosting?" ('Q') to friends running CoFLink;
   a host answers 'L' + lobby id, only to people on its friends list. */
typedef struct { u64 friend_, lobby, at, asked; } Answer;
static Answer g_ans[32];

static Answer *answer_for(u64 friend_, bool make)
{
    int free_ = -1;
    for (int i = 0; i < 32; i++) {
        if (g_ans[i].friend_ == friend_) return &g_ans[i];
        if (!g_ans[i].friend_ && free_ < 0) free_ = i;
    }
    if (!make) return 0;
    if (free_ < 0) free_ = (int)(now_ms() % 32);
    memset(&g_ans[free_], 0, sizeof g_ans[free_]);
    g_ans[free_].friend_ = friend_;
    return &g_ans[free_];
}

static void reset_connect_state(void)
{
    g_server_ready = g_said_wait_server = g_said_ready = false;
    g_last_probe = g_welcomed_ms = 0;
}

static void reset_join_state(void)
{
    g_lobby = g_host = 0;
    g_welcomed = g_host_warned = g_said_down = g_warned_no_answer = false;
    g_up = g_down = g_first_up_ms = 0;
    for (int k = 0; k < NSLOT; k++) { g_slot_port[k] = 0; g_slot_used[k] = 0; }
    reset_connect_state();
}

static void lobby_gone(void)
{
    S.MM_LeaveLobby(MM, g_lobby);
    reset_join_state();
    S.Friends_ClearRichPresence(F);
}

static void request_join(u64 lobby)
{
    if (g_is_host || !lobby || lobby == g_lobby || lobby == g_joining) return;
    if (g_lobby) {
        send_ctl(g_host, 'B');
        S.MM_LeaveLobby(MM, g_lobby);
        reset_join_state();
    }
    g_joining = lobby;
    say("Joining lobby %s ...", idstr(lobby));
    S.MM_JoinLobby(MM, lobby);
}

static bool open_game_socket(void)
{
    if (g_game_sock != BADSOCK) return true;
    for (int i = 0; i < 10; i++) {
        g_game_sock = udp_open_local((uint16_t)(g_local_port + i));
        if (g_game_sock != BADSOCK) {
            g_local_port = (uint16_t)(g_local_port + i);
            return true;
        }
    }
    say("! Could not open a local port near %u for Cry of Fear.", (unsigned)g_local_port);
    return false;
}

static void joined_message(void)
{
    char cmd[64];
    sfmt(cmd, sizeof cmd, "connect 127.0.0.1:%u", (unsigned)g_local_port);
    clipboard_set(cmd);
    say("");
    say("=== Linked to %s. Now join from the game: ===", who(g_host));
    say("1. Click \"Start Cry of Fear\" (or start it from Steam).");
    say("2. In the main menu press ~ to open the console.");
    say("3. Paste with Ctrl+V and press Enter (already copied):   %s", cmd);
    say("");
}

static void on_lobby_entered(u64 lobby, uint32_t response)
{
    if (g_is_host || lobby == g_lobby) return; /* LobbyEnter can arrive twice */
    g_joining = 0;
    if (response != 1) {
        const char *why = response == 2 ? "it doesn't exist any more" :
                          response == 3 ? "you are not allowed to join" :
                          response == 4 ? "it is full" : "Steam refused";
        say("! Could not join that lobby: %s.", why);
        return;
    }
    const char *tag = S.MM_GetLobbyData(MM, lobby, "coflink");
    if (!tag || strcmp(tag, "1") != 0) {
        say("! That lobby is not a Cry of Fear co-op game (CoFLink). Leaving it.");
        S.MM_LeaveLobby(MM, lobby);
        if (g_nrejected < 16) g_rejected[g_nrejected++] = lobby;
        return;
    }
    const char *proto = S.MM_GetLobbyData(MM, lobby, "coflink_proto");
    if (!proto || strcmp(proto, PROTO) != 0) {
        const char *ver = S.MM_GetLobbyData(MM, lobby, "coflink_ver");
        say("! %s uses a different CoFLink version (%s). You both need CoFLink " VERSION ".",
            who(S.MM_GetLobbyOwner(MM, lobby)), ver && *ver ? clean(ver) : "older");
        S.MM_LeaveLobby(MM, lobby);
        if (g_nrejected < 16) g_rejected[g_nrejected++] = lobby;
        return;
    }
    g_lobby = lobby;
    g_host = S.MM_GetLobbyOwner(MM, lobby);
    g_host_last = now_ms();
    say("Joined %s's Cry of Fear lobby. Connecting to them through Steam...", who(g_host));
    S.Friends_SetRichPresence(F, "status", "Playing Cry of Fear co-op (CoFLink)");
    send_ctl(g_host, 'H');
    g_last_hello = now_ms();
    if (open_game_socket()) joined_message();
}

/* ---------- host: lobby + invites ---------- */

static void on_lobby_created(int32_t result, u64 lobby)
{
    if (!g_is_host || g_lobby) return; /* arrives twice on real Steam */
    if (result != 1) {
        say("! Steam could not create a lobby (error %d). Check your internet and try again.", result);
        g_mode = MODE_IDLE;
        g_is_host = false;
        return;
    }
    g_lobby = lobby;
    char conn[64];
    sfmt(conn, sizeof conn, "+connect_lobby %s", idstr(lobby));
    S.MM_SetLobbyData(MM, lobby, "coflink", "1");
    S.MM_SetLobbyData(MM, lobby, "coflink_ver", VERSION);
    S.MM_SetLobbyData(MM, lobby, "coflink_proto", PROTO);
    S.MM_SetLobbyData(MM, lobby, "host", S.Friends_GetPersonaName(F));
    S.MM_SetLobbyJoinable(MM, lobby, true);
    S.Friends_SetRichPresence(F, "status", "Hosting Cry of Fear co-op (CoFLink)");
    S.Friends_SetRichPresence(F, "connect", conn);
    S.Friends_SetRichPresence(F, "coflink", "host");
    clipboard_set(idstr(lobby));
    say("");
    say("=== Lobby is ready (id %s, copied to clipboard) ===", idstr(lobby));
    say("1. Start Cry of Fear and create your co-op server as usual (Co-op -> Create game).");
    say("   In the console (~) type:  sv_lan 1   (your friends will look like LAN players)");
    say("2. Your friend opens CoFLink and clicks \"Join a friend\" (if you don't show up,");
    say("   send them the lobby id above to paste into their CoFLink).");
    say("   Once linked, they connect from their game's console:  connect 127.0.0.1:27115");
    say("");
}

/* Host: the friends linked through CoFLink right now (shown in the window). */
static void publish_connected(void)
{
    static UiPerson list[MAX_PEERS];
    int n = 0;
    for (int i = 0; i < g_npeers; i++) {
        list[n].id = g_peers[i].id;
        list[n].lobby = 0;
        sfmt(list[n].name, UI_NAME, "%s%s", who(g_peers[i].id), g_peers[i].down ? "  (in game)" : "  (linked)");
        n++;
    }
    ui_list(LIST_FRIENDS_ONLINE, n, list);
}

/* ---------- callbacks ---------- */


static u64 rd64(const uint8_t *p) { u64 v; memcpy(&v, p, 8); return v; }
static uint32_t rd32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }

static void handle_cb(int cb, uint8_t *p, int size)
{
    switch (cb) {
    case CB_LobbyCreated:
        if (size >= 16) on_lobby_created((int32_t)rd32(p), rd64(p + 8));
        break;
    case CB_LobbyEnter:
        if (size >= 20) on_lobby_entered(rd64(p), rd32(p + 16));
        break;
    case CB_LobbyChatUpdate: {
        if (size < 28 || rd64(p) != g_lobby) break;
        u64 changed = rd64(p + 8);
        uint32_t st = rd32(p + 24);
        if (st & 1) {
            if (g_is_host && changed != g_me) say("%s joined your lobby.", who(changed));
        } else if (st & (CHAT_LEFT | CHAT_DISCONNECTED | CHAT_KICKED | CHAT_BANNED)) {
            if (g_is_host) {
                if (find_peer(changed)) drop_peer(changed, "left the lobby");
                else say("%s left your lobby.", who(changed));
            } else if (changed == g_host) {
                say("! %s stopped hosting. Waiting for them to host again...", who(g_host));
                lobby_gone();
            }
        }
        break;
    }
    case CB_LobbyInvite:
    case CB_GameLobbyJoinRequested:
    case CB_GameRichPresenceJoinRequested:
        /* Steam invites / "Join Game" are not used: they caused trouble. */
        if (!g_is_host && !g_lobby)
            say("(Steam invites aren't used. Click \"Join a friend\" to link with the host.)");
        break;
    case CB_NetMsgSessionRequest: {
        u64 from = S.Ident_GetSteamID64(p);
        bool ok = g_is_host ? true /* data is checked against the lobby */
                  : (g_host && from == g_host) ||
                    (g_mode == MODE_JOIN && !g_lobby && S.Friends_HasFriend(F, from, FRIEND_IMMEDIATE));
        if (ok) S.NM_AcceptSessionWithUser(NM, p);
        break;
    }
    case CB_NetMsgSessionFailed:
        say("(Steam: a connection attempt failed; it will retry.)");
        break;
    default:
        break;
    }
}

static void pump_callbacks(void)
{
    S.MD_RunFrame(g_pipe);
    CallbackMsg m;
    while (S.MD_GetNextCallback(g_pipe, &m)) {
        if (m.callback == CB_SteamAPICallCompleted && m.size >= 16) {
            u64 call = rd64(m.param);
            int cb = (int)rd32(m.param + 8);
            uint32_t sz = rd32(m.param + 12);
            uint8_t *buf = calloc(1, sz + 16);
            bool failed = false;
            if (buf && S.MD_GetAPICallResult(g_pipe, call, buf, (int)sz, cb, &failed) && !failed)
                handle_cb(cb, buf, (int)sz);
            else if (cb == CB_LobbyEnter && g_joining) {
                say("! Joining the lobby failed. Retrying in a moment.");
                g_joining = 0;
            }
            free(buf);
        } else {
            handle_cb(m.callback, m.param, m.size);
        }
        S.MD_FreeLastCallback(g_pipe);
    }
}

/* ---------- join: find a hosting friend ---------- */

static bool rejected(u64 lobby)
{
    for (int i = 0; i < g_nrejected; i++)
        if (g_rejected[i] == lobby) return true;
    return false;
}

static void scan_for_host(bool verbose)
{
    /* Two ways to see a hosting friend, because Steam doesn't always fill in
       the first: (1) the lobby in their "game played" info, (2) the
       "+connect_lobby <id>" their CoFLink puts in its Steam rich presence. */
    static u64 noted[32];
    static int nnoted;
    static UiPerson found[32];
    int n = 0, total = S.Friends_GetFriendCount(F, FRIEND_IMMEDIATE);
    for (int i = 0; i < total && n < 32; i++) {
        u64 id = S.Friends_GetFriendByIndex(F, i, FRIEND_IMMEDIATE);
        uint8_t info[32] = { 0 };
        if (!S.Friends_GetFriendGamePlayed(F, id, info)) continue;
        u64 game = rd64(info), lob = rd64(info + 16);
        if ((game & 0xFFFFFF) != SPACEWAR_APPID) continue;
        u64 t = now_ms();
        if (!lob) { /* 2nd way: the lobby in their Steam rich presence */
            S.Friends_RequestFriendRichPresence(F, id);
            const char *rp = S.Friends_GetFriendRichPresence(F, id, "connect");
            const char *c = rp ? strstr(rp, "+connect_lobby ") : 0;
            if (c) lob = str_to_u64(c + 15);
        }
        if (!lob) { /* 3rd way: ask their CoFLink directly */
            Answer *a = answer_for(id, true);
            if (a->lobby && t - a->at < 15000) lob = a->lobby;
            else if (!a->asked || t - a->asked > 4000) {
                static const char q[2] = { 'Q', PROTO[0] };
                send_ctl_data(id, q, 2);
                a->asked = t;
            }
        }
        if (!lob) {
            bool seen = false;
            for (int k = 0; k < nnoted; k++) seen |= noted[k] == id;
            if (!seen && nnoted < 32) {
                noted[nnoted++] = id;
                say("(%s is running CoFLink/Spacewar, but no lobby is visible yet - still checking.)", who(id));
            }
            continue;
        }
        if (rejected(lob)) continue;
        found[n].id = id;
        found[n].lobby = lob;
        sfmt(found[n].name, UI_NAME, "%s", who(id));
        n++;
    }
    static int last_n = -1;
    if (n != last_n && n > 0) say(n == 1 ? "Found %s hosting." : "Found %s friends hosting.", n == 1 ? found[0].name : idstr((u64)n));
    last_n = n;
    u64 reject = 0;
    u64 pick = ui_offer_hosts(n, found, &reject);
    if (reject && g_nrejected < 16) g_rejected[g_nrejected++] = reject;
    if (pick) request_join(pick);
    else if (!n && verbose) {
        say("No friend is hosting yet. Waiting...");
        say("  Or: paste the host's lobby id.");
    }
}

/* ---------- relay loops ---------- */

static void drain_steam(void)
{
    void *msgs[64];
    int n;
    while ((n = S.NM_ReceiveMessagesOnChannel(NM, CH_GAME, msgs, 64)) > 0) {
        for (int i = 0; i < n; i++) {
            void *m = msgs[i];
            u64 from = msg_sender(m);
            const void *d = msg_data(m);
            int sz = msg_size(m);
            int slot = sz > 0 ? ((const uint8_t *)d)[0] : -1;
            const uint8_t *body = (const uint8_t *)d + 1;
            int len = sz - 1;
            if (len <= 0 || len > MAX_GAME_PKT || !(slot < NSLOT || slot == SLOT_PROBE)) {
                /* not a CoFLink game packet: drop */
            } else if (g_is_host) {
                Peer *p = find_peer(from);
                if (!p) {
                    if (allowed_peer(from)) p = add_peer(from);
                    else note_ignored(from);
                }
                if (p && rate_ok(&p->rate, now_ms())) {
                    sock_t s_ = peer_sock(p, slot);
                    if (s_ != BADSOCK) udp_send(s_, body, len);
                    if (slot != SLOT_PROBE && !p->up++) p->first_up_ms = now_ms();
                    p->last_heard = now_ms();
                }
            } else if (from == g_host && rate_ok(&g_host_rate, now_ms())) {
                g_host_last = now_ms();
                if (slot == SLOT_PROBE) {
                    on_probe_reply(body, len);
                } else if (g_slot_port[slot]) {
                    udp_sendto(g_game_sock, body, len, g_game_ip, g_slot_port[slot]);
                    g_server_ready = true;
                    if (!g_down++ && !g_said_down) {
                        g_said_down = true;
                        say("The host's server answered - Cry of Fear should be loading in. Have fun!");
                    }
                }
            } else if (from == g_host) {
                g_host_last = now_ms();
            }
            S.Msg_Release(m);
        }
    }
    while ((n = S.NM_ReceiveMessagesOnChannel(NM, CH_CTL, msgs, 64)) > 0) {
        for (int i = 0; i < n; i++) {
            void *m = msgs[i];
            u64 from = msg_sender(m);
            int msz = msg_size(m);
            uint8_t cm[16] = { 0 };
            memcpy(cm, msg_data(m), (size_t)(msz < 16 ? (msz > 0 ? msz : 0) : 16));
            char c = (char)cm[0];
            S.Msg_Release(m);
            if (c == 'Q') { /* a friend's CoFLink asks "are you hosting?" */
                if (g_is_host && g_lobby && from != g_me && S.Friends_HasFriend(F, from, FRIEND_IMMEDIATE)) {
                    uint8_t a[10] = { 'L', (uint8_t)PROTO[0] };
                    memcpy(a + 2, &g_lobby, 8);
                    send_ctl_data(from, a, 10);
                }
                continue;
            }
            if (c == 'L') { /* answer: that friend hosts this lobby */
                if (g_mode == MODE_JOIN && msz >= 10 && S.Friends_HasFriend(F, from, FRIEND_IMMEDIATE)) {
                    Answer *a = answer_for(from, true);
                    a->lobby = rd64(cm + 2);
                    a->at = now_ms();
                }
                continue;
            }
            if (g_is_host) {
                Peer *p = find_peer(from);
                if (c == 'B') { if (p) drop_peer(from, "quit CoFLink"); continue; }
                if (!p) {
                    if (!allowed_peer(from)) { note_ignored(from); continue; }
                    p = add_peer(from);
                    if (!p) continue;
                }
                p->last_heard = now_ms();
                if (c == 'H') { send_ctl(from, 'W'); p->welcomed = true; }
            } else if (from == g_host) {
                g_host_last = now_ms();
                if (c == 'W' && !g_welcomed) {
                    g_welcomed = true;
                    say("Connected to %s through Steam.", who(g_host));
                } else if (c == 'B') {
                    say("! %s closed CoFLink.", who(g_host));
                    lobby_gone();
                }
            }
        }
    }
}

static void host_tick(void)
{
    u64 t = now_ms();
    for (int i = 0; i < g_npeers; i++) {
        Peer *p = &g_peers[i];
        for (int slot = 0; slot < SLOT_SOCKS; slot++) {
            if (p->s[slot] == BADSOCK) continue;
            for (int k = 0; k < 512; k++) {
                int r = udp_recv(p->s[slot], g_buf, BUF_SIZE);
                if (r < 0) break;
                if (r == 0 || r > MAX_GAME_PKT) continue;
                send_game(p->id, slot, g_buf, r);
                if (slot == SLOT_PROBE) continue;
                if (!p->down) say("Your server answered %s - they are loading in.", who(p->id));
                p->down++;
            }
        }
        if (t - p->last_ka >= KEEPALIVE_MS) { send_ctl(p->id, 'K'); p->last_ka = t; }
        if (p->up && !p->down && !p->warned_silent_server && t - p->first_up_ms > 8000) {
            p->warned_silent_server = true;
            say("! %s is trying to join, but nothing answers on 127.0.0.1:%u.", who(p->id), (unsigned)g_game_port);
            say("  Start the co-op game in Cry of Fear first (and type sv_lan 1 in its console).");
        }
        if (t - p->last_heard > PEER_TIMEOUT_MS) { drop_peer(p->id, "timed out"); i--; }
    }
}

static int slot_for(uint16_t port, u64 t)
{
    int lru = 0;
    for (int k = 0; k < NSLOT; k++) {
        if (g_slot_port[k] == port) { g_slot_used[k] = t; return k; }
        if (g_slot_used[k] < g_slot_used[lru]) lru = k;
    }
    for (int k = 0; k < NSLOT; k++)
        if (!g_slot_port[k]) { lru = k; break; }
    g_slot_port[lru] = port;
    g_slot_used[lru] = t;
    return lru;
}

static void probe_server(const uint8_t *challenge)
{
    /* A standard server-info query ("is your game server up?"), sent to the
       host's own Cry of Fear server through the relay. Any answer means yes.
       The old form (no challenge) is what Cry of Fear's server answers; a newer
       server that replies with a challenge gets asked again with it. */
    uint8_t q[32] = "\xFF\xFF\xFF\xFFTSource Engine Query";
    int n = 25; /* includes the final 0 */
    if (challenge) { memcpy(q + 25, challenge, 4); n = 29; }
    send_game(g_host, SLOT_PROBE, q, n);
}

/* An answer to CoFLink's own check arrived from the host's server. */
static void on_probe_reply(const uint8_t *b, int len)
{
    if (len >= 9 && b[0] == 0xFF && b[1] == 0xFF && b[2] == 0xFF && b[3] == 0xFF && b[4] == 'A') {
        probe_server(b + 5); /* server wants its challenge back */
        return;
    }
    if (!g_server_ready) {
        g_server_ready = true;
        say("%s's Cry of Fear co-op game is up.", who(g_host));
    }
}

/* Join side: say when the host's co-op game is up, so the friend knows to
   connect from Cry of Fear's console. CoFLink never connects the game itself. */
static void join_hints(u64 t)
{
    if (!g_welcomed) return;
    if (!g_welcomed_ms) g_welcomed_ms = t;
    if (!g_server_ready) {
        if (!g_said_wait_server && t - g_welcomed_ms > 5000) {
            g_said_wait_server = true;
            say("Waiting for %s to start their co-op game in Cry of Fear...", who(g_host));
        }
        return;
    }
    if (!g_said_ready && g_down < 50) {
        g_said_ready = true;
        char cmd[64];
        sfmt(cmd, sizeof cmd, "connect 127.0.0.1:%u", (unsigned)g_local_port);
        clipboard_set(cmd);
        say("Ready: in Cry of Fear press ~, then Ctrl+V and Enter  (%s)", cmd);
    }
}

static void join_tick(void)
{
    u64 t = now_ms();
    for (int k = 0; k < 512 && g_game_sock != BADSOCK; k++) {
        uint32_t ip;
        uint16_t port;
        int r = udp_recvfrom(g_game_sock, g_buf, BUF_SIZE, &ip, &port);
        if (r < 0) break;
        if (r == 0 || r > MAX_GAME_PKT || !g_lobby) continue;
        g_game_ip = ip;
        bool first = !g_up;
        int slot = slot_for(port, t);
        if (first) say("Cry of Fear is talking to CoFLink. Passing it to %s...", who(g_host));
        if (!g_up++) g_first_up_ms = t;
        send_game(g_host, slot, g_buf, r);
    }
    if (!g_lobby) return;
    if (!g_welcomed && t - g_last_hello > 2000) { send_ctl(g_host, 'H'); g_last_hello = t; }
    /* check the host's co-op server answers (every 2 s until it does) */
    if (g_welcomed && !g_server_ready && t - g_last_probe > 2000) {
        probe_server(0);
        g_last_probe = t;
    }
    if (t - g_last_ka >= KEEPALIVE_MS) { send_ctl(g_host, 'K'); g_last_ka = t; }
    if (t - g_host_last > HOST_QUIET_MS && !g_host_warned) {
        g_host_warned = true;
        say("! No answer from %s for 15 s. Is their CoFLink still open?", who(g_host));
    } else if (t - g_host_last < HOST_QUIET_MS && g_host_warned) {
        g_host_warned = false;
        say("%s is back.", who(g_host));
    }
    if (g_up && !g_down && !g_warned_no_answer && t - g_first_up_ms > 10000) {
        g_warned_no_answer = true;
        say("! The host's Cry of Fear server isn't answering yet. Ask them to start the co-op server.");
    }
    join_hints(t);
}

/* ---------- starting Cry of Fear without its launcher ---------- */

/* Cry of Fear's launcher (CoFLaunchApp.exe) is skipped: cof.exe is started
   directly with the options the Cry of Fear server browser uses. */
#define COF_ARGS "-basedir cryoffear -game cryoffear -hw -heapsize 1536000"

static char *read_small_file(const char *path, int *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    char *b = malloc(65537);
    if (!b) { fclose(f); return 0; }
    int n = (int)fread(b, 1, 65536, f);
    fclose(f);
    b[n < 0 ? 0 : n] = 0;
    *len = n;
    return b;
}

/* Next "key" "value" pair whose key matches, from position *pos. Handles \\ escapes. */
static bool vdf_value(const char *t, int *pos, const char *key, char *out, int n)
{
    char pat[48];
    sfmt(pat, sizeof pat, "\"%s\"", key);
    const char *k = strstr(t + *pos, pat);
    if (!k) return false;
    const char *v = strchr(k + strlen(pat), '"');
    if (!v) return false;
    v++;
    int o = 0;
    while (*v && *v != '"' && o < n - 1) {
        if (*v == '\\' && v[1]) v++;
        out[o++] = *v++;
    }
    out[o] = 0;
    *pos = (int)(v - t);
    return o > 0;
}

static bool cof_in(const char *dir)
{
    char exe[700];
    sfmt(exe, sizeof exe, "%s" PATHSEP "cof.exe", dir);
    return dir[0] && file_exists(exe);
}

/* Find the Cry of Fear folder: ask Steam, else read Steam's library list. */
static bool find_cof_dir(char *out, int n)
{
    if (AP && S.Apps_GetAppInstallDir && S.Apps_GetAppInstallDir(AP, COF_APPID, out, (uint32_t)n) > 0 && cof_in(out))
        return true;
    char steam[600], path[700], lib[600];
    if (!steam_install_path(steam, sizeof steam)) return false;
    sfmt(path, sizeof path, "%s" PATHSEP "steamapps" PATHSEP "libraryfolders.vdf", steam);
    int len = 0;
    char *vdf = read_small_file(path, &len);
    int pos = 0;
    bool found = false;
    for (int i = 0; i < 32 && !found; i++) {
        if (i == 0) sfmt(lib, sizeof lib, "%s", steam);              /* Steam's own library */
        else if (!vdf || !vdf_value(vdf, &pos, "path", lib, sizeof lib)) break;
        sfmt(path, sizeof path, "%s" PATHSEP "steamapps" PATHSEP "appmanifest_%d.acf", lib, COF_APPID);
        int alen = 0;
        char *acf = read_small_file(path, &alen);
        char inst[256];
        int apos = 0;
        if (acf && vdf_value(acf, &apos, "installdir", inst, sizeof inst)) {
            sfmt(out, n, "%s" PATHSEP "steamapps" PATHSEP "common" PATHSEP "%s", lib, inst);
            found = cof_in(out);
        }
        free(acf);
    }
    free(vdf);
    return found;
}

static void start_game(void)
{
    if (process_running("cof.exe")) { say("Cry of Fear is already running."); return; }
    char dir[600], exe[700];
    if (g_steam_ok && find_cof_dir(dir, sizeof dir)) {
        sfmt(exe, sizeof exe, "%s" PATHSEP "cof.exe", dir);
        if (run_program(exe, COF_ARGS, dir)) {
            say("Starting Cry of Fear (the game itself, without its launcher)...");
            return;
        }
        say("! Could not start %s.", exe);
    } else {
        say("(Couldn't find the Cry of Fear folder.)");
    }
    open_url("steam://run/223710");
    say("Starting Cry of Fear through Steam instead...");
}

/* ---------- modes ---------- */

static bool steam_start(void);

static void reset_all(void)
{
    for (int i = 0; i < g_npeers; i++) close_peer_socks(&g_peers[i]);
    g_npeers = 0;
    g_nignored = 0;
    udp_close(g_game_sock);
    g_game_sock = BADSOCK;
    g_joining = 0;
    g_nrejected = 0;
    memset(g_ans, 0, sizeof g_ans);
    g_local_port = g_local_port_base;
    reset_join_state();
}

static void stop_mode(bool quiet)
{
    if (g_mode == MODE_IDLE) return;
    if (g_is_host) {
        for (int i = 0; i < g_npeers; i++) send_ctl(g_peers[i].id, 'B');
    } else if (g_host) {
        send_ctl(g_host, 'B');
    }
    for (int i = 0; i < 20; i++) { S.MD_RunFrame(g_pipe); sleep_ms(10); } /* flush goodbyes */
    if (g_lobby) S.MM_LeaveLobby(MM, g_lobby);
    if (g_joining) S.MM_LeaveLobby(MM, g_joining);
    S.Friends_ClearRichPresence(F);
    reset_all();
    g_mode = MODE_IDLE;
    g_is_host = false;
    ui_list(LIST_FRIENDS_ONLINE, 0, 0);
    ui_list(LIST_HOSTING, 0, 0);
    if (!quiet) say("Stopped.");
}

static void start_mode(int mode)
{
    if (g_mode == mode) return;
    stop_mode(true);
    if (!g_steam_ok && !(g_steam_ok = steam_start())) return;
    g_mode = mode;
    g_is_host = mode == MODE_HOST;
    say("");
    if (g_is_host) {
        say("Creating a friends-only lobby for %d players...", g_max_players);
        S.MM_CreateLobby(MM, LOBBY_FRIENDS_ONLY, g_max_players);
    } else {
        say("Looking for a friend who is hosting...");
    }
}

static void make_status(UiState *st)
{
    memset(st, 0, sizeof *st);
    st->mode = g_mode;
    st->steam_ok = g_steam_ok;
    st->lobby = g_lobby;
    st->local_port = g_local_port;
    st->friends_connected = g_npeers;
    if (g_mode == MODE_IDLE) {
        sfmt(st->status, sizeof st->status, g_steam_ok ? "Ready. Host a game, or join a friend." :
             "Choose Host a game or Join a friend.");
    } else if (g_is_host) {
        int playing = 0;
        for (int i = 0; i < g_npeers; i++) playing += g_peers[i].down > 0;
        if (!g_lobby) sfmt(st->status, sizeof st->status, "Creating lobby...");
        else if (!g_npeers) sfmt(st->status, sizeof st->status, "Hosting - waiting for friends to join");
        else sfmt(st->status, sizeof st->status, "Hosting - %d friend(s) connected, %d in game", g_npeers, playing);
    } else {
        if (g_joining) sfmt(st->status, sizeof st->status, "Joining lobby...");
        else if (!g_lobby) sfmt(st->status, sizeof st->status, "Looking for a friend who is hosting...");
        else if (g_down >= 50) sfmt(st->status, sizeof st->status, "Playing with %s", who(g_host));
        else if (!g_welcomed) sfmt(st->status, sizeof st->status, "Reaching %s through Steam...", who(g_host));
        else if (!g_server_ready) sfmt(st->status, sizeof st->status, "Linked - waiting for %s to start their co-op game", who(g_host));
        else if (g_up) sfmt(st->status, sizeof st->status, "Your game is connecting to %s...", who(g_host));
        else sfmt(st->status, sizeof st->status, "Linked to %s - join from the game console (~)", who(g_host));
    }
}

static void print_status(void)
{
    if (g_is_host) {
        if (!g_npeers) { say("Status: lobby %s open, no friends connected yet.", idstr(g_lobby)); return; }
        for (int i = 0; i < g_npeers; i++) {
            Peer *p = &g_peers[i];
            say("Status: %s - %s (to your server %s, back %s packets)", who(p->id),
                p->down ? "playing" : p->up ? "waiting for your server to answer" : "connected, game not started yet",
                idstr(p->up), idstr(p->down));
        }
    } else if (g_mode == MODE_JOIN) {
        say("Status: %s, game packets sent %s, received %s.",
            g_lobby ? (g_welcomed ? "connected to host" : "waiting for host") : "looking for a host",
            idstr(g_up), idstr(g_down));
    } else {
        say("Status: idle.");
    }
}

static void handle_cmd(const Cmd *c)
{
    switch (c->kind) {
    case CMD_HOST: start_mode(MODE_HOST); break;
    case CMD_JOIN: start_mode(MODE_JOIN); break;
    case CMD_JOIN_LOBBY:
        if (!c->arg) { say("! That isn't a lobby id."); break; }
        if (g_mode != MODE_JOIN) start_mode(MODE_JOIN);
        if (g_mode == MODE_JOIN) { g_joining = 0; request_join(c->arg); }
        break;
    case CMD_LAUNCH: /* "Start Cry of Fear" */
        start_game();
        break;
    case CMD_STATUS: print_status(); break;
    case CMD_STOP: stop_mode(false); break;
    case CMD_QUIT: g_quit = 1; break;
    default: break;
    }
}

void engine_run(const EngineOpts *o, Cmd first)
{
    g_game_port = o->game_port ? o->game_port : 27015;
    g_local_port = g_local_port_base = o->local_port ? o->local_port : 27115;
    g_max_players = o->max_players >= 2 && o->max_players <= 8 ? o->max_players : 4;
    g_no_launch = o->no_launch;
    say("CoFLink " VERSION " - Cry of Fear co-op over Steam (no VPN, no port forwarding)");
    /* Connect to Steam right away (shows "Signed in as..."). If Steam isn't
       running yet, Host/Join retries. */
    g_steam_ok = steam_start();
    if (first.kind != CMD_NONE) handle_cmd(&first);

    u64 last_scan = 0, last_friends = 0, last_state = 0;
    UiState prev;
    memset(&prev, 0xff, sizeof prev);
    int prev_mode = -1;
    while (!g_quit) {
        if (g_steam_ok) {
            pump_callbacks();
            drain_steam();
        }
        u64 t = now_ms();
        if (g_mode != prev_mode) { last_scan = 0; last_friends = 0; prev_mode = g_mode; }
        if (g_mode == MODE_HOST) {
            host_tick();
            if (g_lobby && (!last_friends || t - last_friends > 1000)) { publish_connected(); last_friends = t; }
        } else if (g_mode == MODE_JOIN) {
            join_tick();
            if (!g_lobby && !g_joining && (!last_scan || t - last_scan > 3000)) {
                scan_for_host(last_scan == 0);
                last_scan = now_ms();
            }
        }
        Cmd c;
        while (ui_poll(&c)) handle_cmd(&c);

        if (t - last_state > 200) {
            UiState st;
            make_status(&st);
            if (memcmp(&st, &prev, sizeof st) != 0) { ui_state(&st); prev = st; }
            last_state = t;
        }

        sock_t socks[64];
        int ns = 0;
        if (g_mode == MODE_HOST) {
            for (int i = 0; i < g_npeers; i++)
                for (int k = 0; k < SLOT_SOCKS && ns < 64; k++)
                    if (g_peers[i].s[k] != BADSOCK) socks[ns++] = g_peers[i].s[k];
        } else if (g_mode == MODE_JOIN) {
            socks[ns++] = g_game_sock;
        }
        wait_socks(socks, ns, g_mode == MODE_IDLE ? 20 : 1);
    }
    if (g_steam_ok) {
        stop_mode(true);
        S.Shutdown();
    }
    say("CoFLink closed.");
}

/* ---------- start-up ---------- */

static bool steam_start(void)
{
    file_write_text("steam_appid.txt", "480");
    const char *e = steam_load();
    if (e) { say("! %s", e); return false; }
    if (!S.IsSteamRunning()) { say("! Steam is not running. Start Steam, log in, then try again."); return false; }
    char err[1024] = { 0 };
    int r = S.InitFlat(err);
    if (r != 0) {
        say("! Steam would not start the connection (code %d): %s", r, err);
        say("  Make sure Steam is running and logged in (not in offline mode).");
        return false;
    }
    S.MD_Init();
    g_pipe = S.GetHSteamPipe();
    U = S.SteamUser();
    F = S.SteamFriends();
    MM = S.SteamMatchmaking();
    NM = S.SteamNetMsgs();
    NU = S.SteamNetUtils();
    AP = S.SteamApps();
    if (!U || !F || !MM || !NM || !NU) { say("! Steam interfaces unavailable."); return false; }
    if (!S.User_BLoggedOn(U)) { say("! Steam is in offline mode. Go online and try again."); S.Shutdown(); return false; }
    g_me = S.User_GetSteamID(U);
    S.NU_InitRelayNetworkAccess(NU);
    say("Signed in to Steam as %s.", who(g_me));
    return true;
}
