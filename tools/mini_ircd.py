#!/usr/bin/env python3
"""A tiny IRC server with chatty bots, so a local WeeChat has real channels to relay.

Enough of RFC 1459 for WeeChat: registration, JOIN/PART/QUIT, PRIVMSG/NOTICE, TOPIC, NAMES,
WHOIS, MODE (+o/+v), PING. Bots join channels, talk, mention the first human, and sometimes
come and go.

    python3 tools/mini_ircd.py [--port 6667]
"""
import argparse
import asyncio
import random

SERVER = "mini.irc"
CHANNELS = {
    "#weechat": "WeeChat, the extensible chat client | https://weechat.org | Be nice :heart:",
    "#archlinux": "Arch Linux | Read the wiki before asking: https://wiki.archlinux.org",
    "#qt": "Qt — cross-platform application framework",
}
BOTS = ["alice", "bob", "carol", "dmitri", "eve", "flux", "gwen", "hiro", "ines", "jules"]
LINES = [
    "has anyone tried the relay api yet?",
    "morning all :wave:",
    "I just pushed a fix for the nicklist sorting",
    "that's a great idea :+1:",
    "check this out https://github.com/weechat/weechat",
    "lol :joy:",
    "\x02bold\x02 move, \x0304red\x03 \x0309green\x03 \x0312blue\x03",
    "does /upgrade keep relay clients connected?",
    "ship it :rocket:",
    "🎉",
    "works on my machine ¯\\_(ツ)_/¯",
]


class Client:
    def __init__(self, server, reader=None, writer=None, nick=None, bot=False):
        self.server, self.reader, self.writer = server, reader, writer
        self.nick, self.user, self.bot = nick, "user", bot
        self.channels = set()
        self.registered = bot

    @property
    def mask(self):
        return f"{self.nick}!{self.user}@{'bot' if self.bot else 'localhost'}"

    def send(self, line):
        if self.writer and not self.writer.is_closing():
            self.writer.write((line + "\r\n").encode())


