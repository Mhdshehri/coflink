/* crt0_win.c - program entry for the Windows build (no C runtime startup objects
 * are linked; arguments come from msvcrt.dll, which every Windows has). */
#include "mini.h"

int __getmainargs(int *argc, char ***argv, char ***env, int glob, void *startinfo);
int main(int argc, char **argv);

void __main(void) {} /* clang calls this from main() on MinGW targets; nothing to do */

void mainCRTStartup(void)
{
    int argc = 0, startinfo = 0;
    char **argv = 0, **env = 0;
    __getmainargs(&argc, &argv, &env, 0, &startinfo);
    exit(main(argc, argv));
}
