"""End-to-end tests for CoFLink with the mock Steam (Linux). Prints ALL OK or FAIL.

A fake Cry of Fear server listens on 127.0.0.1:27015 and answers every packet
with b"S:" + packet. Fake game clients send to the port each joiner opens.
"""
import os
import re
import shutil
import signal
import socket
import subprocess
import sys
import tempfile
import threading
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BIN = os.path.join(ROOT, "build", "coflink_mock")
BIN_NEW = os.path.join(ROOT, "build", "coflink_mock_new")  # same code, version 0.10.1
FAILS = []


def check(cond, what):
    print(("  ok   " if cond else "  FAIL ") + what)
    if not cond:
        FAILS.append(what)


# a Cry of Fear listen server's info reply, old GoldSrc 'm' format (no app id)
INFO_M = (b"\xff\xff\xff\xffm" + b"127.0.0.1:27015\0" + b"KaZ game\0" + b"cof_intro\0" + b"cryoffear\0"
          + b"Cry of Fear\0" + bytes([1, 4, 47, ord("L"), ord("W"), 0, 0, 0, 0]))


class FakeServer(threading.Thread):
    def __init__(self, port=27015):
        super().__init__(daemon=True)
        self.s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.s.bind(("127.0.0.1", port))
        self.s.settimeout(0.2)
        self.clients = set()
        self.biggest = 0
        self.stop = False
        self.challenge = False

    def run(self):
        while not self.stop:
            try:
                d, a = self.s.recvfrom(70000)
            except socket.timeout:
                continue
            if not d.startswith(b"\xff\xff\xff\xffTSource Engine Query"):
                self.clients.add(a)  # CoFLink's own server check isn't a game client
            self.biggest = max(self.biggest, len(d))
            if d.startswith(b"\xff\xff\xff\xffTSource Engine Query"):  # info query
                if self.challenge:  # newer server: wants a challenge first
                    if len(d) == 29 and d[25:29] == b"\x11\x22\x33\x44":
                        self.s.sendto(INFO_M, a)
                    else:
                        self.s.sendto(b"\xff\xff\xff\xffA\x11\x22\x33\x44", a)
                elif len(d) == 25:  # like Cry of Fear's old server: only the old form, no challenge
                    self.s.sendto(INFO_M, a)
                continue
            if d.startswith(b"BURST"):  # like a level load: many full-size packets at once
                for i in range(int(d[5:])):
                    self.s.sendto(i.to_bytes(4, "big") + bytes((i + k) & 255 for k in range(1396)), a)
                continue
            self.s.sendto(b"S:" + d, a)


class Proc:
    def __init__(self, name, env, args):
        self.name = name
        self.log = ""
        e = dict(os.environ, **env, ASAN_OPTIONS="detect_leaks=0")
        self.p = subprocess.Popen([BIN] + args, env=e, stdin=subprocess.DEVNULL,
                                  stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        self.t = threading.Thread(target=self._read, daemon=True)
        self.t.start()

    def _read(self):
        for line in self.p.stdout:
            self.log += line

    def wait_for(self, pattern, timeout=10):
        end = time.time() + timeout
        while time.time() < end:
            m = re.search(pattern, self.log)
            if m:
                return m
            time.sleep(0.05)
        return None

    def stop(self):
        if self.p.poll() is None:
            self.p.send_signal(signal.SIGINT)
            try:
                self.p.wait(5)
            except subprocess.TimeoutExpired:
                self.p.kill()
        self.t.join(2)
        return self.p.returncode


def game_client(port, tag, n=200, size=60, timeout=1.0):
    """Send n packets to the joiner's local port; return how many answers came back intact."""
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.bind(("127.0.0.1", 0))
    s.settimeout(timeout)
    good = 0
    for i in range(n):
        msg = (f"{tag}-{i}-".encode() + b"x" * size)[:size]
        s.sendto(msg, ("127.0.0.1", port))
        try:
            d, _ = s.recvfrom(70000)
            good += d == b"S:" + msg
        except socket.timeout:
            pass
    s.close()
    return good


def stream_test(port, n=2000):
    """Fire n packets of mixed sizes without waiting, then collect the replies.
    Returns (replies, all_exact, in_order)."""
    import random
    rnd = random.Random(7)
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 8 << 20)
    s.bind(("127.0.0.1", 0))
    s.settimeout(1.0)
    sent = []
    for i in range(n):
        body = i.to_bytes(4, "big") + bytes(rnd.randrange(256) for _ in range(rnd.randrange(16, 1400)))
        sent.append(body)
        s.sendto(body, ("127.0.0.1", port))
        if i % 10 == 9:
            time.sleep(0.005)  # ~2000 packets/s: about 20x real play, under the 3000/s flood guard
    got = []
    try:
        while True:
            d, _ = s.recvfrom(70000)
            got.append(d)
    except socket.timeout:
        pass
    s.close()
    exact = all(g[:2] == b"S:" and g[2:] == sent[int.from_bytes(g[2:6], "big")] for g in got)
    idx = [int.from_bytes(g[2:6], "big") for g in got]
    return len(got), exact, idx == sorted(idx)


