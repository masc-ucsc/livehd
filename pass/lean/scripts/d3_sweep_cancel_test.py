#!/usr/bin/env python3
"""Cancellation, timeout and concurrency-isolation tests for `d3_sweep.py`.

Two failures this guards against, both observed rather than imagined:

1. `lake env lean` forks a `lean` worker, so killing the lake parent left nine
   reparented workers holding ~70 GiB with no parent to collect their rows —
   and the sweep LOOKED stopped.  A test that only checked the driver's exit
   would have passed.

2. The first fix used one shared marker path for the orphan sweep, so the first
   driver to exit NORMALLY killed every concurrent driver's live workers.  Two
   sweeps side by side is the normal case here, so that is a data-corruption
   bug, not an edge case.

The assertions are therefore on the machine, and per run id:

  phase 1  a per-module timeout leaves nothing behind
  phase 2  SIGTERM to the driver tears down every worker it owns
  phase 3  SIGTERM to ONE of two concurrent drivers kills only its own workers,
           and the survivor is still running

Run: python3 pass/lean/scripts/d3_sweep_cancel_test.py
Exit 0 iff all three phases hold.
"""

import argparse
import csv
import os
import pathlib
import signal
import subprocess
import sys
import time

ROOT = pathlib.Path(__file__).resolve().parents[3]
SWEEP = ROOT / "pass" / "lean" / "scripts" / "d3_sweep.py"


def mark_for(run_id: str) -> str:
    """The per-invocation marker.  Matching the shared `temp/d3_sweep` prefix
    instead would make this test agree with a driver that cross-kills its
    neighbours — the bug phase 3 exists to catch."""
    return str(ROOT / "temp" / "d3_sweep" / "runs" / run_id)


def residual(mark: str) -> list:
    """Live pids whose command line names THIS run's directory."""
    found = []
    me = os.getpid()
    for entry in pathlib.Path("/proc").iterdir():
        if not entry.name.isdigit():
            continue
        pid = int(entry.name)
        if pid == me:
            continue
        try:
            cl = (entry / "cmdline").read_bytes().replace(b"\0", b" ").decode(errors="replace")
        except (FileNotFoundError, ProcessLookupError, PermissionError, OSError):
            continue
        if mark in cl and "d3_sweep_cancel_test" not in cl:
            found.append((pid, cl[:110]))
    return found


def settle(mark: str, seconds: float = 25.0) -> list:
    deadline = time.time() + seconds
    left = residual(mark)
    while left and time.time() < deadline:
        time.sleep(0.5)
        left = residual(mark)
    return left


