/* plat_win.c - Windows side of plat.h. Win32 functions are declared here by
 * hand (no Windows SDK), imported from kernel32/ws2_32/user32/shell32 (winmm loaded from System32 at run time). */
#include "plat.h"

#define WINAPI __attribute__((stdcall))
#define IMP __declspec(dllimport)
typedef void *HANDLE;
typedef int BOOL;
typedef unsigned int UINT;
typedef unsigned long DWORD;
typedef uintptr_t SOCKET;

IMP void WINAPI Sleep(DWORD ms);
IMP u64 WINAPI GetTickCount64(void);
IMP DWORD WINAPI GetModuleFileNameA(HANDLE mod, char *buf, DWORD n);
IMP BOOL WINAPI SetCurrentDirectoryA(const char *dir);
IMP HANDLE WINAPI GlobalAlloc(UINT flags, size_t n);
IMP void *WINAPI GlobalLock(HANDLE h);
IMP BOOL WINAPI GlobalUnlock(HANDLE h);
IMP void *WINAPI LoadLibraryExA(const char *path, void *file, DWORD flags);
IMP void *WINAPI GetProcAddress(void *mod, const char *name);
#define LOAD_LIBRARY_SEARCH_SYSTEM32 0x800
IMP HANDLE WINAPI CreateToolhelp32Snapshot(DWORD flags, DWORD pid);
IMP BOOL WINAPI Process32First(HANDLE snap, void *entry);
IMP BOOL WINAPI Process32Next(HANDLE snap, void *entry);
IMP BOOL WINAPI CloseHandle(HANDLE h);
IMP DWORD WINAPI GetFileAttributesA(const char *path);
typedef struct { DWORD cb; char *reserved, *desktop, *title; DWORD x, y, w, h, cx, cy, fill, flags;
                 uint16_t show, reserved2; void *reserved3; HANDLE in, out, err; } StartupInfoA; /* 68 bytes */
typedef struct { HANDLE process, thread; DWORD pid, tid; } ProcInfo;
IMP BOOL WINAPI CreateProcessA(const char *app, char *cmd, void *pa, void *ta, BOOL inherit, DWORD flags,
                               void *env, const char *dir, StartupInfoA *si, ProcInfo *pi);
IMP long WINAPI RegGetValueA(void *key, const char *sub, const char *value, DWORD flags, DWORD *type, void *data, DWORD *size);
IMP BOOL WINAPI OpenClipboard(HANDLE w);
IMP BOOL WINAPI EmptyClipboard(void);
IMP HANDLE WINAPI SetClipboardData(UINT fmt, HANDLE h);
IMP BOOL WINAPI CloseClipboard(void);
IMP HANDLE WINAPI ShellExecuteA(HANDLE w, const char *op, const char *file, const char *params, const char *dir, int show);

struct sockaddr_in4 { uint16_t family; uint16_t port; uint32_t addr; uint8_t zero[8]; };
typedef struct { UINT count; SOCKET fd[64]; } fd_set_w;
struct timeval_w { long sec; long usec; };
IMP int WINAPI WSAStartup(uint16_t ver, void *data);
IMP SOCKET WINAPI socket(int af, int type, int proto);
IMP int WINAPI bind(SOCKET s, const void *addr, int len);
IMP int WINAPI connect(SOCKET s, const void *addr, int len);
IMP int WINAPI send(SOCKET s, const void *p, int n, int flags);
IMP int WINAPI recv(SOCKET s, void *p, int n, int flags);
IMP int WINAPI sendto(SOCKET s, const void *p, int n, int flags, const void *to, int tolen);
IMP int WINAPI recvfrom(SOCKET s, void *p, int n, int flags, void *from, int *fromlen);
IMP int WINAPI select(int nfds, fd_set_w *r, fd_set_w *w, fd_set_w *e, const struct timeval_w *t);
IMP int WINAPI closesocket(SOCKET s);
IMP int WINAPI ioctlsocket(SOCKET s, long cmd, unsigned long *arg);
IMP int WINAPI getsockname(SOCKET s, void *addr, int *len);
IMP int WINAPI WSAGetLastError(void);
IMP int WINAPI WSAIoctl(SOCKET s, DWORD code, void *in, DWORD inlen, void *out, DWORD outlen, DWORD *ret, void *ov, void *cb);

