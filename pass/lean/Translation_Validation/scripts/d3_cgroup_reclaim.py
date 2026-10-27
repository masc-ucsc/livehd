#!/usr/bin/env python3
"""Reclaim composition cgroups this worktree's runs recorded and left behind.

OWNED PATHS ONLY, AND ONLY THE RECORDED ONES.

`d3_module_proof.py` writes the composition's cgroup path into
`<out-dir>/composition.cgroup` before the build starts, precisely so a corpse
can be found afterwards. This reclaims those directories and nothing else: the
candidate set is read from those files under the run tree given on the command
line, never from a scan of `/sys/fs/cgroup`. A directory that no record names
is not this run's to remove.

WHAT IT REFUSES TO DO.

  * It does not remove a cgroup that still has processes in it. A corpse is
    `populated 0` with an empty `cgroup.procs`; anything else is live, and
    killing it is not cleanup.
  * It does not report success from an exit status. `rmdir` on a cgroup can
    fail for reasons that persist -- a child cgroup, a lingering charge -- so
    every path is re-checked afterwards and anything still present is reported
    as a RESIDUAL with the reason, not silently dropped.
  * It does not retry forever. Bounded attempts with a short pause, then the
    residual is recorded and the next path is tried.

Usage:
  d3_cgroup_reclaim.py <run-dir> [<run-dir> ...] [--dry-run] [--attempts N]
"""
import argparse
import pathlib
import sys
import time


def candidates(roots):
    """Recorded composition cgroup paths, with the record that named each."""
    seen = {}
    for r in roots:
        root = pathlib.Path(r)
        if not root.exists():
            print(f"  note   no such run directory: {root}", file=sys.stderr)
            continue
        for rec in sorted(root.rglob("composition.cgroup")):
            try:
                p = pathlib.Path(rec.read_text().strip())
            except OSError as e:
                print(f"  note   unreadable record {rec}: {e}", file=sys.stderr)
                continue
            if p and str(p).startswith("/sys/fs/cgroup/") and p.name.startswith("d3build_"):
                seen.setdefault(p, rec)
            else:
                print(f"  note   record {rec} names {p!s}, which is not an owned "
                      f"d3build cgroup -- skipped", file=sys.stderr)
    return seen


def state(p: pathlib.Path):
    """(exists, populated, nproc) for a cgroup directory."""
    if not p.is_dir():
        return False, None, None
    pop = None
    try:
        for line in (p / "cgroup.events").read_text().splitlines():
            if line.startswith("populated "):
                pop = int(line.split()[1])
    except OSError:
        pass
    try:
        nproc = len([x for x in (p / "cgroup.procs").read_text().split() if x])
    except OSError:
        nproc = None
    return True, pop, nproc


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("runs", nargs="+")
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--attempts", type=int, default=3)
    a = ap.parse_args()

    found = candidates(a.runs)
    print(f"d3_cgroup_reclaim: {len(found)} recorded composition cgroup(s)")
    reclaimed, absent, live, residual = [], [], [], []
    for p, rec in sorted(found.items()):
        exists, pop, nproc = state(p)
        if not exists:
            absent.append(p)
            print(f"  gone   {p.name}  (already reclaimed; named by {rec})")
            continue
        if pop or nproc:
            live.append((p, pop, nproc))
            print(f"  LIVE   {p.name}  populated={pop} procs={nproc} -- NOT removed; "
                  f"a populated cgroup is not a corpse")
            continue
        if a.dry_run:
            print(f"  would  {p.name}  populated={pop} procs={nproc}")
            continue
        why = ""
        for attempt in range(1, a.attempts + 1):
            try:
                p.rmdir()
                break
            except OSError as e:
                why = f"{e.__class__.__name__}: {e}"
                if attempt < a.attempts:
                    time.sleep(0.2)
        # RE-CHECKED, not trusted. The call returning is not the evidence.
        if p.is_dir():
            residual.append((p, why or "still present after rmdir"))
            print(f"  RESIDUAL {p.name}  {why or 'still present after rmdir'}")
        else:
            reclaimed.append(p)
            print(f"  ok     {p.name}  reclaimed")

    print(f"d3_cgroup_reclaim: reclaimed={len(reclaimed)} already-gone={len(absent)} "
          f"live-skipped={len(live)} residual={len(residual)}")
    return 1 if residual else 0


if __name__ == "__main__":
    raise SystemExit(main())
