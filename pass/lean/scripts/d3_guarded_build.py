#!/usr/bin/env python3
"""Run one command inside a memory cgroup, the way d3_sweep.py runs a probe.

Generated chunk modules are built as separate processes, so their builds need
the same guard the sweep applies to a probe -- otherwise "built under 10 GB" is
a measurement rather than a limit. This reuses `d3_sweep`'s own cgroup
discovery and `cgexec` shim so there is one mechanism, not two.

  d3_guarded_build.py --max-kb N -- <cmd> [args...]

Reports the cgroup's `memory.peak` and whether the kernel killed anything.
Exit status is the command's, or 137 if the cgroup OOM-killed it.
"""
import argparse, importlib.util, os, pathlib, subprocess, sys, tempfile

HERE = pathlib.Path(__file__).resolve().parent
_spec = importlib.util.spec_from_file_location("d3_sweep_mod", HERE / "d3_sweep.py")
sweep = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(sweep)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--max-kb", type=int, required=True)
    ap.add_argument("cmd", nargs=argparse.REMAINDER)
    a = ap.parse_args()
    cmd = a.cmd[1:] if a.cmd and a.cmd[0] == "--" else a.cmd
    if not cmd:
        print("d3_guarded_build: no command", file=sys.stderr)
        return 2

    base = sweep.cgroup_base()
    if base is None:
        print("d3_guarded_build: REFUSING -- no delegated cgroup v2 base. The "
              "build would run unguarded, which is what this wrapper exists to "
              "prevent.", file=sys.stderr)
        return 2

    cg = base / f"d3build_{os.getpid()}"
    cg.mkdir(parents=True, exist_ok=True)
    try:
        (cg / "memory.max").write_text(str(a.max_kb * 1024))
        try:
            (cg / "memory.oom.group").write_text("1")
        except OSError:
            pass
        shim = pathlib.Path(tempfile.mkdtemp(prefix="d3cgx_")) / "cgexec.py"
        shim.write_text(
            "import os, sys\n"
            "with open(os.path.join(sys.argv[1], 'cgroup.procs'), 'w') as fh:\n"
            "    fh.write(str(os.getpid()))\n"
            "os.execvpe(sys.argv[2], sys.argv[2:], os.environ)\n")
        rc = subprocess.run([sys.executable, str(shim), str(cg)] + cmd).returncode
        peak = int((cg / "memory.peak").read_text().strip())
        ev = dict(l.split() for l in (cg / "memory.events").read_text().splitlines())
        killed = int(ev.get("oom_kill", "0"))
        print(f"d3_guarded_build: rc={rc} peak={peak // 1024:,} kB "
              f"cap={a.max_kb:,} kB oom_kill={killed}")
        return 137 if killed else rc
    finally:
        try:
            (cg / "cgroup.kill").write_text("1")
        except OSError:
            pass
        try:
            cg.rmdir()
        except OSError as e:
            print(f"d3_guarded_build: cgroup {cg} not removed: {e}", file=sys.stderr)


if __name__ == "__main__":
    raise SystemExit(main())
