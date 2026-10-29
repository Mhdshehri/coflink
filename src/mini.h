/* mini.h - the few C library pieces CoFLink uses.
 * Windows: declared by hand and imported from msvcrt.dll (no SDK headers needed).
 * Linux (mock test build): the normal libc headers. */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdarg.h>

typedef uint64_t u64;

#ifdef _WIN32
typedef struct FILE FILE;
int _vsnprintf(char *buf, size_t n, const char *fmt, va_list ap);
size_t strlen(const char *s);
int strcmp(const char *a, const char *b);
char *strchr(const char *s, int c);
char *strstr(const char *h, const char *n);
void *memcpy(void *d, const void *s, size_t n);
void *memset(void *d, int c, size_t n);
int memcmp(const void *a, const void *b, size_t n);
void *malloc(size_t n);
void *calloc(size_t n, size_t m);
void free(void *p);
FILE *fopen(const char *path, const char *mode);
size_t fread(void *p, size_t s, size_t n, FILE *f);
size_t fwrite(const void *p, size_t s, size_t n, FILE *f);
int fclose(FILE *f);
void exit(int code);
#else
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#endif

/* snprintf that always terminates */
static inline void sfmt(char *buf, size_t n, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
#ifdef _WIN32
    _vsnprintf(buf, n, fmt, ap);
#else
    vsnprintf(buf, n, fmt, ap);
#endif
    va_end(ap);
    buf[n - 1] = 0;
}
