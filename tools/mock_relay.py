#!/usr/bin/env python3
"""A fake WeeChat `relay api` server for UI development and screenshots.

Speaks enough of the protocol for weechat-desktop: sync, version, buffers (with lines), hotlist,
lines, nicks and input. Posts a random chat line every few seconds, sometimes a highlight.

    python3 tools/mock_relay.py [--port 9001] [--password test] [--quiet]
"""
import argparse
import asyncio
import datetime as dt
import itertools
import json
import random
import re
import urllib.parse

import websockets

OWN_NICK = "rasi"
ESC = "\x1b"

NICKS = ["alice", "bob", "carol", "dmitri", "eve", "flux", "gwen", "hiro", "ines", "jules",
         "kazuki", "lena", "mallory", "nadia", "oskar", "petra", "quinn", "ravi", "sol", "trent"]

CHATTER = [
    "has anyone tried the new relay api? it's so much nicer than the old binary protocol",
    "morning all :wave:",
    "I just pushed a fix for the nicklist sorting",
    "that's a great idea :+1:",
    "check this out https://github.com/weechat/weechat",
    "screenshot: https://www.gstatic.com/webp/gallery/1.jpg",
    "lol :joy:",
    "does /upgrade keep the relay connections alive?",
    "yes, clients just get an upgrade event and resync",
    f"{ESC}[1mbold{ESC}[0m statement: always read the {ESC}[4mchangelog{ESC}[0m",
    f"{ESC}[31mred{ESC}[0m {ESC}[32mgreen{ESC}[0m {ESC}[34mblue{ESC}[0m — rainbow time",
    "🎉🎉🎉",
    "brb, coffee :coffee:",
    "anyone around from #archlinux?",
    "the docs are at https://weechat.org/files/doc/weechat/stable/weechat_relay_api.en.html",
    "I disagree, but I see your point",
    "ship it :rocket:",
    "👍",
    "tbh scrollback search was the feature I missed most",
    "works on my machine ¯\\_(ツ)_/¯",
]
HIGHLIGHTS = [
    f"{OWN_NICK}: could you take a look at the PR when you have a minute?",
    f"hey {OWN_NICK}, your build is green now :tada:",
    f"{OWN_NICK}++ for the emoji picker",
]

line_ids = itertools.count(1000)
buffer_ids = itertools.count(1_700_000_000_000_001)


def now_iso(offset_s=0.0):
    t = dt.datetime.now(dt.timezone.utc) + dt.timedelta(seconds=offset_s)
    return t.strftime("%Y-%m-%dT%H:%M:%S.%fZ")


def nick_prefix(nick, mode=""):
    color = 31 + (sum(map(ord, nick)) % 6)
    return f"{ESC}[{color}m{mode}{nick}{ESC}[0m"


LONG = [
    "Als nach dem Erdbeben das Telefonnetz zusammenbrach, funktionierten Nachrichten über das Internet weiter, und genau deshalb hat sich die App damals so schnell verbreitet.",
    "I think the real reason the relay api is nicer is that it is just JSON over a WebSocket, so any language with a websocket library can talk to WeeChat without a custom parser.",
    "ach Rasi ✂️sSchere✂️ wenn das model chillt, wird der vram auch freigemacht, aber nur wenn man es explizit entlädt oder der timeout abläuft",
]


