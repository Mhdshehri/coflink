/* CoFLink - play Cry of Fear co-op with a Steam friend: no VPN, no port forwarding.
 *
 * Host:  creates a friends-only Steam lobby (through Valve's free "Spacewar"
 *        test app, AppID 480), invites friends, and forwards each friend's
 *        packets into the host's own Cry of Fear co-op server on 127.0.0.1:27015.
 * Join:  finds the host (friend list or Steam invite), joins the lobby, and
 *        opens 127.0.0.1:27115 for the friend's Cry of Fear to connect to.
 * Packets travel through Steam's relay network (ISteamNetworkingMessages).
 * The game is not modified. Pattern: coop-anything HXCoop relay
 * (playbook/networking-steam-spacewar.md). MIT license. */
#include "steam.h"
#include "plat.h"

#define VERSION "1.1"
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

static void *U, *F, *MM, *NM, *NU;
static int32_t g_pipe;
static u64 g_me, g_lobby, g_host, g_joining;
static bool g_is_host;
static uint16_t g_game_port = 27015;  /* host: port of the Cry of Fear server */
static uint16_t g_local_port = 27115; /* join: port the friend's game connects to */
static int g_max_players = 4;
static bool g_no_launch;
static bool g_auto_yes; /* --yes: join a found host without asking (tests) */
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
    printf("%s\n", b);
    fflush(0);
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

static bool ask_yes(const char *question)
{
    if (g_auto_yes) return true;
    printf("%s (Y/N): ", question);
    fflush(0);
    char line[16];
    if (!read_line(line, sizeof line)) return false;
    return line[0] == 'y' || line[0] == 'Y';
}

/* ---------- Steam helpers ---------- */

static void ident_of(uint8_t b[IDENT_SIZE], u64 id)
{
    memset(b, 0, IDENT_SIZE);
    S.Ident_SetSteamID64(b, id);
}

static void send_game(u64 to, const void *p, int n)
{
    uint8_t id[IDENT_SIZE];
    ident_of(id, to);
    /* Cry of Fear (GoldSrc) does its own reliability: send unreliable. */
    S.NM_SendMessageToUser(NM, id, p, (uint32_t)n, SEND_UNRELIABLE | SEND_NONAGLE, CH_GAME);
}

static void send_ctl(u64 to, char c)
{
    uint8_t id[IDENT_SIZE];
    ident_of(id, to);
    char m[2] = { c, '1' }; /* command + protocol version */
    S.NM_SendMessageToUser(NM, id, m, 2, SEND_RELIABLE | SEND_NONAGLE, CH_CTL);
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
    sock_t s;
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
    sock_t s = udp_open_local(0);
    if (s == BADSOCK || !udp_connect_local(s, g_game_port)) {
        udp_close(s);
        say("! Could not open a local socket for %s.", who(id));
        return 0;
    }
    Peer *p = &g_peers[g_npeers++];
    memset(p, 0, sizeof *p);
    p->id = id;
    p->s = s;
    p->last_heard = now_ms();
    say("+ %s connected through Steam (relay port %u -> your server 127.0.0.1:%u).",
        who(id), (unsigned)udp_local_port(s), (unsigned)g_game_port);
    return p;
}

