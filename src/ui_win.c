/* ui_win.c - the CoFLink window (Windows). One program for both players:
 * "Host a game" or "Join a friend", lists of friends, the lobby id, and a log.
 * The engine (coflink.c) runs on a second thread; this file only shows its
 * state and sends it commands. Win32 is declared by hand (no Windows SDK). */
#include "ui.h"
#include "plat.h"

#define WINAPI __attribute__((stdcall))
#define IMP __declspec(dllimport)
typedef void *HANDLE, *HWND, *HINSTANCE, *HICON, *HCURSOR, *HBRUSH, *HFONT, *HDC, *HMENU;
typedef int BOOL;
typedef unsigned int UINT;
typedef unsigned long DWORD;
typedef uintptr_t WPARAM;
typedef intptr_t LPARAM, LRESULT;
typedef uint16_t WCHAR;
typedef LRESULT(WINAPI *WNDPROC)(HWND, UINT, WPARAM, LPARAM);

typedef struct {
    UINT cbSize, style;
    WNDPROC proc;
    int clsExtra, wndExtra;
    HINSTANCE inst;
    HICON icon;
    HCURSOR cursor;
    HBRUSH background;
    const WCHAR *menu, *className;
    HICON iconSm;
} WNDCLASSEXW;
typedef struct { long left, top, right, bottom; } RECT;
typedef struct { HWND hwnd; UINT msg; WPARAM wp; LPARAM lp; DWORD time; long x, y; DWORD priv; } MSG;
typedef struct { void *a, *b; long c, d; void *e, *f; } CRITICAL_SECTION; /* 24 bytes on x86 */

IMP uint16_t WINAPI RegisterClassExW(const WNDCLASSEXW *c);
IMP HWND WINAPI CreateWindowExW(DWORD ex, const WCHAR *cls, const WCHAR *title, DWORD style, int x, int y,
                                int w, int h, HWND parent, HMENU id, HINSTANCE inst, void *param);
IMP LRESULT WINAPI DefWindowProcW(HWND, UINT, WPARAM, LPARAM);
IMP BOOL WINAPI GetMessageW(MSG *m, HWND w, UINT a, UINT b);
IMP BOOL WINAPI TranslateMessage(const MSG *m);
IMP LRESULT WINAPI DispatchMessageW(const MSG *m);
IMP BOOL WINAPI IsDialogMessageW(HWND w, MSG *m);
IMP void WINAPI PostQuitMessage(int code);
IMP LRESULT WINAPI SendMessageW(HWND w, UINT m, WPARAM wp, LPARAM lp);
IMP BOOL WINAPI SetWindowTextW(HWND w, const WCHAR *t);
IMP int WINAPI GetWindowTextW(HWND w, WCHAR *buf, int n);
IMP uintptr_t WINAPI SetTimer(HWND w, uintptr_t id, UINT ms, void *fn);
IMP int WINAPI MessageBoxW(HWND w, const WCHAR *text, const WCHAR *cap, UINT type);
IMP HCURSOR WINAPI LoadCursorW(HINSTANCE i, const WCHAR *name);
IMP HICON WINAPI LoadIconW(HINSTANCE i, const WCHAR *name);
IMP BOOL WINAPI ShowWindow(HWND w, int cmd);
IMP BOOL WINAPI UpdateWindow(HWND w);
IMP BOOL WINAPI EnableWindow(HWND w, BOOL on);
IMP BOOL WINAPI DestroyWindow(HWND w);
IMP HDC WINAPI GetDC(HWND w);
IMP int WINAPI ReleaseDC(HWND w, HDC dc);
IMP BOOL WINAPI AdjustWindowRectEx(RECT *r, DWORD style, BOOL menu, DWORD ex);
IMP int WINAPI GetSystemMetrics(int i);
IMP HFONT WINAPI CreateFontW(int h, int w, int esc, int orient, int weight, DWORD italic, DWORD underline,
                            DWORD strike, DWORD charset, DWORD outprec, DWORD clipprec, DWORD quality,
                            DWORD pitch, const WCHAR *face);
