/* ui_win.c - the CoFLink window (Windows). One program for both players:
 * "Host a game" or "Join a friend", lists of friends, the lobby id, and a log.
 * The engine (coflink.c) runs on a second thread; this file only shows its
 * state and sends it commands. Win32 is declared by hand (no Windows SDK). */
#include "ui.h"
#include "plat.h"

void lobby_code(u64 lobby, char out[16]);
u64 lobby_parse(const char *s);

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
IMP HBRUSH WINAPI CreateSolidBrush(DWORD color);
IMP HANDLE WINAPI CreatePen(int style, int width, DWORD color);
IMP HANDLE WINAPI SelectObject(HDC dc, HANDLE obj);
IMP BOOL WINAPI DeleteObject(HANDLE obj);
IMP HANDLE WINAPI GetStockObject(int i);
IMP DWORD WINAPI SetTextColor(HDC dc, DWORD c);
IMP DWORD WINAPI SetBkColor(HDC dc, DWORD c);
IMP int WINAPI SetBkMode(HDC dc, int mode);
IMP BOOL WINAPI RoundRect(HDC dc, int l, int t, int r, int b, int w, int h);
IMP int WINAPI FillRect(HDC dc, const RECT *r, HBRUSH b);
IMP int WINAPI DrawTextW(HDC dc, const WCHAR *t, int n, RECT *r, UINT fmt);
IMP BOOL WINAPI GetClientRect(HWND w, RECT *r);
IMP BOOL WINAPI RedrawWindow(HWND w, const RECT *r, HANDLE rgn, UINT flags);
typedef struct { UINT size; HWND hwnd; DWORD flags; UINT count; DWORD timeout; } FLASHWINFO;
IMP BOOL WINAPI FlashWindowEx(const FLASHWINFO *f);
IMP HWND WINAPI GetForegroundWindow(void);
IMP BOOL WINAPI MessageBeep(UINT type);
IMP HMENU WINAPI CreatePopupMenu(void);
IMP BOOL WINAPI AppendMenuW(HMENU m, UINT flags, uintptr_t id, const WCHAR *text);
IMP BOOL WINAPI TrackPopupMenu(HMENU m, UINT flags, int x, int y, int reserved, HWND w, const RECT *r);
IMP BOOL WINAPI DestroyMenu(HMENU m);
IMP BOOL WINAPI EnumDisplaySettingsW(const WCHAR *device, DWORD mode, void *devmode);
IMP BOOL WINAPI GetWindowRect(HWND w, RECT *r);
IMP void *WINAPI LoadLibraryExA(const char *path, void *file, DWORD flags);
IMP void *WINAPI GetProcAddress(void *mod, const char *name);

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
#define LB_GETTEXT 0x189
#define LBN_DBLCLK 2
#define WM_ERASEBKGND 0x14
#define WM_DRAWITEM 0x2B
#define WM_MEASUREITEM 0x2C
#define WM_CTLCOLOREDIT 0x133
#define WM_CTLCOLORLISTBOX 0x134
#define WM_CTLCOLORSTATIC 0x138
#define BS_OWNERDRAW 0xBL
#define LBS_OWNERDRAWFIXED 0x10L
#define LBS_HASSTRINGS 0x40L
#define ODT_LISTBOX 2
#define ODS_SELECTED 0x1
#define ODS_DISABLED 0x4
#define ODS_FOCUS 0x10
#define DT_CENTER 0x1
#define DT_VCENTER 0x4
#define DT_SINGLELINE 0x20
#define DT_END_ELLIPSIS 0x8000
#define TRANSPARENT_BK 1
#define NULL_BRUSH 5
#define RGBC(r, g, b) ((DWORD)((r) | ((g) << 8) | ((b) << 16)))
#define WS_CHILD 0x40000000L
#define WS_VISIBLE 0x10000000L
#define WS_TABSTOP 0x10000L
#define WS_VSCROLL 0x200000L
#define WS_CAPTION 0xC00000L
#define WS_SYSMENU 0x80000L
#define WS_MINIMIZEBOX 0x20000L
#define ES_MULTILINE 0x4L
#define ES_AUTOVSCROLL 0x40L
#define ES_AUTOHSCROLL 0x80L
#define ES_READONLY 0x800L
#define LBS_NOTIFY 0x1L
#define LBS_NOINTEGRALHEIGHT 0x100L
#define CP_UTF8 65001
#define MB_ICONWARNING 0x30
#define MB_YESNO 0x4
#define MB_ICONQUESTION 0x20
#define IDYES 6
#define COLOR_BTNFACE 15
#define LOGPIXELSY 90
#define FW_NORMAL 400
#define FW_BOLD 700
#define CLEARTYPE_QUALITY 5

#define WTEXT(s) L##s /* only for ASCII literals: wchar_t is 16-bit on this target */

enum {
    ID_HOST = 101, ID_JOIN, ID_STOP, ID_STATUS, ID_LOBBY, ID_COPYID, ID_JOINID,
    ID_LISTLABEL, ID_LIST, ID_ACTION, ID_LAUNCH, ID_COPYCONN, ID_LOG, ID_TITLE, ID_REFRESH, ID_ACCENT, ID_THEME, ID_TAB_HOWTO, ID_TAB_LOG, ID_COPYLOG, ID_HOWTO, ID_DISPLAY,
    /* the Settings tab */
    ID_TAB_SET, ID_SEC_DISP, ID_SEC_MOUSE, ID_SL_MODE, ID_MODE_GAME, ID_MODE_FULL, ID_MODE_WIN, ID_MODE_BL,
    ID_SL_RES, ID_SET_W, ID_SL_X, ID_SET_H, ID_SET_PICK, ID_SF_RES, ID_SET_DPI, ID_SF_DPI, ID_SET_RAW, ID_SF_RAW,
    ID_SET_NOTE, ID_SET_RESET,
    ID_LOBBYLABEL,
};

typedef struct { UINT type, id, item, action, state; HWND hwnd; HDC dc; RECT rc; uintptr_t data; } DrawItem;
typedef struct { UINT type, id, item, width, height; uintptr_t data; } MeasureItem;

/* ---------- shared with the engine thread ---------- */

static CRITICAL_SECTION g_cs;
static char g_pend[65536];
static int g_npend;
static UiState g_state, g_shown;
static bool g_state_dirty;
static UiPerson g_lists[2][64];
static int g_nlist[2];
static bool g_list_dirty[2];
static bool g_alert, g_warned; /* g_warned: a "!" line arrived */
static Cmd g_cmds[32];
static int g_cmd_head, g_cmd_tail;