static void drop_peer(u64 id, const char *why)
{
    for (int i = 0; i < g_npeers; i++) {
        if (g_peers[i].id != id) continue;
        say("- %s disconnected (%s).", who(id), why);
        udp_close(g_peers[i].s);
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
static uint16_t g_game_cport;
static bool g_have_game, g_welcomed, g_host_warned, g_said_down, g_warned_no_answer;
static u64 g_host_last, g_last_hello, g_last_ka, g_up, g_down, g_first_up_ms;
static u64 g_launch_ms;
static bool g_reminded;
static u64 g_rejected[16];
static int g_nrejected;

static void reset_join_state(void)
{
    g_lobby = g_host = 0;
    g_welcomed = g_host_warned = g_said_down = g_warned_no_answer = false;
    g_up = g_down = g_first_up_ms = 0;
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

static void launch_game(void)
{
    char cmd[64], url[160];
    sfmt(cmd, sizeof cmd, "connect 127.0.0.1:%u", (unsigned)g_local_port);
    clipboard_set(cmd);
    say("");
    say("=== Now start Cry of Fear and connect ===");
    if (!g_no_launch) {
        sfmt(url, sizeof url, "steam://run/%d//-console%%20+connect%%20127.0.0.1:%u/", COF_APPID, (unsigned)g_local_port);
        open_url(url);
        say("Starting Cry of Fear through Steam (if Steam asks to allow the launch options, click OK).");
        say("It may connect by itself. If you end up in the main menu instead:");
    } else {
        say("Start Cry of Fear (or switch to it), then:");
    }
    say("  1. press ~ to open the console (it opens by itself when CoFLink starts the game)");
    say("  2. press Ctrl+V and Enter - this is already on your clipboard:");
    say("       %s", cmd);
    say("");
    g_launch_ms = now_ms();
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
    g_lobby = lobby;
    g_host = S.MM_GetLobbyOwner(MM, lobby);
    g_host_last = now_ms();
    say("Joined %s's Cry of Fear lobby. Connecting to them through Steam...", who(g_host));
    S.Friends_SetRichPresence(F, "status", "Playing Cry of Fear co-op (CoFLink)");
    send_ctl(g_host, 'H');
    g_last_hello = now_ms();
    if (open_game_socket()) launch_game();
}

/* ---------- host: lobby + invites ---------- */

static void on_lobby_created(int32_t result, u64 lobby)
{
    if (!g_is_host || g_lobby) return; /* arrives twice on real Steam */
    if (result != 1) {
        say("! Steam could not create a lobby (error %d). Check your internet and restart CoFLink.", result);
        g_quit = 1;
        return;
    }
    g_lobby = lobby;
    char conn[64];
    sfmt(conn, sizeof conn, "+connect_lobby %s", idstr(lobby));
    S.MM_SetLobbyData(MM, lobby, "coflink", "1");
    S.MM_SetLobbyData(MM, lobby, "coflink_ver", VERSION);
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
    say("2. Tell your friend to run Join-CoF.bat. It finds you on its own.");
    say("   If it doesn't: press I here to send them a Steam invite, or send them");
    say("   this lobby id, which they paste after pressing L in their window: %s", idstr(lobby));
    say("Keys:  I = invite a friend   S = status   Q = quit");
    say("");
}

static void invite_menu(void)
{
    u64 ids[200];
    int n = 0, total = S.Friends_GetFriendCount(F, FRIEND_IMMEDIATE);
    say("Friends online:");
    for (int i = 0; i < total && n < 200; i++) {
        u64 id = S.Friends_GetFriendByIndex(F, i, FRIEND_IMMEDIATE);
        if (S.Friends_GetFriendPersonaState(F, id) == 0) continue; /* offline */
        ids[n++] = id;
        say("  %2d. %s", n, who(id));
    }
    if (!n) { say("  (nobody is online)"); return; }
    printf("Number to invite (Enter = cancel): ");
    fflush(0);
    char line[32];
    if (!read_line(line, sizeof line) || !line[0]) { say("Cancelled."); return; }
    int k = atoi(line);
    if (k < 1 || k > n) { say("No friend with that number."); return; }
    if (S.MM_InviteUserToLobby(MM, g_lobby, ids[k - 1]))
        say("Invite sent to %s. They must run Join-CoF.bat, then accept it.", who(ids[k - 1]));
    else
        say("! Steam would not send the invite.");
}

static void host_status(void)
{
    if (!g_npeers) { say("Status: lobby %s open, no friends connected yet.", idstr(g_lobby)); return; }
    for (int i = 0; i < g_npeers; i++) {
        Peer *p = &g_peers[i];
        say("Status: %s - %s (to your server %s, back %s packets)", who(p->id),
            p->down ? "playing" : p->up ? "waiting for your server to answer" : "connected, game not started yet",
            idstr(p->up), idstr(p->down));
    }
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
                S.MM_LeaveLobby(MM, g_lobby);
                reset_join_state();
            }
        }
        break;
    }
    case CB_GameLobbyJoinRequested:
        if (size >= 16) {
            say("Accepted invite from %s.", who(rd64(p + 8)));
            request_join(rd64(p));
        }
        break;
    case CB_GameRichPresenceJoinRequested: {
        if (size < 9) break;
        char conn[257] = { 0 };
        memcpy(conn, p + 8, (size_t)(size - 8 < 256 ? size - 8 : 256));
        const char *c = strstr(conn, "+connect_lobby ");
        if (c) request_join(str_to_u64(c + 15));
        break;
    }
    case CB_NetMsgSessionRequest: {
        u64 from = S.Ident_GetSteamID64(p);
        bool ok = g_is_host ? true /* data is checked against the lobby */ : from == g_host;
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
    u64 lobs[32], ids[32];
    int n = 0, total = S.Friends_GetFriendCount(F, FRIEND_IMMEDIATE);
    for (int i = 0; i < total && n < 32; i++) {
        u64 id = S.Friends_GetFriendByIndex(F, i, FRIEND_IMMEDIATE);
        uint8_t info[32] = { 0 };
        if (!S.Friends_GetFriendGamePlayed(F, id, info)) continue;
        u64 game = rd64(info), lob = rd64(info + 16);
        if ((game & 0xFFFFFF) != SPACEWAR_APPID) continue;
        if (!lob) {
            S.Friends_RequestFriendRichPresence(F, id);
            const char *rp = S.Friends_GetFriendRichPresence(F, id, "connect");
            const char *c = rp ? strstr(rp, "+connect_lobby ") : 0;
            if (c) lob = str_to_u64(c + 15);
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
        ids[n] = id;
        lobs[n++] = lob;
    }
    if (n == 1) {
        char q[160];
        sfmt(q, sizeof q, "%s is hosting. Join their game?", who(ids[0]));
        if (ask_yes(q)) request_join(lobs[0]);
        else if (g_nrejected < 16) { g_rejected[g_nrejected++] = lobs[0]; say("OK, not joining. Still looking..."); }
    } else if (n > 1) {
        say("Several friends are hosting:");
        for (int i = 0; i < n; i++) say("  %d. %s", i + 1, who(ids[i]));
        printf("Number to join: ");
        fflush(0);
        char line[32];
        int k = read_line(line, sizeof line) ? atoi(line) : 0;
        if (k >= 1 && k <= n) request_join(lobs[k - 1]);
    } else if (verbose) {
        say("No friend is hosting yet. Waiting...");
        say("  Or: accept the host's Steam invite, or press L and paste their lobby id.");
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
            if (sz <= 0 || sz > MAX_GAME_PKT) {
                /* not a Cry of Fear packet: drop */
            } else if (g_is_host) {
                Peer *p = find_peer(from);
                if (!p) {
                    if (allowed_peer(from)) p = add_peer(from);
                    else note_ignored(from);
                }
                if (p && rate_ok(&p->rate, now_ms())) {
                    if (!p->up) p->first_up_ms = now_ms();
                    udp_send(p->s, d, sz);
                    p->up++;
                    p->last_heard = now_ms();
                }
            } else if (from == g_host && g_have_game && rate_ok(&g_host_rate, now_ms())) {
                udp_sendto(g_game_sock, d, sz, g_game_ip, g_game_cport);
                if (!g_down++ && !g_said_down) {
                    g_said_down = true;
                    say("The host's server answered - Cry of Fear should be loading in. Have fun!");
                }
                g_host_last = now_ms();
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
            char c = msg_size(m) > 0 ? *(const char *)msg_data(m) : 0;
            S.Msg_Release(m);
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
                    S.MM_LeaveLobby(MM, g_lobby);
                    reset_join_state();
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
        for (int k = 0; k < 512; k++) {
            int r = udp_recv(p->s, g_buf, BUF_SIZE);
            if (r < 0) break;
            if (r == 0 || r > MAX_GAME_PKT) continue;
            if (!p->down) say("Your server answered %s - they are loading in.", who(p->id));
            send_game(p->id, g_buf, r);
            p->down++;
        }
        if (t - p->last_ka >= KEEPALIVE_MS) { send_ctl(p->id, 'K'); p->last_ka = t; }
        if (p->up && !p->down && !p->warned_silent_server && t - p->first_up_ms > 8000) {
            p->warned_silent_server = true;
            say("! %s is trying to join, but nothing answers on 127.0.0.1:%u.", who(p->id), (unsigned)g_game_port);
            say("  Start the co-op server in Cry of Fear first. If you use another port, run: CoFLink host --port <port>");
        }
        if (t - p->last_heard > PEER_TIMEOUT_MS) { drop_peer(p->id, "timed out"); i--; }
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
        if (r == 0 || r > MAX_GAME_PKT) continue;
        if (!g_have_game || port != g_game_cport) {
            g_have_game = true;
            g_game_ip = ip;
            g_game_cport = port;
            if (g_lobby) say("Cry of Fear is talking to CoFLink. Passing it to %s...", who(g_host));
        }
        if (g_lobby) {
            if (!g_up++) g_first_up_ms = t;
            send_game(g_host, g_buf, r);
        }
    }
    if (!g_lobby) return;
    if (g_launch_ms && !g_have_game && !g_reminded && t - g_launch_ms > 45000) {
        g_reminded = true;
        char cmd[64];
        sfmt(cmd, sizeof cmd, "connect 127.0.0.1:%u", (unsigned)g_local_port);
        clipboard_set(cmd);
        say("Cry of Fear hasn't connected yet. In the game: press ~, then Ctrl+V and Enter.");
        say("  (copied again:  %s )", cmd);
    }
    if (!g_welcomed && t - g_last_hello > 2000) { send_ctl(g_host, 'H'); g_last_hello = t; }
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
}

static void relay_loop(void)
{
    u64 last_scan = 0;
    while (!g_quit) {
        pump_callbacks();
        drain_steam();
        if (g_is_host) {
            host_tick();
        } else {
            join_tick();
            if (!g_lobby && !g_joining && now_ms() - last_scan > 3000) {
                scan_for_host(last_scan == 0);
                last_scan = now_ms();
            }
        }
        int key = key_poll();
        if (key == 'q' || key == 'Q') g_quit = 1;
        if (g_is_host && g_lobby && (key == 'i' || key == 'I')) invite_menu();
        if (g_is_host && (key == 's' || key == 'S')) host_status();
        if (!g_is_host && !g_lobby && (key == 'l' || key == 'L')) {
            printf("Paste the host's lobby id (right-click or Ctrl+V), then Enter: ");
            fflush(0);
            char line[64];
            u64 id = read_line(line, sizeof line) ? str_to_u64(line) : 0;
            if (id) { g_joining = 0; request_join(id); }
            else say("That isn't a lobby id. Press L to try again.");
        }
        if (!g_is_host && (key == 's' || key == 'S'))
            say("Status: %s, game packets sent %s, received %s.",
                g_lobby ? (g_welcomed ? "connected to host" : "waiting for host") : "looking for a host",
                idstr(g_up), idstr(g_down));

        sock_t socks[MAX_PEERS + 1];
        int ns = 0;
        if (g_is_host) for (int i = 0; i < g_npeers; i++) socks[ns++] = g_peers[i].s;
        else socks[ns++] = g_game_sock;
        wait_socks(socks, ns, 1);
    }
}

/* ---------- start-up and shut-down ---------- */

static bool steam_start(void)
{
    file_write_text("steam_appid.txt", "480");
    const char *e = steam_load();
    if (e) { say("! %s", e); return false; }
    if (!S.IsSteamRunning()) { say("! Steam is not running. Start Steam, log in, then run this again."); return false; }
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
    if (!U || !F || !MM || !NM || !NU) { say("! Steam interfaces unavailable."); return false; }
    if (!S.User_BLoggedOn(U)) { say("! Steam is in offline mode. Go online and try again."); return false; }
    g_me = S.User_GetSteamID(U);
    S.NU_InitRelayNetworkAccess(NU);
    say("Signed in to Steam as %s.", S.Friends_GetPersonaName(F));
    return true;
}

static void shutdown_all(void)
{
    if (g_is_host) {
        for (int i = 0; i < g_npeers; i++) send_ctl(g_peers[i].id, 'B');
    } else if (g_host) {
        send_ctl(g_host, 'B');
    }
    for (int i = 0; i < 20; i++) { S.MD_RunFrame(g_pipe); sleep_ms(10); } /* flush goodbyes */
    if (g_lobby) S.MM_LeaveLobby(MM, g_lobby);
    S.Friends_ClearRichPresence(F);
    for (int i = 0; i < g_npeers; i++) udp_close(g_peers[i].s);
    udp_close(g_game_sock);
    S.Shutdown();
    say("CoFLink closed.");
}

static void usage(void)
{
    say("CoFLink " VERSION " - Cry of Fear co-op over Steam (no VPN, no port forwarding)");
    say("  CoFLink host [--port 27015] [--players 4]   host a game for your friends");
    say("  CoFLink join [lobby id] [--port 27115] [--no-launch]   join a friend");
}

int main(int argc, char **argv)
{
    plat_init();
    int mode = 0; /* 1 host, 2 join */
    u64 join_lobby = 0;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "host")) mode = 1;
        else if (!strcmp(a, "join")) mode = 2;
        else if (!strcmp(a, "--port") && i + 1 < argc) {
            int p = atoi(argv[++i]);
            if (p > 0 && p < 65536) { g_game_port = (uint16_t)p; g_local_port = (uint16_t)p; }
        } else if (!strcmp(a, "--players") && i + 1 < argc) {
            g_max_players = atoi(argv[++i]);
            if (g_max_players < 2 || g_max_players > 8) g_max_players = 4;
        } else if (!strcmp(a, "--no-launch")) g_no_launch = true;
        else if (!strcmp(a, "--yes")) g_auto_yes = true;
        else if (!strcmp(a, "+connect_lobby") && i + 1 < argc) { mode = 2; join_lobby = str_to_u64(argv[++i]); }
        else if (a[0] >= '0' && a[0] <= '9') join_lobby = str_to_u64(a);
        else if (!strcmp(a, "-h") || !strcmp(a, "--help") || !strcmp(a, "/?")) { usage(); return 0; }
    }
    /* --port means the server port when hosting and the local port when joining */
    if (mode == 1) g_local_port = 27115;
    if (mode == 2 && g_game_port != 27015) g_local_port = g_game_port;

    if (!mode) {
        usage();
        printf("\n1 = host a game, 2 = join a friend: ");
        fflush(0);
        char line[16];
        if (!read_line(line, sizeof line)) return 1;
        mode = line[0] == '1' ? 1 : line[0] == '2' ? 2 : 0;
        if (!mode) return 1;
    }
    g_is_host = mode == 1;
    set_title(g_is_host ? "CoFLink - hosting Cry of Fear co-op" : "CoFLink - joining Cry of Fear co-op");
    say("CoFLink " VERSION " (%s)", g_is_host ? "host" : "join");
    if (!steam_start()) return 1;

    if (g_is_host) {
        say("Creating a friends-only lobby for %d players...", g_max_players);
        S.MM_CreateLobby(MM, LOBBY_FRIENDS_ONLY, g_max_players);
    } else if (join_lobby) {
        request_join(join_lobby);
    } else {
        say("Looking for a friend who is hosting...");
    }
    relay_loop();
    shutdown_all();
    return 0;
}