IMP int WINAPI GetDeviceCaps(HDC dc, int i);
IMP void WINAPI InitCommonControls(void);
IMP HANDLE WINAPI CreateThread(void *sec, size_t stack, DWORD(WINAPI *fn)(void *), void *arg, DWORD flags, DWORD *id);
IMP DWORD WINAPI WaitForSingleObject(HANDLE h, DWORD ms);
IMP void WINAPI InitializeCriticalSection(CRITICAL_SECTION *cs);
IMP void WINAPI EnterCriticalSection(CRITICAL_SECTION *cs);
IMP void WINAPI LeaveCriticalSection(CRITICAL_SECTION *cs);
IMP int WINAPI MultiByteToWideChar(UINT cp, DWORD flags, const char *s, int n, WCHAR *w, int wn);
IMP int WINAPI WideCharToMultiByte(UINT cp, DWORD flags, const WCHAR *w, int wn, char *s, int n, const char *d, BOOL *u);
IMP HINSTANCE WINAPI GetModuleHandleW(const WCHAR *name);

#define WM_CREATE 0x1
#define WM_DESTROY 0x2
#define WM_CLOSE 0x10
#define WM_SETFONT 0x30
#define WM_GETTEXTLENGTH 0xE
#define WM_COMMAND 0x111
#define WM_TIMER 0x113
#define EM_SETSEL 0xB1
#define EM_SCROLLCARET 0xB7
#define EM_REPLACESEL 0xC2
#define EM_SETLIMITTEXT 0xC5
#define EM_SETREADONLY 0xCF
#define LB_ADDSTRING 0x180
#define LB_RESETCONTENT 0x184
#define LB_SETCURSEL 0x186
#define LB_GETCURSEL 0x188
#define LBN_DBLCLK 2
#define WS_CHILD 0x40000000L
#define WS_VISIBLE 0x10000000L
#define WS_TABSTOP 0x10000L
#define WS_VSCROLL 0x200000L
#define WS_CAPTION 0xC00000L
#define WS_SYSMENU 0x80000L
#define WS_MINIMIZEBOX 0x20000L
#define WS_EX_CLIENTEDGE 0x200L
#define ES_MULTILINE 0x4L
#define ES_AUTOVSCROLL 0x40L
#define ES_AUTOHSCROLL 0x80L
#define ES_READONLY 0x800L
#define LBS_NOTIFY 0x1L
#define LBS_NOINTEGRALHEIGHT 0x100L
#define CP_UTF8 65001
#define MB_ICONWARNING 0x30
#define COLOR_BTNFACE 15
#define LOGPIXELSY 90
#define FW_NORMAL 400
#define FW_BOLD 700
#define CLEARTYPE_QUALITY 5

#define WTEXT(s) L##s /* only for ASCII literals: wchar_t is 16-bit on this target */

enum {
    ID_HOST = 101, ID_JOIN, ID_STOP, ID_STATUS, ID_LOBBY, ID_COPYID, ID_JOINID,
    ID_LISTLABEL, ID_LIST, ID_ACTION, ID_LAUNCH, ID_COPYCONN, ID_LOG, ID_TITLE, ID_CONNECT, ID_LOBBYLABEL,
};

/* ---------- shared with the engine thread ---------- */

static CRITICAL_SECTION g_cs;
static char g_pend[65536];
static int g_npend;
static UiState g_state, g_shown;
static bool g_state_dirty;
static UiPerson g_lists[2][64];
static int g_nlist[2];
static bool g_list_dirty[2];
static Cmd g_cmds[32];
static int g_cmd_head, g_cmd_tail;

void ui_log(const char *line)
{
    EnterCriticalSection(&g_cs);
    int n = (int)strlen(line);
    if (g_npend + n + 2 < (int)sizeof g_pend) {
        memcpy(g_pend + g_npend, line, (size_t)n);
        g_npend += n;
        g_pend[g_npend++] = '\r';
        g_pend[g_npend++] = '\n';
    }
    LeaveCriticalSection(&g_cs);
}

void ui_state(const UiState *s)
{
    EnterCriticalSection(&g_cs);
    g_state = *s;
    g_state_dirty = true;
    LeaveCriticalSection(&g_cs);
}

void ui_list(int which, int n, const UiPerson *p)
{
    if (which < 0 || which > 1) return;
    EnterCriticalSection(&g_cs);
    if (n > 64) n = 64;
    bool same = n == g_nlist[which];
    for (int i = 0; same && i < n; i++)
        same = g_lists[which][i].id == p[i].id && g_lists[which][i].lobby == p[i].lobby &&
               strcmp(g_lists[which][i].name, p[i].name) == 0;
    if (!same) {
        for (int i = 0; i < n; i++) g_lists[which][i] = p[i];
        g_nlist[which] = n;
        g_list_dirty[which] = true;
    }
    LeaveCriticalSection(&g_cs);
}