char *_ui64toa(u64 v, char *buf, int radix);
u64 _strtoui64(const char *s, char **end, int radix);

#define AF_INET 2
#define SOCK_DGRAM 2
#define IPPROTO_UDP 17
#define FIONBIO ((long)0x8004667E)
#define SIO_UDP_CONNRESET 0x9800000C
#define WSAEWOULDBLOCK 10035
#define WSAECONNRESET 10054
#define WSAEMSGSIZE 10040
#define LOOPBACK 0x0100007Fu /* 127.0.0.1 in network order on little endian */

volatile int g_quit;
static char g_exe_dir[600];

static uint16_t be16(uint16_t v) { return (uint16_t)((v >> 8) | (v << 8)); }

void plat_init(void)
{
    static uint8_t wsa[512];
    WSAStartup(0x0202, wsa);
    /* 1 ms timer resolution, so relayed packets are not held for Windows'
       default 15.6 ms tick (that would add lag and hurt sync). winmm.dll is
       loaded from System32 only, never from the download folder. */
    void *winmm = LoadLibraryExA("winmm.dll", 0, LOAD_LIBRARY_SEARCH_SYSTEM32);
    UINT(WINAPI * tbp)(UINT) = winmm ? (UINT(WINAPI *)(UINT))GetProcAddress(winmm, "timeBeginPeriod") : 0;
    if (tbp) tbp(1);
    DWORD n = GetModuleFileNameA(0, g_exe_dir, sizeof g_exe_dir - 1);
    g_exe_dir[n] = 0;
    for (int i = (int)n - 1; i >= 0; i--)
        if (g_exe_dir[i] == '\\' || g_exe_dir[i] == '/') { g_exe_dir[i] = 0; break; }
    SetCurrentDirectoryA(g_exe_dir); /* steam_appid.txt and steam_api.dll live here */
}

const char *exe_dir(void) { return g_exe_dir; }
u64 now_ms(void) { return GetTickCount64(); }
void sleep_ms(int ms) { Sleep((DWORD)ms); }

static void set_nonblock(SOCKET s)
{
    unsigned long on = 1;
    ioctlsocket(s, FIONBIO, &on);
    /* Without this, an ICMP "port unreachable" (game not started yet) makes
       every later recv on the socket fail with WSAECONNRESET. */
    BOOL off = 0;
    DWORD ret = 0;
    WSAIoctl(s, SIO_UDP_CONNRESET, &off, sizeof off, 0, 0, &ret, 0, 0);
}

sock_t udp_open_local(uint16_t port)
{
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == (SOCKET)~0u) return BADSOCK;
    struct sockaddr_in4 a = { AF_INET, be16(port), LOOPBACK, {0} };
    if (bind(s, &a, sizeof a) != 0) { closesocket(s); return BADSOCK; }
    set_nonblock(s);
    return (sock_t)s;
}

bool udp_connect_local(sock_t s, uint16_t port)
{
    struct sockaddr_in4 a = { AF_INET, be16(port), LOOPBACK, {0} };
    return connect((SOCKET)s, &a, sizeof a) == 0;
}

static int recv_result(int r)
{
    if (r >= 0) return r > 0 ? r : 0;
    int e = WSAGetLastError();
    if (e == WSAECONNRESET || e == WSAEMSGSIZE) return 0; /* skip, keep draining */
    return -1;
}

int udp_send(sock_t s, const void *p, int n) { return send((SOCKET)s, p, n, 0); }
int udp_recv(sock_t s, void *p, int n) { return recv_result(recv((SOCKET)s, p, n, 0)); }

int udp_recvfrom(sock_t s, void *p, int n, uint32_t *ip, uint16_t *port)
{
    struct sockaddr_in4 a;
    int len = sizeof a;
    int r = recv_result(recvfrom((SOCKET)s, p, n, 0, &a, &len));
    if (r > 0) { *ip = a.addr; *port = be16(a.port); }
    return r;
}

