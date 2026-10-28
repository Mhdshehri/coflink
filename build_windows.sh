#!/bin/sh
# Build CoFLink.exe (32-bit Windows, windowed app) with the system's clang + lld only.
# No Windows SDK and no downloaded toolchain: the few Windows functions used are
# listed below and turned into import libraries with llvm-dlltool.
# Every DLL imported is a core Windows system DLL.
set -eu
cd "$(dirname "$0")"
OUT=build/win
mkdir -p "$OUT/lib"

mkdef() { # name, then exports
    name=$1; shift
    { echo "LIBRARY $name.dll"; echo "EXPORTS"; for e in "$@"; do echo "$e"; done; } > "$OUT/lib/$name.def"
    llvm-dlltool -m i386 -k -d "$OUT/lib/$name.def" -l "$OUT/lib/lib$name.a"
}
mkdef kernel32 Sleep@4 GetTickCount64@0 GetModuleFileNameA@12 SetCurrentDirectoryA@4 \
    GlobalAlloc@8 GlobalLock@4 GlobalUnlock@4 LoadLibraryExA@12 GetProcAddress@8 \
    CreateThread@24 WaitForSingleObject@8 InitializeCriticalSection@4 EnterCriticalSection@4 \
    LeaveCriticalSection@4 MultiByteToWideChar@24 WideCharToMultiByte@32 GetModuleHandleW@4 \
    CreateToolhelp32Snapshot@8 Process32First@8 Process32Next@8 CloseHandle@4 \
    GetFileAttributesA@4 CreateProcessA@40
mkdef advapi32 RegGetValueA@28
mkdef user32 OpenClipboard@4 EmptyClipboard@0 SetClipboardData@8 CloseClipboard@0 \
    RegisterClassExW@4 CreateWindowExW@48 DefWindowProcW@16 GetMessageW@16 TranslateMessage@4 \
    DispatchMessageW@4 IsDialogMessageW@8 PostQuitMessage@4 SendMessageW@16 SetWindowTextW@8 \
    GetWindowTextW@12 SetTimer@16 MessageBoxW@16 LoadCursorW@8 LoadIconW@8 ShowWindow@8 \
    UpdateWindow@4 EnableWindow@8 DestroyWindow@4 GetDC@4 ReleaseDC@8 AdjustWindowRectEx@16 \
    GetSystemMetrics@4
mkdef gdi32 CreateFontW@56 GetDeviceCaps@8
mkdef comctl32 InitCommonControls@0
mkdef shell32 ShellExecuteA@24
mkdef ws2_32 WSAStartup@8 socket@12 bind@12 connect@12 send@16 recv@16 sendto@24 recvfrom@24 \
    select@20 closesocket@4 ioctlsocket@12 getsockname@12 WSAGetLastError@0 WSAIoctl@36
mkdef msvcrt printf _vsnprintf strlen strcmp strncmp strchr strstr memcpy memset memcmp atoi \
    malloc calloc free fopen fread fwrite fclose fgets fflush exit __iob_func \
    _ui64toa _strtoui64 __getmainargs

# icon, manifest (modern look, sharp text on high-DPI screens) and version info
llvm-rc -no-preprocess -C 65001 -FO "$OUT/coflink.res" res/coflink.rc
llvm-cvtres -machine:x86 -out:"$OUT/coflink_res.obj" "$OUT/coflink.res" >/dev/null

CFLAGS="--target=i686-w64-windows-gnu -std=c11 -O2 -Wall -Wextra -Wno-unused-parameter
  -ffreestanding -fno-stack-protector -mno-stack-arg-probe -fno-asynchronous-unwind-tables
  -fno-ident -fno-builtin -D_WIN32"
SRC="src/crt0_win.c src/ui_win.c src/coflink.c src/lobbycode.c src/plat_win.c src/steam_win.c src/sha256.c"

# shellcheck disable=SC2086
clang $CFLAGS -fuse-ld=lld -nostdlib -nostartfiles \
    -Wl,-e,mainCRTStartup -Wl,--subsystem,windows:6.0 -Wl,--dynamicbase -Wl,--nxcompat \
    -Wl,--no-insert-timestamp -Wl,--build-id=none \
    -o "$OUT/CoFLink.exe" $SRC "$OUT/coflink_res.obj" \
    -L"$OUT/lib" -lkernel32 -ladvapi32 -luser32 -lgdi32 -lcomctl32 -lshell32 -lws2_32 -lmsvcrt
echo "built $OUT/CoFLink.exe"
sha256sum "$OUT/CoFLink.exe"
