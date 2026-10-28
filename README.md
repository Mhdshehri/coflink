# CoFLink

Play **Cry of Fear** co-op with a Steam friend: no VPN (no Hamachi or Radmin), no port forwarding, no IP addresses.

The host runs the game's own co-op server on their PC. CoFLink carries the game traffic through Steam's relay network, so your friend joins from Steam.

**Download:** grab `CoFLink-0.7.zip` from [Releases](../../releases). Both players unzip it and open `CoFLink.exe`. (Beta: the connection has been played in a real two-player session; the window is new.)

## How to play

CoFLink is one program with a simple window for both players.

**Host**
1. Click **Host a game**, then **Start Cry of Fear** and create a co-op game (choose LAN).
2. Keep CoFLink open while you play. Friends who are linked show up in the list.

**Friend** (CoFLink only links you; you join the game yourself)
1. Click **Join a friend**, pick the host under *Friends hosting* and click **Join**
   (or paste the host's short lobby code, like `1MCE-2B2`, and click **Join this code**). Steam invites are not used.
2. When the log says the host's co-op game is up: click **Start Cry of Fear**, press `~` in the
   game's menu, then `Ctrl+V` and Enter (`connect 127.0.0.1:27115` is already copied).

See [package/README.txt](package/README.txt) for the full guide, troubleshooting and low-FPS tips.

## How it works

```
Friend's Cry of Fear --UDP--> CoFLink (127.0.0.1:27115)
        --Steam relay (ISteamNetworkingMessages)-->
Host's CoFLink --UDP--> Host's Cry of Fear server (127.0.0.1:27015)
```

- Lobbies and the relay come from Valve's free "Spacewar" test app (AppID 480), which every Steam user can run. While CoFLink is open, Steam shows you as "playing Spacewar".
- The game is never modified. Nothing is injected, and no game files are changed.
- The game's own netcode keeps players in sync. CoFLink forwards its packets unchanged and in order, adding about 1–2 ms.

## Safety

- Nothing is installed and no ports are opened. The only file CoFLink writes is `steam_appid.txt`, in its own folder.
- Neither player sees the other's IP address.
- The host only lets in players who are in the lobby **and** on the host's Steam friends list. Everyone else is blocked.
- The joiner asks before joining, and only accepts traffic from the host it chose.
- Oversized packets and floods are dropped.
- `steam_api.dll` is Valve's signed redistributable from [Steamworks.NET 2025.164.1](https://github.com/rlabrecque/Steamworks.NET/releases).
  CoFLink checks its SHA-256 (`fdafb7c65ec7d4182b42bf92cc8671c68e4dc547cc6e613899d2618a47eb65da`) and refuses to load any other file.
- No third-party code: CoFLink.exe imports only core Windows DLLs (kernel32, advapi32, user32, gdi32, comctl32, shell32, ws2_32, msvcrt) plus the pinned `steam_api.dll`.

## Building

You need clang and lld; no Windows SDK is required.

```sh
sh build_windows.sh      # -> build/win/CoFLink.exe (32-bit, windowed)
```

The build is reproducible: building the same source gives the same SHA-256.

## Tests

The tests run on Linux, with a mock Steam and a fake game server:

```sh
mkdir -p build
gcc -std=c11 -fsanitize=address,undefined -o build/coflink_mock src/coflink.c src/lobbycode.c src/ui_console.c src/plat_posix.c test/steam_mock.c
python3 test/run_tests.py   # prints ALL OK
```

The engine (`src/coflink.c`) is shared by the window (`src/ui_win.c`) and a text front end (`src/ui_console.c`) that the tests drive.

The tests cover: two friends playing through the relay; packets arriving exact and in order; level-load bursts; latency; strangers being blocked; quit and reconnect; foreign lobbies being refused; join confirmation; finding the host through rich presence and by asking a friend's CoFLink directly (host opened first); joining by short lobby code; Steam invites never joining anything; CoFLink never connecting the game itself; checking the host's co-op server is up (also with challenge-first servers); server-info replies passing unchanged; the game and other local programs sharing the relay; version checks; starting cof.exe directly; stopping, then switching between hosting and joining; and 10% packet loss.

## Credits

The relay pattern comes from the [Co-op Anything](https://github.com/cranebeaker/coop-anything) playbook (HXCoop / Steam Spacewar networking). Cry of Fear is by Team Psykskallar. This project is not affiliated with them or with Valve.

MIT license.