def spawn(certs: str, run_id: str, jobs: int, timeout: int, out_name: str, only: str = ""):
    env = dict(os.environ, D3_RUN_ID=run_id)
    # --force: these outputs are throwaway and persist between runs, so without
    # it the second run of this suite would trip the overwrite guard and fail
    # for a reason that has nothing to do with process teardown. The guard
    # itself is tested in d3_runner_test.py.
    cmd = [sys.executable, str(SWEEP), "--certs", certs,
           "--out", str(ROOT / "temp" / "d3_sweep" / out_name),
           "--jobs", str(jobs), "--timeout", str(timeout), "--force"]
    if only:
        cmd += ["--only", only]
    return subprocess.Popen(cmd, cwd=ROOT, env=env,
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def wait_workers(mark: str, want: int, proc, limit: float = 300.0) -> list:
    deadline = time.time() + limit
    while time.time() < deadline:
        w = residual(mark)
        if len(w) >= want:
            return w
        if proc.poll() is not None:
            return residual(mark)
        time.sleep(1)
    return residual(mark)


def force_cleanup(mark: str, procs) -> None:
    """Final teardown for a phase, however it ended.

    A failure branch that only calls `proc.kill()` can itself leak the driver's
    child groups -- killing the driver is exactly the thing that strands lake's
    `lean` workers.  So: terminate the driver NORMALLY first and give its own
    handler a chance to run, then sweep this run's marker as a fallback.
    """
    for pr in procs:
        if pr is None or pr.poll() is not None:
            continue
        try:
            pr.send_signal(signal.SIGTERM)
            pr.wait(timeout=30)
        except Exception:
            try:
                pr.kill()
                pr.wait(timeout=10)
            except Exception:
                pass
    for pid, _ in residual(mark):
        try:
            os.killpg(os.getpgid(pid), signal.SIGKILL)
        except Exception:
            pass


def report(left: list, what: str) -> bool:
    if left:
        print(f"FAIL: {len(left)} process(es) survived {what}:", flush=True)
        for pid, cl in left:
            print(f"  {pid} {cl}", flush=True)
        return False
    return True


def phase_timeout(certs: str, mod: str) -> bool:
    print("== phase 1: per-module timeout ==", flush=True)
    rid = f"test-timeout-{os.getpid()}"
    mark = mark_for(rid)
    out = ROOT / "temp" / "d3_sweep" / "cancel_timeout.tsv"
    if out.exists():
        out.unlink()
    env = dict(os.environ, D3_RUN_ID=rid)
    try:
        subprocess.run(
            [sys.executable, str(SWEEP), "--certs", certs, "--out", str(out),
             "--only", mod, "--jobs", "1", "--timeout", "5", "--force"],
            cwd=ROOT, env=env, capture_output=True, text=True, timeout=400)
        if not report(settle(mark), "the timeout"):
            return False
        # The TSV must EXIST. An earlier version only checked its contents when
        # the file happened to be there, so a run that wrote nothing at all
        # passed this phase.
        if not out.is_file():
            print("FAIL: no TSV was written, so nothing was verified", flush=True)
            return False
        rows = list(csv.DictReader(out.open(), delimiter="\t"))
        if len(rows) != 1:
            print(f"FAIL: expected exactly one row, got {len(rows)}", flush=True)
            return False
        if rows[0].get("module") != mod:
            print(f"FAIL: row is for {rows[0].get('module')!r}, not {mod!r}", flush=True)
            return False
        detail = rows[0].get("detail", "")
        if "timeout" not in detail:
            print(f"FAIL: the run did not time out; detail={detail!r}", flush=True)
            return False
        print(f"ok: timed out ({detail}), one row for {mod}, nothing left behind", flush=True)
        return True
    finally:
        force_cleanup(mark, [])


def phase_sigterm(certs: str) -> bool:
    print("== phase 2: SIGTERM to a single driver ==", flush=True)
    rid = f"test-term-{os.getpid()}"
    mark = mark_for(rid)
    proc = spawn(certs, rid, 3, 900, "cancel_sigterm.tsv")
    try:
        started = wait_workers(mark, 2, proc)
        if len(started) < 2:
            print("FAIL: no workers appeared; nothing was tested", flush=True)
            return False
        print(f"  {len(started)} worker(s) running; SIGTERM to the driver", flush=True)
        proc.send_signal(signal.SIGTERM)
        try:
            proc.wait(timeout=60)
        except subprocess.TimeoutExpired:
            proc.kill()
        if not report(settle(mark), "SIGTERM"):
            return False
        print("ok: SIGTERM tore down every worker", flush=True)
        return True
    finally:
        force_cleanup(mark, [proc])


def phase_isolation(certs_a: str, certs_b: str) -> bool:
    print("== phase 3: two concurrent drivers, one cancelled ==", flush=True)
    ra, rb = f"test-iso-a-{os.getpid()}", f"test-iso-b-{os.getpid()}"
    ma, mb = mark_for(ra), mark_for(rb)
    pa = spawn(certs_a, ra, 3, 900, "cancel_iso_a.tsv")
    pb = spawn(certs_b, rb, 3, 900, "cancel_iso_b.tsv")
    try:
        wa = wait_workers(ma, 2, pa)
        wb = wait_workers(mb, 2, pb)
        if len(wa) < 2 or len(wb) < 2:
            print(f"FAIL: workers did not both start (a={len(wa)}, b={len(wb)})", flush=True)
            return False
        print(f"  A has {len(wa)} worker(s), B has {len(wb)}; SIGTERM to A only", flush=True)

        pa.send_signal(signal.SIGTERM)
        try:
            pa.wait(timeout=60)
        except subprocess.TimeoutExpired:
            pa.kill()
        ok = report(settle(ma), "SIGTERM to driver A")

        # The point of the phase: B must be untouched.  A shared marker broke
        # exactly this -- A's exit swept B's workers away.
        survivors = residual(mb)
        if not survivors:
            print("FAIL: driver B lost every worker when A exited (cross-kill)", flush=True)
            ok = False
        else:
            print(f"ok: driver B still has {len(survivors)} worker(s) after A was cancelled",
                  flush=True)
            if pb.poll() is not None:
                print("FAIL: driver B exited when A was cancelled", flush=True)
                ok = False

        pb.send_signal(signal.SIGTERM)
        try:
            pb.wait(timeout=60)
        except subprocess.TimeoutExpired:
            pb.kill()
        ok = report(settle(mb), "SIGTERM to driver B") and ok
        if ok:
            print("ok: both runs left nothing behind", flush=True)
        return ok
    finally:
        force_cleanup(ma, [pa])
        force_cleanup(mb, [pb])


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--certs",
                    default="/soe/czeng14/projects/livehd-new/generated/vc_sweep2/lean")
    ap.add_argument("--certs-b",
                    default="/soe/czeng14/projects/livehd-new/generated/cva6_vc/lean")
    ap.add_argument("--module", default="intpipe_alu",
                    help="a module large enough that 5s is certain to time out")
    a = ap.parse_args()
    ok = phase_timeout(a.certs, a.module)
    ok = phase_sigterm(a.certs) and ok
    ok = phase_isolation(a.certs, a.certs_b) and ok
    print("CANCEL-TEST", "OK" if ok else "FAILED", flush=True)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
