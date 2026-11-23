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
IMP HDC WINAPI CreateCompatibleDC(HDC dc);
IMP HANDLE WINAPI CreateCompatibleBitmap(HDC dc, int w, int h);
IMP BOOL WINAPI DeleteDC(HDC dc);
IMP BOOL WINAPI StretchBlt(HDC d, int x, int y, int w, int h, HDC s, int sx, int sy, int sw, int sh, DWORD rop);
IMP int WINAPI SetStretchBltMode(HDC dc, int mode);
IMP BOOL WINAPI SetBrushOrgEx(HDC dc, int x, int y, void *old);
IMP HANDLE WINAPI GetCurrentObject(HDC dc, UINT type);
IMP int WINAPI GetObjectW(HANDLE obj, int n, void *out);
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
IMP BOOL WINAPI IsWindowVisible(HWND w);
IMP HANDLE WINAPI LoadImageW(HINSTANCE i, const WCHAR *name, UINT type, int cx, int cy, UINT flags);
IMP BOOL WINAPI DrawIconEx(HDC dc, int x, int y, HICON icon, int cx, int cy, UINT step, HBRUSH flicker, UINT flags);

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
    /* ID_HOST is Play's Host | Join; ID_JOIN the button next to it that starts it (Stop while running) */
    ID_HOST = 101, ID_JOIN, ID_STATUS, ID_LOBBY, ID_COPYID, ID_JOINID,
    ID_LISTLABEL, ID_LIST, ID_ACTION, ID_LAUNCH, ID_COPYCONN, ID_LOG, ID_REFRESH, ID_THEME, ID_TAB_HOWTO, ID_TAB_LOG, ID_COPYLOG, ID_HOWTO,
    /* the Settings page */
    ID_TAB_SET, ID_SL_MODE, ID_MODE_GAME, ID_MODE_FULL, ID_MODE_WIN, ID_MODE_BL,
    ID_SL_RES, ID_SET_W, ID_SL_X, ID_SET_H, ID_SET_PICK, ID_SF_RES, ID_SET_DPI, ID_SF_DPI, ID_SET_RAW, ID_SF_RAW,
    ID_SET_NOTE, ID_SET_RESET,
    ID_LOBBYLABEL,
    ID_NAV_PLAY, /* the sidebar's "Play" (the other pages use the ID_TAB_* buttons) */
    ID_LAST,
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

static HWND g_wnd, g_c[ID_LAST];
static HFONT g_font, g_bold, g_big, g_mid, g_icon, g_icon_big; /* g_mid: 14 px bold; g_icon*: Segoe MDL2 Assets (Windows 10 and 11) */
static int g_dpi = 96;
static HANDLE g_thread;
static bool g_closing;
static u64 g_close_ms;
static UiPerson g_view[2][64]; /* what the list box shows (GUI thread copy) */
static int g_nview[2];
static int g_list_shown = -1;
enum { TAB_HOWTO, TAB_LOG, TAB_SET, TAB_PLAY };
static int g_tab = TAB_PLAY; /* the page shown (picked in the sidebar) */
static bool g_log_unseen;    /* a warning arrived while another page was shown: red dot on Log */
static int g_role;           /* Play's Host | Join while stopped: 0 host, 1 join */

static int S_(int v) { return v * g_dpi / 96; }

/* Icons: Segoe MDL2 Assets glyphs */
#define IC_HOME 0xE80F
#define IC_SETTINGS 0xE713
#define IC_LIST 0xE8FD
#define IC_HELP 0xE897
#define IC_MOON 0xE708
#define IC_SUN 0xE706
#define IC_PLAY 0xE768
#define IC_COPY 0xE8C8
#define IC_REFRESH 0xE72C
#define IC_HOST 0xE704 /* a broadcast-like mark */
#define IC_JOIN 0xE8FA /* add friend */
#define IC_STOP 0xE71A
#define IC_INFO 0xE946
#define IC_KICK 0xE8F8 /* block contact */

/* The window (96 dpi units): the sidebar on the left, the page on the right. */
#define SIDE_W 168
#define CX 188 /* the page's left edge */
#define CW 552 /* its width */
#define WIN_W 760
#define WIN_H 504
#define LIST_Y 170 /* Play: the friends card */
#define LIST_H 252
#define DISP_Y 62  /* Settings: the Display card */
#define DISP_H 168
#define MOUSE_Y 244 /* and the Mouse card */
#define MOUSE_H 92

/* ---------- dark / light theme ---------- */

typedef struct {
    bool dark;
    DWORD bg, panel, text, dim, accent, accent_down, btn, btn_down, border, dis_bg, dis_text, status, sel_text;
    DWORD side, navsel, field; /* the sidebar, its picked page, text boxes and lists */
} Theme;

/* Cry of Fear: near-black with blood red, like the CoFLink icon. The page,
   cards on it (panel), a darker sidebar. */
static const Theme DARK = { true,
    RGBC(18, 18, 20), RGBC(28, 29, 33), RGBC(232, 230, 227), RGBC(139, 142, 150),
    RGBC(163, 22, 26), RGBC(120, 14, 18), RGBC(40, 42, 47), RGBC(30, 31, 35), RGBC(50, 52, 58),
    RGBC(32, 33, 37), RGBC(85, 88, 96), RGBC(235, 87, 87), RGBC(255, 255, 255),
    RGBC(11, 11, 13), RGBC(34, 35, 40), RGBC(20, 20, 23) };
/* light: soft gray page, white cards with thin gray outlines */
static const Theme LIGHT = { false,
    RGBC(244, 245, 247), RGBC(255, 255, 255), RGBC(28, 30, 34), RGBC(106, 112, 122),
    RGBC(196, 32, 38), RGBC(156, 22, 28), RGBC(255, 255, 255), RGBC(234, 236, 240), RGBC(220, 223, 228),
    RGBC(238, 240, 243), RGBC(166, 171, 180), RGBC(196, 32, 38), RGBC(255, 255, 255),
    RGBC(233, 235, 239), RGBC(255, 255, 255), RGBC(248, 249, 251) };

