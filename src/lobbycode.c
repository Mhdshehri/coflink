/* lobbycode.c - short lobby codes like "1PCW-4HB" instead of 18-digit ids.
 *
 * A Steam lobby id is a 64-bit Steam ID whose top 32 bits are the same for
 * every lobby (universe 1, chat type, lobby flag = 0x01860000); only the low
 * 32 bits differ. Those 32 bits are written as 7 Crockford base-32 characters
 * (no I, L, O or U, so nothing is easy to misread). Typing the full number
 * still works. */
#include "mini.h"

#define LOBBY_HIGH 0x0186000000000000ULL
static const char ALPHA[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";

void lobby_code(u64 lobby, char out[16])
{
    uint32_t v = (uint32_t)(lobby & 0xFFFFFFFFu);
    char c[7];
    for (int i = 6; i >= 0; i--) { c[i] = ALPHA[v & 31]; v >>= 5; }
    int o = 0;
    for (int i = 0; i < 7; i++) {
        if (i == 4) out[o++] = '-';
        out[o++] = c[i];
    }
    out[o] = 0;
}

static int val(char ch)
{
    if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 32);
    if (ch == 'O') return 0;               /* easy mix-ups */
    if (ch == 'I' || ch == 'L') return 1;
    for (int i = 0; i < 32; i++)
        if (ALPHA[i] == ch) return i;
    return -1;
}

/* Text the player pasted or typed: a 7-character code (dashes and spaces are
   ignored) or the full lobby number. Returns 0 if it's neither. */
u64 lobby_parse(const char *s)
{
    char t[40];
    int n = 0;
    bool digits = true;
    for (; *s && n < 39; s++) {
        if (*s == '-' || *s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') continue;
        if (*s < '0' || *s > '9') digits = false;
        t[n++] = *s;
    }
    t[n] = 0;
    if (n == 7) {
        u64 v = 0;
        for (int i = 0; i < 7; i++) {
            int d = val(t[i]);
            if (d < 0) return 0;
            v = (v << 5) | (u64)d;
        }
        if (v > 0xFFFFFFFFull) return 0;
        return LOBBY_HIGH | v;
    }
    if (digits && n >= 15 && n <= 20) {
        u64 v = 0;
        for (int i = 0; i < n; i++) v = v * 10 + (u64)(t[i] - '0');
        return v;
    }
    return 0;
}
