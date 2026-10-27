# CoFLink

Play **Cry of Fear** co-op with a Steam friend: no VPN (no Hamachi or Radmin), no port forwarding, no IP addresses.

The host runs the game's own co-op server on their PC. CoFLink carries the game traffic through Steam's relay network, so your friend joins from Steam.

**Download:** grab `CoFLink-1.2.zip` from [Releases](../../releases). Both players unzip it and open `CoFLink.exe`.

## How to play

CoFLink is one program with a simple window for both players.

**Host**
1. Click **Host a game**.
2. Start Cry of Fear and create a co-op game. Open the console (`~`) and type `sv_lan 1`.
3. Keep CoFLink open while you play. To bring a friend in directly, pick them under *Friends online* and click **Invite**.

**Friend**
1. Click **Join a friend**, pick the host under *Friends hosting*, and click **Join**.
   If they don't appear, paste the host's lobby id (the host clicks **Copy id**) and click **Join this id**.
2. Cry of Fear starts. If you land in the main menu, press `~`, then `Ctrl+V` and Enter
   (`connect 127.0.0.1:27115` is already on your clipboard).

See [package/README.txt](package/README.txt) for the full guide, troubleshooting and low-FPS tips.

## How it works

```
Friend's Cry of Fear --UDP--> CoFLink (127.0.0.1:27115)
        --Steam relay (ISteamNetworkingMessages)-->
Host's CoFLink --UDP--> Host's Cry of Fear server (127.0.0.1:27015)
```

- Lobbies, invites and the relay come from Valve's free "Spacewar" test app (AppID 480), which every Steam user can run. While CoFLink is open, Steam shows you as "playing Spacewar".
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
- No third-party code: CoFLink.exe imports only core Windows DLLs (kernel32, user32, gdi32, comctl32, shell32, ws2_32, msvcrt) plus the pinned `steam_api.dll`.

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
gcc -std=c11 -fsanitize=address,undefined -o build/coflink_mock src/coflink.c src/ui_console.c src/plat_posix.c test/steam_mock.c
python3 test/run_tests.py   # prints ALL OK
```

The engine (`src/coflink.c`) is shared by the window (`src/ui_win.c`) and a text front end (`src/ui_console.c`) that the tests drive.

The tests cover: two friends playing through the relay; packets arriving exact and in order; level-load bursts; latency; strangers being blocked; quit and reconnect; invites; foreign lobbies being refused; join confirmation; discovery through rich presence; joining by lobby id; stopping, then switching between hosting and joining; inviting from the friends list; and 10% packet loss.

## Credits

The relay pattern comes from the [Co-op Anything](https://github.com/cranebeaker/coop-anything) playbook (HXCoop / Steam Spacewar networking). Cry of Fear is by Team Psykskallar. This project is not affiliated with them or with Valve.

MIT license.