static Theme T;
/* text boxes and the list sit inside rounded frames drawn in the theme's
   colors (Windows' own borders are black in both themes) */
typedef struct { int id, x, y, w, h; } Frame;
static Frame g_frames[8];
static int g_nframes;
static HBRUSH g_br_bg, g_br_panel, g_br_accent, g_br_border, g_br_side, g_br_field;

/* ---------- smooth round shapes ----------
   Windows' RoundRect has no anti-aliasing: small round shapes (switches, dots,
   button corners) come out as visible steps. So a part of the shape is drawn
   AA times bigger in memory, on a copy of what is under it, then shrunk back
   with HALFTONE, which blends the edge pixels. GDI only (gdi32). */
#define AA 4
typedef struct { HDC m; HANDLE bmp, old; int x, y, w, h; } AaPart;

/* Draws the shape l,t,r,b (with dc's pen and brush) into a memory copy of the
   area x,y,w,h, AA times bigger. False: no memory, nothing done. */
static bool aa_begin(AaPart *a, HDC dc, int x, int y, int w, int h, int l, int t, int r, int b, int rw, int rh)
{
    a->m = CreateCompatibleDC(dc);
    a->bmp = a->m ? CreateCompatibleBitmap(dc, w * AA, h * AA) : 0;
    if (!a->bmp) {
        if (a->m) DeleteDC(a->m);
        return false;
    }
    a->old = SelectObject(a->m, a->bmp);
    a->x = x; a->y = y; a->w = w; a->h = h;
    SetStretchBltMode(a->m, 3 /* COLORONCOLOR */);
    StretchBlt(a->m, 0, 0, w * AA, h * AA, dc, x, y, w, h, 0x00CC0020 /* SRCCOPY */);
    struct { UINT style; long wx, wy; DWORD color; } lp; /* LOGPEN */
    memset(&lp, 0, sizeof lp);
    HANDLE pen = 0;
    if (GetObjectW(GetCurrentObject(dc, 1 /* OBJ_PEN */), sizeof lp, &lp) == (int)sizeof lp && lp.style != 5 /* PS_NULL */)
        pen = CreatePen(6 /* PS_INSIDEFRAME: the line stays inside the shape, as at 1:1 */, (lp.wx > 0 ? lp.wx : 1) * AA, lp.color);
    HANDLE op = SelectObject(a->m, pen ? pen : GetStockObject(8 /* NULL_PEN */));
    HANDLE ob = SelectObject(a->m, GetCurrentObject(dc, 2 /* OBJ_BRUSH */));
    RoundRect(a->m, (l - x) * AA, (t - y) * AA, (r - x) * AA, (b - y) * AA, rw * AA, rh * AA);
    SelectObject(a->m, op);
    SelectObject(a->m, ob);
    if (pen) DeleteObject(pen);
    return true;
}

/* Shrinks the part back onto dc, smoothly, and frees it. */
static void aa_end(AaPart *a, HDC dc)
{
    int mode = SetStretchBltMode(dc, 4 /* HALFTONE */);
    SetBrushOrgEx(dc, 0, 0, 0); /* HALFTONE needs it */
    StretchBlt(dc, a->x, a->y, a->w, a->h, a->m, 0, 0, a->w * AA, a->h * AA, 0x00CC0020 /* SRCCOPY */);
    if (mode) SetStretchBltMode(dc, mode);
    SelectObject(a->m, a->old);
    DeleteObject(a->bmp);
    DeleteDC(a->m);
}

/* RoundRect, smooth: a small shape is drawn whole that way; a big one (a card)
   normally, then its four corners smooth (its straight sides need nothing). */
static void smooth_round_rect(HDC dc, int l, int t, int r, int b, int rw, int rh)
{
    int w = r - l, h = b - t;
    if (w <= 0 || h <= 0) return;
    AaPart a;
    if ((long)w * h <= 25000) {
        if (aa_begin(&a, dc, l, t, w, h, l, t, r, b, rw, rh)) aa_end(&a, dc);
        else RoundRect(dc, l, t, r, b, rw, rh);
        return;
    }
    int cw = rw / 2 + 2, ch = rh / 2 + 2;
    if (cw > w / 2) cw = w / 2;
    if (ch > h / 2) ch = h / 2;
    const int xs[4] = { l, r - cw, l, r - cw }, ys[4] = { t, t, b - ch, b - ch };
    AaPart c[4];
    bool ok[4];
    for (int i = 0; i < 4; i++) ok[i] = aa_begin(&c[i], dc, xs[i], ys[i], cw, ch, l, t, r, b, rw, rh);
    RoundRect(dc, l, t, r, b, rw, rh);
    for (int i = 0; i < 4; i++)
        if (ok[i]) aa_end(&c[i], dc);
}

static void draw_frames(HDC dc)
{
    HANDLE p = CreatePen(0, 1, T.border);
    HANDLE ob = SelectObject(dc, g_br_field), op = SelectObject(dc, p);
    for (int i = 0; i < g_nframes; i++) {
        Frame *f = &g_frames[i];
        if (!IsWindowVisible(g_c[f->id])) continue;
        smooth_round_rect(dc, S_(f->x), S_(f->y), S_(f->x + f->w), S_(f->y + f->h), S_(8), S_(8));
    }
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(p);
}

