/* plat_posix.c - Linux side of plat.h, used only by the mock test build. */
#define _GNU_SOURCE
#include "plat.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

volatile int g_quit;

static void on_sig(int s) { (void)s; g_quit = 1; }

void plat_init(void)
{
    signal(SIGINT, on_sig);
    signal(SIGTERM, on_sig);
    setvbuf(stdout, 0, _IONBF, 0);
    setvbuf(stdin, 0, _IONBF, 0); /* keys typed together: poll() must see each one */
}

const char *exe_dir(void) { return "."; }

u64 now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (u64)t.tv_sec * 1000 + (u64)t.tv_nsec / 1000000;
}

void sleep_ms(int ms) { usleep((useconds_t)ms * 1000); }

sock_t udp_open_local(uint16_t port)
{
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) return BADSOCK;
    struct sockaddr_in a = { 0 };
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(s, (void *)&a, sizeof a) != 0) { close(s); return BADSOCK; }
    fcntl(s, F_SETFL, O_NONBLOCK);
    return s;
}

bool udp_connect_local(sock_t s, uint16_t port)
{
    struct sockaddr_in a = { 0 };
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    return connect((int)s, (void *)&a, sizeof a) == 0;
}

static int recv_result(ssize_t r)
{
    if (r >= 0) return (int)r;
    if (errno == ECONNREFUSED) return 0;
    return -1;
}

int udp_send(sock_t s, const void *p, int n) { return (int)send((int)s, p, (size_t)n, 0); }
int udp_recv(sock_t s, void *p, int n) { return recv_result(recv((int)s, p, (size_t)n, 0)); }

int udp_recvfrom(sock_t s, void *p, int n, uint32_t *ip, uint16_t *port)
{
    struct sockaddr_in a;
    socklen_t len = sizeof a;
    int r = recv_result(recvfrom((int)s, p, (size_t)n, 0, (void *)&a, &len));
    if (r > 0) { *ip = a.sin_addr.s_addr; *port = ntohs(a.sin_port); }
    return r;
}

int udp_sendto(sock_t s, const void *p, int n, uint32_t ip, uint16_t port)
{
    struct sockaddr_in a = { 0 };
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    a.sin_addr.s_addr = ip;
    return (int)sendto((int)s, p, (size_t)n, 0, (void *)&a, sizeof a);
}

uint16_t udp_local_port(sock_t s)
{
    struct sockaddr_in a;
    socklen_t len = sizeof a;
    if (getsockname((int)s, (void *)&a, &len) != 0) return 0;
    return ntohs(a.sin_port);
}

void udp_close(sock_t s) { if (s != BADSOCK) close((int)s); }

void wait_socks(const sock_t *s, int n, int ms)
{
    struct pollfd p[64];
    int k = 0;
    for (int i = 0; i < n && k < 64; i++)
        if (s[i] != BADSOCK) { p[k].fd = (int)s[i]; p[k].events = POLLIN; k++; }
    if (k == 0) { sleep_ms(ms); return; }
    poll(p, (nfds_t)k, ms);
}

/* Tests drive keys through stdin: one key per line. */
int key_poll(void)
{
    static bool eof;
    if (eof) return -1;
    struct pollfd p = { 0, POLLIN, 0 };
    if (poll(&p, 1, 0) <= 0) return -1;
    int c = getchar();
    if (c == EOF) { eof = true; return -1; }
    int d;
    while ((d = getchar()) != EOF && d != '\n') {}
    return c;
}

bool read_line(char *buf, int n)
{
    if (!fgets(buf, n, stdin)) return false;
    size_t l = strlen(buf);
    while (l && (buf[l - 1] == '\n' || buf[l - 1] == '\r')) buf[--l] = 0;
    return true;
}

void clipboard_set(const char *t) { (void)t; }
/* tests: the mock "game" counts as running when MOCK_COF=1 */
bool process_running(const char *n) { (void)n; const char *e = getenv("MOCK_COF"); return e && *e == '1'; }
void open_url(const char *u) { printf("[mock] would open %s\n", u); }
bool file_exists(const char *path) { return access(path, F_OK) == 0; }
bool run_program(const char *exe, const char *args, const char *dir)
{
    printf("[mock] would run %s %s (in %s)\n", exe, args, dir);
    return true;
}
bool steam_install_path(char *out, int n)
{
    const char *e = getenv("MOCK_STEAM_PATH");
    if (!e) return false;
    snprintf(out, (size_t)n, "%s", e);
    return true;
}
void u64_to_str(u64 v, char out[24]) { snprintf(out, 24, "%llu", (unsigned long long)v); }
u64 str_to_u64(const char *s) { return strtoull(s, 0, 10); }

bool file_write_text(const char *path, const char *text)
{
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    fputs(text, f);
    fclose(f);
    return true;
}
