<div align="center">

# CoFLink

**Play Cry of Fear co-op with a Steam friend.**
No VPN. No port forwarding. No IP addresses.

[**Download the latest release**](../../releases/latest) · Windows · Free · MIT license

</div>

---

Cry of Fear's co-op only works on a LAN, so people usually reach for Hamachi or Radmin. CoFLink does the job without a VPN. The host runs the game's normal co-op server, and CoFLink carries its traffic through Steam's own relay network to a friend on your Steam friends list.

You don't install anything, and you don't need admin rights or router settings. Unzip it and open it.

## Quick start

Both players: download `CoFLink-<version>.zip` from [Releases](../../releases), unzip it anywhere, and open **CoFLink.exe**. Steam must be running. You both need the same version.

### 🎮 Host

1. Click **Host a game**.
2. Click **Start Cry of Fear**, then create a co-op game and choose **LAN**.
3. Keep CoFLink open while you play.

When a friend links, they show up in your list. CoFLink also flashes in the taskbar with a short sound. To remove someone, pick them and click **Kick**.

### 🤝 Friend

1. Click **Join a friend**, pick the host and click **Join**.
   The host isn't listed yet? Click **Refresh**, or paste their short lobby code (like `1MCE-2B2`) and click **Join this code**.
2. Wait until the log says the host's co-op game is up. CoFLink flashes and plays a sound when it is.
3. Click **Start Cry of Fear**. In the main menu press `~`, then `Ctrl+V` and `Enter`.
   The command `connect 127.0.0.1:27115` is already copied for you.

That's it. CoFLink only *links* you; you join from the game yourself. It doesn't use Steam invites or Steam's "Join Game" button.

> The full player guide, with troubleshooting and low-FPS tips, is in the zip as `README.txt` ([read it here](package/README.txt)).

## How it works

```
 Friend's PC                                         Host's PC
┌───────────────┐   UDP    ┌──────────┐           ┌──────────┐   UDP    ┌──────────────────┐
│ Cry of Fear   │ ───────▶ │ CoFLink  │ ═ Steam ═▶│ CoFLink  │ ───────▶ │ Cry of Fear      │
│ (game client) │ ◀─────── │ :27115   │ ◀═ relay ═│          │ ◀─────── │ co-op server     │
└───────────────┘ 127.0.0.1└──────────┘           └──────────┘ 127.0.0.1│ :27015           │
                                                                         └──────────────────┘
```

- **Lobbies and the relay** come from Valve's free *Spacewar* test app (AppID 480), which every Steam account can use. While CoFLink is open, Steam shows you as "playing Spacewar". Nobody has to install or buy anything extra.
- **Finding the host:** CoFLink looks at your Steam friends to see who is hosting. Asking a host's CoFLink directly still works after the host has started Cry of Fear.
- **The game is never modified.** Nothing is injected, and no game files change. Cry of Fear's own netcode keeps everyone in sync. CoFLink passes its packets along unchanged and in order, adding about 1–2 ms.

## Safety and privacy

| | |
|---|---|
| 🔒 **Your IP stays private** | Traffic always goes through Valve's relay. CoFLink turns off Steam's direct connections, so neither player sees the other's IP, whatever Steam's "IP sharing" setting is. |
| 👥 **Friends only** | The host only talks to people on its Steam friends list, and only lets in those who are also in the lobby. Everyone else is blocked and logged. |
| 🎯 **You choose who to link to** | The friend links only to the host they picked, and only accepts data from that host. CoFLink never joins or connects anything on its own. |
| 🚪 **Kick** | The host can kick a friend. A kicked friend can't link again, even with the lobby code, until the host hosts a new game. |
| 🧱 **Nothing opened** | No ports are opened on your PC or router. CoFLink only listens on `127.0.0.1` (your own PC). |
| 📦 **Packets checked** | Oversized packets and floods are dropped. |
| ✅ **Pinned Steam DLL** | `steam_api.dll` is Valve's signed file from [Steamworks.NET 2025.164.1](https://github.com/rlabrecque/Steamworks.NET/releases). CoFLink checks its SHA-256 (`fdafb7c6…eb65da`) and refuses to load anything else. |
| 🧩 **No third-party code** | `CoFLink.exe` uses only core Windows DLLs (kernel32, advapi32, user32, gdi32, comctl32, shell32, ws2_32, msvcrt). A few more (dwmapi, uxtheme, winmm) are loaded from System32 only. |
| 🛡️ **No admin rights** | CoFLink runs as a normal program. Run it normally, not as administrator. |
| 🧹 **Leaves no trace** | It writes only `steam_appid.txt` and `coflink_theme.txt`, in its own folder. To remove it, delete the folder. |