def make_line(nick, text, offset=0.0, kind="privmsg", highlight=False, self_msg=False):
    tags = []
    prefix = nick_prefix(nick)
    message = text
    if kind == "privmsg":
        tags = ["irc_privmsg", f"nick_{nick}", "log1", "notify_message"]
    elif kind == "action":
        tags = ["irc_privmsg", "irc_action", f"nick_{nick}", "notify_message"]
        prefix = " *"
        message = f"{nick} {text}"
    elif kind == "notice":
        tags = ["irc_notice", f"nick_{nick}", "notify_private"]
    elif kind == "join":
        tags = ["irc_join", f"nick_{nick}", "log4"]
        prefix = f"{ESC}[32m-->{ESC}[0m"
        message = f"{nick} (~{nick}@example.org) has joined the channel"
    elif kind == "part":
        tags = ["irc_part", f"nick_{nick}", "log4"]
        prefix = f"{ESC}[31m<--{ESC}[0m"
        message = f"{nick} (~{nick}@example.org) has left the channel"
    elif kind == "quit":
        tags = ["irc_quit", f"nick_{nick}", "log4"]
        prefix = f"{ESC}[31m<--{ESC}[0m"
        message = f"{nick} (~{nick}@example.org) has quit (Ping timeout: 240 seconds)"
    elif kind == "topic":
        tags = ["irc_topic", f"nick_{nick}"]
        prefix = "--"
        message = f"{nick} has changed topic to \"{text}\""
    elif kind == "system":
        prefix = "--"
        tags = []
    if self_msg:
        tags.append("self_msg")
    if highlight:
        tags.append("notify_highlight")
    return {
        "id": next(line_ids), "y": -1, "date": now_iso(offset), "date_printed": now_iso(offset),
        "displayed": True, "highlight": highlight, "notify_level": 3 if highlight else 1,
        "prefix": prefix, "message": message, "tags": tags,
    }


class Buffer:
    def __init__(self, name, short, kind, server, number, title="", nicks=()):
        self.id = next(buffer_ids)
        self.name = name
        self.short = short
        self.kind = kind
        self.server = server
        self.number = number
        self.title = title
        self.nicks = list(nicks)
        self.lines = []

    def json(self, lines=0):
        lv = {"plugin": "irc" if self.server else "core", "name": self.name.split(".", 1)[-1]}
        if self.kind in ("channel", "private", "server"):
            lv.update({"type": self.kind, "server": self.server, "nick": OWN_NICK})
            if self.kind != "server":
                lv["channel"] = self.short
        o = {"id": self.id, "name": self.name, "short_name": self.short, "number": self.number,
             "type": "formatted", "hidden": False, "title": self.title, "local_variables": lv}
        if lines:
            o["lines"] = self.lines[-lines:]
        return o


def build_world():
    bufs = [
        Buffer("core.weechat", "weechat", "core", "", 1, "WeeChat 4.10.4 — https://weechat.org/"),
        Buffer("irc.server.libera", "libera", "server", "libera", 2),
        Buffer("irc.libera.#weechat", "#weechat", "channel", "libera", 3,
               f"WeeChat, the extensible chat client | {ESC}[1mv4.10.4{ESC}[0m released | https://weechat.org | Be nice :heart:",
               NICKS[:16]),
        Buffer("irc.libera.#archlinux", "#archlinux", "channel", "libera", 4,
               "Arch Linux | Read the wiki before asking: https://wiki.archlinux.org", NICKS[3:20]),
        Buffer("irc.libera.alice", "alice", "private", "libera", 5, "alice@example.org"),
        Buffer("irc.libera.#qt", "#qt", "channel", "libera", 6, "Qt — cross-platform application framework", NICKS[5:14]),
        Buffer("irc.server.oftc", "oftc", "server", "oftc", 7),
        Buffer("irc.oftc.#debian", "#debian", "channel", "oftc", 8, "Debian support", NICKS[8:18]),
        Buffer("irc.oftc.#tor", "#tor", "channel", "oftc", 9, "Tor project discussion", NICKS[2:9]),
    ]
    rnd = random.Random(7)
    for b in bufs:
        n = {"channel": 140, "private": 14, "server": 6, "core": 4}[b.kind]
        t = -3600 * 26.0
        for i in range(n):
            t += rnd.uniform(5, 600) if i % 9 else rnd.uniform(1, 30)
            if b.kind == "core":
                b.lines.append(make_line("", "Plugins loaded: alias, buflist, charset, exec, fifo, irc, logger, relay, script", t, kind="system"))
            elif b.kind == "server":
                b.lines.append(make_line("", f"irc: connected to {b.server} (203.0.113.{i})", t, kind="system"))
            elif b.kind == "private":
                speaker = rnd.choice(["alice", OWN_NICK])
                b.lines.append(make_line(speaker, rnd.choice(CHATTER), t, self_msg=speaker == OWN_NICK))
            else:
                r = rnd.random()
                nick = rnd.choice(b.nicks)
                if r < 0.07:
                    b.lines.append(make_line(nick, "", t, kind=rnd.choice(["join", "part", "quit"])))
                elif r < 0.11:
                    b.lines.append(make_line(nick, rnd.choice(["waves", "is away for lunch", "dances :dancer:"]), t, kind="action"))
                elif r < 0.14:
                    b.lines.append(make_line(nick, rnd.choice(HIGHLIGHTS), t, highlight=True))
                elif r < 0.19:
                    b.lines.append(make_line(OWN_NICK, rnd.choice(CHATTER), t, self_msg=True))
                else:
                    b.lines.append(make_line(nick, rnd.choice(CHATTER), t))
    # A channel of long, wrapping messages (the UI checks use it: wrapped rows once sent the
    # compact layout into an endless re-layout).
    essays = Buffer("irc.libera.#essays", "#essays", "channel", "libera", 10, "Long messages", NICKS[:8])
    t = -3600 * 3.0
    for i in range(60):
        t += rnd.uniform(20, 120)
        essays.lines.append(make_line(rnd.choice(essays.nicks), " ".join([rnd.choice(LONG)] * rnd.randint(1, 3)), t))
    bufs.append(essays)
    return bufs


