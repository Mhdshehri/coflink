CoFLink 1.1 - Cry of Fear co-op with a Steam friend
=====================================================
No VPN (no Hamachi/Radmin), no port forwarding, no IP addresses.
You host on your own PC; your friend joins from Steam.

Both of you need: Windows, Steam running and logged in, Cry of Fear
installed from Steam, and this whole folder (unzip it anywhere, e.g. Desktop).

Before the first game, both of you: in Steam, right-click Cry of Fear ->
Properties -> Installed Files -> "Verify integrity of game files".
Same game files on both PCs = no "different version" errors or desync.


HOST (the person whose game everyone joins)
-------------------------------------------
1. Double-click Host-CoF.bat. Wait for "Lobby is ready".
2. Start Cry of Fear and start a Co-op game from the main menu (host/create).
   Open the console with ~ and type:   sv_lan 1
   (Through CoFLink your friends arrive as LAN players. sv_lan 1 also stops
   strangers on the internet from joining your server directly.)
3. Your friend runs Join-CoF.bat (below). It finds you by itself.
   If it doesn't within ~10 seconds: send them the lobby id shown in your
   window (it's on your clipboard) - they press L and paste it.
   Or press I in your window to send them a Steam invite.
4. Keep the CoFLink window open while you play. Press Q to stop.

The window tells you when your friend connects and when your server
answers them ("they are loading in").


FRIEND (joining)
----------------
1. Double-click Join-CoF.bat.
2. It finds the host by itself and asks "Join their game? (Y/N)" - type Y.
   If it doesn't find them: ask the host for the lobby id, press L in the
   CoFLink window, paste it (right-click or Ctrl+V) and press Enter.
   (If the host sent you a Steam invite instead, run Join-CoF.bat FIRST,
   then accept the invite.)
3. CoFLink starts Cry of Fear. If Steam asks whether to allow the launch
   options, click OK. If you end up in the main menu instead of the host's
   game: press ~ for the console, paste with Ctrl+V and press Enter.
   The command
       connect 127.0.0.1:27115
   is already on your clipboard.
4. Keep the CoFLink window open while you play.


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
  friends list. Anyone else is blocked and the window says so.
- The friend only connects to the host they said yes to, and only accepts
  data from that host.
- Packets bigger than any Cry of Fear packet, or floods, are dropped.
- steam_api.dll is Valve's official file (digitally signed by Valve Corp.).
  CoFLink checks its SHA-256 before loading it and refuses any other file.
- Only play with people you trust, as with any game server.

Windows may say "Windows protected your PC", because CoFLink is new and
not code-signed. Click "More info" -> "Run anyway". You can first check the
files are exactly the ones that were built: open Command Prompt in this
folder and run
    certutil -hashfile CoFLink.exe SHA256
    certutil -hashfile steam_api.dll SHA256
and compare with SHA256SUMS.txt. Both of you should get the same numbers.
The full source code is in the "source" folder.

While CoFLink runs, Steam shows you as "playing Spacewar". That is Valve's
free test app, which CoFLink uses for lobbies and the relay. Nobody needs
to own or install anything extra.


STAYING IN SYNC
---------------
Cry of Fear's own co-op code keeps everyone in sync; the host's game is the
boss and the others follow it. CoFLink only carries its packets, unchanged
and in order, adding about 1-2 ms on top of the internet delay.
For the smoothest game (see also LOW FPS below):
- the player with the best PC and connection should host;
- same game files on both PCs (verify integrity, see top);
- don't run other mods unless everyone has the same ones.


LOW FPS / LAG
-------------
Low FPS is the game, not CoFLink (CoFLink only adds ~1-2 ms of delay).
- The phone/flashlight light is the most common FPS killer, mainly in big
  outdoor areas: turn it off when you don't need it.
- Turn off anything your graphics driver forces (anti-aliasing, etc.) and
  lower the in-game Video -> Advanced options.
- Laptop: Windows Settings -> System -> Display -> Graphics -> add cof.exe
  (in the Cry of Fear folder) -> High performance, so it uses the real GPU.
- Turn V-Sync off for cof.exe in the NVIDIA/AMD control panel.
- Keep fps_max at 100. Half-Life engine games glitch above 100 FPS.
- The host's PC runs the game for everyone, so the stronger PC should host.
- Rubber-banding (people jumping around) rather than low FPS: both of you
  can type in the console:   rate 25000; cl_updaterate 60; cl_cmdrate 60


TROUBLESHOOTING
---------------
"Steam is not running" / "offline mode":  start Steam, go online, retry.
"steam_api.dll is not the expected file": use the steam_api.dll from this zip.
Friend: "No friend is hosting yet":  the host must run Host-CoF.bat first,
    and you two must be Steam friends.
Host: "nothing answers on 127.0.0.1:27015":  start the co-op game in Cry of
    Fear. If your server uses another port, run in Command Prompt:
    CoFLink.exe host --port <port>
Friend connects but is kicked with a Steam validation error:  the host
    types sv_lan 1 in the console (see HOST step 2) and the friend
    reconnects. If it still fails, try sv_lan 0 instead and tell me.
Game connects but stays on "Connecting...": both press S in CoFLink to see
    the status; check both have verified game files.

Options:
  CoFLink.exe host [--port 27015] [--players 4]
  CoFLink.exe join [lobby id] [--port 27115] [--no-launch]

CoFLink is MIT licensed. Built with the Co-op Anything playbook
(github.com/cranebeaker/coop-anything).
