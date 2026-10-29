/* ui.h - what the CoFLink engine (coflink.c) needs from a front end.
 * Two front ends exist: ui_win.c (the Windows window) and ui_console.c
 * (a text version, used by the automated tests). The engine runs on its own
 * thread in the window version; every function here must be thread-safe there. */
#pragma once
#include "mini.h"

typedef enum {
    CMD_NONE,
    CMD_HOST,        /* start hosting */
    CMD_JOIN,        /* start looking for a host */
    CMD_JOIN_LOBBY,  /* arg = lobby id */
    CMD_LAUNCH,      /* start Cry of Fear (through Steam, as usual) */
    CMD_REFRESH,     /* look for a hosting friend right now */
    CMD_KICK,        /* host: arg = friend's Steam id */
    CMD_STATUS,      /* print a status line */
    CMD_STOP,        /* stop hosting/joining, back to the start screen */
    CMD_QUIT,
} CmdKind;

typedef struct { CmdKind kind; u64 arg; } Cmd;

enum { MODE_IDLE = 0, MODE_HOST = 1, MODE_JOIN = 2 };

typedef struct {
    int mode;
    u64 lobby;            /* host: own lobby; join: lobby joined */
    uint16_t local_port;  /* join: where Cry of Fear connects */
    char status[160];     /* one-line summary for the big status label */
} UiState;

#define UI_NAME 72
typedef struct { u64 id, lobby; char name[UI_NAME]; } UiPerson;

enum { LIST_FRIENDS_ONLINE = 0 /* host: friends linked now */, LIST_HOSTING = 1 };

void ui_log(const char *line);
/* something the player is waiting for happened (a friend linked, the host's
   game is up): the window flashes in the taskbar and plays a short sound */
void ui_alert(const char *what);
void ui_state(const UiState *s);
void ui_list(int which, int n, const UiPerson *p);
/* Friends found hosting. Return the lobby to join now, or 0. Set *reject to a
   lobby the user said no to (so it isn't offered again). */
u64 ui_offer_hosts(int n, const UiPerson *p, u64 *reject);
bool ui_poll(Cmd *c);

/* engine entry: runs until CMD_QUIT. first = optional starting command. */
typedef struct {
    uint16_t game_port, local_port;
    int max_players;
} EngineOpts;
void engine_run(const EngineOpts *o, Cmd first);
