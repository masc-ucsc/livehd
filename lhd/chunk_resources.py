# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
"""Resource accounting for isolated placement subprocess groups."""

import os
import signal
import subprocess
import sys
import tempfile
import threading
from contextlib import contextmanager
from pathlib import Path


@contextmanager
def memory_group(parent, budget):
    """Enter a delegated Linux cgroup before starting the native process tree."""
    if parent is None:
        yield []
        return
    parent = Path(parent).resolve()
    if sys.platform != "linux" or not (parent / "cgroup.controllers").is_file():
        raise ValueError("a delegated Linux cgroup v2 directory is required")
    group = Path(tempfile.mkdtemp(dir=parent, prefix="livehd-"))
    try:
        (group / "memory.max").write_text(str(budget) if budget else "max")
        if budget and (group / "memory.swap.max").is_file():
            (group / "memory.swap.max").write_text("0")
        yield [sys.executable, str(Path(__file__).resolve()), str(group)]
    finally:
        group.rmdir()


@contextmanager
def monitor(proc, limit):
    """Record aggregate resident bytes across one private process group."""
    stop = threading.Event()
    stats = {"peak_tree_bytes": 0, "limit_bytes": limit}
    errors = []

    def sample():
        try:
            while not stop.is_set() and proc.poll() is None:
                records = subprocess.check_output(
                    ["ps", "-axo", "pgid=,rss="], text=True
                )
                rss = sum(
                    int(fields[1]) * 1024
                    for line in records.splitlines()
                    if len(fields := line.split()) == 2 and int(fields[0]) == proc.pid
                )
                stats["peak_tree_bytes"] = max(stats["peak_tree_bytes"], rss)
                if limit and rss > limit:
                    stats["over_budget"] = True
                    os.killpg(proc.pid, signal.SIGKILL)
                    return
                stop.wait(0.1)
        except (ValueError, OSError, subprocess.SubprocessError) as exc:
            if proc.poll() is None:
                if limit:
                    errors.append(exc)
                    os.killpg(proc.pid, signal.SIGKILL)
                else:
                    stats["peak_tree_bytes"] = None

    watcher = threading.Thread(target=sample, daemon=True)
    watcher.start()
    try:
        yield stats
    finally:
        stop.set()
        watcher.join()
    if errors:
        raise ValueError(f"could not measure process group: {errors[0]}")
    if stats.get("over_budget"):
        raise ValueError(
            f"process group exceeded its {limit} byte limit (sampled {stats['peak_tree_bytes']})"
        )


if __name__ == "__main__":
    (Path(sys.argv[1]) / "cgroup.procs").write_text(str(os.getpid()))
    os.execvp(sys.argv[2], sys.argv[2:])