class Server:
    def __init__(self):
        self.clients = {}            # nick (lower) → Client
        self.channels = {c: {"topic": t, "members": {}} for c, t in CHANNELS.items()}  # members: nick → mode

    def humans(self):
        return [c for c in self.clients.values() if not c.bot]

    def to_channel(self, chan, line, skip=None):
        for nick in self.channels[chan]["members"]:
            c = self.clients.get(nick.lower())
            if c and c is not skip:
                c.send(line)

    def join(self, client, chan):
        if chan not in self.channels:
            self.channels[chan] = {"topic": "", "members": {}}
        ch = self.channels[chan]
        mode = "@" if not ch["members"] else ""
        ch["members"][client.nick] = mode
        client.channels.add(chan)
        self.to_channel(chan, f":{client.mask} JOIN {chan}")
        if not client.bot:
            if ch["topic"]:
                client.send(f":{SERVER} 332 {client.nick} {chan} :{ch['topic']}")
            names = " ".join(m + n for n, m in ch["members"].items())
            client.send(f":{SERVER} 353 {client.nick} = {chan} :{names}")
            client.send(f":{SERVER} 366 {client.nick} {chan} :End of /NAMES list.")

    def part(self, client, chan, msg="bye"):
        ch = self.channels.get(chan)
        if not ch or client.nick not in ch["members"]:
            return
        self.to_channel(chan, f":{client.mask} PART {chan} :{msg}")
        del ch["members"][client.nick]
        client.channels.discard(chan)

    def quit(self, client, msg="Quit"):
        seen = set()
        for chan in list(client.channels):
            for nick in self.channels[chan]["members"]:
                c = self.clients.get(nick.lower())
                if c and c is not client and c not in seen:
                    c.send(f":{client.mask} QUIT :{msg}")
                    seen.add(c)
            del self.channels[chan]["members"][client.nick]
        client.channels.clear()
        self.clients.pop(client.nick.lower(), None)

    def privmsg(self, client, target, text, cmd="PRIVMSG"):
        if target.startswith("#"):
            if target in self.channels:
                self.to_channel(target, f":{client.mask} {cmd} {target} :{text}", skip=client)
        else:
            c = self.clients.get(target.lower())
            if c:
                c.send(f":{client.mask} {cmd} {target} :{text}")
                if c.bot and not client.bot and cmd == "PRIVMSG":
                    asyncio.get_event_loop().call_later(
                        1.5, lambda: self.privmsg(c, client.nick, random.choice(
                            ["hey! :wave:", "sure, one sec", "haha yes", f"you said: {text[:40]}"])))
            else:
                client.send(f":{SERVER} 401 {client.nick} {target} :No such nick/channel")

    def welcome(self, c):
        c.registered = True
        self.clients[c.nick.lower()] = c
        for num, text in [("001", f"Welcome to the mini IRC network {c.nick}"), ("002", f"Your host is {SERVER}"),
                          ("003", "This server was created today"), ("004", f"{SERVER} mini-1.0 io ovbtn"),
                          ("005", "CHANTYPES=# PREFIX=(ov)@+ NETWORK=mini CASEMAPPING=ascii :are supported"),
                          ("375", f"- {SERVER} Message of the day -"), ("372", "- Have fun testing!"),
                          ("376", "End of /MOTD command.")]:
            c.send(f":{SERVER} {num} {c.nick} :{text}" if num not in ("004", "005") else f":{SERVER} {num} {c.nick} {text}")

    async def handle(self, reader, writer):
        c = Client(self, reader, writer)
        try:
            while line := await reader.readline():
                line = line.decode(errors="replace").rstrip("\r\n")
                if not line:
                    continue
                trailing = None
                if " :" in line:
                    line, trailing = line.split(" :", 1)
                parts = line.split()
                cmd, args = parts[0].upper(), parts[1:] + ([trailing] if trailing is not None else [])
                await self.command(c, cmd, args)
                await writer.drain()
        except (ConnectionError, asyncio.IncompleteReadError):
            pass
        finally:
            if c.nick and c.registered:
                self.quit(c, "Connection closed")
            writer.close()

    async def command(self, c, cmd, args):
        if cmd == "CAP":
            if args and args[0] == "LS":
                c.send(f":{SERVER} CAP * LS :")
        elif cmd == "NICK" and args:
            new = args[0]
            if new.lower() in self.clients and self.clients[new.lower()] is not c:
                c.send(f":{SERVER} 433 * {new} :Nickname is already in use")
                return
            if c.registered:
                old_mask = c.mask
                self.clients.pop(c.nick.lower(), None)
                for chan in c.channels:
                    m = self.channels[chan]["members"]
                    m[new] = m.pop(c.nick)
                c.nick = new
                self.clients[new.lower()] = c
                c.send(f":{old_mask} NICK :{new}")
                for chan in c.channels:
                    self.to_channel(chan, f":{old_mask} NICK :{new}", skip=c)
            else:
                c.nick = new
                if c.user != "user" or True:
                    pass
        elif cmd == "USER" and args:
            c.user = args[0]
            if c.nick and not c.registered:
                self.welcome(c)
        elif cmd == "PING":
            c.send(f":{SERVER} PONG {SERVER} :{args[0] if args else ''}")
        elif cmd == "JOIN" and args:
            for chan in args[0].split(","):
                if chan.startswith("#") and chan not in c.channels:
                    self.join(c, chan)
        elif cmd == "PART" and args:
            for chan in args[0].split(","):
                self.part(c, chan, args[1] if len(args) > 1 else "Leaving")
        elif cmd in ("PRIVMSG", "NOTICE") and len(args) >= 2:
            self.privmsg(c, args[0], args[1], cmd)
        elif cmd == "TOPIC" and args:
            ch = self.channels.get(args[0])
            if ch is None:
                return
            if len(args) > 1:
                ch["topic"] = args[1]
                self.to_channel(args[0], f":{c.mask} TOPIC {args[0]} :{args[1]}")
            else:
                c.send(f":{SERVER} 332 {c.nick} {args[0]} :{ch['topic']}")
        elif cmd == "NAMES" and args:
            ch = self.channels.get(args[0], {"members": {}})
            names = " ".join(m + n for n, m in ch["members"].items())
            c.send(f":{SERVER} 353 {c.nick} = {args[0]} :{names}")
            c.send(f":{SERVER} 366 {c.nick} {args[0]} :End of /NAMES list.")
        elif cmd == "MODE" and args:
            target = args[0]
            if target.startswith("#") and len(args) >= 3 and args[1] in ("+o", "-o", "+v", "-v"):
                ch = self.channels.get(target)
                if ch and args[2] in ch["members"]:
                    ch["members"][args[2]] = {"+o": "@", "+v": "+"}.get(args[1], "")
                    self.to_channel(target, f":{c.mask} MODE {target} {args[1]} {args[2]}")
            elif target.startswith("#"):
                c.send(f":{SERVER} 324 {c.nick} {target} +nt")
            else:
                c.send(f":{SERVER} 221 {c.nick} +i")
        elif cmd == "WHOIS" and args:
            n = args[-1]
            t = self.clients.get(n.lower())
            if t:
                c.send(f":{SERVER} 311 {c.nick} {t.nick} {t.user} {'bot' if t.bot else 'localhost'} * :{t.nick} the {'bot' if t.bot else 'human'}")
                c.send(f":{SERVER} 319 {c.nick} {t.nick} :{' '.join(sorted(t.channels))}")
                c.send(f":{SERVER} 312 {c.nick} {t.nick} {SERVER} :Mini IRC, for testing")
                if t.bot:
                    c.send(f":{SERVER} 671 {c.nick} {t.nick} :is using a secure connection")
                    c.send(f":{SERVER} 330 {c.nick} {t.nick} {t.nick}_acct :is logged in as")
                c.send(f":{SERVER} 317 {c.nick} {t.nick} {random.randint(0, 4000)} 1794600000 :seconds idle, signon time")
            else:
                c.send(f":{SERVER} 401 {c.nick} {n} :No such nick/channel")
            c.send(f":{SERVER} 318 {c.nick} {n} :End of /WHOIS list.")
        elif cmd == "WHO" and args:
            c.send(f":{SERVER} 315 {c.nick} {args[0]} :End of /WHO list.")
        elif cmd == "QUIT":
            self.quit(c, args[0] if args else "Quit")
            c.writer.close()

    async def bots(self):
        bots = []
        for name in BOTS:
            b = Client(self, nick=name, bot=True)
            self.clients[name] = b
            bots.append(b)
            for chan in random.sample(list(CHANNELS), k=random.randint(1, 3)):
                self.join(b, chan)
        while True:
            await asyncio.sleep(random.uniform(2, 5))
            b = random.choice(bots)
            r = random.random()
            humans = self.humans()
            if not b.channels:
                self.join(b, random.choice(list(CHANNELS)))
                continue
            chan = random.choice(sorted(b.channels))
            if r < 0.06:
                self.part(b, chan, "brb")
                asyncio.get_event_loop().call_later(random.uniform(5, 20), lambda b=b, chan=chan: self.join(b, chan))
            elif r < 0.12 and humans:
                h = random.choice(humans)
                self.privmsg(b, chan, random.choice([f"{h.nick}: could you review my PR?", f"thanks {h.nick} :tada:"]))
            elif r < 0.16:
                self.privmsg(b, chan, "\x01ACTION " + random.choice(["waves", "gets coffee :coffee:", "dances"]) + "\x01")
            elif r < 0.18 and humans:
                self.privmsg(b, random.choice(humans).nick, random.choice(["hey, got a minute?", "lunch? :pizza:"]))
            else:
                self.privmsg(b, chan, random.choice(LINES))


async def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=6667)
    args = ap.parse_args()
    server = Server()
    srv = await asyncio.start_server(server.handle, "127.0.0.1", args.port)
    print(f"mini ircd on 127.0.0.1:{args.port}", flush=True)
    async with srv:
        await asyncio.gather(srv.serve_forever(), server.bots())


if __name__ == "__main__":
    asyncio.run(main())
