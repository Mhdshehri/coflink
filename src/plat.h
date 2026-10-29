/* plat.h - small OS layer: UDP on 127.0.0.1, time, console, clipboard. */
#pragma once
#include "mini.h"

typedef intptr_t sock_t;
#define BADSOCK ((sock_t)-1)

extern volatile int g_quit;

void plat_init(void);            /* sockets, timer resolution, Ctrl+C, cwd = exe folder */
u64 now_ms(void);
void sleep_ms(int ms);

sock_t udp_open_local(uint16_t port);          /* bound to 127.0.0.1:port (0 = any free port) */
bool udp_connect_local(sock_t s, uint16_t port);/* default destination 127.0.0.1:port */
int udp_send(sock_t s, const void *p, int n);   /* connected socket */
int udp_recv(sock_t s, void *p, int n);         /* >0 bytes, 0 = try again, <0 = nothing more */
int udp_recvfrom(sock_t s, void *p, int n, uint32_t *ip, uint16_t *port);
int udp_sendto(sock_t s, const void *p, int n, uint32_t ip, uint16_t port);
uint16_t udp_local_port(sock_t s);
void udp_close(sock_t s);
void wait_socks(const sock_t *s, int n, int ms); /* until one is readable or ms pass */

/* console front end only (Linux test build) */
int key_poll(void);              /* key pressed, or -1. Never blocks. */
bool read_line(char *buf, int n);/* blocking line read */
void clipboard_set(const char *text);
bool process_running(const char *exe_name); /* e.g. "cof.exe", case-insensitive */
void open_url(const char *url);
bool file_exists(const char *path);
bool run_program(const char *exe, const char *args, const char *dir); /* start it, don't wait */
bool steam_install_path(char *out, int n); /* Steam's own folder (from the registry) */
#ifdef _WIN32
#define PATHSEP "\\"
#else
#define PATHSEP "/"
#endif

void u64_to_str(u64 v, char out[24]);
u64 str_to_u64(const char *s);
bool file_write_text(const char *path, const char *text);
const char *exe_dir(void);