u64 ui_offer_hosts(int n, const UiPerson *p, u64 *reject)
{
    (void)reject;
    ui_list(LIST_HOSTING, n, p); /* the player picks one and clicks Join */
    return 0;
}

static void push_cmd(CmdKind k, u64 arg)
{
    EnterCriticalSection(&g_cs);
    int next = (g_cmd_tail + 1) % 32;
    if (next != g_cmd_head) {
        g_cmds[g_cmd_tail].kind = k;
        g_cmds[g_cmd_tail].arg = arg;
        g_cmd_tail = next;
    }
    LeaveCriticalSection(&g_cs);
}

bool ui_poll(Cmd *c)
{
    bool got = false;
    EnterCriticalSection(&g_cs);
    if (g_cmd_head != g_cmd_tail) {
        *c = g_cmds[g_cmd_head];
        g_cmd_head = (g_cmd_head + 1) % 32;
        got = true;
    }
    LeaveCriticalSection(&g_cs);
    return got;
}

/* ---------- window ---------- */

static HWND g_wnd, g_c[ID_LOBBYLABEL + 1];
static HFONT g_font, g_bold, g_big;
static int g_dpi = 96;
static HANDLE g_thread;
static bool g_closing;
static u64 g_close_ms;
static UiPerson g_view[2][64]; /* what the list box shows (GUI thread copy) */
static int g_nview[2];
static int g_list_shown = -1;

static int S_(int v) { return v * g_dpi / 96; }

static WCHAR *wide(const char *s, WCHAR *buf, int n)
{
    int r = MultiByteToWideChar(CP_UTF8, 0, s, -1, buf, n);
    if (r <= 0) buf[0] = 0;
    buf[n - 1] = 0;
    return buf;
}

static void set_text(int id, const char *utf8)
{
    static WCHAR w[512];
    SetWindowTextW(g_c[id], wide(utf8, w, 512));
}

/* For labels updated on every tick: only touch the control when the text changes. */
static void set_text_once(int id, const char *literal)
{
    static const char *last[ID_LOBBYLABEL + 1];
    if (last[id] == literal) return;
    last[id] = literal;
    set_text(id, literal);
}

static HWND ctl(int id, const WCHAR *cls, const WCHAR *text, DWORD style, DWORD ex, int x, int y, int w, int h, HFONT f)
{
    HWND h_ = CreateWindowExW(ex, cls, text, WS_CHILD | WS_VISIBLE | style, S_(x), S_(y), S_(w), S_(h), g_wnd,
                              (HMENU)(uintptr_t)id, GetModuleHandleW(0), 0);
    SendMessageW(h_, WM_SETFONT, (WPARAM)f, 1);
    g_c[id] = h_;
    return h_;
}

