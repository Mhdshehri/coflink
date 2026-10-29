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

#ifndef VERSION /* the tests build a second copy with a different version */
#define VERSION "0.9.6"
#endif
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
   source on the friend's PC (normally just Cry of Fear), so answers go back
   to the right program. */
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
static uint8_t g_buf[BUF_SIZE];


/* ---------- output ---------- */

void lobby_code(u64 lobby, char out[16]);

static const char *codestr(u64 lobby)
{
    static char ring[4][16];
    static int k;
    char *o = ring[k++ & 3];
    lobby_code(lobby, o);
    return o;
}

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
    if (n && n[i]) { /* cut short: don't leave half of a UTF-8 character (e.g. Arabic) */
        int j = i;
        while (j > 0 && ((unsigned char)o[j - 1] & 0xC0) == 0x80) j--; /* back to the character's first byte */
        if (j > 0) {
            unsigned char lead = (unsigned char)o[j - 1];
            int need = lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : lead >= 0xC0 ? 2 : 1;
            if (j - 1 + need > i) i = j - 1; /* incomplete: drop it */
        }
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

/* 'H' hello: command, protocol, then this CoFLink's version (older hosts
   only read the first byte) */
static void send_hello(void)
{
    char m[16] = { 'H', PROTO[0] };
    int n = (int)strlen(VERSION);
    memcpy(m + 2, VERSION, (size_t)n);
    send_ctl_data(g_host, m, 2 + n);
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
static unsigned g_nignored;
/* Friends the host kicked: blocked until the host stops hosting. */
static u64 g_kicked[32], g_kick_told[32];
static int g_nkicked;

static int kicked_index(u64 id)
{
    for (int i = 0; i < g_nkicked; i++)
        if (g_kicked[i] == id) return i;
    return -1;
}
static bool kicked(u64 id) { return kicked_index(id) >= 0; }

/* "0.9" vs "0.10.1": compares number by number. <0, 0, >0 like strcmp. */
static int ver_cmp(const char *a, const char *b)
{
    for (int k = 0; k < 4; k++) {
        int x = 0, y = 0;
        while (*a >= '0' && *a <= '9') { if (x < 100000) x = x * 10 + (*a - '0'); a++; }
        while (*b >= '0' && *b <= '9') { if (y < 100000) y = y * 10 + (*b - '0'); b++; }
        if (x != y) return x < y ? -1 : 1;
        if (*a == '.') a++;
        if (*b == '.') b++;
    }
    return 0;
}

/* Tell the player when a friend runs a different CoFLink version. */
static void version_note(u64 friend_, const char *ver)
{
    if (!ver || !*ver) return;
    int c = ver_cmp(ver, VERSION);
    if (c > 0)
        say("! %s has a newer CoFLink (%s, you have " VERSION "). Update yours: github.com/Mhdshehri/coflink/releases",
            who(friend_), clean(ver));
    else if (c < 0)
        say("(%s has an older CoFLink (%s). It still works; tell them CoFLink " VERSION " is out.)",
            who(friend_), clean(ver));
}

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
    ui_alert("friend linked");
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
    return id && id != g_me && !kicked(id) && in_lobby(id) && S.Friends_HasFriend(F, id, FRIEND_IMMEDIATE);
}

/* Host: "Kick" button. The friend is cut off and can't link again until the
   host stops hosting. Their CoFLink is told so it stops trying. */
static void kick_peer(u64 id)
{
    if (!g_is_host || !id || id == g_me) return;
    if (!kicked(id) && g_nkicked < 32) { g_kick_told[g_nkicked] = now_ms(); g_kicked[g_nkicked++] = id; }
    bool linked = find_peer(id) != 0;
    send_ctl(id, 'X');
    if (linked) drop_peer(id, "kicked by you");
    say("%s is kicked. They can't link to this game again until you click Stop.", who(id));
}

static void note_ignored(u64 id)
{
    int k = kicked_index(id);
    if (k >= 0) { /* still trying: tell them again, at most every 2 s */
        if (now_ms() - g_kick_told[k] > 2000) { send_ctl(id, 'X'); g_kick_told[k] = now_ms(); }
        return;
    }
    for (int i = 0; i < 16; i++)
        if (g_ignored[i] == id) return;
    g_ignored[g_nignored++ % 16] = id; /* said once each (the last 16 people) */
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
static bool g_welcomed, g_host_warned, g_warned_slow, g_said_down, g_warned_no_answer;
static u64 g_host_last, g_last_hello, g_last_ka, g_up, g_down, g_first_up_ms;
/* CoFLink checks the host's co-op server answers, to tell the friend when to connect */
static bool g_server_ready, g_said_wait_server, g_said_ready;
static u64 g_last_probe, g_welcomed_ms;
static u64 g_rejected[16];
static int g_nrejected;
/* Direct discovery: "are you hosting?" ('Q') to friends running CoFLink;
   a host answers 'L' + lobby id, only to people on its friends list. */
typedef struct { u64 friend_, lobby, at, asked; } Answer;
#define NANS 256
static Answer g_ans[NANS];

static Answer *answer_for(u64 friend_, bool make)
{
    int free_ = -1, oldest = 0;
    for (int i = 0; i < NANS; i++) {
        if (g_ans[i].friend_ == friend_) return &g_ans[i];
        if (!g_ans[i].friend_ && free_ < 0) free_ = i;
        if (g_ans[i].asked < g_ans[oldest].asked) oldest = i;
    }
    if (!make) return 0;
    if (free_ < 0) free_ = oldest; /* full: reuse the one asked longest ago */
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
    g_welcomed = g_host_warned = g_warned_slow = g_said_down = g_warned_no_answer = false;
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
    say("Joining lobby %s ...", codestr(lobby));
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
    say("Linked to %s. Connect command copied: %s", who(g_host), cmd); /* the steps are in "How to" */
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
    u64 owner = S.MM_GetLobbyOwner(MM, lobby);
    if (!S.Friends_HasFriend(F, owner, FRIEND_IMMEDIATE)) {
        say("! %s hosts that game, but is not on your Steam friends list, so they can't let you in.", who(owner));
        say("  Add %s as a Steam friend, then click Refresh.", who(owner));
        S.MM_LeaveLobby(MM, lobby);
        return;
    }
    version_note(owner, S.MM_GetLobbyData(MM, lobby, "coflink_ver"));
    g_lobby = lobby;
    g_host = S.MM_GetLobbyOwner(MM, lobby);
    g_host_last = now_ms();
    say("Joined %s's Cry of Fear lobby. Connecting to them through Steam...", who(g_host));
    S.Friends_SetRichPresence(F, "status", "Playing Cry of Fear co-op (CoFLink)");
    send_hello();
    g_last_hello = now_ms();
    if (open_game_socket()) joined_message();
}

/* ---------- host: the lobby ---------- */

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
    S.MM_SetLobbyData(MM, lobby, "coflink", "1");
    S.MM_SetLobbyData(MM, lobby, "coflink_ver", VERSION);
    S.MM_SetLobbyData(MM, lobby, "coflink_proto", PROTO);
    S.MM_SetLobbyJoinable(MM, lobby, true);
    S.Friends_SetRichPresence(F, "status", "Hosting Cry of Fear co-op (CoFLink)");
    /* Not the "connect" key: that one gives friends a Steam "Join Game"
       button, which only launches Spacewar. */
    S.Friends_SetRichPresence(F, "coflink_lobby", idstr(lobby));
    clipboard_set(codestr(lobby));
    say("Lobby is ready (code %s, copied to clipboard). Friends can link now.", codestr(lobby));
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
        /* Host: only Steam friends get a session (their data is then checked
           against the lobby). Friend: only the host, or friends answering
           "are you hosting?" while looking. */
        u64 from = S.Ident_GetSteamID64(p);
        bool ok = g_is_host ? S.Friends_HasFriend(F, from, FRIEND_IMMEDIATE)
                  : (g_host && from == g_host) ||
                    (g_mode == MODE_JOIN && !g_lobby && S.Friends_HasFriend(F, from, FRIEND_IMMEDIATE));
        if (ok) S.NM_AcceptSessionWithUser(NM, p);
        else if (g_is_host) note_ignored(from);
        break;
    }
    case CB_NetMsgSessionFailed:
        /* Asking friends who don't run CoFLink fails quietly; only report
           trouble reaching the host. (The callback starts with the remote
           identity.) */
        if (g_host && size >= 16 && S.Ident_GetSteamID64(p) == g_host)
            say("(Steam: a connection attempt to the host failed; it will retry.)");
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

static int g_last_found_n = -1;
static int g_refresh;      /* Refresh button: 1 = scan now, 2 = scan again shortly and report */
static u64 g_refresh_at;

static int scan_for_host(bool verbose)
{
    /* Ways to see a hosting friend: (1) the lobby in their "game played" info,
       (2) the lobby id their CoFLink puts in its rich presence -
       both only while Steam shows them in CoFLink ("Spacewar"). Once the host
       starts Cry of Fear, Steam shows "playing Cry of Fear" instead and both
       go blank, so (3) CoFLink asks friends' CoFLink directly: every few
       seconds for friends in Spacewar or Cry of Fear, and every online friend
       when Refresh is clicked (or every 30 s). Only a host answers, and only
       to people on its own friends list. */
    static u64 noted[32];
    static int nnoted;
    static UiPerson found[32];
    int n = 0, total = S.Friends_GetFriendCount(F, FRIEND_IMMEDIATE);
    for (int i = 0; i < total && n < 32; i++) {
        u64 id = S.Friends_GetFriendByIndex(F, i, FRIEND_IMMEDIATE);
        uint8_t info[32] = { 0 };
        bool playing = S.Friends_GetFriendGamePlayed(F, id, info);
        uint32_t app = playing ? (uint32_t)(rd64(info) & 0xFFFFFF) : 0;
        bool in_coflink = app == SPACEWAR_APPID, in_cof = app == COF_APPID;
        /* A friend who only JOINED someone's game is in a CoFLink lobby too, so
           a lobby on its own doesn't mean they host. It counts only when the
           friend's own CoFLink says it hosts that lobby: in its rich presence
           (set by hosts only) or in a direct answer (hosts only answer). */
        u64 lob = in_coflink ? rd64(info + 16) : 0;
        bool host = false;
        u64 t = now_ms();
        if (in_coflink) {
            S.Friends_RequestFriendRichPresence(F, id);
            const char *rp = S.Friends_GetFriendRichPresence(F, id, "coflink_lobby");
            u64 r = (rp && *rp >= '0' && *rp <= '9') ? str_to_u64(rp) : 0;
            if (r && (!lob || r == lob)) { lob = r; host = true; }
        }
        Answer *a = answer_for(id, false);
        if (!host && a && a->lobby && t - a->at < 15000 && (!lob || a->lobby == lob)) { lob = a->lobby; host = true; }
        if (!host && S.Friends_GetFriendPersonaState(F, id) != 0) { /* online: ask their CoFLink directly */
            u64 every = (in_coflink || in_cof) ? 4000 : 30000;
            if (g_refresh == 1) every = 0; /* Refresh: ask everyone now */
            if (!a) a = answer_for(id, true);
            if (!a->asked || t - a->asked > every) {
                static const char q[2] = { 'Q', PROTO[0] };
                send_ctl_data(id, q, 2);
                a->asked = t;
            }
        }
        if (!host) {
            if (lob || !in_coflink) continue; /* in someone's lobby as a player, or not in CoFLink */
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
    if (n != g_last_found_n && n > 0) say(n == 1 ? "Found %s hosting." : "Found %s friends hosting.", n == 1 ? found[0].name : idstr((u64)n));
    g_last_found_n = n;
    u64 reject = 0;
    u64 pick = ui_offer_hosts(n, found, &reject);
    if (reject && g_nrejected < 16) g_rejected[g_nrejected++] = reject;
    if (pick) request_join(pick);
    else if (!n && verbose) {
        say("No friend is hosting yet. Waiting...");
        say("  Or: paste the host's lobby code.");
    }
    return n;
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
                if (g_is_host && g_lobby && from != g_me && !kicked(from) && S.Friends_HasFriend(F, from, FRIEND_IMMEDIATE)) {
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
                if (c == 'H') {
                    send_ctl(from, 'W');
                    if (!p->welcomed && msz > 2) { /* their version follows */
                        char v[16] = { 0 };
                        int vn = msz - 2 < 13 ? msz - 2 : 13;
                        memcpy(v, cm + 2, (size_t)vn);
                        version_note(from, v);
                    }
                    p->welcomed = true;
                }
            } else if (from == g_host) {
                g_host_last = now_ms();
                if (c == 'W' && !g_welcomed) {
                    g_welcomed = true;
                    say("Connected to %s through Steam.", who(g_host));
                } else if (c == 'B') {
                    say("! %s closed CoFLink.", who(g_host));
                    lobby_gone();
                } else if (c == 'X') {
                    say("! %s removed you from their game. You can't link to it again until they host a new one.",
                        who(g_host));
                    if (g_nrejected < 16) g_rejected[g_nrejected++] = g_lobby;
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
        ui_alert("host's game is up");
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
        if (r == 0 || r > MAX_GAME_PKT) continue;
        if (!g_lobby) continue;
        g_game_ip = ip;
        bool first = !g_up;
        int slot = slot_for(port, t);
        if (first) say("Cry of Fear is talking to CoFLink. Passing it to %s...", who(g_host));
        if (!g_up++) g_first_up_ms = t;
        send_game(g_host, slot, g_buf, r);
    }
    if (!g_lobby) return;
    if (!g_welcomed && t - g_last_hello > 2000) { send_hello(); g_last_hello = t; }
    /* check the host's co-op server answers (every 2 s until it does) */
    if (g_welcomed && !g_server_ready && t - g_last_probe > 2000) {
        probe_server(0);
        g_last_probe = t;
    }
    if (t - g_last_ka >= KEEPALIVE_MS) { send_ctl(g_host, 'K'); g_last_ka = t; }
    if (t - g_host_last > HOST_QUIET_MS && !g_host_warned) {
        g_host_warned = true;
        g_warned_slow = !g_welcomed;
        if (g_warned_slow) /* never reached them yet: usually a slow connection */
            say("Still reaching %s through Steam (slow internet?). Keep waiting...", who(g_host));
        else
            say("! No answer from %s for 15 s. Is their CoFLink still open?", who(g_host));
    } else if (t - g_host_last < HOST_QUIET_MS && g_host_warned) {
        g_host_warned = false;
        if (!g_warned_slow) say("%s is back.", who(g_host)); /* else "Connected to ..." says it */
    }
    if (g_up && !g_down && !g_warned_no_answer && t - g_first_up_ms > 10000) {
        g_warned_no_answer = true;
        say("! The host's Cry of Fear server isn't answering yet. Ask them to start the co-op server.");
    }
    join_hints(t);
}

/* ---------- starting Cry of Fear without its launcher ---------- */

/* Cry of Fear's launcher (CoFLaunchApp.exe) is skipped: cof.exe is started
   directly, with the options the launcher would give it. */
#define COF_ARGS "-basedir cryoffear -game cryoffear -hw -heapsize 1536000" /* as its launcher passes */

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
    g_nkicked = 0;
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
    st->lobby = g_lobby;
    st->local_port = g_local_port;
    if (g_is_host) { /* progress for the "How to" steps */
        int linked = g_npeers, answered = 0, playing = 0;
        for (int i = 0; i < g_npeers; i++) { answered += g_peers[i].down > 0; playing += g_peers[i].down >= 50; }
        st->done = (g_lobby ? 1 : 0) | (answered ? 2 : 0) | (linked ? 4 : 0) | (playing ? 8 : 0);
    } else if (g_mode == MODE_JOIN) {
        st->done = (g_lobby && g_welcomed ? 1 : 0) | (g_server_ready ? 2 : 0) | (g_down > 0 ? 4 : 0) | (g_down >= 50 ? 8 : 0);
    }
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
        if (!g_npeers) { say("Status: lobby %s open, no friends connected yet.", codestr(g_lobby)); return; }
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
        if (!c->arg) { say("! That isn't a lobby code."); break; }
        if (g_mode != MODE_JOIN) start_mode(MODE_JOIN);
        if (g_mode == MODE_JOIN) { g_joining = 0; request_join(c->arg); }
        break;
    case CMD_KICK: kick_peer(c->arg); break;
    case CMD_LAUNCH: /* "Start Cry of Fear" */
        start_game();
        break;
    case CMD_REFRESH: /* look for a hosting friend right now */
        if (g_is_host) { say("You are hosting. Refresh is for finding a friend who hosts."); break; }
        if (g_mode != MODE_JOIN) start_mode(MODE_JOIN);
        if (g_mode != MODE_JOIN) break;
        if (g_lobby || g_joining) { say("Already linked. Click Stop first to look for someone else."); break; }
        say("Refreshing...");
        g_nrejected = 0;               /* offer hosts you said no to again */
        g_last_found_n = -1;           /* and say who is hosting, even if unchanged */
        for (int i = 0; i < NANS; i++) g_ans[i].asked = 0; /* ask friends' CoFLink again now */
        g_refresh = 1;
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
            if (!g_lobby && !g_joining && g_refresh == 1) {
                scan_for_host(false);
                g_refresh = 2;                 /* answers to direct questions take a moment */
                g_refresh_at = last_scan = now_ms();
            } else if (!g_lobby && !g_joining && g_refresh == 2 && t - g_refresh_at > 1500) {
                if (scan_for_host(false) == 0)
                    say("Nobody is hosting right now. When your friend clicks \"Host a game\", click Refresh again.");
                g_refresh = 0;
                last_scan = now_ms();
            } else if (!g_lobby && !g_joining && (!last_scan || t - last_scan > 3000)) {
                scan_for_host(last_scan == 0);
                last_scan = now_ms();
            }
            if (g_lobby || g_joining) g_refresh = 0;
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
    /* Always through Valve's relay: never share either player's IP address
       for a direct connection, whatever Steam's own "IP sharing" setting is. */
    if (!S.NU_SetGlobalConfigInt32(NU, 104 /* P2P_Transport_ICE_Enable */, 0 /* disable */))
        say("! Steam would not turn off direct connections; your IP may be shared with the host.");
    say("Signed in to Steam as %s.", who(g_me));
    return true;
}
