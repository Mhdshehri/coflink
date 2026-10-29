/* ui_console.c - text front end for the CoFLink engine. Used by the automated
 * tests (Linux, mock Steam); behaves like the old console version:
 *   coflink host [--port N] [--players N]
 *   coflink join [lobby id] [--port N] [--no-launch] [--yes]
 * Keys (one per line on stdin): L paste lobby id, S status, G start game,
 * X stop, H host, J join, K kick (then friend number), Q quit (the same actions as the window's buttons). */
#include "ui.h"
#include "plat.h"

u64 lobby_parse(const char *s);

static bool g_yes;
static int g_mode;
static UiPerson g_friends[200];
static int g_nfriends;

void ui_log(const char *line)
{
    printf("%s\n", line);
    fflush(0);
}

void ui_alert(const char *what) { printf("[alert] %s\n", what); fflush(0); }

void ui_state(const UiState *s) { g_mode = s->mode; }

void ui_list(int which, int n, const UiPerson *p)
{
    if (which != LIST_FRIENDS_ONLINE) return;
    g_nfriends = n < 200 ? n : 200;
    for (int i = 0; i < g_nfriends; i++) g_friends[i] = p[i];
}

static bool ask_yes(const char *q)
{
    if (g_yes) return true;
    printf("%s (Y/N): ", q);
    fflush(0);
    char line[16];
    if (!read_line(line, sizeof line)) return false;
    return line[0] == 'y' || line[0] == 'Y';
}

u64 ui_offer_hosts(int n, const UiPerson *p, u64 *reject)
{
    if (n == 1) {
        char q[160];
        sfmt(q, sizeof q, "%s is hosting. Join their game?", p[0].name);
        if (ask_yes(q)) return p[0].lobby;
        *reject = p[0].lobby;
        ui_log("OK, not joining. Still looking...");
    } else if (n > 1) {
        ui_log("Several friends are hosting:");
        for (int i = 0; i < n; i++) printf("  %d. %s\n", i + 1, p[i].name);
        printf("Number to join: ");
        fflush(0);
        char line[32];
        int k = read_line(line, sizeof line) ? atoi(line) : 0;
        if (k >= 1 && k <= n) return p[k - 1].lobby;
    }
    return 0;
}

bool ui_poll(Cmd *c)
{
    int key = key_poll();
    if (key < 0) return false;
    c->arg = 0;
    c->kind = CMD_NONE;
    char line[64];
    switch (key) {
    case 'q': case 'Q': c->kind = CMD_QUIT; break;
    case 's': case 'S': c->kind = CMD_STATUS; break;
    case 'x': case 'X': c->kind = CMD_STOP; break;
    case 'g': case 'G': c->kind = CMD_LAUNCH; break;
    case 'r': case 'R': c->kind = CMD_REFRESH; break;
    case 'k': case 'K': /* host: kick friend number N (next line) */
        if (read_line(line, sizeof line)) {
            int k = atoi(line);
            if (k >= 1 && k <= g_nfriends) { c->kind = CMD_KICK; c->arg = g_friends[k - 1].id; }
        }
        break;
    case 'h': case 'H': c->kind = CMD_HOST; break;
    case 'j': case 'J': c->kind = CMD_JOIN; break;
    case 'l': case 'L':
        printf("Paste the host's lobby code (right-click or Ctrl+V), then Enter: ");
        fflush(0);
        if (read_line(line, sizeof line)) { c->kind = CMD_JOIN_LOBBY; c->arg = lobby_parse(line); }
        break;
    default: break;
    }
    return c->kind != CMD_NONE;
}

int main(int argc, char **argv)
{
    plat_init();
    EngineOpts o = { 27015, 27115, 4, false };
    Cmd first = { CMD_NONE, 0 };
    int port = 0;
    u64 lobby = 0;
    bool idle = false; /* start on the start screen, like the window does */
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "idle")) idle = true;
        else if (!strcmp(a, "host")) first.kind = CMD_HOST;
        else if (!strcmp(a, "join")) first.kind = CMD_JOIN;
        else if (!strcmp(a, "--port") && i + 1 < argc) port = atoi(argv[++i]);
        else if (!strcmp(a, "--players") && i + 1 < argc) o.max_players = atoi(argv[++i]);
        else if (!strcmp(a, "--no-launch")) o.no_launch = true;
        else if (!strcmp(a, "--yes")) g_yes = true;
        else if (lobby_parse(a)) lobby = lobby_parse(a); /* a lobby code or full id */
    }
    if (port > 0 && port < 65536) {
        if (first.kind == CMD_HOST) o.game_port = (uint16_t)port;
        else o.local_port = (uint16_t)port;
    }
    if (lobby) { first.kind = CMD_JOIN_LOBBY; first.arg = lobby; }
    if (first.kind == CMD_NONE && !idle) {
        printf("usage: coflink host | join [lobby id]\n");
        return 1;
    }
    engine_run(&o, first);
    return 0;
}