static void build(void)
{
    const WCHAR *B = WTEXT("BUTTON"), *T = WTEXT("STATIC"), *E = WTEXT("EDIT"), *L = WTEXT("LISTBOX");
    ctl(ID_TITLE, T, WTEXT("Cry of Fear co-op over Steam: no VPN, no port forwarding"), 0, 0, 14, 12, 572, 20, g_bold);
    ctl(ID_HOST, B, WTEXT("Host a game"), WS_TABSTOP, 0, 14, 40, 180, 38, g_bold);
    ctl(ID_JOIN, B, WTEXT("Join a friend"), WS_TABSTOP, 0, 204, 40, 180, 38, g_bold);
    ctl(ID_STOP, B, WTEXT("Stop"), WS_TABSTOP, 0, 486, 40, 100, 38, g_font);
    ctl(ID_STATUS, T, WTEXT(""), 0, 0, 14, 90, 572, 26, g_big);
    ctl(ID_LOBBYLABEL, T, WTEXT("Lobby id:"), 0, 0, 14, 130, 70, 22, g_font);
    ctl(ID_LOBBY, E, WTEXT(""), WS_TABSTOP | ES_AUTOHSCROLL, WS_EX_CLIENTEDGE, 84, 127, 250, 24, g_font);
    ctl(ID_COPYID, B, WTEXT("Copy id"), WS_TABSTOP, 0, 342, 126, 100, 26, g_font);
    ctl(ID_JOINID, B, WTEXT("Join this id"), WS_TABSTOP, 0, 450, 126, 136, 26, g_font);
    ctl(ID_LISTLABEL, T, WTEXT(""), 0, 0, 14, 166, 572, 20, g_font);
    ctl(ID_LIST, L, WTEXT(""), WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT, WS_EX_CLIENTEDGE,
        14, 188, 430, 112, g_font);
    ctl(ID_ACTION, B, WTEXT("Invite"), WS_TABSTOP, 0, 452, 188, 134, 34, g_bold);
    ctl(ID_LAUNCH, B, WTEXT("Start Cry of Fear"), WS_TABSTOP, 0, 14, 310, 180, 30, g_font);
    ctl(ID_CONNECT, B, WTEXT("Connect game"), WS_TABSTOP, 0, 204, 310, 180, 30, g_font);
    ctl(ID_COPYCONN, B, WTEXT("Copy connect command"), WS_TABSTOP, 0, 394, 310, 192, 30, g_font);
    ctl(ID_LOG, E, WTEXT(""), WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY, WS_EX_CLIENTEDGE,
        14, 350, 572, 214, g_font);
    SendMessageW(g_c[ID_LOG], EM_SETLIMITTEXT, 0x7FFFFFFE, 0);
}

static void append_log(const char *utf8)
{
    static WCHAR w[70000];
    wide(utf8, w, 70000);
    HWND h = g_c[ID_LOG];
    int len = (int)SendMessageW(h, WM_GETTEXTLENGTH, 0, 0);
    if (len > 400000) { /* keep the log from growing forever */
        SendMessageW(h, EM_SETSEL, 0, 200000);
        SendMessageW(h, EM_REPLACESEL, 0, (LPARAM)WTEXT(""));
        len = (int)SendMessageW(h, WM_GETTEXTLENGTH, 0, 0);
    }
    SendMessageW(h, EM_SETSEL, (WPARAM)len, len);
    SendMessageW(h, EM_REPLACESEL, 0, (LPARAM)w);
    SendMessageW(h, EM_SCROLLCARET, 0, 0);
}

static void show_list(int which)
{
    HWND lb = g_c[ID_LIST];
    SendMessageW(lb, LB_RESETCONTENT, 0, 0);
    static WCHAR w[UI_NAME + 8];
    for (int i = 0; i < g_nview[which]; i++)
        SendMessageW(lb, LB_ADDSTRING, 0, (LPARAM)wide(g_view[which][i].name, w, UI_NAME + 8));
    if (g_nview[which] == 1) SendMessageW(lb, LB_SETCURSEL, 0, 0);
    g_list_shown = which;
}

static void refresh_controls(void)
{
    const UiState *s = &g_shown;
    bool idle = s->mode == MODE_IDLE, host = s->mode == MODE_HOST, join = s->mode == MODE_JOIN;
    EnableWindow(g_c[ID_HOST], idle && !g_closing);
    EnableWindow(g_c[ID_JOIN], idle && !g_closing);
    EnableWindow(g_c[ID_STOP], !idle && !g_closing);
    static int ro = -1;
    int want_ro = host || (join && s->lobby);
    if (want_ro != ro) { SendMessageW(g_c[ID_LOBBY], EM_SETREADONLY, (WPARAM)want_ro, 0); ro = want_ro; }
    EnableWindow(g_c[ID_COPYID], host && s->lobby);
    EnableWindow(g_c[ID_JOINID], (idle || join) && !s->lobby);
    int which = host ? LIST_FRIENDS_ONLINE : LIST_HOSTING;
    if (which != g_list_shown) show_list(which);
    set_text_once(ID_LISTLABEL, idle ? "" :
                  host ? "Friends online - pick one and click Invite (or they click Join a friend):" :
                         "Friends hosting - pick one and click Join:");
    set_text_once(ID_ACTION, host ? "Invite" : "Join");
    EnableWindow(g_c[ID_ACTION], (host && s->lobby) || (join && !s->lobby));
    EnableWindow(g_c[ID_LAUNCH], !g_closing);
    EnableWindow(g_c[ID_CONNECT], join && s->lobby);
    EnableWindow(g_c[ID_COPYCONN], join && s->lobby);
}