void ui_log(const char *line)
{
    EnterCriticalSection(&g_cs);
    int n = (int)strlen(line);
    if (line[0] == '!') g_warned = true;
    if (g_npend + n + 2 < (int)sizeof g_pend) {
        memcpy(g_pend + g_npend, line, (size_t)n);
        g_npend += n;
        g_pend[g_npend++] = '\r';
        g_pend[g_npend++] = '\n';
    }
    LeaveCriticalSection(&g_cs);
}

void ui_alert(const char *what)
{
    (void)what;
    EnterCriticalSection(&g_cs);
    g_alert = true;
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
enum { TAB_HOWTO, TAB_LOG, TAB_SET };
static int g_tab;          /* what the box shows: "How to", the log or the Settings */
static bool g_log_unseen;  /* a warning arrived while "How to" was shown: red dot on Log */

static int S_(int v) { return v * g_dpi / 96; }

/* ---------- dark / light theme ---------- */

typedef struct {
    bool dark;
    DWORD bg, panel, text, dim, accent, accent_down, btn, btn_down, border, dis_bg, dis_text, status, sel_text;
} Theme;

static const Theme DARK = { true,
    RGBC(18, 18, 20), RGBC(28, 29, 33), RGBC(232, 230, 227), RGBC(139, 142, 150),
    RGBC(163, 22, 26), RGBC(120, 14, 18), RGBC(40, 42, 47), RGBC(30, 31, 35), RGBC(62, 65, 72),
    RGBC(26, 27, 31), RGBC(85, 88, 96), RGBC(235, 87, 87), RGBC(255, 255, 255) };
/* light: soft gray page, white boxes and buttons with thin gray outlines */
static const Theme LIGHT = { false,
    RGBC(244, 245, 247), RGBC(255, 255, 255), RGBC(28, 30, 34), RGBC(106, 112, 122),
    RGBC(196, 32, 38), RGBC(156, 22, 28), RGBC(255, 255, 255), RGBC(234, 236, 240), RGBC(208, 212, 219),
    RGBC(236, 238, 241), RGBC(166, 171, 180), RGBC(196, 32, 38), RGBC(255, 255, 255) };

static Theme T;
/* text boxes and the list sit inside rounded frames drawn in the theme's
   colors (Windows' own borders are black in both themes) */
typedef struct { int id, x, y, w, h, tab; } Frame; /* tab: only drawn on that tab (-1: always) */
static Frame g_frames[8];
static int g_frame_tab = -1; /* the tab the next framed() boxes belong to */
static int g_nframes;
static HBRUSH g_br_bg, g_br_panel, g_br_accent;

static void draw_frames(HDC dc)
{
    HBRUSH b = CreateSolidBrush(T.panel);
    HANDLE p = CreatePen(0, 1, T.border);
    HANDLE ob = SelectObject(dc, b), op = SelectObject(dc, p);
    for (int i = 0; i < g_nframes; i++) {
        Frame *f = &g_frames[i];
        if (f->tab >= 0 && f->tab != g_tab) continue;
        RoundRect(dc, S_(f->x), S_(f->y), S_(f->x + f->w), S_(f->y + f->h), S_(8), S_(8));
    }
    if (g_tab == TAB_SET) { /* a line under each section's title */
        static const int ys[2] = { 414, 552 };
        for (int i = 0; i < 2; i++) {
            RECT l = { S_(28), S_(ys[i]), S_(572), S_(ys[i]) + 1 };
            HBRUSH lb = CreateSolidBrush(T.border);
            FillRect(dc, &l, lb);
            DeleteObject(lb);
        }
    }
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(b);
    DeleteObject(p);
}

static void *sys_proc(const char *dll, const char *fn)
{
    void *m = LoadLibraryExA(dll, 0, 0x800 /* only from System32 */);
    return m ? GetProcAddress(m, fn) : 0;
}

/* Title bar and scrollbars follow the theme on Windows 10 (1809+) and 11;
   older Windows just keeps its normal title bar. */
static void apply_system_parts(void)
{
    typedef long(WINAPI * DwmSet)(HWND, DWORD, const void *, DWORD);
    typedef long(WINAPI * SetTheme)(HWND, const WCHAR *, const WCHAR *);
    static DwmSet dwm;
    static SetTheme theme;
    static bool looked;
    if (!looked) {
        looked = true;
        dwm = (DwmSet)sys_proc("dwmapi.dll", "DwmSetWindowAttribute");
        theme = (SetTheme)sys_proc("uxtheme.dll", "SetWindowTheme");
    }
    if (dwm) {
        BOOL on = T.dark;
        if (dwm(g_wnd, 20, &on, sizeof on) != 0) dwm(g_wnd, 19, &on, sizeof on); /* dark title bar */
    }
    if (theme) {
        const WCHAR *name = T.dark ? WTEXT("DarkMode_Explorer") : WTEXT("Explorer");
        theme(g_c[ID_LOG], name, 0);
        theme(g_c[ID_HOWTO], name, 0);
        theme(g_c[ID_LIST], name, 0);
    }
}

/* ---------- "Display": how Start Cry of Fear starts the game ---------- */

static WCHAR *wide(const char *s, WCHAR *buf, int n);

typedef struct { unsigned w, h; } Res;
static int g_disp_mode;           /* DISPLAY_* */
static unsigned g_disp_w, g_disp_h; /* 0 = the game's own resolution */
static Res g_res[32];
static int g_nres;
static Res g_desktop;

/* screen sizes Windows offers for the main monitor, biggest first */
static void load_resolutions(void)
{
    uint8_t dm[220]; /* DEVMODEW */
    g_nres = 0;
    for (DWORD i = 0; g_nres < 32; i++) {
        memset(dm, 0, sizeof dm);
        dm[68] = 220; /* dmSize */
        if (!EnumDisplaySettingsW(0, i, dm)) break;
        unsigned w, h;
        memcpy(&w, dm + 172, 4); /* dmPelsWidth */
        memcpy(&h, dm + 176, 4); /* dmPelsHeight */
        if (w < 800 || h < 600) continue;
        bool dup = false;
        for (int k = 0; k < g_nres; k++) dup |= g_res[k].w == w && g_res[k].h == h;
        if (!dup) g_res[g_nres++] = (Res){ w, h };
    }
    for (int a = 0; a < g_nres; a++) /* biggest first */
        for (int b = a + 1; b < g_nres; b++)
            if ((u64)g_res[b].w * g_res[b].h > (u64)g_res[a].w * g_res[a].h) { Res t = g_res[a]; g_res[a] = g_res[b]; g_res[b] = t; }
    memset(dm, 0, sizeof dm);
    dm[68] = 220;
    if (EnumDisplaySettingsW(0, 0xFFFFFFFFu /* current */, dm)) {
        memcpy(&g_desktop.w, dm + 172, 4);
        memcpy(&g_desktop.h, dm + 176, 4);
    }
}

/* ---------- the Settings tab: how Start Cry of Fear starts the game ---------- */

static bool g_set_dpi, g_set_raw; /* the two switches */
static bool g_set_filling;        /* the boxes are being filled in by CoFLink, not typed */

static void set_ctl_text(int id, const char *t)
{
    WCHAR w[128];
    SetWindowTextW(g_c[id], wide(t, w, 128));
}

/* the button by Start Cry of Fear: a short summary, opens the Settings tab */
static void display_label(void)
{
    static const char *names[] = { "Display: game's own", "Fullscreen", "Windowed", "Borderless" };
    char t[64];
    if (g_disp_mode == DISPLAY_DEFAULT) sfmt(t, sizeof t, "%s", names[0]);
    else if (g_disp_mode == DISPLAY_BORDERLESS || !g_disp_w) sfmt(t, sizeof t, "%s", names[g_disp_mode]);
    else sfmt(t, sizeof t, "%s %ux%u", names[g_disp_mode], g_disp_w, g_disp_h);
    set_ctl_text(ID_DISPLAY, t);
}

static void save_display(void)
{
    char t[64];
    sfmt(t, sizeof t, "%d %u %u %d %d\n", g_disp_mode, g_disp_w, g_disp_h, g_set_dpi, g_set_raw);
    file_write_text("coflink_display.txt", t);
}

static void load_display(void)
{
    FILE *f = fopen("coflink_display.txt", "rb");
    if (!f) return; /* first start: the game's own setting, both switches off */
    char b[64] = { 0 };
    fread(b, 1, sizeof b - 1, f);
    fclose(f);
    unsigned v[5] = { 0, 0, 0, 0, 0 };
    int k = 0;
    for (char *p = b; *p && k < 5; ) {
        while (*p == ' ') p++;
        if (*p < '0' || *p > '9') break;
        while (*p >= '0' && *p <= '9') { if (v[k] < 100000) v[k] = v[k] * 10 + (unsigned)(*p - '0'); p++; }
        k++;
    }
    g_disp_mode = v[0] <= DISPLAY_BORDERLESS ? (int)v[0] : DISPLAY_DEFAULT;
    g_disp_w = v[1];
    g_disp_h = v[2];
    if (g_disp_w < 320 || g_disp_h < 200 || g_disp_w > 16384 || g_disp_h > 16384) g_disp_w = g_disp_h = 0;
    g_set_dpi = v[3] == 1;
    g_set_raw = v[4] == 1;
}

/* what Start Cry of Fear sends to the engine */
static u64 display_arg(void)
{
    int flags = (g_set_dpi ? DISPLAY_DPI : 0) | (g_set_raw ? DISPLAY_RAWMOUSE : 0);
    if (g_disp_mode == DISPLAY_BORDERLESS) /* always the screen's own size */
        return DISPLAY_ARG(DISPLAY_BORDERLESS, flags, g_desktop.w, g_desktop.h);
    if (g_disp_mode == DISPLAY_DEFAULT) return DISPLAY_ARG(DISPLAY_DEFAULT, flags, 0, 0);
    return DISPLAY_ARG(g_disp_mode, flags, g_disp_w, g_disp_h);
}

/* the Settings tab's controls show the current choice */
static void show_settings(void)
{
    static const int modes[4] = { ID_MODE_GAME, ID_MODE_FULL, ID_MODE_WIN, ID_MODE_BL };
    for (int i = 0; i < 4; i++) RedrawWindow(g_c[modes[i]], 0, 0, 0x1 | 0x4);
    bool own_size = g_disp_mode == DISPLAY_FULL || g_disp_mode == DISPLAY_WINDOW;
    char t[96];
    g_set_filling = true;
    if (g_disp_mode == DISPLAY_BORDERLESS) {
        sfmt(t, sizeof t, "%u", g_desktop.w); set_ctl_text(ID_SET_W, t);
        sfmt(t, sizeof t, "%u", g_desktop.h); set_ctl_text(ID_SET_H, t);
    } else if (own_size && g_disp_w) {
        sfmt(t, sizeof t, "%u", g_disp_w); set_ctl_text(ID_SET_W, t);
        sfmt(t, sizeof t, "%u", g_disp_h); set_ctl_text(ID_SET_H, t);
    } else {
        set_ctl_text(ID_SET_W, "");
        set_ctl_text(ID_SET_H, "");
    }
    g_set_filling = false;
    EnableWindow(g_c[ID_SET_W], own_size);
    EnableWindow(g_c[ID_SET_H], own_size);
    EnableWindow(g_c[ID_SET_PICK], own_size);
    if (g_disp_mode == DISPLAY_BORDERLESS) sfmt(t, sizeof t, "always your screen's size");
    else if (g_disp_mode == DISPLAY_DEFAULT) sfmt(t, sizeof t, "the game's own (pick a mode to set it)");
    else sfmt(t, sizeof t, "%s", g_disp_w ? "" : "empty = the game's own");
    set_ctl_text(ID_SF_RES, t);
    RedrawWindow(g_c[ID_SET_DPI], 0, 0, 0x1 | 0x4);
    RedrawWindow(g_c[ID_SET_RAW], 0, 0, 0x1 | 0x4);
    display_label();
}

static void settings_changed(void)
{
    save_display();
    show_settings();
}

/* typed into the resolution boxes */
static void read_size_boxes(void)
{
    if (g_set_filling || (g_disp_mode != DISPLAY_FULL && g_disp_mode != DISPLAY_WINDOW)) return;
    unsigned v[2] = { 0, 0 };
    for (int i = 0; i < 2; i++) {
        WCHAR w[16];
        int n = GetWindowTextW(g_c[i ? ID_SET_H : ID_SET_W], w, 16);
        for (int k = 0; k < n; k++)
            if (w[k] >= '0' && w[k] <= '9' && v[i] < 100000) v[i] = v[i] * 10 + (unsigned)(w[k] - '0');
    }
    bool ok = v[0] >= 320 && v[1] >= 200 && v[0] <= 16384 && v[1] <= 16384;
    g_disp_w = ok ? v[0] : 0;
    g_disp_h = ok ? v[1] : 0;
    save_display();
    set_ctl_text(ID_SF_RES, g_disp_w ? "" : "empty = the game's own");
    display_label();
}

/* "Pick": the sizes the screen supports */
static void pick_resolution(void)
{
    enum { M_GAME = 1, M_RES = 10 };
    load_resolutions();
    HMENU m = CreatePopupMenu();
    if (!m) return;
    const UINT ON = 0x8 /* checked */;
    WCHAR w[96];
    AppendMenuW(m, !g_disp_w ? ON : 0, M_GAME, WTEXT("Game's own resolution"));
    for (int i = 0; i < g_nres; i++) {
        char t[40];
        bool screen = g_res[i].w == g_desktop.w && g_res[i].h == g_desktop.h;
        sfmt(t, sizeof t, "%u x %u%s", g_res[i].w, g_res[i].h, screen ? "  (your screen)" : "");
        AppendMenuW(m, g_disp_w == g_res[i].w && g_disp_h == g_res[i].h ? ON : 0, M_RES + i, wide(t, w, 96));
    }
    RECT r;
    GetWindowRect(g_c[ID_SET_PICK], &r);
    int id = (int)TrackPopupMenu(m, 0x100 /* return the choice */, r.left, r.bottom, 0, g_wnd, 0);
    DestroyMenu(m);
    if (id == M_GAME) g_disp_w = g_disp_h = 0;
    else if (id >= M_RES && id < M_RES + g_nres) { g_disp_w = g_res[id - M_RES].w; g_disp_h = g_res[id - M_RES].h; }
    else return;
    settings_changed();
}

static void save_theme(void) { file_write_text("coflink_theme.txt", T.dark ? "dark\n" : "light\n"); }

static bool saved_light(void)
{
    FILE *f = fopen("coflink_theme.txt", "rb");
    if (!f) return false; /* first start: dark */
    char b[16] = { 0 };
    fread(b, 1, sizeof b - 1, f);
    fclose(f);
    return b[0] == 'l';
}

static void set_theme(bool dark)
{
    T = dark ? DARK : LIGHT;
    if (g_br_bg) { DeleteObject(g_br_bg); DeleteObject(g_br_panel); DeleteObject(g_br_accent); }
    g_br_bg = CreateSolidBrush(T.bg);
    g_br_panel = CreateSolidBrush(T.panel);
    g_br_accent = CreateSolidBrush(T.accent);
    if (g_wnd) {
        SetWindowTextW(g_c[ID_THEME], T.dark ? WTEXT("Light mode") : WTEXT("Dark mode"));
        apply_system_parts();
        RedrawWindow(g_wnd, 0, 0, 0x1 | 0x4 | 0x80 | 0x400); /* invalidate, erase, children, frame */
    }
}

static bool is_primary(UINT id) { return id == ID_HOST || id == ID_JOIN || id == ID_ACTION; }
static bool is_tab(UINT id) { return id == ID_TAB_HOWTO || id == ID_TAB_LOG || id == ID_TAB_SET; }
static int tab_of(UINT id) { return id == ID_TAB_LOG ? TAB_LOG : id == ID_TAB_SET ? TAB_SET : TAB_HOWTO; }
static bool on_settings_panel(UINT id) { return id >= ID_SEC_DISP && id <= ID_SET_RESET; }
static bool is_mode_btn(UINT id) { return id >= ID_MODE_GAME && id <= ID_MODE_BL; }
static bool is_switch(UINT id) { return id == ID_SET_DPI || id == ID_SET_RAW; }

/* "How to" / "Log": the chosen one looks like a raised tab with a red line under it */
static void draw_tab(DrawItem *d)
{
    bool on = tab_of(d->id) == g_tab;
    FillRect(d->dc, &d->rc, g_br_bg);
    if (on) {
        HBRUSH b = CreateSolidBrush(T.btn);
        HANDLE p = CreatePen(0, 1, T.border);
        HANDLE ob = SelectObject(d->dc, b), op = SelectObject(d->dc, p);
        RoundRect(d->dc, d->rc.left, d->rc.top, d->rc.right, d->rc.bottom, S_(8), S_(8));
        SelectObject(d->dc, ob);
        SelectObject(d->dc, op);
        DeleteObject(b);
        DeleteObject(p);
        RECT u = { d->rc.left + S_(10), d->rc.bottom - S_(3), d->rc.right - S_(10), d->rc.bottom - S_(1) };
        FillRect(d->dc, &u, g_br_accent);
    }
    WCHAR w[32];
    int n = GetWindowTextW(d->hwnd, w, 32);
    HANDLE of = SelectObject(d->dc, on ? g_bold : g_font);
    SetBkMode(d->dc, TRANSPARENT_BK);
    SetTextColor(d->dc, on ? T.text : T.dim);
    RECT tr = d->rc;
    DrawTextW(d->dc, w, n, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(d->dc, of);
    if (d->id == ID_TAB_LOG && g_log_unseen) { /* red dot: a warning is waiting in the log */
        HANDLE p = CreatePen(0, 1, T.accent);
        HANDLE ob = SelectObject(d->dc, g_br_accent), op = SelectObject(d->dc, p);
        int r = S_(7), x = d->rc.right - S_(12), y = d->rc.top + S_(6);
        RoundRect(d->dc, x, y, x + r, y + r, r, r);
        SelectObject(d->dc, ob);
        SelectObject(d->dc, op);
        DeleteObject(p);
    }
}

/* An on/off switch (as in SCCTLink): a pill, red with I when on, grey with O
   when off, then its name. */
static void draw_switch(DrawItem *d)
{
    bool off = d->state & ODS_DISABLED, focus = d->state & ODS_FOCUS;
    bool on = (d->id == ID_SET_DPI ? g_set_dpi : g_set_raw) && !off;
    FillRect(d->dc, &d->rc, g_br_panel);
    int h = S_(22), w = S_(44), x = d->rc.left, y = (d->rc.top + d->rc.bottom - h) / 2;
    DWORD fill = off ? T.dis_bg : on ? T.accent : T.btn, edge = off ? T.dis_bg : on ? T.accent : T.border;
    HBRUSH b = CreateSolidBrush(fill);
    HANDLE p = CreatePen(0, 1, focus && !off ? T.text : edge);
    HANDLE ob = SelectObject(d->dc, b), op = SelectObject(d->dc, p);
    RoundRect(d->dc, x, y, x + w, y + h, h, h);
    SelectObject(d->dc, ob);
    SelectObject(d->dc, op);
    DeleteObject(b);
    DeleteObject(p);
    int k = S_(18), kx = on ? x + w - k - S_(2) : x + S_(2), ky = y + S_(2); /* the knob */
    DWORD kc = off ? T.dis_text : on ? RGBC(255, 255, 255) : T.dim;
    b = CreateSolidBrush(kc);
    p = CreatePen(0, 1, kc);
    ob = SelectObject(d->dc, b);
    op = SelectObject(d->dc, p);
    RoundRect(d->dc, kx, ky, kx + k, ky + k, k, k);
    SelectObject(d->dc, ob);
    SelectObject(d->dc, op);
    DeleteObject(b);
    DeleteObject(p);
    SetBkMode(d->dc, TRANSPARENT_BK);
    HANDLE of = SelectObject(d->dc, g_bold);
    SetTextColor(d->dc, on ? RGBC(255, 255, 255) : off ? T.dis_text : T.dim);
    RECT ir = { on ? x + S_(8) : x + w - S_(18), y, on ? x + S_(18) : x + w - S_(6), y + h };
    DrawTextW(d->dc, on ? WTEXT("I") : WTEXT("O"), 1, &ir, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(d->dc, g_font);
    WCHAR t[64];
    int n = GetWindowTextW(d->hwnd, t, 64);
    SetTextColor(d->dc, off ? T.dis_text : T.text);
    RECT tr = { x + w + S_(10), d->rc.top, d->rc.right, d->rc.bottom };
    DrawTextW(d->dc, t, n, &tr, DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    SelectObject(d->dc, of);
}

static void draw_button(DrawItem *d)
{
    if (is_tab(d->id)) { draw_tab(d); return; }
    if (is_switch(d->id)) { draw_switch(d); return; }
    bool down = d->state & ODS_SELECTED, off = d->state & ODS_DISABLED, focus = d->state & ODS_FOCUS;
    /* the chosen display mode looks like a red (primary) button */
    bool primary = is_primary(d->id) || (is_mode_btn(d->id) && (int)(d->id - ID_MODE_GAME) == g_disp_mode);
    DWORD fill, text, edge;
    if (off) { fill = T.dis_bg; text = T.dis_text; edge = T.dis_bg; }
    else if (primary) { fill = down ? T.accent_down : T.accent; text = T.sel_text; edge = fill; }
    else { fill = down ? T.btn_down : T.btn; text = T.text; edge = T.border; }
    FillRect(d->dc, &d->rc, on_settings_panel(d->id) ? g_br_panel : g_br_bg);
    HBRUSH b = CreateSolidBrush(fill);
    HANDLE p = CreatePen(0, 1, edge);
    HANDLE ob = SelectObject(d->dc, b), op = SelectObject(d->dc, p);
    int r = S_(10);
    RoundRect(d->dc, d->rc.left, d->rc.top, d->rc.right, d->rc.bottom, r, r);
    if (focus && !off) { /* keyboard focus ring */
        HANDLE fp = CreatePen(0, 1, primary ? T.sel_text : T.dim);
        SelectObject(d->dc, fp);
        SelectObject(d->dc, GetStockObject(NULL_BRUSH));
        int k = S_(3);
        RoundRect(d->dc, d->rc.left + k, d->rc.top + k, d->rc.right - k, d->rc.bottom - k, r, r);
        SelectObject(d->dc, p);
        DeleteObject(fp);
    }
    SelectObject(d->dc, ob);
    SelectObject(d->dc, op);
    DeleteObject(b);
    DeleteObject(p);
    WCHAR w[64];
    int n = GetWindowTextW(d->hwnd, w, 64);
    HANDLE of = SelectObject(d->dc, primary ? g_bold : g_font);
    SetBkMode(d->dc, TRANSPARENT_BK);
    SetTextColor(d->dc, text);
    RECT tr = d->rc;
    if (down) { tr.top += 1; tr.bottom += 1; }
    DrawTextW(d->dc, w, n, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    SelectObject(d->dc, of);
}

static void draw_list_item(DrawItem *d)
{
    bool sel = d->state & ODS_SELECTED;
    FillRect(d->dc, &d->rc, sel ? g_br_accent : g_br_panel);
    if (d->item == (UINT)-1) return;
    WCHAR w[UI_NAME + 8];
    int n = (int)SendMessageW(d->hwnd, LB_GETTEXT, d->item, (LPARAM)w);
    if (n <= 0) return;
    HANDLE of = SelectObject(d->dc, g_font);
    SetBkMode(d->dc, TRANSPARENT_BK);
    SetTextColor(d->dc, sel ? T.sel_text : T.text);
    RECT tr = d->rc;
    tr.left += S_(8);
    DrawTextW(d->dc, w, n, &tr, DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    SelectObject(d->dc, of);
}

/* colors for labels and text boxes; returns the background brush */
static LRESULT control_colors(UINT msg, HDC dc, HWND ctl_)
{
    if (ctl_ == g_c[ID_ACCENT]) return (LRESULT)g_br_accent;
    bool panel_label = false, dim_label = false;
    for (int id = ID_SEC_DISP; id <= ID_SET_RESET; id++)
        if (ctl_ == g_c[id]) {
            panel_label = true;
            dim_label = id == ID_SEC_DISP || id == ID_SEC_MOUSE || id == ID_SF_RES || id == ID_SF_DPI ||
                        id == ID_SF_RAW || id == ID_SET_NOTE;
        }
    bool box = msg != WM_CTLCOLORSTATIC || ctl_ == g_c[ID_LOG] || ctl_ == g_c[ID_LOBBY] || ctl_ == g_c[ID_HOWTO] || panel_label;
    SetTextColor(dc, (ctl_ == g_c[ID_LISTLABEL] || ctl_ == g_c[ID_LOBBYLABEL] || dim_label) ? T.dim : T.text);
    SetBkColor(dc, box ? T.panel : T.bg);
    return (LRESULT)(box ? g_br_panel : g_br_bg);
}

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

static HWND ctl(int id, const WCHAR *cls, const WCHAR *text, DWORD style, DWORD ex, int x, int y, int w, int h, HFONT f);

/* a box (text box or list) inside a drawn frame; ix/iy = space inside the frame */
static void framed(int id, const WCHAR *cls, DWORD style, int x, int y, int w, int h, int ix, int iy)
{
    if (g_nframes < 8) g_frames[g_nframes++] = (Frame){ id, x, y, w, h, g_frame_tab };
    ctl(id, cls, WTEXT(""), style, 0, x + ix, y + iy, w - 2 * ix, h - 2 * iy, g_font);
}

static void build(void)
{
    const WCHAR *B = WTEXT("BUTTON"), *T_ = WTEXT("STATIC"), *E = WTEXT("EDIT"), *L = WTEXT("LISTBOX");
    const DWORD OD = WS_TABSTOP | BS_OWNERDRAW;
    ctl(ID_TITLE, T_, WTEXT("Cry of Fear co-op over Steam: no VPN, no port forwarding"), 0, 0, 14, 12, 460, 20, g_bold);
    ctl(ID_THEME, B, WTEXT(""), OD, 0, 486, 9, 100, 24, g_font);
    ctl(ID_ACCENT, T_, WTEXT(""), 0, 0, 14, 36, 572, 2, g_font);
    ctl(ID_HOST, B, WTEXT("Host a game"), OD, 0, 14, 48, 180, 38, g_bold);
    ctl(ID_JOIN, B, WTEXT("Join a friend"), OD, 0, 204, 48, 180, 38, g_bold);
    ctl(ID_STOP, B, WTEXT("Stop"), OD, 0, 486, 48, 100, 38, g_font);
    ctl(ID_STATUS, T_, WTEXT(""), 0, 0, 14, 96, 572, 26, g_big);
    ctl(ID_LOBBYLABEL, T_, WTEXT("Lobby code:"), 0, 0, 14, 134, 74, 22, g_font);
    framed(ID_LOBBY, E, WS_TABSTOP | ES_AUTOHSCROLL, 88, 130, 246, 26, 7, 5);
    ctl(ID_COPYID, B, WTEXT("Copy code"), OD, 0, 342, 130, 100, 26, g_font);
    ctl(ID_JOINID, B, WTEXT("Join this code"), OD, 0, 450, 130, 136, 26, g_font);
    ctl(ID_LISTLABEL, T_, WTEXT(""), 0, 0, 14, 168, 572, 20, g_font);
    framed(ID_LIST, L, WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT | LBS_OWNERDRAWFIXED |
           LBS_HASSTRINGS, 14, 190, 430, 112, 4, 4);
    ctl(ID_ACTION, B, WTEXT("Join"), OD, 0, 452, 190, 134, 34, g_bold);
    ctl(ID_REFRESH, B, WTEXT("Refresh"), OD, 0, 452, 230, 134, 30, g_font);
    ctl(ID_LAUNCH, B, WTEXT("Start Cry of Fear"), OD, 0, 14, 312, 180, 30, g_font);
    ctl(ID_COPYCONN, B, WTEXT("Copy connect command"), OD, 0, 204, 312, 200, 30, g_font);
    ctl(ID_DISPLAY, B, WTEXT("Display"), OD, 0, 410, 312, 176, 30, g_font); /* opens Settings */
    ctl(ID_TAB_HOWTO, B, WTEXT("How to"), OD, 0, 14, 350, 96, 28, g_font);
    ctl(ID_TAB_LOG, B, WTEXT("Log"), OD, 0, 114, 350, 80, 28, g_font);
    ctl(ID_TAB_SET, B, WTEXT("Settings"), OD, 0, 198, 350, 96, 28, g_font);
    ctl(ID_COPYLOG, B, WTEXT("Copy log"), OD, 0, 486, 350, 100, 26, g_font);
    /* one box: "How to", the log and the Settings take turns in it */
    framed(ID_LOG, E, WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY, 14, 384, 572, 250, 7, 5);
    ctl(ID_HOWTO, E, WTEXT(""), WS_VSCROLL | ES_MULTILINE | ES_READONLY, 0, 14 + 7, 384 + 5, 572 - 14, 250 - 10, g_font);
    /* Settings: DISPLAY (mode, resolution, sharp picture) and MOUSE */
    const DWORD NUM = WS_TABSTOP | ES_AUTOHSCROLL | 0x2000 /* ES_NUMBER */, ELL = 0x4000 /* SS_ENDELLIPSIS */;
    ctl(ID_SEC_DISP, T_, WTEXT("DISPLAY"), 0, 0, 28, 396, 300, 16, g_font);
    ctl(ID_SL_MODE, T_, WTEXT("Mode"), ELL, 0, 28, 428, 88, 18, g_font);
    ctl(ID_MODE_GAME, B, WTEXT("Game's own"), OD, 0, 120, 424, 108, 26, g_font);
    ctl(ID_MODE_FULL, B, WTEXT("Fullscreen"), OD, 0, 232, 424, 108, 26, g_font);
    ctl(ID_MODE_WIN, B, WTEXT("Windowed"), OD, 0, 344, 424, 108, 26, g_font);
    ctl(ID_MODE_BL, B, WTEXT("Borderless"), OD, 0, 456, 424, 108, 26, g_font);
    ctl(ID_SL_RES, T_, WTEXT("Resolution"), ELL, 0, 28, 464, 88, 18, g_font);
    g_frame_tab = TAB_SET;
    framed(ID_SET_W, E, NUM, 120, 460, 64, 26, 7, 5);
    ctl(ID_SL_X, T_, WTEXT("x"), 0, 0, 190, 464, 12, 18, g_font);
    framed(ID_SET_H, E, NUM, 204, 460, 64, 26, 7, 5);
    g_frame_tab = -1;
    ctl(ID_SET_PICK, B, WTEXT("Pick..."), OD, 0, 276, 460, 76, 26, g_font);
    ctl(ID_SF_RES, T_, WTEXT(""), ELL, 0, 360, 464, 212, 18, g_font);
    ctl(ID_SET_DPI, B, WTEXT("Sharp picture"), OD, 0, 28, 494, 200, 26, g_font);
    ctl(ID_SF_DPI, T_, WTEXT("for a blurry or cut-off picture (Windows zoom 125%+)"),
        ELL, 0, 236, 498, 336, 18, g_font);
    ctl(ID_SEC_MOUSE, T_, WTEXT("MOUSE"), 0, 0, 28, 534, 300, 16, g_font);
    ctl(ID_SET_RAW, B, WTEXT("No mouse acceleration"), OD, 0, 28, 560, 200, 26, g_font);
    ctl(ID_SF_RAW, T_, WTEXT("raw mouse: the game ignores Windows' acceleration"), ELL, 0, 236, 564, 336, 18, g_font);
    ctl(ID_SET_NOTE, T_, WTEXT("Used by Start Cry of Fear (not when started from Steam)."),
        ELL, 0, 28, 604, 400, 18, g_font);
    ctl(ID_SET_RESET, B, WTEXT("Reset to defaults"), OD, 0, 436, 600, 136, 24, g_font);
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

/* ---------- "How to": what to do next, for the host or the friend ---------- */

static const char *mark(const UiState *s, int bit, int first_open)
{
    /* plain characters on purpose: every Windows font has them */
    if (s->done & (1 << bit)) return "[x] ";          /* done */
    return bit == first_open ? "[>] " : "[  ] ";       /* [>] = you are here */
}

static void build_howto(const UiState *s, char *o, int n)
{
    int open = 0;
    while (open < 4 && (s->done & (1 << open))) open++;
    char port[8];
    sfmt(port, sizeof port, "%u", (unsigned)(s->local_port ? s->local_port : 27115));
    if (s->mode == MODE_HOST) {
        sfmt(o, n,
            "HOSTING - the others join your game\r\n\r\n"
            "%s1. Lobby ready. Friends find you under \"Join a friend\" (or click Copy code and send it).\r\n"
            "%s2. Click \"Start Cry of Fear\" -> Co-op -> create a game -> choose LAN.\r\n"
            "%s3. A friend links: they show up in the list above (CoFLink flashes).\r\n"
            "%s4. They join from their game console. Have fun!\r\n\r\n"
            "WATCH OUT\r\n"
            "- Keep CoFLink open the whole time you play.\r\n"
            "- Only people on your Steam friends list can link. Kick removes someone until you click Stop.\r\n"
            "- The player with the best PC and internet should host.\r\n"
            "- A friend says the server isn't answering? You haven't started the co-op game yet.\r\n"
            "- Same CoFLink version and same game files on both PCs (Steam -> Verify integrity).",
            mark(s, 0, open), mark(s, 1, open), mark(s, 2, open), mark(s, 3, open));
    } else if (s->mode == MODE_JOIN) {
        sfmt(o, n,
            "JOINING - you join your friend's game\r\n\r\n"
            "%s1. Pick the host and click Join (or paste their code). Not listed? Click Refresh.\r\n"
            "%s2. Wait until the host's co-op game is up (CoFLink flashes).\r\n"
            "%s3. Click \"Start Cry of Fear\". In the main menu press ~, then Ctrl+V and Enter.\r\n"
            "      (connect 127.0.0.1:%s is already copied; \"Copy connect command\" copies it again.)\r\n"
            "%s4. You're in. Have fun!\r\n\r\n"
            "WATCH OUT\r\n"
            "- Join with the console only. The game's server list and Steam's \"Join Game\" don't work.\r\n"
            "- Keep CoFLink open the whole time you play.\r\n"
            "- Same CoFLink version and same game files as the host (Steam -> Verify integrity).\r\n"
            "- Players jumping around? Both type in the console:  rate 25000; cl_updaterate 60; cl_cmdrate 60",
            mark(s, 0, open), mark(s, 1, open), mark(s, 2, open), port, mark(s, 3, open));
    } else {
        sfmt(o, n,
            "HOW TO PLAY\r\n\r\n"
            "One of you hosts (the game runs on their PC), the others join.\r\n\r\n"
            "- HOST: click \"Host a game\".\r\n"
            "- FRIEND: click \"Join a friend\".\r\n\r\n"
            "Both of you need: Steam running and online, Cry of Fear from Steam,\r\n"
            "and the same CoFLink version. You must be Steam friends.\r\n\r\n"
            "Before your first game, both of you: Steam -> right-click Cry of Fear -> Properties\r\n"
            "-> Installed Files -> Verify integrity of game files.\r\n\r\n"
            "In the steps, [x] = done and [>] = what to do now.\r\n"
            "Settings: fullscreen, windowed or borderless, resolution, a sharp picture, no mouse acceleration.\r\n"
            "Everything CoFLink does is in the Log (red dot = something needs a look).");
    }
}

static void show_tab(int tab)
{
    g_tab = tab;
    if (tab == TAB_LOG) g_log_unseen = false;
    ShowWindow(g_c[ID_LOG], tab == TAB_LOG ? 5 : 0);
    ShowWindow(g_c[ID_HOWTO], tab == TAB_HOWTO ? 5 : 0);
    ShowWindow(g_c[ID_COPYLOG], tab == TAB_LOG ? 5 : 0);
    for (int id = ID_SEC_DISP; id <= ID_SET_RESET; id++) ShowWindow(g_c[id], tab == TAB_SET ? 5 : 0);
    if (tab == TAB_SET) show_settings();
    RedrawWindow(g_wnd, 0, 0, 0x1 | 0x4 | 0x80); /* the box's frames and lines differ per tab */
}

static void update_howto(void)
{
    static UiState last;
    static bool first = true;
    if (!first && last.mode == g_shown.mode && last.done == g_shown.done && last.local_port == g_shown.local_port) return;
    first = false;
    last = g_shown;
    static char t[4096];
    static WCHAR w[4096];
    build_howto(&g_shown, t, sizeof t);
    SetWindowTextW(g_c[ID_HOWTO], wide(t, w, 4096));
}

static void copy_log(void)
{
    HWND h = g_c[ID_LOG];
    int n = (int)SendMessageW(h, WM_GETTEXTLENGTH, 0, 0);
    WCHAR *w = malloc(((size_t)n + 1) * 2);
    char *u = malloc((size_t)n * 3 + 1);
    if (w && u) {
        GetWindowTextW(h, w, n + 1);
        WideCharToMultiByte(CP_UTF8, 0, w, -1, u, n * 3 + 1, 0, 0);
        clipboard_set(u);
    }
    free(w);
    free(u);
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
                  host ? "Friends linked to your game right now:" :
                         "Friends hosting - pick one and click Join:");
    /* the same button is "Join" for the friend and "Kick" for the host */
    set_text_once(ID_ACTION, host ? "Kick" : "Join");
    ShowWindow(g_c[ID_ACTION], idle ? 0 : 5);
    EnableWindow(g_c[ID_ACTION], !g_closing && ((join && !s->lobby) || (host && g_nview[LIST_FRIENDS_ONLINE] > 0)));
    ShowWindow(g_c[ID_REFRESH], host ? 0 : 5);
    EnableWindow(g_c[ID_REFRESH], !g_closing && (idle || (join && !s->lobby)));
    EnableWindow(g_c[ID_LAUNCH], !g_closing);
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
    bool alert = g_alert, warned = g_warned;
    g_alert = g_warned = false;
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
    if (warned && g_tab != TAB_LOG && !g_log_unseen) { /* red dot on "Log" */
        g_log_unseen = true;
        RedrawWindow(g_c[ID_TAB_LOG], 0, 0, 0x1 | 0x4);
    }
    if (alert && GetForegroundWindow() != g_wnd) { /* only when CoFLink isn't the window in front */
        FLASHWINFO f = { sizeof f, g_wnd, 0x3 | 0xC /* caption + taskbar, until it's clicked */, 0, 0 };
        FlashWindowEx(&f);
        MessageBeep(0x40); /* Windows' "Asterisk" sound */
    }
    if (st_dirty) {
        bool lobby_changed = st.lobby != g_shown.lobby || st.mode != g_shown.mode;
        g_shown = st;
        set_text(ID_STATUS, st.status);
        if (lobby_changed) {
            char id[24] = "";
            if (st.lobby) lobby_code(st.lobby, id);
            if (st.lobby || st.mode != MODE_JOIN) set_text(ID_LOBBY, id);
        }
    }
    for (int k = 0; k < 2; k++)
        if (ld[k] && g_list_shown == k) show_list(k);
    refresh_controls();
    update_howto();

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
            char code[16];
            lobby_code(g_shown.lobby, code);
            copy_text(code);
            ui_log("Lobby code copied. Send it to your friend (Steam chat).");
        }
        break;
    case ID_JOINID: {
        WCHAR w[64];
        char a[64];
        GetWindowTextW(g_c[ID_LOBBY], w, 64);
        WideCharToMultiByte(CP_UTF8, 0, w, -1, a, 64, 0, 0);
        u64 lob = lobby_parse(a);
        if (!lob) {
            MessageBoxW(g_wnd, WTEXT("Paste the lobby code your friend sent you (like 1PCW-4HB) into the Lobby code box first."),
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
        if (g_shown.mode == MODE_HOST) { /* host: kick the picked friend */
            if (code == LBN_DBLCLK) break; /* never kick by double-click */
            int sel = (int)SendMessageW(g_c[ID_LIST], LB_GETCURSEL, 0, 0);
            if (sel < 0 || sel >= g_nview[LIST_FRIENDS_ONLINE]) {
                MessageBoxW(g_wnd, WTEXT("Pick a friend in the list first."), WTEXT("CoFLink"), MB_ICONWARNING);
                break;
            }
            UiPerson *p = &g_view[LIST_FRIENDS_ONLINE][sel];
            char name[UI_NAME], q[256];
            sfmt(name, sizeof name, "%s", p->name);
            char *cut = strstr(name, "  ("); /* drop "  (linked)" */
            if (cut) *cut = 0;
            sfmt(q, sizeof q, "Kick %s?\n\nThey are cut off and can't link to your game again until you click Stop.", name);
            static WCHAR wq[256];
            if (MessageBoxW(g_wnd, wide(q, wq, 256), WTEXT("CoFLink"), MB_YESNO | MB_ICONQUESTION) == IDYES)
                push_cmd(CMD_KICK, p->id);
            break;
        }
        int which = LIST_HOSTING;
        int sel = (int)SendMessageW(g_c[ID_LIST], LB_GETCURSEL, 0, 0);
        if (sel < 0 || sel >= g_nview[which]) {
            MessageBoxW(g_wnd, WTEXT("Pick a friend in the list first."), WTEXT("CoFLink"), MB_ICONWARNING);
            break;
        }
        push_cmd(CMD_JOIN_LOBBY, g_view[which][sel].lobby);
        break;
    }
    case 1: /* Enter key: join the pasted lobby id, if there is one */
        if (g_shown.mode != MODE_HOST && !g_shown.lobby && SendMessageW(g_c[ID_LOBBY], WM_GETTEXTLENGTH, 0, 0) > 0)
            on_command(ID_JOINID, 0);
        break;
    case ID_LAUNCH: push_cmd(CMD_LAUNCH, display_arg()); break;
    case ID_DISPLAY: show_tab(TAB_SET); break;
    case ID_MODE_GAME: case ID_MODE_FULL: case ID_MODE_WIN: case ID_MODE_BL:
        g_disp_mode = id - ID_MODE_GAME;
        if (g_disp_mode == DISPLAY_FULL && !g_disp_w) { g_disp_w = g_desktop.w; g_disp_h = g_desktop.h; }
        settings_changed();
        break;
    case ID_SET_W: case ID_SET_H:
        if (code == 0x300 /* EN_CHANGE */) read_size_boxes();
        break;
    case ID_SET_PICK: pick_resolution(); break;
    case ID_SET_DPI: g_set_dpi = !g_set_dpi; settings_changed(); break;
    case ID_SET_RAW: g_set_raw = !g_set_raw; settings_changed(); break;
    case ID_SET_RESET:
        g_disp_mode = DISPLAY_DEFAULT;
        g_disp_w = g_disp_h = 0;
        g_set_dpi = g_set_raw = false;
        settings_changed();
        break;
    case ID_REFRESH: push_cmd(CMD_REFRESH, 0); break;
    case ID_TAB_HOWTO: show_tab(TAB_HOWTO); break;
    case ID_TAB_LOG: show_tab(TAB_LOG); break;
    case ID_TAB_SET: show_tab(TAB_SET); break;
    case ID_COPYLOG:
        copy_log();
        ui_log("(Log copied. Paste it with Ctrl+V.)");
        break;
    case ID_THEME:
        set_theme(!T.dark);
        save_theme();
        break;
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
    case WM_ERASEBKGND: {
        RECT r;
        GetClientRect(w, &r);
        FillRect((HDC)wp, &r, g_br_bg);
        draw_frames((HDC)wp);
        return 1;
    }
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
        return control_colors(m, (HDC)wp, (HWND)lp);
    case WM_MEASUREITEM: {
        MeasureItem *mi = (MeasureItem *)lp;
        if (mi->type == ODT_LISTBOX) mi->height = (UINT)S_(24);
        return 1;
    }
    case WM_DRAWITEM: {
        DrawItem *d = (DrawItem *)lp;
        if (d->type == ODT_LISTBOX) draw_list_item(d);
        else draw_button(d);
        return 1;
    }
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
    EngineOpts o = { 27015, 27115, 4 };
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
    set_theme(!saved_light()); /* dark unless the player picked light before */
    wc.background = 0;         /* painted in WM_ERASEBKGND with the theme color */
    wc.className = WTEXT("CoFLinkWindow");
    RegisterClassExW(&wc);

    DWORD style = WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT r = { 0, 0, S_(600), S_(648) };
    AdjustWindowRectEx(&r, style, 0, 0);
    int ww = r.right - r.left, wh = r.bottom - r.top;
    int x = (GetSystemMetrics(0) - ww) / 2, y = (GetSystemMetrics(1) - wh) / 2;
    g_wnd = CreateWindowExW(0, wc.className, WTEXT("CoFLink - Cry of Fear co-op"), style, x < 0 ? 0 : x,
                            y < 0 ? 0 : y, ww, wh, 0, 0, inst, 0);
    if (!g_wnd) return 1;
    build();
    load_display();     /* the Display choice from last time */
    load_resolutions();
    display_label();
    set_theme(T.dark); /* button label, title bar, scrollbars */
    show_tab(TAB_HOWTO); /* start on "How to" */
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
