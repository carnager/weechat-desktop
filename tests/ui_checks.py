#!/usr/bin/env python3
"""Headless UI checks: run the app against the mock relay through a set of scenarios.

Needs a development build (WD_DEVTOOLS, for --screenshot-action) and the Python `websockets`
package (for the mock relay). Each scenario runs the app offscreen with its own config and
watches it: the app must finish on its own, stay under a memory limit (a layout loop shows up as
runaway memory), and report "CHECK <name> ok" where the scenario expects one.

    python3 tests/ui_checks.py --app build/weechat-desktop --mock tools/mock_relay.py
Exit status: 0 all passed, 1 a failure, 77 skipped (no websockets package).
"""
import argparse
import os
import re
import socket
import subprocess
import sys
import tempfile
import time

MEMORY_LIMIT_MB = 1200
TIMEOUT_S = 30

# name, settings, window (w, h), --screenshot-action, CHECK names that must report ok
SCENARIOS = [
    ("select (cozy)", {}, (1240, 800), "select", ["select"]),
    ("select (compact)", {"layout": "compact"}, (1240, 800), "select", ["select"]),
    ("wheel scrolls the chat", {}, (1240, 800), "scroll", ["scroll"]),
    ("sending jumps to the bottom", {}, (1240, 800), "scrollsend", ["scrollsend"]),
    ("multi-line paste asks first", {}, (1240, 800), "paste", ["paste"]),
    ("confirmed paste is sent", {}, (1240, 800), "pastesend", ["pastesend"]),
    # Compact rows used to lay out forever (CPU 100%, memory growing) once lines wrapped.
    ("compact layout with wrapping lines settles", {"layout": "compact", "lastBuffer": "irc.libera.#essays"},
     (900, 700), "", []),
    ("cozy layout with wrapping lines settles", {"lastBuffer": "irc.libera.#essays"}, (900, 700), "", []),
]


def free_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def rss_mb(pid):
    try:
        with open(f"/proc/{pid}/status") as f:
            for line in f:
                if line.startswith("VmRSS:"):
                    return int(line.split()[1]) / 1024
    except OSError:
        pass
    return 0


def run(app, port, tmp, name, settings, size, action, expect):
    cfg = os.path.join(tmp, re.sub(r"\W+", "-", name))
    os.makedirs(os.path.join(cfg, "config", "weechat-desktop"), exist_ok=True)
    conf = {"host": "127.0.0.1", "port": port, "tls": "false", "password": "test",
            "lastBuffer": "irc.libera.#weechat", **settings}
    with open(os.path.join(cfg, "config", "weechat-desktop", "weechat-desktop.conf"), "w") as f:
        f.write("[General]\n" + "".join(f"{k}={v}\n" for k, v in conf.items()))
        f.write(f"\n[window]\nwidth={size[0]}\nheight={size[1]}\n")
    env = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_FORCE_STDERR_LOGGING="1",
               XDG_CONFIG_HOME=os.path.join(cfg, "config"), XDG_CACHE_HOME=os.path.join(cfg, "cache"))
    cmd = [app, "--new-instance", "--screenshot", os.path.join(cfg, "shot.png"), "--screenshot-delay", "4500"]
    if action:
        cmd += ["--screenshot-action", action]
    proc = subprocess.Popen(cmd, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    start, peak, problem = time.time(), 0.0, None
    while proc.poll() is None:
        peak = max(peak, rss_mb(proc.pid))
        if peak > MEMORY_LIMIT_MB:
            problem = f"memory ran away ({peak:.0f} MB)"
        elif time.time() - start > TIMEOUT_S:
            problem = f"didn't finish in {TIMEOUT_S} s"
        if problem:
            proc.kill()
            break
        time.sleep(0.1)
    out = proc.communicate()[0]
    if not problem and proc.returncode != 0:
        problem = f"exited with {proc.returncode}"
    checks = dict(re.findall(r"CHECK (\S+) (ok|FAIL)", out))
    for c in expect:
        if checks.get(c) != "ok":
            problem = problem or f"CHECK {c}: {checks.get(c, 'not reported')}"
    if not problem and (m := re.search(r"Binding loop detected.*", out)):
        problem = m.group(0)
    return problem, peak, out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--app", required=True)
    ap.add_argument("--mock", required=True)
    args = ap.parse_args()
    try:
        import websockets  # noqa: F401 (the mock relay needs it)
    except ImportError:
        print("SKIP: the Python 'websockets' package is needed for the mock relay")
        return 77

    port = free_port()
    mock = subprocess.Popen([sys.executable, args.mock, "--port", str(port), "--password", "test", "--quiet"],
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    failed = 0
    try:
        time.sleep(1)
        with tempfile.TemporaryDirectory(prefix="wd-ui-") as tmp:
            for name, settings, size, action, expect in SCENARIOS:
                problem, peak, out = run(args.app, port, tmp, name, settings, size, action, expect)
                print(f"{'FAIL' if problem else 'ok  '}  {name}  ({peak:.0f} MB)" + (f": {problem}" if problem else ""))
                if problem:
                    failed += 1
                    print("      " + "\n      ".join(out.strip().splitlines()[-15:]))
    finally:
        mock.terminate()
        mock.wait()
    print(f"{len(SCENARIOS) - failed}/{len(SCENARIOS)} passed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