static void on_timer(void)
{
    static char pend[65536];
    int n;
    UiState st;
    bool st_dirty, ld[2];
    EnterCriticalSection(&g_cs);
    n = g_npend;
    memcpy(pend, g_pend, (size_t)n);
    g_npend = 0;
    st = g_state;
    st_dirty = g_state_dirty;
    g_state_dirty = false;
    for (int k = 0; k < 2; k++) {
        ld[k] = g_list_dirty[k];
        if (ld[k]) {
            g_nview[k] = g_nlist[k];
            for (int i = 0; i < g_nlist[k]; i++) g_view[k][i] = g_lists[k][i];
            g_list_dirty[k] = false;
        }
    }
    LeaveCriticalSection(&g_cs);

    if (n) { pend[n] = 0; append_log(pend); }
    if (st_dirty) {
        bool lobby_changed = st.lobby != g_shown.lobby || st.mode != g_shown.mode;
        g_shown = st;
        set_text(ID_STATUS, st.status);
        if (lobby_changed) {
            char id[24] = "";
            if (st.lobby) u64_to_str(st.lobby, id);
            if (st.lobby || st.mode != MODE_JOIN) set_text(ID_LOBBY, id);
        }
    }
    for (int k = 0; k < 2; k++)
        if (ld[k] && g_list_shown == k) show_list(k);
    refresh_controls();

    if (g_closing && (WaitForSingleObject(g_thread, 0) == 0 || now_ms() - g_close_ms > 5000))
        DestroyWindow(g_wnd);
}

static void copy_text(const char *t) { clipboard_set(t); }

static void on_command(int id, int code)
{
    switch (id) {
    case ID_HOST: push_cmd(CMD_HOST, 0); break;
    case ID_JOIN: push_cmd(CMD_JOIN, 0); break;
    case ID_STOP: push_cmd(CMD_STOP, 0); break;
    case ID_COPYID:
        if (g_shown.lobby) {
            char id_[24];
            u64_to_str(g_shown.lobby, id_);
            copy_text(id_);
            ui_log("Lobby id copied. Send it to your friend (Steam chat).");
        }
        break;
    case ID_JOINID: {
        WCHAR w[64];
        char a[64], digits[24];
        GetWindowTextW(g_c[ID_LOBBY], w, 64);
        WideCharToMultiByte(CP_UTF8, 0, w, -1, a, 64, 0, 0);
        int k = 0;
        for (int i = 0; a[i] && k < 20; i++)
            if (a[i] >= '0' && a[i] <= '9') digits[k++] = a[i];
        digits[k] = 0;
        u64 lob = k >= 10 ? str_to_u64(digits) : 0;
        if (!lob) {
            MessageBoxW(g_wnd, WTEXT("Paste the lobby id your friend sent you (a long number) into the Lobby id box first."),
                        WTEXT("CoFLink"), MB_ICONWARNING);
            break;
        }
        push_cmd(CMD_JOIN_LOBBY, lob);
        break;
    }
    case ID_LIST:
        if (code != LBN_DBLCLK) break;
        /* fall through: double-click = button */
    case ID_ACTION: {
        int which = g_shown.mode == MODE_HOST ? LIST_FRIENDS_ONLINE : LIST_HOSTING;
        int sel = (int)SendMessageW(g_c[ID_LIST], LB_GETCURSEL, 0, 0);
        if (sel < 0 || sel >= g_nview[which]) {
            MessageBoxW(g_wnd, WTEXT("Pick a friend in the list first."), WTEXT("CoFLink"), MB_ICONWARNING);
            break;
        }
        if (which == LIST_FRIENDS_ONLINE) push_cmd(CMD_INVITE, g_view[which][sel].id);
        else push_cmd(CMD_JOIN_LOBBY, g_view[which][sel].lobby);
        break;
    }
    case 1: /* Enter key: join the pasted lobby id, if there is one */
        if (g_shown.mode != MODE_HOST && !g_shown.lobby && SendMessageW(g_c[ID_LOBBY], WM_GETTEXTLENGTH, 0, 0) > 0)
            on_command(ID_JOINID, 0);
        break;
    case ID_LAUNCH: push_cmd(CMD_LAUNCH, 0); break;
    case ID_CONNECT: push_cmd(CMD_CONNECT, 0); break;
    case ID_COPYCONN: {
        char cmd[48];
        sfmt(cmd, sizeof cmd, "connect 127.0.0.1:%u", (unsigned)g_shown.local_port);
        copy_text(cmd);
        ui_log("Copied. In Cry of Fear press ~, then Ctrl+V and Enter.");
        break;
    }
    default: break;
    }
}