/* A rounded box in fill with an edge (96 dpi units). */
static void rbox(HDC dc, int x, int y, int w, int h, int r, DWORD fill, DWORD edge)
{
    HBRUSH b = CreateSolidBrush(fill);
    HANDLE p = CreatePen(0, 1, edge);
    HANDLE ob = SelectObject(dc, b), op = SelectObject(dc, p);
    smooth_round_rect(dc, S_(x), S_(y), S_(x + w), S_(y + h), S_(r), S_(r));
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(b);
    DeleteObject(p);
}
/* a card on the page */
static void card(HDC dc, int x, int y, int w, int h) { rbox(dc, x, y, w, h, 14, T.panel, T.border); }
/* an icon (one glyph) centred in r, device pixels */
static void glyph(HDC dc, WCHAR g, RECT r, HFONT f, DWORD color)
{
    WCHAR t[2] = { g, 0 };
    HANDLE of = SelectObject(dc, f);
    SetBkMode(dc, TRANSPARENT_BK);
    SetTextColor(dc, color);
    DrawTextW(dc, t, 1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(dc, of);
}
/* a button's face: an icon and its text, centred together (icon 0: text only) */
#define DT_CALCRECT 0x400
static void icon_text(HDC dc, RECT r, WCHAR ic, const WCHAR *t, int n, HFONT f, DWORD color)
{
    HANDLE of = SelectObject(dc, f);
    SetBkMode(dc, TRANSPARENT_BK);
    SetTextColor(dc, color);
    RECT m = { 0, 0, 2000, 100 };
    DrawTextW(dc, t, n, &m, DT_SINGLELINE | DT_CALCRECT);
    int iw = ic ? S_(16) + S_(8) : 0, tw = m.right - m.left, room = r.right - r.left - S_(12);
    if (iw + tw > room) tw = room - iw;
    int x = (r.left + r.right - iw - tw) / 2;
    if (ic) { RECT ir = { x, r.top, x + S_(16), r.bottom }; glyph(dc, ic, ir, g_icon, color); }
    SelectObject(dc, f);
    SetTextColor(dc, color);
    RECT tr = { x + iw, r.top, x + iw + tw, r.bottom };
    DrawTextW(dc, t, n, &tr, DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    SelectObject(dc, of);
}
/* text at x, y, w, h (96 dpi units) */
static void label(HDC dc, const WCHAR *t, int x, int y, int w, int h, HFONT f, DWORD color, UINT fmt)
{
    HANDLE of = SelectObject(dc, f);
    SetBkMode(dc, TRANSPARENT_BK);
    SetTextColor(dc, color);
    RECT r = { S_(x), S_(y), S_(x + w), S_(y + h) };
    DrawTextW(dc, t, -1, &r, fmt | DT_SINGLELINE | DT_END_ELLIPSIS);
    SelectObject(dc, of);
}
/* a ring (circle outline) of pen width k, centre cx, cy, radius r (device pixels) */
static void ring(HDC dc, int cx, int cy, int r, int k, DWORD color)
{
    HANDLE p = CreatePen(0, k, color);
    HANDLE ob = SelectObject(dc, GetStockObject(NULL_BRUSH)), op = SelectObject(dc, p);
    smooth_round_rect(dc, cx - r, cy - r, cx + r, cy + r, 2 * r, 2 * r);
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(p);
}
/* The CoFLink mark: the program's own icon (res/coflink.ico has 16 to 256 px,
   so Windows picks a sharp one for the screen's scale); drawn by hand only if
   that fails: a dark tile with a red ring and a white ring. */
static void draw_logo(HDC dc, int x, int y, int size)
{
    static HICON ic; /* loaded once: the size never changes while CoFLink runs */
    int px = S_(size);
    if (!ic) ic = (HICON)LoadImageW(GetModuleHandleW(0), (const WCHAR *)(uintptr_t)1, 1 /* IMAGE_ICON */, px, px, 0);
    if (ic && DrawIconEx(dc, S_(x), S_(y), ic, px, px, 0, 0, 3 /* DI_NORMAL */)) return;
    rbox(dc, x, y, size, size, size / 3, RGBC(24, 24, 27), RGBC(120, 14, 18));
    int r = S_(size) * 17 / 100, k = S_(size) / 12 > 1 ? S_(size) / 12 : 2, cy = S_(y) + S_(size) / 2;
    ring(dc, S_(x) + S_(size) * 39 / 100, cy, r, k, RGBC(200, 30, 36));
    ring(dc, S_(x) + S_(size) * 62 / 100, cy, r, k, RGBC(236, 236, 236));
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

static DWORD red_text(void) { return T.status; }

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
    else if (g_disp_mode == DISPLAY_DEFAULT) sfmt(t, sizeof t, "the game's own");
    else sfmt(t, sizeof t, "%s", g_disp_w ? "" : "empty = the game's own");
    set_ctl_text(ID_SF_RES, t);
    RedrawWindow(g_c[ID_SET_DPI], 0, 0, 0x1 | 0x4);
    RedrawWindow(g_c[ID_SET_RAW], 0, 0, 0x1 | 0x4);
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
    if (g_br_bg) {
        DeleteObject(g_br_bg); DeleteObject(g_br_panel); DeleteObject(g_br_accent);
        DeleteObject(g_br_border); DeleteObject(g_br_side); DeleteObject(g_br_field);
    }
    g_br_bg = CreateSolidBrush(T.bg);
    g_br_panel = CreateSolidBrush(T.panel);
    g_br_accent = CreateSolidBrush(T.accent);
    g_br_border = CreateSolidBrush(T.border);
    g_br_side = CreateSolidBrush(T.side);
    g_br_field = CreateSolidBrush(T.field);
    if (g_wnd) {
        SetWindowTextW(g_c[ID_THEME], T.dark ? WTEXT("Light theme") : WTEXT("Dark theme"));
        apply_system_parts();
        RedrawWindow(g_wnd, 0, 0, 0x1 | 0x4 | 0x80 | 0x400); /* invalidate, erase, children, frame */
    }
}

static bool is_tab(UINT id)
{
    return id == ID_NAV_PLAY || id == ID_TAB_HOWTO || id == ID_TAB_LOG || id == ID_TAB_SET || id == ID_THEME;
}
static int tab_of(UINT id)
{
    return id == ID_NAV_PLAY ? TAB_PLAY : id == ID_TAB_LOG ? TAB_LOG : id == ID_TAB_SET ? TAB_SET
         : id == ID_TAB_HOWTO ? TAB_HOWTO : -1;
}
static bool is_mode_btn(UINT id) { return id >= ID_MODE_GAME && id <= ID_MODE_BL; }
static bool is_switch(UINT id) { return id == ID_SET_DPI || id == ID_SET_RAW; }
/* Play's start button: "Stop" while hosting or joining */
static bool is_stop(UINT id) { return id == ID_JOIN && g_shown.mode != MODE_IDLE; }
/* red buttons: Join / Kick, Start Cry of Fear, the start button, the picked display mode */
static bool is_primary(UINT id)
{
    return id == ID_ACTION || id == ID_LAUNCH || (id == ID_JOIN && !is_stop(id)) ||
           (is_mode_btn(id) && (int)(id - ID_MODE_GAME) == g_disp_mode);
}
/* What a button sits on: a card (panel) or the page. */
static bool on_card(UINT id)
{
    return id == ID_ACTION || id == ID_REFRESH || id == ID_COPYID || id == ID_JOINID || is_mode_btn(id) ||
           id == ID_SET_PICK || is_switch(id);
}

/* The sidebar's pages: the picked one on a lighter pill with a red bar on its left. */
static void draw_tab(DrawItem *d)
{
    bool on = tab_of(d->id) == g_tab;
    FillRect(d->dc, &d->rc, g_br_side);
    if (on) {
        HBRUSH b = CreateSolidBrush(T.navsel);
        HANDLE p = CreatePen(0, 1, T.dark ? T.navsel : T.border);
        HANDLE ob = SelectObject(d->dc, b), op = SelectObject(d->dc, p);
        smooth_round_rect(d->dc, d->rc.left, d->rc.top, d->rc.right, d->rc.bottom, S_(10), S_(10));
        SelectObject(d->dc, ob);
        SelectObject(d->dc, op);
        DeleteObject(b);
        DeleteObject(p);
        RECT u = { d->rc.left + S_(6), d->rc.top + S_(9), d->rc.left + S_(9), d->rc.bottom - S_(9) };
        FillRect(d->dc, &u, g_br_accent);
    }
    WCHAR w[32];
    int n = GetWindowTextW(d->hwnd, w, 32);
    WCHAR ic = d->id == ID_NAV_PLAY ? IC_HOME : d->id == ID_TAB_SET ? IC_SETTINGS : d->id == ID_TAB_LOG ? IC_LIST
             : d->id == ID_TAB_HOWTO ? IC_HELP : T.dark ? IC_SUN : IC_MOON;
    RECT ir = { d->rc.left + S_(16), d->rc.top, d->rc.left + S_(34), d->rc.bottom };
    glyph(d->dc, ic, ir, g_icon, on ? T.text : T.dim);
    HANDLE of = SelectObject(d->dc, on ? g_bold : g_font);
    SetBkMode(d->dc, TRANSPARENT_BK);
    SetTextColor(d->dc, on ? T.text : T.dim);
    RECT tr = d->rc;
    tr.left += S_(44);
    DrawTextW(d->dc, w, n, &tr, DT_VCENTER | DT_SINGLELINE);
    SelectObject(d->dc, of);
    if (d->id == ID_TAB_LOG && g_log_unseen) { /* red dot: a warning is waiting in the log */
        DWORD c = red_text();
        HBRUSH b = CreateSolidBrush(c);
        HANDLE p = CreatePen(0, 1, c);
        HANDLE ob = SelectObject(d->dc, b), op = SelectObject(d->dc, p);
        int r = S_(8), x = d->rc.right - S_(20), y = (d->rc.top + d->rc.bottom - r) / 2;
        smooth_round_rect(d->dc, x, y, x + r, y + r, r, r);
        SelectObject(d->dc, ob);
        SelectObject(d->dc, op);
        DeleteObject(b);
        DeleteObject(p);
    }
}

/* Play's Host | Join: two halves, the picked one (or what is running) raised
   with a red edge; greyed while hosting or joining (it can't change then). */
static void draw_segment(DrawItem *d)
{
    const UiState *s = &g_shown;
    bool off = d->state & ODS_DISABLED, right = s->mode == MODE_JOIN || (s->mode == MODE_IDLE && g_role == 1);
    FillRect(d->dc, &d->rc, g_br_bg);
    HBRUSH b = CreateSolidBrush(T.dark ? T.side : T.btn_down);
    HANDLE p = CreatePen(0, 1, T.border);
    HANDLE ob = SelectObject(d->dc, b), op = SelectObject(d->dc, p);
    smooth_round_rect(d->dc, d->rc.left, d->rc.top, d->rc.right, d->rc.bottom, S_(14), S_(14));
    SelectObject(d->dc, ob);
    SelectObject(d->dc, op);
    DeleteObject(b);
    DeleteObject(p);
    int mid = (d->rc.left + d->rc.right) / 2, k = S_(4);
    RECT half = { right ? mid + k / 2 : d->rc.left + k, d->rc.top + k, right ? d->rc.right - k : mid - k / 2, d->rc.bottom - k };
    DWORD edge = off ? T.border : T.accent;
    b = CreateSolidBrush(T.panel);
    p = CreatePen(0, S_(1) + (off ? 0 : 1), edge);
    ob = SelectObject(d->dc, b);
    op = SelectObject(d->dc, p);
    smooth_round_rect(d->dc, half.left, half.top, half.right, half.bottom, S_(12), S_(12));
    SelectObject(d->dc, ob);
    SelectObject(d->dc, op);
    DeleteObject(b);
    DeleteObject(p);
    if (d->state & ODS_FOCUS) { /* keyboard focus: a thin line inside the picked half */
        HANDLE fp = CreatePen(0, 1, T.dim);
        ob = SelectObject(d->dc, GetStockObject(NULL_BRUSH));
        op = SelectObject(d->dc, fp);
        smooth_round_rect(d->dc, half.left + k, half.top + k, half.right - k, half.bottom - k, S_(9), S_(9));
        SelectObject(d->dc, ob);
        SelectObject(d->dc, op);
        DeleteObject(fp);
    }
    /* each half: an icon, the name and a line under it; the picked one bright */
    SetBkMode(d->dc, TRANSPARENT_BK);
    for (int i = 0; i < 2; i++) {
        bool on = (i == 1) == right;
        int l = i ? mid : d->rc.left, r = i ? d->rc.right : mid;
        RECT ir = { l + S_(14), d->rc.top, l + S_(40), d->rc.bottom };
        glyph(d->dc, i ? IC_JOIN : IC_HOST, ir, g_icon_big, on ? (off ? T.text : red_text()) : T.dim);
        HANDLE of = SelectObject(d->dc, g_bold);
        SetTextColor(d->dc, on ? T.text : T.dim);
        RECT tr = { l + S_(48), d->rc.top + S_(13), r - S_(8), d->rc.top + S_(32) };
        DrawTextW(d->dc, i ? WTEXT("Join a friend") : WTEXT("Host a game"), -1, &tr, DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        SelectObject(d->dc, g_font);
        SetTextColor(d->dc, T.dim);
        RECT sr = { l + S_(48), d->rc.top + S_(32), r - S_(8), d->rc.bottom - S_(10) };
        DrawTextW(d->dc, i ? WTEXT("From the list or a code") : WTEXT("Friends-only lobby"), -1, &sr, DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        SelectObject(d->dc, of);
    }
}

/* A settings row: the name on the left, a small switch on the right. */
static void draw_switch(DrawItem *d)
{
    bool off = d->state & ODS_DISABLED, focus = d->state & ODS_FOCUS;
    bool on = (d->id == ID_SET_DPI ? g_set_dpi : g_set_raw) && !off;
    FillRect(d->dc, &d->rc, g_br_panel);
    int h = S_(18), w = S_(34), x = d->rc.right - w, y = (d->rc.top + d->rc.bottom - h) / 2;
    DWORD fill = on ? T.accent : off ? T.dis_bg : T.btn, edge = on ? fill : off ? T.dis_bg : T.border;
    HBRUSH b = CreateSolidBrush(fill);
    HANDLE p = CreatePen(0, 1, focus && !off ? T.text : edge);
    HANDLE ob = SelectObject(d->dc, b), op = SelectObject(d->dc, p);
    smooth_round_rect(d->dc, x, y, x + w, y + h, h, h);
    SelectObject(d->dc, ob);
    SelectObject(d->dc, op);
    DeleteObject(b);
    DeleteObject(p);
    int k = h - S_(4), kx = on ? x + w - k - S_(2) : x + S_(2), ky = y + S_(2); /* the knob */
    DWORD kc = on ? RGBC(255, 255, 255) : off ? T.dis_text : T.dim;
    b = CreateSolidBrush(kc);
    p = CreatePen(0, 1, kc);
    ob = SelectObject(d->dc, b);
    op = SelectObject(d->dc, p);
    smooth_round_rect(d->dc, kx, ky, kx + k, ky + k, k, k);
    SelectObject(d->dc, ob);
    SelectObject(d->dc, op);
    DeleteObject(b);
    DeleteObject(p);
    SetBkMode(d->dc, TRANSPARENT_BK);
    HANDLE of = SelectObject(d->dc, g_font);
    WCHAR t[64];
    int n = GetWindowTextW(d->hwnd, t, 64);
    SetTextColor(d->dc, off ? T.dis_text : T.text);
    RECT tr = { d->rc.left, d->rc.top, x - S_(10), d->rc.bottom };
    DrawTextW(d->dc, t, n, &tr, DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    SelectObject(d->dc, of);
}

static void draw_button(DrawItem *d)
{
    if (is_tab(d->id)) { draw_tab(d); return; }
    if (is_switch(d->id)) { draw_switch(d); return; }
    if (d->id == ID_HOST) { draw_segment(d); return; }
    bool down = d->state & ODS_SELECTED, off = d->state & ODS_DISABLED, focus = d->state & ODS_FOCUS;
    bool primary = is_primary(d->id), stop = is_stop(d->id);
    DWORD fill, text, edge;
    if (off) { fill = T.dis_bg; text = T.dis_text; edge = T.dis_bg; }
    else if (primary) { fill = down ? T.accent_down : T.accent; text = T.sel_text; edge = fill; }
    else if (stop || d->id == ID_SET_RESET) { /* red words on a plain button */
        fill = down ? T.btn_down : T.btn; text = red_text(); edge = T.dark ? RGBC(90, 34, 36) : RGBC(230, 180, 180);
    }
    else { fill = down ? T.btn_down : T.btn; text = T.text; edge = T.border; }
    FillRect(d->dc, &d->rc, on_card(d->id) ? g_br_panel : g_br_bg);
    HBRUSH b = CreateSolidBrush(fill);
    HANDLE p = CreatePen(0, 1, edge);
    HANDLE ob = SelectObject(d->dc, b), op = SelectObject(d->dc, p);
    int r = S_(10);
    smooth_round_rect(d->dc, d->rc.left, d->rc.top, d->rc.right, d->rc.bottom, r, r);
    if (focus && !off) { /* keyboard focus ring */
        HANDLE fp = CreatePen(0, 1, primary ? T.sel_text : T.dim);
        SelectObject(d->dc, fp);
        SelectObject(d->dc, GetStockObject(NULL_BRUSH));
        int k = S_(3);
        smooth_round_rect(d->dc, d->rc.left + k, d->rc.top + k, d->rc.right - k, d->rc.bottom - k, r, r);
        SelectObject(d->dc, p);
        DeleteObject(fp);
    }
    SelectObject(d->dc, ob);
    SelectObject(d->dc, op);
    DeleteObject(b);
    DeleteObject(p);
    WCHAR w[64];
    int n = GetWindowTextW(d->hwnd, w, 64);
    RECT tr = d->rc;
    if (down) { tr.top += 1; tr.bottom += 1; }
    bool host = g_shown.mode == MODE_HOST;
    WCHAR ic = d->id == ID_LAUNCH ? IC_PLAY : d->id == ID_COPYCONN || d->id == ID_COPYID || d->id == ID_COPYLOG ? IC_COPY
             : d->id == ID_REFRESH ? IC_REFRESH : d->id == ID_JOINID ? IC_JOIN : d->id == ID_ACTION ? (host ? IC_KICK : IC_JOIN)
             : d->id == ID_JOIN ? (stop ? IC_STOP : g_role ? IC_JOIN : IC_HOST) : 0;
    icon_text(d->dc, tr, ic, w, n, primary || d->id == ID_JOIN ? g_bold : g_font, text);
}

static void draw_list_item(DrawItem *d)
{
    bool sel = d->state & ODS_SELECTED;
    FillRect(d->dc, &d->rc, sel ? g_br_accent : g_br_field);
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
    (void)msg;
    /* text boxes and the list: the field color inside their frames */
    bool field = ctl_ == g_c[ID_LOBBY] || ctl_ == g_c[ID_LIST] || ctl_ == g_c[ID_SET_W] || ctl_ == g_c[ID_SET_H];
    /* everything else sits on a card, except the note under the Settings cards */
    bool page = ctl_ == g_c[ID_SET_NOTE];
    bool dim = ctl_ == g_c[ID_LISTLABEL] || ctl_ == g_c[ID_LOBBYLABEL] || ctl_ == g_c[ID_SF_RES] ||
               ctl_ == g_c[ID_SF_DPI] || ctl_ == g_c[ID_SF_RAW] || ctl_ == g_c[ID_SET_NOTE];
    SetTextColor(dc, dim ? T.dim : T.text);
    SetBkColor(dc, field ? T.field : page ? T.bg : T.panel);
    return (LRESULT)(field ? g_br_field : page ? g_br_bg : g_br_panel);
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
    static const char *last[ID_LAST];
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
    if (g_nframes < 8) g_frames[g_nframes++] = (Frame){ id, x, y, w, h };
    ctl(id, cls, WTEXT(""), style, 0, x + ix, y + iy, w - 2 * ix, h - 2 * iy, g_font);
}

static void build(void)
{
    const WCHAR *B = WTEXT("BUTTON"), *T_ = WTEXT("STATIC"), *E = WTEXT("EDIT"), *L = WTEXT("LISTBOX");
    const DWORD OD = WS_TABSTOP | BS_OWNERDRAW, ELL = 0x4000 /* SS_ENDELLIPSIS */;
    /* the sidebar: the logo and name are drawn (draw_page), the pages are buttons */
    ctl(ID_NAV_PLAY, B, WTEXT("Play"), OD, 0, 10, 70, 148, 36, g_font);
    ctl(ID_TAB_SET, B, WTEXT("Settings"), OD, 0, 10, 110, 148, 36, g_font);
    ctl(ID_TAB_LOG, B, WTEXT("Log"), OD, 0, 10, 150, 148, 36, g_font);
    ctl(ID_TAB_HOWTO, B, WTEXT("How to"), OD, 0, 10, WIN_H - 92, 148, 36, g_font);
    ctl(ID_THEME, B, WTEXT(""), OD, 0, 10, WIN_H - 52, 148, 36, g_font);

    /* Play: the status card, Host | Join and the button that starts it (Stop
       while running), the friends card with the lobby code, Start */
    ctl(ID_STATUS, T_, WTEXT(""), ELL, 0, CX + 38, 20 + 15, CW - 54, 22, g_mid);
    ctl(ID_HOST, B, WTEXT("Host or join"), OD, 0, CX, 90, 400, 64, g_bold);
    ctl(ID_JOIN, B, WTEXT("Host"), OD, 0, CX + 412, 90, 140, 64, g_bold);
    ctl(ID_LISTLABEL, T_, WTEXT(""), ELL, 0, CX + 16, LIST_Y + 14, 300, 20, g_font);
    ctl(ID_ACTION, B, WTEXT("Join"), OD, 0, CX + CW - 16 - 196, LIST_Y + 10, 92, 28, g_bold);
    ctl(ID_REFRESH, B, WTEXT("Refresh"), OD, 0, CX + CW - 16 - 96, LIST_Y + 10, 96, 28, g_font);
    framed(ID_LIST, L, WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT | LBS_OWNERDRAWFIXED |
           LBS_HASSTRINGS, CX + 12, LIST_Y + 44, CW - 24, 148, 4, 4);
    ctl(ID_LOBBYLABEL, T_, WTEXT("Lobby code"), 0, 0, CX + 16, LIST_Y + 212, 80, 20, g_font);
    framed(ID_LOBBY, E, WS_TABSTOP | ES_AUTOHSCROLL, CX + 100, LIST_Y + 206, 180, 30, 8, 7);
    /* one of these two shows: the host copies their code, a friend joins one */
    ctl(ID_COPYID, B, WTEXT("Copy code"), OD, 0, CX + 290, LIST_Y + 206, 128, 30, g_font);
    ctl(ID_JOINID, B, WTEXT("Join this code"), OD, 0, CX + 290, LIST_Y + 206, 128, 30, g_font);
    ctl(ID_LAUNCH, B, WTEXT("Start Cry of Fear"), OD, 0, CX, 442, 300, 42, g_bold);
    ctl(ID_COPYCONN, B, WTEXT("Copy connect command"), OD, 0, CX + 312, 442, 240, 42, g_font);

    /* Log and How to: a title (drawn), the box in a card; Copy log on the title's line */
    ctl(ID_COPYLOG, B, WTEXT("Copy log"), OD, 0, CX + CW - 112, 20, 112, 30, g_font);
    ctl(ID_LOG, E, WTEXT(""), WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY, 0, CX + 14, 72, CW - 28, 402, g_font);
    ctl(ID_HOWTO, E, WTEXT(""), WS_VSCROLL | ES_MULTILINE | ES_READONLY, 0, CX + 14, 72, CW - 28, 402, g_font);
    SendMessageW(g_c[ID_LOG], EM_SETLIMITTEXT, 0x7FFFFFFE, 0);

    /* Settings: the Display card (mode, resolution, sharp picture), the Mouse
       card, then Reset; the cards' titles are drawn (draw_set_deco) */
    const DWORD NUM = WS_TABSTOP | ES_AUTOHSCROLL | 0x2000 /* ES_NUMBER */;
    const int X = CX + 16, RW = CW - 32; /* a row inside a card */
    ctl(ID_SL_MODE, T_, WTEXT("Mode"), ELL, 0, X, DISP_Y + 46, 96, 18, g_font);
    ctl(ID_MODE_GAME, B, WTEXT("Game's own"), OD, 0, X + 104, DISP_Y + 40, 102, 30, g_font);
    ctl(ID_MODE_FULL, B, WTEXT("Fullscreen"), OD, 0, X + 210, DISP_Y + 40, 102, 30, g_font);
    ctl(ID_MODE_WIN, B, WTEXT("Windowed"), OD, 0, X + 316, DISP_Y + 40, 102, 30, g_font);
    ctl(ID_MODE_BL, B, WTEXT("Borderless"), OD, 0, X + 422, DISP_Y + 40, 98, 30, g_font);
    ctl(ID_SL_RES, T_, WTEXT("Resolution"), ELL, 0, X, DISP_Y + 84, 96, 18, g_font);
    framed(ID_SET_W, E, NUM, X + 104, DISP_Y + 78, 70, 30, 8, 7);
    ctl(ID_SL_X, T_, WTEXT("x"), 0, 0, X + 180, DISP_Y + 84, 12, 18, g_font);
    framed(ID_SET_H, E, NUM, X + 196, DISP_Y + 78, 70, 30, 8, 7);
    ctl(ID_SET_PICK, B, WTEXT("Pick..."), OD, 0, X + 274, DISP_Y + 78, 76, 30, g_font);
    ctl(ID_SF_RES, T_, WTEXT(""), ELL, 0, X + 358, DISP_Y + 84, RW - 358, 18, g_font);
    ctl(ID_SET_DPI, B, WTEXT("Sharp picture"), OD, 0, X, DISP_Y + 118, RW, 26, g_font);
    ctl(ID_SF_DPI, T_, WTEXT("For a blurry or cut-off picture (Windows zoom 125% or more)"),
        ELL, 0, X, DISP_Y + 144, RW, 18, g_font);
    ctl(ID_SET_RAW, B, WTEXT("No mouse acceleration"), OD, 0, X, MOUSE_Y + 38, RW, 26, g_font);
    ctl(ID_SF_RAW, T_, WTEXT("Raw mouse: the game ignores Windows' acceleration"), ELL, 0, X, MOUSE_Y + 64, RW, 18, g_font);
    ctl(ID_SET_NOTE, T_, WTEXT("Used by Start Cry of Fear (not when started from Steam)."),
        ELL, 0, CX + 2, MOUSE_Y + MOUSE_H + 14, CW, 18, g_font);
    ctl(ID_SET_RESET, B, WTEXT("Reset to defaults"), OD, 0, CX, 442, 160, 42, g_font);
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
            "%s1. Lobby ready. Friends find you under \"Join a friend\" (or click Copy code on Play and send it).\r\n"
            "%s2. Click \"Start Cry of Fear\" -> Co-op -> create a game -> choose LAN.\r\n"
            "%s3. A friend links: they show up in the list on Play (CoFLink flashes).\r\n"
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
            "- HOST: on Play, pick \"Host a game\", then click Host.\r\n"
            "- FRIEND: on Play, pick \"Join a friend\", then click Join.\r\n\r\n"
            "Both of you need: Steam running and online, Cry of Fear from Steam,\r\n"
            "and the same CoFLink version. You must be Steam friends.\r\n\r\n"
            "Before your first game, both of you: Steam -> right-click Cry of Fear -> Properties\r\n"
            "-> Installed Files -> Verify integrity of game files.\r\n\r\n"
            "In the steps, [x] = done and [>] = what to do now.\r\n"
            "Settings: fullscreen, windowed or borderless, resolution, a sharp picture, no mouse acceleration.\r\n"
            "Everything CoFLink does is in the Log (red dot = something needs a look).");
    }
}

/* The page each control is on (-1: the sidebar, always shown). */
static int page_of(int id)
{
    if (is_tab((UINT)id)) return -1;
    if (id == ID_LOG || id == ID_COPYLOG) return TAB_LOG;
    if (id == ID_HOWTO) return TAB_HOWTO;
    if (id >= ID_SL_MODE && id <= ID_SET_RESET) return TAB_SET;
    return TAB_PLAY;
}
/* Shown now: on the page in view, and wanted in this mode. */
static bool ctl_wanted(int id)
{
    int pg = page_of(id);
    if (pg >= 0 && pg != g_tab) return false;
    const UiState *s = &g_shown;
    bool host = s->mode == MODE_HOST;
    if (id == ID_ACTION) return s->mode != MODE_IDLE;
    if (id == ID_REFRESH) return !host;
    if (id == ID_COPYID) return host;  /* the host only copies their code */
    if (id == ID_JOINID) return !host; /* a friend only joins one */
    return true;
}
static void apply_visibility(void)
{
    for (int i = ID_HOST; i < ID_LAST; i++) {
        if (!g_c[i]) continue;
        bool want = ctl_wanted(i);
        if (want != (IsWindowVisible(g_c[i]) != 0)) ShowWindow(g_c[i], want ? 5 : 0);
    }
}

/* The Settings page: the cards with their titles. */
static void draw_set_deco(HDC dc)
{
    label(dc, WTEXT("Settings"), CX + 2, 20, 300, 30, g_big, T.text, DT_VCENTER);
    card(dc, CX, DISP_Y, CW, DISP_H);
    label(dc, WTEXT("Display"), CX + 16, DISP_Y + 12, 200, 18, g_bold, T.text, 0);
    card(dc, CX, MOUSE_Y, CW, MOUSE_H);
    label(dc, WTEXT("Mouse"), CX + 16, MOUSE_Y + 12, 200, 18, g_bold, T.text, 0);
}

/* The Play page: a dot and the status in a card; the friends card. */
static void draw_play(HDC dc)
{
    card(dc, CX, 20, CW, 52);
    rbox(dc, CX + 6, 20 + 12, 3, 28, 0, T.accent, T.accent); /* a red bar on the left, like the sidebar's */
    /* the dot: grey while stopped, red while hosting or joining */
    DWORD dot = g_shown.mode == MODE_IDLE ? T.dim : red_text();
    rbox(dc, CX + 18, 20 + 21, 10, 10, 10, dot, dot);
    card(dc, CX, LIST_Y, CW, LIST_H);
}

/* The whole window's background: the sidebar with the logo, then the page. */
static void draw_page(HDC dc)
{
    RECT r;
    GetClientRect(g_wnd, &r);
    FillRect(dc, &r, g_br_bg);
    RECT side = { 0, 0, S_(SIDE_W), r.bottom };
    FillRect(dc, &side, g_br_side);
    RECT line = { S_(SIDE_W) - 1, 0, S_(SIDE_W), r.bottom };
    FillRect(dc, &line, g_br_border);
    draw_logo(dc, 18, 20, 32);
    label(dc, WTEXT("CoFLink"), 58, 20, 104, 32, g_mid, T.text, DT_VCENTER);
    switch (g_tab) {
    case TAB_PLAY: draw_play(dc); break;
    case TAB_SET: draw_set_deco(dc); break;
    default: /* Log, How to: a title and one card */
        label(dc, g_tab == TAB_LOG ? WTEXT("Log") : WTEXT("How to play"), CX + 2, 20, 300, 30, g_big, T.text, DT_VCENTER);
        card(dc, CX, 62, CW, WIN_H - 62 - 20);
        break;
    }
    draw_frames(dc);
}

static void show_tab(int tab)
{
    g_tab = tab;
    if (tab == TAB_LOG) g_log_unseen = false;
    apply_visibility();
    if (tab == TAB_SET) show_settings();
    RedrawWindow(g_wnd, 0, 0, 0x1 | 0x4 | 0x80); /* invalidate, erase, children: cards and frames differ per page */
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
    /* Host | Join is picked while stopped; the button next to it starts it, or stops */
    EnableWindow(g_c[ID_HOST], idle && !g_closing);
    EnableWindow(g_c[ID_JOIN], !g_closing);
    set_text_once(ID_JOIN, !idle ? "Stop" : g_role ? "Join" : "Host");
    static int ro = -1;
    int want_ro = host || (join && s->lobby);
    if (want_ro != ro) { SendMessageW(g_c[ID_LOBBY], EM_SETREADONLY, (WPARAM)want_ro, 0); ro = want_ro; }
    set_text_once(ID_LOBBYLABEL, host ? "Your code" : "Lobby code");
    EnableWindow(g_c[ID_COPYID], host && s->lobby);
    EnableWindow(g_c[ID_JOINID], (idle || join) && !s->lobby);
    int which = host ? LIST_FRIENDS_ONLINE : LIST_HOSTING;
    if (which != g_list_shown) show_list(which);
    set_text_once(ID_LISTLABEL, host ? "Friends linked to your game" : "Friends hosting");
    /* the same button is "Join" for the friend and "Kick" for the host */
    set_text_once(ID_ACTION, host ? "Kick" : "Join");
    EnableWindow(g_c[ID_ACTION], !g_closing && ((join && !s->lobby) || (host && g_nview[LIST_FRIENDS_ONLINE] > 0)));
    EnableWindow(g_c[ID_REFRESH], !g_closing && (idle || (join && !s->lobby)));
    EnableWindow(g_c[ID_LAUNCH], !g_closing);
    EnableWindow(g_c[ID_COPYCONN], join && s->lobby);
    apply_visibility();
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
        bool mode_changed = st.mode != g_shown.mode;
        g_shown = st;
        if (mode_changed && g_tab == TAB_PLAY) /* the status dot, Host | Join and its button */
            RedrawWindow(g_wnd, 0, 0, 0x1 | 0x4 | 0x80);
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
    case ID_HOST: /* Host | Join: pick (only while stopped) */
        if (g_shown.mode == MODE_IDLE) {
            g_role = !g_role;
            RedrawWindow(g_c[ID_HOST], 0, 0, 0x1 | 0x4);
            refresh_controls(); /* the start button's name */
        }
        break;
    case ID_JOIN: /* the start button: Host or Join, Stop while running */
        if (g_shown.mode != MODE_IDLE) push_cmd(CMD_STOP, 0);
        else push_cmd(g_role ? CMD_JOIN : CMD_HOST, 0);
        break;
    case ID_NAV_PLAY: show_tab(TAB_PLAY); break;
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
    case WM_ERASEBKGND:
        draw_page((HDC)wp);
        return 1;
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
    g_mid = CreateFontW(-S_(14), 0, 0, 0, FW_BOLD, 0, 0, 0, 1, 0, 0, CLEARTYPE_QUALITY, 0, face);
    g_icon = CreateFontW(-S_(15), 0, 0, 0, FW_NORMAL, 0, 0, 0, 1, 0, 0, CLEARTYPE_QUALITY, 0, WTEXT("Segoe MDL2 Assets"));
    g_icon_big = CreateFontW(-S_(20), 0, 0, 0, FW_NORMAL, 0, 0, 0, 1, 0, 0, CLEARTYPE_QUALITY, 0, WTEXT("Segoe MDL2 Assets"));

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
    RECT r = { 0, 0, S_(WIN_W), S_(WIN_H) };
    AdjustWindowRectEx(&r, style, 0, 0);
    int ww = r.right - r.left, wh = r.bottom - r.top;
    int x = (GetSystemMetrics(0) - ww) / 2, y = (GetSystemMetrics(1) - wh) / 2;
    g_wnd = CreateWindowExW(0, wc.className, WTEXT("CoFLink - Cry of Fear co-op"), style, x < 0 ? 0 : x,
                            y < 0 ? 0 : y, ww, wh, 0, 0, inst, 0);
    if (!g_wnd) return 1;
    build();
    load_display();     /* the Display choice from last time */
    load_resolutions();
    set_theme(T.dark); /* button label, title bar, scrollbars */
    show_tab(TAB_PLAY); /* start on Play */
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