def response(request, body_type=None, body=None, code=200):
    return json.dumps({"code": code, "message": "OK" if code < 300 else "Error", "request": request,
                       "request_body": None, "request_id": None, "body_type": body_type, "body": body})


def event(name, buffer_id, body_type, body):
    return json.dumps({"code": 0, "message": "Event", "event_name": name, "request": "",
                       "request_body": None, "request_id": None, "buffer_id": buffer_id,
                       "body_type": body_type, "body": body})


def nick_tree(buf):
    def group(name, nicks, mode):
        return {"id": abs(hash(name)) % 10**9, "parent_id": 0, "name": name, "color_name": "", "color": "",
                "visible": False, "groups": [],
                "nicks": [{"id": abs(hash(n)) % 10**9, "parent_id": 0, "prefix": mode, "prefix_color_name": "",
                           "prefix_color": "", "name": n, "color_name": "", "color": "", "visible": True}
                          for n in nicks]}
    ops, voiced, rest = buf.nicks[:2], buf.nicks[2:4], buf.nicks[4:] + [OWN_NICK]
    return {"id": 0, "parent_id": -1, "name": "root", "color_name": "", "color": "", "visible": False,
            "groups": [group("000|o", ops, "@"), group("001|v", voiced, "+"), group("999|...", rest, " ")],
            "nicks": []}