static LRESULT WINAPI wndproc(HWND w, UINT m, WPARAM wp, LPARAM lp)
{
    switch (m) {
    case WM_COMMAND:
        on_command((int)(wp & 0xFFFF), (int)((wp >> 16) & 0xFFFF));
        return 0;
    case WM_TIMER:
        on_timer();
        return 0;
    case WM_CLOSE:
        if (!g_closing) {
            g_closing = true;
            g_close_ms = now_ms();
            push_cmd(CMD_QUIT, 0); /* the engine leaves the lobby and says goodbye */
            ui_log("Closing...");
        }
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(w, m, wp, lp);
    }
}

static DWORD WINAPI engine_thread(void *arg)
{
    (void)arg;
    EngineOpts o = { 27015, 27115, 4, false };
    Cmd none = { CMD_NONE, 0 };
    engine_run(&o, none);
    return 0;
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    plat_init();
    InitCommonControls();
    InitializeCriticalSection(&g_cs);

    HDC dc = GetDC(0);
    g_dpi = GetDeviceCaps(dc, LOGPIXELSY);
    ReleaseDC(0, dc);
    if (g_dpi < 96) g_dpi = 96;
    const WCHAR *face = WTEXT("Segoe UI");
    g_font = CreateFontW(-S_(12), 0, 0, 0, FW_NORMAL, 0, 0, 0, 1, 0, 0, CLEARTYPE_QUALITY, 0, face);
    g_bold = CreateFontW(-S_(12), 0, 0, 0, FW_BOLD, 0, 0, 0, 1, 0, 0, CLEARTYPE_QUALITY, 0, face);
    g_big = CreateFontW(-S_(16), 0, 0, 0, FW_BOLD, 0, 0, 0, 1, 0, 0, CLEARTYPE_QUALITY, 0, face);

    HINSTANCE inst = GetModuleHandleW(0);
    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof wc);
    wc.cbSize = sizeof wc;
    wc.proc = wndproc;
    wc.inst = inst;
    wc.icon = LoadIconW(inst, (const WCHAR *)(uintptr_t)1);
    wc.iconSm = wc.icon;
    wc.cursor = LoadCursorW(0, (const WCHAR *)(uintptr_t)32512);
    wc.background = (HBRUSH)(uintptr_t)(COLOR_BTNFACE + 1);
    wc.className = WTEXT("CoFLinkWindow");
    RegisterClassExW(&wc);

    DWORD style = WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT r = { 0, 0, S_(600), S_(578) };
    AdjustWindowRectEx(&r, style, 0, 0);
    int ww = r.right - r.left, wh = r.bottom - r.top;
    int x = (GetSystemMetrics(0) - ww) / 2, y = (GetSystemMetrics(1) - wh) / 2;
    g_wnd = CreateWindowExW(0, wc.className, WTEXT("CoFLink - Cry of Fear co-op"), style, x < 0 ? 0 : x,
                            y < 0 ? 0 : y, ww, wh, 0, 0, inst, 0);
    if (!g_wnd) return 1;
    build();
    ui_log("HOST: click \"Host a game\". Then start Cry of Fear, create a co-op game,");
    ui_log("      and type  sv_lan 1  in its console (~).");
    ui_log("FRIEND: click \"Join a friend\" and pick the host (or just accept their Steam invite).");
    ui_log("        Then start Cry of Fear as usual - CoFLink connects it to the host by itself.");
    ui_log("");
    on_timer();
    ShowWindow(g_wnd, 1);
    UpdateWindow(g_wnd);
    SetTimer(g_wnd, 1, 100, 0);

    g_thread = CreateThread(0, 0, engine_thread, 0, 0, 0);
    if (!g_thread) {
        MessageBoxW(g_wnd, WTEXT("Could not start."), WTEXT("CoFLink"), MB_ICONWARNING);
        return 1;
    }

    MSG msg;
    while (GetMessageW(&msg, 0, 0, 0) > 0) {
        if (IsDialogMessageW(g_wnd, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}
