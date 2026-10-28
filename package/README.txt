CoFLink 0.5 (beta) - Cry of Fear co-op with a Steam friend
=====================================================
No VPN (no Hamachi/Radmin), no port forwarding, no IP addresses.
One program for both players: open CoFLink.exe and click a button.

Both of you need: Windows, Steam running and logged in, Cry of Fear
installed from Steam, and this whole folder (unzip it anywhere, e.g. Desktop).
Keep CoFLink.exe and steam_api.dll together in the same folder.
You BOTH need the same CoFLink version (0.5).

Before the first game, both of you: in Steam, right-click Cry of Fear ->
Properties -> Installed Files -> "Verify integrity of game files".
Same game files on both PCs = no "different version" errors or desync.


HOST (the person whose game everyone joins)
-------------------------------------------
1. Open CoFLink.exe and click "Host a game".
   The status line says "Hosting - waiting for friends to join".
2. Click "Start Cry of Fear" (starts the game itself, skipping the
   launcher), then start a Co-op game and choose LAN.
   (Choosing LAN is the same as typing  sv_lan 1  in the console.)
3. Your friend clicks "Join a friend" in their CoFLink - you show up in
   their list. If not, pick them under "Friends online" and click Invite,
   or click "Copy id" and send them the lobby id in Steam chat.
4. Keep CoFLink open while you play. Click Stop (or close it) when done.


FRIEND (joining)
----------------
1. Open CoFLink.exe. Then either:
   - let the host invite you: with CoFLink open, the invite is picked up
     by itself (or accept it in Steam), or
   - click "Join a friend", pick the host under "Friends hosting", click Join, or
   - paste the lobby id the host sent you and click "Join this id".
2. Click "Start Cry of Fear" in CoFLink. It starts the game itself
   (cof.exe), skipping the Cry of Fear launcher that Steam's Play button
   opens. Starting it from Steam also works. You don't need to type
   anything: CoFLink checks that the host's co-op game is up, waits for your
   game to finish loading its menu, and connects it to the host by itself.
   The status line tells you what it is waiting for.
3. If the game is still in the menu after about a minute: click
   "Connect game", or press ~ in the game, then Ctrl+V and Enter
   (the command  connect 127.0.0.1:27115  is already copied).
4. Keep CoFLink open while you play.


IS IT SAFE?
-----------
- Nothing is installed, no settings or registry are changed. CoFLink only
  writes steam_appid.txt (containing 480) into its own folder.
  To remove it, delete the folder.
- No ports are opened on your PC or router. CoFLink only talks to programs
  on your own PC (127.0.0.1) and to Steam.
- Traffic goes through Valve's Steam relay, so neither of you sees the
  other's IP address.
- The host only lets in people who are in the lobby AND on the host's Steam
  friends list. Anyone else is blocked and the log says so.
- The friend only joins the host they picked, and only accepts data from
  that host.
- Packets bigger than any Cry of Fear packet, or floods, are dropped.
- steam_api.dll is Valve's official file (digitally signed by Valve Corp.).
  CoFLink checks its SHA-256 before loading it and refuses any other file.
- The game itself is never modified (nothing is injected into it).
- Only play with people you trust, as with any game server.

Windows may say "Windows protected your PC", because CoFLink is new and
not code-signed. Click "More info" -> "Run anyway". You can first check the
files are exactly the ones that were built: open Command Prompt in this
folder and run
    certutil -hashfile CoFLink.exe SHA256
    certutil -hashfile steam_api.dll SHA256
and compare with SHA256SUMS.txt. Both of you should get the same numbers.
Source code: https://github.com/Mhdshehri/coflink

While CoFLink runs, Steam shows you as "playing Spacewar". That is Valve's
free test app, which CoFLink uses for lobbies and the relay. Nobody needs
to own or install anything extra.


STAYING IN SYNC
---------------
Cry of Fear's own co-op code keeps everyone in sync; the host's game is the
boss and the others follow it. CoFLink only carries its packets, unchanged
and in order, adding about 1-2 ms on top of the internet delay.
- the player with the best PC and connection should host;
- same game files on both PCs (verify integrity, see top);
- don't run other mods unless everyone has the same ones.


LOW FPS / LAG
-------------
Low FPS is the game, not CoFLink.
- The phone/flashlight light is the most common FPS killer, mainly in big
  outdoor areas: turn it off when you don't need it.
- Turn off anything your graphics driver forces (anti-aliasing, etc.) and
  lower the in-game Video -> Advanced options.
- Laptop: Windows Settings -> System -> Display -> Graphics -> add cof.exe
  (in the Cry of Fear folder) -> High performance, so it uses the real GPU.
- Turn V-Sync off for cof.exe in the NVIDIA/AMD control panel.
- Keep fps_max at 100. Half-Life engine games glitch above 100 FPS.
- Rubber-banding (people jumping around) rather than low FPS: both of you
  can type in the console:   rate 25000; cl_updaterate 60; cl_cmdrate 60


TROUBLESHOOTING (read the log at the bottom of the window)
---------------
"Steam is not running" / "offline mode":  start Steam, go online, click
    Host/Join again.
"steam_api.dll is not the expected file / missing": use the steam_api.dll
    from this zip, in the same folder as CoFLink.exe.
Friend: the host never shows up:  check you both have CoFLink 0.5 and are
    Steam friends. Or ask for the lobby id ("Copy id" on the host's side)
    and use "Join this id", or have the host Invite you.
"uses a different CoFLink version":  both of you download the same version.
Friend: "Waiting for <host> to start their co-op game":  the host starts a
    co-op game in Cry of Fear and types sv_lan 1.
Host: "nothing answers on 127.0.0.1:27015":  start the co-op game in Cry
    of Fear and type sv_lan 1.
Friend is kicked with a Steam validation error:  host types sv_lan 1 and
    the friend reconnects. If it still fails, try sv_lan 0.

CoFLink is MIT licensed. Built with the Co-op Anything playbook
(github.com/cranebeaker/coop-anything).