Windows SmartScreen may warn you because CoFLink is new and not code-signed. Check the files first if you like: open a command prompt in the folder and compare `certutil -hashfile CoFLink.exe SHA256` with `SHA256SUMS.txt`.

As with any game server, only play with people you trust.

## Features

- One small program for both players, with a dark theme by default and a light theme.
- **How to** shows your steps as you go (`[x]` done, `[>]` do this now). **Log** shows everything that happened, with a red dot when something needs a look, and a **Copy log** button.
- Finds friends who are hosting automatically, with a **Refresh** button and short lobby codes.
- **Start Cry of Fear** runs the game directly, skipping its launcher.
- Alerts when a friend links, or when the host's game is up.
- **Kick** for the host.
- Tells you when your friend has a newer or older CoFLink.
- Up to 4 players.

## Building from source

You need `clang` and `lld`. No Windows SDK is needed: the few Windows functions CoFLink uses are declared by hand.

```sh
sh build_windows.sh        # -> build/win/CoFLink.exe (32-bit, windowed)
```

The build is **reproducible**: the same source always gives the same SHA-256.

## Tests

The tests run on Linux with a mock Steam and a fake Cry of Fear server, under AddressSanitizer and UBSan:

```sh
mkdir -p build
SRC="src/coflink.c src/lobbycode.c src/ui_console.c src/plat_posix.c test/steam_mock.c"
gcc -std=c11 -fsanitize=address,undefined -o build/coflink_mock $SRC
gcc -std=c11 -fsanitize=address,undefined -DVERSION='"0.10.1"' -o build/coflink_mock_new $SRC
python3 test/run_tests.py  # prints ALL OK
```

The tests cover:
- **Play:** friends playing through the relay; packets arriving byte-exact and in order; level-load bursts; latency; 10% packet loss.
- **Safety:** strangers blocked; direct connections turned off; foreign lobbies refused; kicking, including a kicked friend trying again with the code.
- **Finding the host:** every discovery path, including a host who has already started Cry of Fear; lobby codes; Refresh.
- **Everyday use:** Steam invites never joining anything; CoFLink never connecting the game itself; the server-up check; quitting, reconnecting and switching modes; alerts; version notices; starting `cof.exe` directly.

## Project layout

| File | What it does |
|---|---|
| `src/coflink.c` | The engine: lobbies, discovery, relay, safety checks |
| `src/ui_win.c` | The Windows window |
| `src/ui_console.c` | A text front end that the tests drive |
| `src/steam_win.c` | Loads the pinned `steam_api.dll` |
| `src/plat_win.c` | Windows sockets, clipboard and process helpers |
| `src/lobbycode.c` | Short lobby codes like `1MCE-2B2` |
| `src/sha256.c` | Checks `steam_api.dll` before it is loaded |
| `test/` | Mock Steam and the end-to-end tests |

## Credits

The relay pattern comes from the [Co-op Anything](https://github.com/cranebeaker/coop-anything) playbook (HXCoop / Steam Spacewar networking). Cry of Fear is made by Team Psykskallar. CoFLink is not affiliated with Team Psykskallar or with Valve.

CoFLink's own code is released under the [MIT license](LICENSE). `steam_api.dll`, which ships in the release zip, is Valve Corporation's Steamworks redistributable (from Steamworks.NET 2025.164.1). It is not covered by the MIT license; it is distributed under the Steamworks SDK terms.