int udp_sendto(sock_t s, const void *p, int n, uint32_t ip, uint16_t port)
{
    struct sockaddr_in4 a = { AF_INET, be16(port), ip, {0} };
    return sendto((SOCKET)s, p, n, 0, &a, sizeof a);
}

uint16_t udp_local_port(sock_t s)
{
    struct sockaddr_in4 a;
    int len = sizeof a;
    if (getsockname((SOCKET)s, &a, &len) != 0) return 0;
    return be16(a.port);
}

void udp_close(sock_t s) { if (s != BADSOCK) closesocket((SOCKET)s); }

void wait_socks(const sock_t *s, int n, int ms)
{
    if (n <= 0) { Sleep((DWORD)ms); return; }
    fd_set_w r;
    r.count = 0;
    for (int i = 0; i < n && i < 64; i++)
        if (s[i] != BADSOCK) r.fd[r.count++] = (SOCKET)s[i];
    struct timeval_w t = { 0, ms * 1000 };
    if (r.count == 0) { Sleep((DWORD)ms); return; }
    select(0, &r, 0, 0, &t);
}

void clipboard_set(const char *text)
{
    size_t n = strlen(text) + 1;
    HANDLE h = GlobalAlloc(2 /* GMEM_MOVEABLE */, n);
    if (!h) return;
    memcpy(GlobalLock(h), text, n);
    GlobalUnlock(h);
    if (OpenClipboard(0)) {
        EmptyClipboard();
        SetClipboardData(1 /* CF_TEXT */, h);
        CloseClipboard();
    }
}

/* PROCESSENTRY32 (ANSI, 32-bit): 9 fields of 4 bytes, then the exe name */
typedef struct { DWORD size, usage, pid; uintptr_t heap; DWORD module, threads, parent; long prio; DWORD flags; char exe[260]; } ProcEntry;

static char lower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }

bool process_running(const char *exe_name)
{
    HANDLE snap = CreateToolhelp32Snapshot(2 /* TH32CS_SNAPPROCESS */, 0);
    if (snap == (HANDLE)(intptr_t)-1) return false;
    ProcEntry e;
    memset(&e, 0, sizeof e);
    e.size = sizeof e;
    bool found = false;
    for (BOOL ok = Process32First(snap, &e); ok && !found; ok = Process32Next(snap, &e)) {
        int i = 0;
        while (e.exe[i] && exe_name[i] && lower(e.exe[i]) == lower(exe_name[i])) i++;
        found = !e.exe[i] && !exe_name[i];
    }
    CloseHandle(snap);
    return found;
}


bool file_exists(const char *path)
{
    DWORD a = GetFileAttributesA(path);
    return a != 0xFFFFFFFFu && !(a & 0x10 /* directory */);
}

bool run_program(const char *exe, const char *args, const char *dir)
{
    static char cmd[1400];
    sfmt(cmd, sizeof cmd, "\"%s\" %s", exe, args);
    StartupInfoA si;
    memset(&si, 0, sizeof si);
    si.cb = sizeof si;
    ProcInfo pi;
    memset(&pi, 0, sizeof pi);
    if (!CreateProcessA(exe, cmd, 0, 0, 0, 0, 0, dir, &si, &pi)) return false;
    CloseHandle(pi.thread);
    CloseHandle(pi.process);
    return true;
}

bool steam_install_path(char *out, int n)
{
    DWORD size = (DWORD)n;
    /* HKEY_CURRENT_USER\Software\Valve\Steam  SteamPath  (REG_SZ) */
    if (RegGetValueA((void *)(uintptr_t)0x80000001u, "Software\\Valve\\Steam", "SteamPath", 2 /* RRF_RT_REG_SZ */,
                     0, out, &size) != 0)
        return false;
    out[n - 1] = 0;
    return out[0] != 0;
}

void open_url(const char *url) { ShellExecuteA(0, "open", url, 0, 0, 1); }
void u64_to_str(u64 v, char out[24]) { _ui64toa(v, out, 10); }
u64 str_to_u64(const char *s) { return _strtoui64(s, 0, 10); }

bool file_write_text(const char *path, const char *text)
{
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    fwrite(text, 1, strlen(text), f);
    fclose(f);
    return true;
}
