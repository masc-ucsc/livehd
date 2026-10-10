#!/usr/bin/env python3
"""Run one command inside a memory cgroup, the way d3_sweep.py runs a probe.

Generated chunk modules are built as separate processes, so their builds need
the same guard the sweep applies to a probe -- otherwise "built under 10 GB" is
a measurement rather than a limit. This reuses `d3_sweep`'s own cgroup
discovery and `cgexec` shim so there is one mechanism, not two.

  d3_guarded_build.py --max-kb N -- <cmd> [args...]

Reports the cgroup's `memory.peak`, whether the kernel killed anything, and
the `memory.events` counters that say whether the cap was BINDING:

  oom_kill  the kernel killed a process -- the build did not fit
  max       allocations were blocked at `memory.max` and the kernel reclaimed
  high      the throttling threshold was crossed

`max > 0` with `oom_kill == 0` is the case that is easy to misread as success:
the build completed, but only because reclaim kept evicting pages it then had
to fault back in. `peak == cap` exactly is its signature. Recording the counter
makes the difference explicit instead of leaving it to be inferred from the
peak, so "fit under the cap" is distinguishable from "ran pinned against it".

Exit status is the command's, or 137 if the cgroup OOM-killed it.
"""
import argparse, importlib.util, os, pathlib, shutil, subprocess, sys, tempfile

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[2]
_spec = importlib.util.spec_from_file_location("d3_sweep_mod", HERE / "d3_sweep.py")
sweep = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(sweep)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--max-kb", type=int, required=True)
    ap.add_argument("--cgroup-path-file", default="",
                    help="write this run's cgroup path here as soon as it "
                         "exists. A caller that KILLS this wrapper takes the "
                         "whole process group with it, so the `finally` never "
                         "runs: the peak is never reported and the cgroup "
                         "leaks. With the path on disk the caller can still "
                         "read `memory.peak` from the corpse and remove it. "
                         "Measured: a timed-out composition reported rss=0 "
                         "charge=0 while its orphaned cgroup held the real "
                         "12,207,062 kB.")
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
    if a.cgroup_path_file:
        # Written BEFORE the command starts, so it is on disk even if this
        # process is killed in the first instant.
        try:
            pathlib.Path(a.cgroup_path_file).write_text(str(cg))
        except OSError as e:
            print(f"d3_guarded_build: could not record cgroup path: {e}",
                  file=sys.stderr)
    shimdir = None
    try:
        (cg / "memory.max").write_text(str(a.max_kb * 1024))
        try:
            (cg / "memory.oom.group").write_text("1")
        except OSError:
            pass
        # Project-local scratch, not the system temp dir. `tempfile.mkdtemp`
        # follows TMPDIR, which the module-proof driver sets project-local --
        # but this wrapper also runs standalone, and then the shim landed in
        # /tmp, which does not survive a host rebuild. Enforce it here too
        # rather than depend on the caller's environment.
        tmproot = ROOT / "temp/d3_tmpdir"
        tmproot.mkdir(parents=True, exist_ok=True)
        shimdir = pathlib.Path(tempfile.mkdtemp(prefix="d3cgx_", dir=str(tmproot)))
        shim = shimdir / "cgexec.py"
        shim.write_text(
            "import os, sys\n"
            "with open(os.path.join(sys.argv[1], 'cgroup.procs'), 'w') as fh:\n"
            "    fh.write(str(os.getpid()))\n"
            "os.execvpe(sys.argv[2], sys.argv[2:], os.environ)\n")
        rc = subprocess.run([sys.executable, str(shim), str(cg)] + cmd).returncode
        peak = int((cg / "memory.peak").read_text().strip())
        ev = dict(l.split() for l in (cg / "memory.events").read_text().splitlines())
        killed = int(ev.get("oom_kill", "0"))
        at_max = int(ev.get("max", "0"))
        at_high = int(ev.get("high", "0"))
        print(f"d3_guarded_build: rc={rc} peak={peak // 1024:,} kB "
              f"cap={a.max_kb:,} kB oom_kill={killed} max={at_max} high={at_high}")
        if at_max and not killed:
            print(f"d3_guarded_build: the cap was BINDING -- {at_max} allocation(s) "
                  f"blocked at memory.max and reclaimed. The command completed, but "
                  f"its peak is the CAP, not its demand; its demand is unmeasured "
                  f"and strictly greater.", file=sys.stderr)
        return 137 if killed else rc
    finally:
        if shimdir is not None:
            shutil.rmtree(shimdir, ignore_errors=True)
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
