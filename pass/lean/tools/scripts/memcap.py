#!/usr/bin/env python3
"""Run a command under a memory cap, by the best way this host offers.

    memcap.py [--mem 20G] -- <command> [args...]     run it; exit with its exit code
    memcap.py --detect                               print the way that will be used

The ways, tried in this order once per process (or taken from MEMCAP_METHOD, which a caller
running many commands sets to what --detect printed):

  scope      systemd-run --user --scope -p MemoryMax=<mem> -p MemorySwapMax=0: the kernel's
             cgroup cap, as the census measurements and sym_survey.py have always been run;
  scope-xdg  the same with XDG_RUNTIME_DIR and DBUS_SESSION_BUS_ADDRESS pointed at the user's
             runtime folder (/run/user/<uid>): cron starts a job without them, and systemd-run
             --user then cannot reach the user's manager although it runs;
  watch      no user manager at all (no login session and no linger): the command runs in a
             session of its own and a watchdog sums the resident memory of every process in that
             session every 0.5 s, and kills every one of them (SIGKILL, process groups such as
             `timeout`'s included) when the sum passes the cap.
             Coarser than a cgroup: the command can pass the cap for up to one interval, and
             memory that is not resident (page cache, swap) is not counted.

A command killed for memory exits as a SIGKILL does (137 from the command line, -9 from run());
in the watch way a line `memcap: killed ...` goes where the command's stderr goes as well.
"""
import os
import signal
import subprocess
import sys
import time

_METHOD = None


def parse_mem(s):
    """`20G`, `512M`, `1024K` or a byte count, as bytes."""
    s = s.strip()
    mult = {"K": 1 << 10, "M": 1 << 20, "G": 1 << 30, "T": 1 << 40}
    if s and s[-1].upper() in mult:
        return int(float(s[:-1]) * mult[s[-1].upper()])
    return int(s)


def _xdg_env():
    uid = os.getuid()
    run = f"/run/user/{uid}"
    env = dict(os.environ)
    env["XDG_RUNTIME_DIR"] = run
    env["DBUS_SESSION_BUS_ADDRESS"] = f"unix:path={run}/bus"
    return env


def _scope_ok(env):
    try:
        r = subprocess.run(["systemd-run", "--user", "--scope", "-q", "-p", "MemoryMax=64M",
                            "-p", "MemorySwapMax=0", "/bin/true"], env=env,
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=30)
        return r.returncode == 0
    except (OSError, subprocess.SubprocessError):
        return False


def method():
    """The way this process caps memory: scope, scope-xdg or watch."""
    global _METHOD
    if _METHOD is None:
        m = os.environ.get("MEMCAP_METHOD", "")
        if m in ("scope", "scope-xdg", "watch"):
            _METHOD = m
        elif _scope_ok(None):
            _METHOD = "scope"
        elif os.path.exists(f"/run/user/{os.getuid()}/bus") and _scope_ok(_xdg_env()):
            _METHOD = "scope-xdg"
        else:
            _METHOD = "watch"
    return _METHOD


def _session(sid):
    """(pid, resident pages) of every process whose session is `sid`."""
    out = []
    for p in os.listdir("/proc"):
        if not p.isdigit():
            continue
        try:
            with open(f"/proc/{p}/stat", "rb") as fh:
                st = fh.read()
        except OSError:
            continue
        f = st[st.rfind(b")") + 2:].split()
        # after the command name: state ppid pgrp session ... rss is the 22nd field; a zombie
        # (state Z) holds no memory and waits only for its parent
        if len(f) > 21 and int(f[3]) == sid and f[0] != b"Z":
            out.append((int(p), int(f[21])))
    return out


def _kill_session(sid):
    """SIGKILL every process of the session, its own process groups included (`timeout` makes one)."""
    for _ in range(50):
        pids = [p for p, _ in _session(sid)]
        if not pids:
            return
        for p in pids:
            try:
                os.kill(p, signal.SIGKILL)
            except ProcessLookupError:
                pass
        time.sleep(0.05)


def run(cmd, mem="20G", stdout=None, stderr=None):
    """Run `cmd` (a list) under the cap; returns its exit code (negative for a signal)."""
    m = method()
    if m in ("scope", "scope-xdg"):
        pre = ["systemd-run", "--user", "--scope", "-q", "-p", f"MemoryMax={mem}", "-p", "MemorySwapMax=0"]
        env = _xdg_env() if m == "scope-xdg" else None
        return subprocess.run(pre + list(cmd), env=env, stdout=stdout, stderr=stderr).returncode
    cap = parse_mem(mem)
    page = os.sysconf("SC_PAGE_SIZE")
    p = subprocess.Popen(list(cmd), stdout=stdout, stderr=stderr, start_new_session=True)
    while True:
        try:
            return p.wait(timeout=0.5)
        except subprocess.TimeoutExpired:
            pass
        rss = sum(n for _, n in _session(p.pid)) * page
        if rss > cap:
            _kill_session(p.pid)
            p.wait()
            # the line goes where the command's stderr went (its log), or to ours
            out = stderr if hasattr(stderr, "write") else \
                stdout if stderr == subprocess.STDOUT and hasattr(stdout, "write") else sys.stderr
            print(f"memcap: killed at {rss >> 20} MB resident (cap {mem}, watchdog)", file=out, flush=True)
            return -signal.SIGKILL


def main(argv):
    mem = "20G"
    if argv[:1] == ["--detect"]:
        print(method())
        return 0
    while argv and argv[0] != "--":
        if argv[0] == "--mem" and len(argv) > 1:
            mem = argv[1]
            argv = argv[2:]
        else:
            print(__doc__, file=sys.stderr)
            return 2
    if len(argv) < 2:
        print(__doc__, file=sys.stderr)
        return 2
    rc = run(argv[1:], mem)
    return 128 - rc if rc < 0 else rc


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