def burst_test(port, n=300):
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 8 << 20)
    s.bind(("127.0.0.1", 0))
    s.settimeout(1.0)
    s.sendto(b"BURST%d" % n, ("127.0.0.1", port))
    got = []
    try:
        while True:
            got.append(s.recvfrom(70000)[0])
    except socket.timeout:
        pass
    s.close()
    ok = all(len(g) == 1400 and g[4:] == bytes((int.from_bytes(g[:4], "big") + k) & 255 for k in range(1396)) for g in got)
    idx = [int.from_bytes(g[:4], "big") for g in got]
    return len(got), ok, idx == sorted(idx)


def rtt_ms(port, n=200):
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.bind(("127.0.0.1", 0))
    s.settimeout(1.0)
    t = []
    for i in range(n):
        a = time.perf_counter()
        s.sendto(b"ping%d" % i, ("127.0.0.1", port))
        try:
            s.recvfrom(70000)
            t.append((time.perf_counter() - a) * 1000)
        except socket.timeout:
            pass
    s.close()
    t.sort()
    return t[len(t) // 2] if t else 999


ALPHA = "0123456789ABCDEFGHJKMNPQRSTVWXYZ"


def code_to_id(code):
    v = 0
    for ch in code.replace("-", ""):
        v = v * 32 + ALPHA.index(ch)
    return str(0x0186000000000000 | v)


def lobby_id_from(log):
    return code_to_id(re.search(r"code ([0-9A-Z]{4}-[0-9A-Z]{3})", log).group(1))


def local_port(proc):
    m = proc.wait_for(r"connect 127\.0\.0\.1:(\d+)")
    return int(m.group(1)) if m else None


def main():
    if not os.path.exists(BIN):
        print("build first"); return 1
    d = tempfile.mkdtemp(prefix="coflink-mock-")
    base = {"MOCK_DIR": d}
    srv = FakeServer()
    srv.start()
    procs = []
    try:
        print("[1] host + two friends through the relay")
        host = Proc("host", dict(base, MOCK_ID="1001", MOCK_FRIENDS="1002,1003,1005"), ["host"])
        procs.append(host)
        check(host.wait_for(r"Lobby is ready") is not None, "host created a lobby")
        a = Proc("A", dict(base, MOCK_ID="1002", MOCK_FRIENDS="1001"), ["join", "--yes"])
        procs.append(a)
        check(a.wait_for(r"Connected to user1001") is not None, "friend A found the host and connected")
        pa = local_port(a)
        check(pa == 27115, f"friend A's game port is 27115 (got {pa})")
        b = Proc("B", dict(base, MOCK_ID="1003", MOCK_FRIENDS="1001"), ["join", "--yes"])
        procs.append(b)
        check(b.wait_for(r"Connected to user1001") is not None, "friend B connected")
        pb = local_port(b)
        check(pb == 27116, f"friend B got the next free port 27116 (got {pb})")

        ga = game_client(pa, "A")
        gb = game_client(pb, "B")
        check(ga == 200, f"A: 200/200 packets went to the server and back ({ga})")
        check(gb == 200, f"B: 200/200 packets went to the server and back ({gb})")
        check(len(srv.clients) == 2, f"server saw A and B as two separate clients ({len(srv.clients)})")
        check(all(c[0] == "127.0.0.1" for c in srv.clients), "server only sees 127.0.0.1 (no IPs exposed)")
        check(a.wait_for(r"host's server answered") is not None, "A was told the server answered")
        check(host.wait_for(r"Your server answered user1002") is not None, "host was told A is loading in")
        check(host.log.count("[alert] friend linked") == 2, "the host's window alerted once per friend who linked")
        check(host.wait_for(r"\[how-to\] steps done: 15") is not None and a.wait_for(r"\[how-to\] steps done: 15") is not None,
              "How to: all 4 steps ticked for host and friend once they play")
        seq = [int(x) for x in re.findall(r"steps done: (\d+)", a.log)]
        check(len(seq) >= 2 and seq[-1] == 15 and all(seq[i] & ~seq[i + 1] == 0 for i in range(len(seq) - 1)),
              f"How to: the friend's ticks only ever add up, never jump back ({seq})")
        check("CoFLink (" not in a.log + host.log, "same version: no version notice")
        check("[mock] global config 104 = 0" in host.log and "[mock] global config 104 = 0" in a.log,
              "both sides turn off direct connections (IP never shared, always Valve's relay)")
        big = game_client(pa, "BIG", n=3, size=9000, timeout=0.5)
        check(big == 0 and srv.biggest <= 8192, "oversized packets (9000 bytes) are dropped")

        print("[1b] sync: packets arrive exact, in order, quickly, bursts intact")
        n, exact, ordered = stream_test(pa, 2000)
        check(n == 2000, f"2000 mixed-size packets sent back-to-back: all 2000 came back ({n})")
        check(exact, "every packet came back byte-for-byte identical")
        check(ordered, "packets stayed in order")
        n, ok, ordered = burst_test(pb, 300)
        check(n == 300 and ok and ordered, f"server burst of 300 x 1400-byte packets (level load) reached B intact and in order ({n})")
        direct, relayed = rtt_ms(27015), rtt_ms(pa)
        check(relayed - direct < 5, f"relay adds under 5 ms round trip (direct {direct:.2f} ms, relayed {relayed:.2f} ms)")

        print("[2] a stranger who is not the host's friend")
        st = Proc("stranger", dict(base, MOCK_ID="1009", MOCK_FRIENDS="1001"), ["join", "--yes"])
        procs.append(st)
        ps = local_port(st)
        before = len(srv.clients)
        gs = game_client(ps, "S", n=20, timeout=0.3) if ps else 0
        check(gs == 0, f"stranger got no answers ({gs})")
        check(len(srv.clients) == before, "stranger's packets never reached the server")
        check(host.wait_for(r"Blocked user1009: they are in the lobby but not on your Steam friends list") is not None,
              "host log says the stranger was blocked")
        st.stop()

        print("[3] friend A quits")
        a.stop()
        check(host.wait_for(r"user1002 disconnected \(quit CoFLink\)") is not None, "host dropped A cleanly")
        check(game_client(pb, "B2", n=50) == 50, "B still plays after A left")

        print("[4] Steam invites are not used: an accepted invite joins nothing")
        c = Proc("C", dict(base, MOCK_ID="1005", MOCK_FRIENDS="1001"), ["idle"])
        procs.append(c)
        time.sleep(1)
        lobby = lobby_id_from(host.log)
        with open(os.path.join(d, "invite_1005"), "w") as f:  # a Steam invite arriving (CoFLink never sends any)
            f.write(f"{lobby} 1001\n")
        check(c.wait_for(r"Steam invites aren't used") is not None, "C is told to use Join a friend instead")
        time.sleep(2)
        check("Joining lobby" not in c.log and "Joined" not in c.log, "and CoFLink joined nothing")
        c.stop()

        print("[5] host quits")
        host.stop()
        check(b.wait_for(r"user1001 closed CoFLink|stopped hosting") is not None, "B was told the host left")
        time.sleep(1)
        check(b.p.poll() is None, "B keeps running, waiting for the host to come back")
        for p in procs:
            p.stop()
        check(all("ERROR: AddressSanitizer" not in p.log and "runtime error" not in p.log for p in procs),
              "no memory errors (AddressSanitizer / UBSan)")

        print("[6] a Spacewar lobby that is not CoFLink is refused")
        shutil.rmtree(d); os.makedirs(d)
        with open(os.path.join(d, "lobby_555"), "w") as f:
            f.write("owner 1007\nmember 1007\n")
        with open(os.path.join(d, "playing_1007"), "w") as f:
            f.write("555\n")
        with open(os.path.join(d, "rp_1007"), "w") as f:  # it says it hosts that lobby
            f.write("555")
        j = Proc("J", dict(base, MOCK_ID="1008", MOCK_FRIENDS="1007"), ["join", "--yes"])
        procs.append(j)
        check(j.wait_for(r"not a Cry of Fear co-op game") is not None, "joiner refused the foreign lobby")
        time.sleep(4)
        check(j.log.count("Joining lobby") == 1, "and did not keep retrying it")
        j.stop()

        print("[7] confirmation: without --yes the joiner asks first and a 'no' joins nothing")
        h2 = Proc("H2", dict(base, MOCK_ID="1011", MOCK_FRIENDS="1012"), ["host"])
        procs.append(h2)
        h2.wait_for(r"Lobby is ready")
        e = dict(os.environ, **base, MOCK_ID="1012", MOCK_FRIENDS="1011", ASAN_OPTIONS="detect_leaks=0")
        q = subprocess.Popen([BIN, "join"], env=e, stdin=subprocess.PIPE,
                             stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        try:
            out, _ = q.communicate("n\n", timeout=6)
        except subprocess.TimeoutExpired:
            q.send_signal(signal.SIGINT)
            out, _ = q.communicate(timeout=5)
        check("Join their game? (Y/N)" in out, "joiner asked before joining")
        check("Joining lobby" not in out, "answering no joined nothing")
        h2.stop()
        print("[9] host's lobby hidden from 'game played': found through rich presence")
        h4 = Proc("H4", dict(base, MOCK_ID="1031", MOCK_FRIENDS="1032"), ["host"])
        procs.append(h4)
        check(h4.wait_for(r"Lobby is ready") is not None, "host ready")
        j4 = Proc("J4", dict(base, MOCK_ID="1032", MOCK_FRIENDS="1031", MOCK_HIDE_LOBBY="1"), ["join", "--yes"])
        procs.append(j4)
        check(j4.wait_for(r"Connected to user1031") is not None, "joiner found the host anyway and connected")
        p4 = local_port(j4)
        check(p4 is not None and game_client(p4, "RP", n=50) == 50, "packets flow")
        j4.stop()

        print("[10] manual: press L and paste the short lobby code")
        lob4 = re.search(r"code ([0-9A-Z]{4}-[0-9A-Z]{3})", h4.log).group(1)  # paste the short code
        e = dict(os.environ, **base, MOCK_ID="1033", MOCK_FRIENDS="", ASAN_OPTIONS="detect_leaks=0")
        with open(os.path.join(d, "friends_note"), "w"):
            pass
        q = subprocess.Popen([BIN, "join"], env=e, stdin=subprocess.PIPE,
                             stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        time.sleep(1.0)
        q.stdin.write("L\n" + lob4 + "\n"); q.stdin.flush()
        time.sleep(3.0)
        q.send_signal(signal.SIGINT)
        out, _ = q.communicate(timeout=5)
        check("Paste the host's lobby code" in out, "L asked for the lobby code")
        check("user1031 hosts that game, but is not on your Steam friends list" in out and "Add user1031 as a Steam friend" in out,
              "pasting a code of a host who isn't your friend: told to add them as a friend")
        check("Linked to" not in out and "user1033 connected" not in h4.log, "and nothing is linked")
        e = dict(os.environ, **base, MOCK_ID="1035", MOCK_FRIENDS="1031", ASAN_OPTIONS="detect_leaks=0")
        q = subprocess.Popen([BIN, "idle"], env=e, stdin=subprocess.PIPE,
                             stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        time.sleep(1.0)
        q.stdin.write("L\n" + lob4 + "\n"); q.stdin.flush()
        time.sleep(3.0)
        q.send_signal(signal.SIGINT)
        out, _ = q.communicate(timeout=5)
        check("Joined user1031's Cry of Fear lobby" in out, "a friend pasting the code joins the host's lobby")
        h4.stop()

        print("[11] one program: stop hosting, then switch to joining")
        e = dict(os.environ, **base, MOCK_ID="1041", MOCK_FRIENDS="1042,1043", ASAN_OPTIONS="detect_leaks=0")
        sw = subprocess.Popen([BIN, "host"], env=e, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                              stderr=subprocess.STDOUT, text=True)
        swlog = []
        threading.Thread(target=lambda: [swlog.append(l) for l in sw.stdout], daemon=True).start()
        def swwait(pat, t=8):
            end = time.time() + t
            while time.time() < end:
                if re.search(pat, "".join(swlog)): return True
                time.sleep(0.05)
            return False
        check(swwait(r"Lobby is ready"), "app hosts")
        lobfile = lobby_id_from("".join(swlog))
        sw.stdin.write("X\n"); sw.stdin.flush()
        check(swwait(r"Stopped\."), "Stop works")
        time.sleep(0.3)
        check(not os.path.exists(os.path.join(d, "lobby_" + lobfile)), "stopping closed the lobby")
        h5 = Proc("H5", dict(base, MOCK_ID="1042", MOCK_FRIENDS="1041"), ["host"])
        procs.append(h5)
        h5.wait_for(r"Lobby is ready")
        sw.stdin.write("J\n"); sw.stdin.flush()
        check(swwait(r"Found user1042 hosting"), "same app now joins and finds a host")
        sw.stdin.write("y\n"); sw.stdin.flush()
        check(swwait(r"Connected to user1042"), "and connects to them")
        sw.send_signal(signal.SIGINT)
        try: sw.wait(5)
        except subprocess.TimeoutExpired: sw.kill()
        h5.stop()

        print("[12] after linking, the friend joins from the game; CoFLink never connects the game itself")
        hz = Proc("HZ", dict(base, MOCK_ID="1051", MOCK_FRIENDS="1052,1053"), ["host", "--port", "27999"])  # server not started
        procs.append(hz)
        hz.wait_for(r"Lobby is ready")
        jz = Proc("JZ", dict(base, MOCK_ID="1052", MOCK_FRIENDS="1051", MOCK_COF="1"), ["join", "--yes"])
        procs.append(jz)
        check(jz.wait_for(r"Connected to user1051") is not None, "joiner linked to the host")
        check(jz.wait_for(r"Linked to user\d+\. Connect command copied: connect 127\.0\.0\.1:\d+", 5) is not None,
              "joiner is told it is linked (the steps are in How to)")
        check(jz.wait_for(r"Waiting for user1051 to start their co-op game", 10) is not None,
              "host's co-op game not running: joiner says it is waiting")
        jz.stop(); hz.stop()
        hr = Proc("HR", dict(base, MOCK_ID="1051", MOCK_FRIENDS="1052,1053"), ["host"])
        procs.append(hr)
        hr.wait_for(r"Lobby is ready")
        ja = Proc("JA", dict(base, MOCK_ID="1052", MOCK_FRIENDS="1051", MOCK_COF="1"), ["join", "--yes"])
        procs.append(ja)
        check(ja.wait_for(r"user1051's Cry of Fear co-op game is up", 10) is not None, "joiner checked the host's server is up")
        check(ja.wait_for(r"\[alert\] host's game is up") is not None, "and the window alerts the friend (flash + sound)")
        check(ja.wait_for(r"Ready: in Cry of Fear press ~, then Ctrl\+V and Enter  \(connect 127\.0\.0\.1:27115\)") is not None,
              "then says exactly what to type in the game")
        time.sleep(12)  # longer than the old auto-connect waited, with the game "running"
        check("steam://connect" not in ja.log, "CoFLink never opens steam://connect")
        pa_ = local_port(ja)
        check(pa_ is not None and game_client(pa_, "MANUAL", n=50) == 50, "connecting from the game (by hand) works")
        ja.stop(); hr.stop()

        print("[13] invites are ignored on every screen")
        hi = Proc("HI", dict(base, MOCK_ID="1061", MOCK_FRIENDS="1062"), ["host"])
        procs.append(hi)
        hi.wait_for(r"Lobby is ready")
        lobi = lobby_id_from(hi.log)
        ji2 = Proc("JI2", dict(base, MOCK_ID="1062", MOCK_FRIENDS="1061"), ["host"])
        procs.append(ji2)
        ji2.wait_for(r"Lobby is ready")
        with open(os.path.join(d, "invite_1062"), "w") as f:
            f.write(f"{lobi} 1061\n")
        time.sleep(2)
        check("Joined user1061" not in ji2.log and "Joining lobby" not in ji2.log, "while hosting: nothing happens")
        ji2.stop()
        jj = Proc("JJ", dict(base, MOCK_ID="1062", MOCK_FRIENDS="1061"), ["join"])
        procs.append(jj)
        check(jj.wait_for(r"Found user1061 hosting") is not None, "while looking for a host: still finds the host normally")
        jj.stop(); hi.stop()

        print("[14] the game and Steam's server query share the relay without mixing answers")
        hs = Proc("HS", dict(base, MOCK_ID="1071", MOCK_FRIENDS="1072"), ["host"])
        js = Proc("JS", dict(base, MOCK_ID="1072", MOCK_FRIENDS="1071"), ["join", "--yes"])
        procs += [hs, js]
        ps_ = local_port(js)
        js.wait_for(r"Connected to user1071")
        before = len(srv.clients)
        a_ = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); a_.bind(("127.0.0.1", 0)); a_.settimeout(1)
        b_ = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); b_.bind(("127.0.0.1", 0)); b_.settimeout(1)
        okA = okB = 0
        for i in range(100):
            a_.sendto(b"GAME-%d" % i, ("127.0.0.1", ps_))
            b_.sendto(b"QUERY-%d" % i, ("127.0.0.1", ps_))
            try: okA += a_.recvfrom(9000)[0] == b"S:GAME-%d" % i
            except socket.timeout: pass
            try: okB += b_.recvfrom(9000)[0] == b"S:QUERY-%d" % i
            except socket.timeout: pass
        if okA != 100 or okB != 100:
            print("---- HS log ----\n" + hs.log + "\n---- JS log ----\n" + js.log + f"\nport={ps_}")
        check(okA == 100 and okB == 100, f"each program got only its own answers ({okA}/100, {okB}/100)")
        check(len(srv.clients) - before == 2, "host's server saw them as two separate clients, like on a real PC")
        hs.stop(); js.stop()

        print("[15] versions must match")
        with open(os.path.join(d, "lobby_777"), "w") as f:
            f.write("owner 1081\nmember 1081\ndata coflink 1\ndata coflink_ver 1.1\n")
        with open(os.path.join(d, "playing_1081"), "w") as f:
            f.write("777\n")
        with open(os.path.join(d, "rp_1081"), "w") as f:
            f.write("777")
        jv = Proc("JV", dict(base, MOCK_ID="1082", MOCK_FRIENDS="1081"), ["join", "--yes"])
        procs.append(jv)
        check(jv.wait_for(r"user1081 uses a different CoFLink version \(1\.1\)") is not None,
              "an old CoFLink host is refused with a clear message")
        jv.stop()

        print("[16] host opened CoFLink first, Steam shares nothing: the friend's CoFLink asks directly")
        hq = Proc("HQ", dict(base, MOCK_ID="1091", MOCK_FRIENDS="1092,1093"), ["host"])
        procs.append(hq)
        hq.wait_for(r"Lobby is ready")
        time.sleep(3)  # host is up well before the friend
        jq = Proc("JQ", dict(base, MOCK_ID="1092", MOCK_FRIENDS="1091", MOCK_HIDE_LOBBY="1", MOCK_HIDE_RP="1"),
                  ["join", "--yes"])
        procs.append(jq)
        check(jq.wait_for(r"Found user1091 hosting") is not None, "friend found the host by asking its CoFLink")
        check(jq.wait_for(r"Connected to user1091") is not None, "and connected")
        jq.stop()
        jx = Proc("JX", dict(base, MOCK_ID="1094", MOCK_FRIENDS="1091", MOCK_HIDE_LOBBY="1", MOCK_HIDE_RP="1"),
                  ["join", "--yes"])  # 1094 is not on the host's friends list
        procs.append(jx)
        time.sleep(8)
        check("Found user1091" not in jx.log, "someone who isn't the host's friend gets no answer")
        jx.stop(); hq.stop()

        print("[20] a server that asks for a challenge first is still seen as up")
        srv.challenge = True
        hc = Proc("HC", dict(base, MOCK_ID="1131", MOCK_FRIENDS="1132"), ["host"])
        jc = Proc("JC", dict(base, MOCK_ID="1132", MOCK_FRIENDS="1131"), ["join", "--yes"])
        procs += [hc, jc]
        check(jc.wait_for(r"user1131's Cry of Fear co-op game is up") is not None, "challenge answered, server seen as up")
        srv.challenge = False
        hc.stop(); jc.stop()

        print("[18] an invite that just arrives (not accepted) joins nothing either")
        hv = Proc("HV", dict(base, MOCK_ID="1111", MOCK_FRIENDS="1112"), ["host"])
        procs.append(hv)
        hv.wait_for(r"Lobby is ready")
        lobv = lobby_id_from(hv.log)
        jv1 = Proc("JV1", dict(base, MOCK_ID="1112", MOCK_FRIENDS="1111", MOCK_INVITE_NOT_ACCEPTED="1"), ["idle"])
        procs.append(jv1)
        time.sleep(1)
        with open(os.path.join(d, "invite_1112"), "w") as f:
            f.write(f"{lobv} 1111\n")
        time.sleep(3)
        check("Joined" not in jv1.log and "Joining lobby" not in jv1.log, "no automatic join")
        jv1.stop(); hv.stop()

        print("[19] Start Cry of Fear runs the game itself (cof.exe), not its launcher")
        g1 = os.path.join(d, "steamlib1", "Cry of Fear"); os.makedirs(g1)
        open(os.path.join(g1, "cof.exe"), "w").close()
        open(os.path.join(g1, "CoFLaunchApp.exe"), "w").close()
        def launch_log(env):
            e = dict(os.environ, **base, MOCK_ID="1121", MOCK_FRIENDS="", ASAN_OPTIONS="detect_leaks=0", **env)
            q = subprocess.Popen([BIN, "idle"], env=e, stdin=subprocess.PIPE,
                                 stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
            time.sleep(1.0)
            q.stdin.write("G\n"); q.stdin.flush()
            time.sleep(1.5)
            q.send_signal(signal.SIGINT)
            out, _ = q.communicate(timeout=5)
            return out
        out = launch_log({"MOCK_COF_DIR": g1})
        want = f"would run {g1}/cof.exe -basedir cryoffear -game cryoffear -hw -heapsize 1536000 (in {g1})"
        check(want in out, "folder from Steam: runs cof.exe with the game options, in the game folder")
        check("CoFLaunchApp" not in out and "steam://run" not in out, "the launcher is not used")
        # Steam doesn't say: find it in Steam's library list (a second library, like D:\\SteamLibrary)
        steam = os.path.join(d, "Steam"); lib2 = os.path.join(d, "Lib Two")
        os.makedirs(os.path.join(steam, "steamapps")); os.makedirs(os.path.join(lib2, "steamapps", "common", "Cry of Fear"))
        open(os.path.join(lib2, "steamapps", "common", "Cry of Fear", "cof.exe"), "w").close()
        with open(os.path.join(steam, "steamapps", "libraryfolders.vdf"), "w") as f:
            f.write('"libraryfolders"\n{\n\t"0"\n\t{\n\t\t"path"\t\t"%s"\n\t}\n\t"1"\n\t{\n\t\t"path"\t\t"%s"\n\t\t"apps"\n\t\t{\n\t\t\t"223710"\t\t"1"\n\t\t}\n\t}\n}\n' % (steam, lib2))
        with open(os.path.join(lib2, "steamapps", "appmanifest_223710.acf"), "w") as f:
            f.write('"AppState"\n{\n\t"appid"\t\t"223710"\n\t"name"\t\t"Cry of Fear"\n\t"installdir"\t\t"Cry of Fear"\n}\n')
        out = launch_log({"MOCK_STEAM_PATH": steam})
        g2 = os.path.join(lib2, "steamapps", "common", "Cry of Fear")
        check(f"would run {g2}/cof.exe -basedir cryoffear" in out, "found through Steam's library list (second drive)")
        out = launch_log({})
        check("Couldn't find the Cry of Fear folder" in out and "would open steam://run/223710" in out,
              "game folder not found: falls back to starting it through Steam")
        out = launch_log({"MOCK_COF_DIR": g1, "MOCK_COF": "1"})
        check("Cry of Fear is already running" in out and "would run" not in out, "doesn't start a second copy")

        print("[21] Refresh: see right away when a friend starts hosting")
        e = dict(os.environ, **base, MOCK_ID="1142", MOCK_FRIENDS="1141", ASAN_OPTIONS="detect_leaks=0")
        rq = subprocess.Popen([BIN, "idle"], env=e, stdin=subprocess.PIPE,
                              stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        rlog = []
        threading.Thread(target=lambda: [rlog.append(l) for l in rq.stdout], daemon=True).start()
        def rwait(pat, t=8):
            end = time.time() + t
            while time.time() < end:
                if re.search(pat, "".join(rlog)): return True
                time.sleep(0.02)
            return False
        time.sleep(1)
        rq.stdin.write("R\n"); rq.stdin.flush()
        check(rwait(r"Refreshing\.\.\."), "Refresh works from the start screen")
        check(rwait(r"Nobody is hosting right now"), "and says plainly when nobody hosts")
        hh = Proc("HH", dict(base, MOCK_ID="1141", MOCK_FRIENDS="1142", MOCK_HIDE_LOBBY="1", MOCK_HIDE_RP="1"), ["host"])
        procs.append(hh)
        hh.wait_for(r"Lobby is ready")
        t0 = time.time()
        rq.stdin.write("R\n"); rq.stdin.flush()
        ok = rwait(r"Found user1141 hosting", 5)
        dt = time.time() - t0
        check(ok and dt < 3, f"after Refresh the new host shows up within ~2 s ({dt:.1f} s)")
        rq.send_signal(signal.SIGINT)
        try: rq.wait(5)
        except subprocess.TimeoutExpired: rq.kill()
        hh.stop()

        print("[22] the host already started Cry of Fear (Steam shows 'playing Cry of Fear'): still found")
        hg = Proc("HG", dict(base, MOCK_ID="1151", MOCK_FRIENDS="1152,1153,1155"), ["host"])
        procs.append(hg)
        hg.wait_for(r"Lobby is ready")
        jg = Proc("JG", dict(base, MOCK_ID="1152", MOCK_FRIENDS="1151", MOCK_FRIEND_IN_COF="1", MOCK_HIDE_RP="1"),
                  ["join", "--yes"])
        procs.append(jg)
        check(jg.wait_for(r"Found user1151 hosting", 8) is not None, "found by itself within a few seconds")
        check(jg.wait_for(r"Connected to user1151") is not None, "and linked")
        jg.stop()
        print("     Steam shows no game at all for the host: Refresh still finds them")
        e = dict(os.environ, **base, MOCK_ID="1153", MOCK_FRIENDS="1151,1199,1198", MOCK_FRIEND_NO_GAME="1", MOCK_HIDE_RP="1",
                 ASAN_OPTIONS="detect_leaks=0")
        rq = subprocess.Popen([BIN, "idle"], env=e, stdin=subprocess.PIPE,
                              stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        rlog = []
        threading.Thread(target=lambda: [rlog.append(l) for l in rq.stdout], daemon=True).start()
        time.sleep(1)
        rq.stdin.write("R\n"); rq.stdin.flush()
        end = time.time() + 5
        while time.time() < end and "Found user1151 hosting" not in "".join(rlog):
            time.sleep(0.05)
        check("Found user1151 hosting" in "".join(rlog), "Refresh asks every online friend and finds the host")
        check("connection attempt" not in "".join(rlog), "friends without CoFLink (1199, 1198) cause no error messages")
        rq.send_signal(signal.SIGINT)
        try: rq.wait(5)
        except subprocess.TimeoutExpired: rq.kill()
        print("     someone who isn't the host's friend still gets no answer")
        jn2 = Proc("JN2", dict(base, MOCK_ID="1154", MOCK_FRIENDS="1151", MOCK_FRIEND_IN_COF="1", MOCK_HIDE_RP="1"),
                   ["join", "--yes"])
        procs.append(jn2)
        time.sleep(7)
        check("Found user1151" not in jn2.log, "not on the host's friends list: host stays hidden")
        jn2.stop(); hg.stop()

        print("[24] host kicks a friend")
        def piped(name, env, args):
            e = dict(os.environ, **base, **env, ASAN_OPTIONS="detect_leaks=0")
            pr = subprocess.Popen([BIN] + args, env=e, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                  stderr=subprocess.STDOUT, text=True)
            pr.out = []
            threading.Thread(target=lambda: [pr.out.append(l) for l in pr.stdout], daemon=True).start()
            def wait(pat, t=8, start=0):
                end = time.time() + t
                while time.time() < end:
                    if re.search(pat, "".join(pr.out[start:])): return True
                    time.sleep(0.05)
                return False
            pr.wait_pat = wait
            pr.key = lambda k: (pr.stdin.write(k + "\n"), pr.stdin.flush())
            return pr
        def end(pr):
            pr.send_signal(signal.SIGINT)
            try: pr.wait(5)
            except subprocess.TimeoutExpired: pr.kill()
        hk = piped("HK", dict(MOCK_ID="1171", MOCK_FRIENDS="1172,1173"), ["host"])
        check(hk.wait_pat(r"Lobby is ready"), "host up")
        code = re.search(r"code (\w{4}-\w{3})", "".join(hk.out)).group(1)
        jk = piped("JK", dict(MOCK_ID="1172", MOCK_FRIENDS="1171"), ["join", "--yes"])
        jk2 = piped("JK2", dict(MOCK_ID="1173", MOCK_FRIENDS="1171"), ["join", "--yes"])
        check(jk.wait_pat(r"Connected to user1171") and jk2.wait_pat(r"Connected to user1171"), "two friends linked")
        time.sleep(1.5)  # the host's list is published once a second
        idx = 1 + [l for l in "".join(hk.out).splitlines() if "connected through Steam" in l].index(
            next(l for l in "".join(hk.out).splitlines() if "user1172 connected through Steam" in l))
        hk.key("K"); hk.key(str(idx))
        check(hk.wait_pat(r"user1172 disconnected \(kicked by you\)"), "host: the kicked friend is cut off")
        check(jk.wait_pat(r"user1171 removed you from their game"), "the kicked friend is told")
        n0 = len(hk.out)
        time.sleep(4)  # the kicked friend's CoFLink keeps looking (--yes)
        check(not hk.wait_pat(r"user1172 connected", 0.1, n0), "the kicked friend does not link again")
        check(not jk.wait_pat(r"Joined user1171", 0.1, len(jk.out) - 3), "and their CoFLink doesn't rejoin that lobby")
        k0 = len(jk.out)
        jk.key("X"); jk.key("L"); jk.key(code)  # stop, then try again with the lobby code
        check(jk.wait_pat(r"removed you from their game", 10, k0), "trying again with the code: still kicked")
        check(not hk.wait_pat(r"user1172 connected", 0.1, n0), "and still never reaches the host's server")
        check(jk2.wait_pat(r"Connected to user1171") and "removed you" not in "".join(jk2.out),
              "the other friend stays linked")
        hk.key("X"); hk.key("H")  # host stops and hosts again: the kick is over
        check(hk.wait_pat(r"Lobby is ready", 8, n0 + 1), "host hosts a new game")
        j0 = len(jk.out)
        jk.key("X"); jk.key("J")
        check(jk.wait_pat(r"Connected to user1171", 15, j0), "after a new hosting, the friend can link again")
        for pr in (jk, jk2, hk): end(pr)

        print("[25] friends on different CoFLink versions are told")
        hv = Proc("HV", dict(base, MOCK_ID="1181", MOCK_FRIENDS="1182"), ["host"])
        procs.append(hv)
        hv.wait_for(r"Lobby is ready")
        e = dict(os.environ, **base, MOCK_ID="1182", MOCK_FRIENDS="1181", ASAN_OPTIONS="detect_leaks=0")
        jvp = subprocess.Popen([BIN_NEW, "join", "--yes"], env=e, stdin=subprocess.DEVNULL,
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        vlog = []
        threading.Thread(target=lambda: [vlog.append(l) for l in jvp.stdout], daemon=True).start()
        cur = re.search(r'#define VERSION "([\d.]+)"', open(os.path.join(ROOT, "src", "coflink.c")).read()).group(1)
        cv = re.escape(cur)
        check(hv.wait_for(r"user1182 has a newer CoFLink \(0\.10\.1, you have " + cv + r"\)\. Update yours: "
                          r"github\.com/Mhdshehri/coflink/releases") is not None, f"host ({cur}) is told the friend has 0.10.1")
        time.sleep(1)
        check(re.search(r"user1181 has an older CoFLink \(" + cv + r"\)\. It still works", "".join(vlog)) is not None,
              "the friend (0.10.1) is told the host has the older one")
        check(re.search(r"Connected to user1181", "".join(vlog)) is not None, "and they still link")
        jvp.send_signal(signal.SIGINT)
        try: jvp.wait(5)
        except subprocess.TimeoutExpired: jvp.kill()
        hv.stop()

        print("[26] only hosts are listed: a friend who just joined someone isn't 'hosting'")
        hs3 = Proc("HS3", dict(base, MOCK_ID="1191", MOCK_FRIENDS="1192"), ["host"])          # Sky
        procs.append(hs3)
        hs3.wait_for(r"Lobby is ready")
        pc = Proc("PC", dict(base, MOCK_ID="1192", MOCK_FRIENDS="1191,1193"), ["join", "--yes"])  # KaZ
        procs.append(pc)
        check(pc.wait_for(r"Connected to user1191") is not None, "the PC links to the host")
        lap = Proc("LAP", dict(base, MOCK_ID="1193", MOCK_FRIENDS="1192"), ["join", "--yes"])      # KaZ_763: friend of the PC only
        procs.append(lap)
        time.sleep(6)
        check("Found user1192 hosting" not in lap.log and "Joining lobby" not in lap.log,
              "the laptop does not see the PC (a player, not a host) as hosting")
        lap.stop()
        lap2 = Proc("LAP2", dict(base, MOCK_ID="1193", MOCK_FRIENDS="1192,1191"), ["join", "--yes"])  # now friends with the host too
        procs.append(lap2)
        check(lap2.wait_for(r"Found user1191 hosting") is not None and "user1192 hosting" not in lap2.log,
              "once friends with the host, the laptop sees the real host (and only the host)")
        lap2.stop(); pc.stop(); hs3.stop()

        print("[8] bad internet: 10% of packets lost each way")
        h3 = Proc("H3", dict(base, MOCK_ID="1021", MOCK_FRIENDS="1022", MOCK_LOSS="10"), ["host"])
        j3 = Proc("J3", dict(base, MOCK_ID="1022", MOCK_FRIENDS="1021", MOCK_LOSS="10"), ["join", "--yes"])
        procs += [h3, j3]
        ok3 = j3.wait_for(r"Connected to user1021") is not None
        if not ok3: print("---- H3 log ----\n" + h3.log + "\n---- J3 log ----\n" + j3.log)
        check(ok3, "joiner still connects (control messages are reliable)")
        p3 = local_port(j3)
        n, exact, ordered = stream_test(p3, 1000) if p3 else (0, False, False)
        check(700 <= n < 1000, f"about 81% of round trips survive, as expected for 10% loss each way ({n}/1000)")
        check(exact and ordered, "what arrives is still exact and in order (the game re-sends the rest itself)")
        h3.stop(); j3.stop()
    finally:
        for p in procs:
            p.stop()
        srv.stop = True
        shutil.rmtree(d, ignore_errors=True)

    print("ALL OK" if not FAILS else f"FAIL ({len(FAILS)}): " + "; ".join(FAILS))
    return 0 if not FAILS else 1


if __name__ == "__main__":
    sys.exit(main())