class Server:
    def __init__(self, password, quiet):
        self.password = password
        self.quiet = quiet
        self.buffers = build_world()
        self.clients = set()

    def by_ref(self, ref):
        ref = urllib.parse.unquote(ref)
        for b in self.buffers:
            if str(b.id) == ref or b.name == ref:
                return b
        return None

    async def broadcast_line(self, buf, line):
        buf.lines.append(line)
        msg = event("buffer_line_added", buf.id, "line", line)
        for ws in list(self.clients):
            try:
                await ws.send(msg)
            except Exception:
                pass

    async def chatter(self):
        rnd = random.Random()
        while True:
            await asyncio.sleep(rnd.uniform(2.5, 6))
            if self.quiet:
                continue
            buf = rnd.choice([b for b in self.buffers if b.kind in ("channel", "private")])
            if buf.kind == "private":
                line = make_line("alice", rnd.choice(CHATTER))
            elif rnd.random() < 0.12:
                line = make_line(rnd.choice(buf.nicks), rnd.choice(HIGHLIGHTS), highlight=True)
            elif rnd.random() < 0.08:
                line = make_line(rnd.choice(buf.nicks), "", kind=rnd.choice(["join", "part"]))
            else:
                line = make_line(rnd.choice(buf.nicks), rnd.choice(CHATTER))
            await self.broadcast_line(buf, line)

    async def handle(self, ws):
        self.clients.add(ws)
        try:
            async for raw in ws:
                req = json.loads(raw)
                path = req.get("request", "")
                body = req.get("body") or {}
                await self.dispatch(ws, path, body)
        finally:
            self.clients.discard(ws)

    async def dispatch(self, ws, path, body):
        method, _, url = path.partition(" ")
        parsed = urllib.parse.urlparse(url)
        q = urllib.parse.parse_qs(parsed.query)
        p = parsed.path
        if p == "/api/sync" or p == "/api/ping":
            await ws.send(response(path, code=204))
        elif p == "/api/version":
            await ws.send(response(path, "version", {"weechat_version": "4.10.4", "weechat_version_git": "",
                                                     "weechat_version_number": 0x040a0400, "relay_api_version": "0.4.0"}))
        elif p == "/api/hotlist":
            hot = []
            for b in self.buffers:
                if b.name in ("irc.libera.#archlinux", "irc.oftc.#debian", "irc.libera.alice"):
                    hot.append({"priority": 2, "date": now_iso(), "buffer_id": b.id,
                                "count": [3, 5 if b.kind == "channel" else 0, 2 if b.kind == "private" else 0,
                                          1 if b.name.endswith("archlinux") else 0]})
            await ws.send(response(path, "hotlist", hot))
        elif p == "/api/buffers":
            n = abs(int(q.get("lines", ["0"])[0]))
            await ws.send(response(path, "buffers", [b.json(n) for b in self.buffers]))
        elif m := re.match(r"/api/buffers/([^/]+)/lines", p):
            b = self.by_ref(m.group(1))
            n = abs(int(q.get("lines", ["-100"])[0]))
            await ws.send(response(path, "lines", b.lines[-n:] if b else []))
        elif m := re.match(r"/api/buffers/([^/]+)/nicks", p):
            b = self.by_ref(m.group(1))
            await ws.send(response(path, "nick_group", nick_tree(b) if b else {}))
        elif p == "/api/input":
            await ws.send(response(path, code=204))
            b = self.by_ref(body.get("buffer_name") or str(body.get("buffer_id", "")))
            cmd = body.get("command", "")
            if not b:
                return
            if cmd.startswith("/me "):
                await self.broadcast_line(b, make_line(OWN_NICK, cmd[4:], kind="action", self_msg=True))
            elif cmd.startswith("//") or not cmd.startswith("/"):
                await self.broadcast_line(b, make_line(OWN_NICK, cmd[1:] if cmd.startswith("//") else cmd, self_msg=True))
        else:
            await ws.send(response(path, code=404, body={"error": "Not found"}))


async def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=9001)
    ap.add_argument("--password", default="test")
    ap.add_argument("--quiet", action="store_true", help="no random chatter")
    args = ap.parse_args()
    server = Server(args.password, args.quiet)

    def process_request(connection, request):
        protos = [p.strip() for p in request.headers.get("Sec-WebSocket-Protocol", "").split(",")]
        token = next((p for p in protos if p.startswith("base64url.bearer.authorization.weechat.")), "")
        import base64
        raw = token.rsplit(".", 1)[-1]
        try:
            decoded = base64.urlsafe_b64decode(raw + "=" * (-len(raw) % 4)).decode()
        except Exception:
            decoded = ""
        if decoded != f"plain:{args.password}":
            return connection.respond(401, '{"error": "Invalid password"}\n')
        return None

    async with websockets.serve(server.handle, "127.0.0.1", args.port, subprotocols=["api.weechat"],
                                process_request=process_request, max_size=2**24):
        print(f"mock relay on ws://127.0.0.1:{args.port}/api (password: {args.password})", flush=True)
        await server.chatter()


if __name__ == "__main__":
    asyncio.run(main())
