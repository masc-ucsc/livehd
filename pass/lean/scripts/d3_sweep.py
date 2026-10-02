#!/usr/bin/env python3
"""Direction 3 sweep: per-module gates from DesignCert to a checked fast simulator.

The point of this script is that "supported" is not one bit.  A module can
elaborate its certificate and have `compileDesign` refuse it; the compiler can
accept and the reifier fail to fold some constructor; the reified `def` can
typecheck and then disagree with the interpreter on its first stimulus.  Each of
those is a different engineering state and a different next action, so each gets
its own column and a module is only ever credited with the gates it actually
passed.

GATES, in the order they are reached:

  cert       the emitted DesignCert file exists and names `<m>_designCert`
  compile    `compileDesign` returned `.ok` -- observed at ELABORATION time,
             inside `reify_design`.  This is NOT the kernel-checked claim; see
             `--native` for `compilesOk = true by native_decide`.
  reify      the reifier folded the whole residual into a Lean `def`
  typecheck  that file elaborated with no error
  sim        the reified def executed on one stimulus
  agree      reified def = `denoteResidual` on N stimuli (differential, not proof)
  proof      per-design translation proof.  NOT ATTEMPTED by this pass -- the
             operand-read walk covers `rand` only, so reporting anything but
             `na` here would be a claim the code does not support.

A module's verdict is the LAST gate it passed, never the gate the sweep hoped
for.  Failures are classified by the FIRST error the log shows, because a later
error is usually a consequence of the first.

Usage:
  d3_sweep.py --certs DIR [DIR...] --out TSV [--jobs N] [--timeout S]
              [--samples N] [--limit N] [--only REGEX] [--native]
"""

import argparse
import atexit
import concurrent.futures
import csv
import errno
import hashlib
import json
import os
import pathlib
import re
import shutil
import uuid
import signal
import subprocess
import sys
import threading
import time

ROOT = pathlib.Path(__file__).resolve().parents[3]
LEAN_DIR = ROOT / "formal" / "lean"
LAKE = os.environ.get("LAKE", "/mada/users/czeng14/.elan/bin/lake")
# Filled by `main` from `resolve_lean()`.  Empty means "go through lake env",
# which is what `--via-lake` selects and what a failed resolution falls back to.
LEAN_BIN, LEAN_ENV, LEAN_ENV_DIGEST = "", {}, ""
# Set by `main` when cgroup enforcement is active; None means sampled-only.
CGROUP_BASE, CGROUP_MAX_KB = None, 0

# `checker` sits BETWEEN sim and agree deliberately.  An agreement result from a
# checker that cannot reject a wrong answer is not weak evidence, it is no
# evidence, so it must not be able to earn a verdict.  Ordering it here means
# `verdict()` stops at `sim` whenever the self-test fails, whatever `agree` says.
GATES = ["cert", "compile", "reify", "typecheck", "sim", "checker", "agree", "proof"]

# Outcomes produced by the SCHEDULER rather than by the design under test.  Each
# means "no gate was judged", which is a different thing from "no gate passed":
# their rows carry all-zero gates, so recomputing a verdict from those zeros
# would relabel a memory decision as a compilation result.  They are also
# NONTERMINAL -- `--resume` must run them again rather than treat them as done.
#
#   deferred    the scheduler declined to START it (threshold, or soft cap)
#   rss_killed  it was started and then TERMINATED by the hard aggregate limit
#   timeout     it was started and ran past `--timeout`, so it was killed
#
# `timeout` is the one that was learned the expensive way.  `scoreboard_gate`
# exceeded 3600 s, and because a timeout was not in this set its verdict was
# recomputed from its own all-zero gates into `cert` -- a statement about the
# DESIGN ("it passed only the first gate") that nothing had established -- and
# `run_status=done` made the row terminal, so `--resume` would have reused a
# timeout as finished work.
NONTERMINAL_SCHEDULER_STATUSES = {"deferred", "rss_killed", "timeout"}

# Per-probe failures that invalidate the WHOLE run rather than one row.
#
# A cgroup that cannot be removed keeps its memory charge, so the budget every
# LATER probe is scheduled against is already wrong.  Reporting one
# `runner_error` row and carrying on would let the rest of a cohort run under a
# silently weakened limit while each row still receives an ordinary verdict --
# the same shape of hazard as artifact drift, and it gets the same treatment:
# stop launching, cancel what is queued, tear down what is live, mark the
# sidecar, and exit nonzero.  Unlike drift, the failing row IS kept, because it
# is the diagnosis.
FATAL_RUN_STATUSES = {"fatal_cleanup"}

# `--prove`: run the last-mile proof probe instead of the plain sim probe.
# OFF by default, because the proof probe imports Mathlib (~6.9 GB against the
# sim probe's ~1.5 GB) and because `proof` must stay `na` for every design not
# actually checked -- an unattempted gate is not a failed one, and neither is a
# passed one.
PROVE = False

# Which reifier BOTH stages use. `legacy` is `reify_design` + `prove_reified`,
# unchanged and still the default. `named` is `reify_design_named` +
# `prove_reified_incr`, where the executable and proof stages share one model.
#
# One setting drives both stages ON PURPOSE. Letting them differ would mean
# simulating one function and proving another, and the row would credit a
# theorem that says nothing about what ran.
REIFIER = "legacy"
# Segment size for the incremental walk, passed to the PROOF probe only (0 = off).
PROOF_SEGMENT = 0

# ---------------------------------------------------------------------------
# Child lifetime.
#
# `lake env lean` forks a `lean` worker, so killing the lake parent leaves the
# worker running.  Measured: stopping a sweep left nine reparented lean
# processes holding ~70 GiB with no parent to collect their rows.  Every child
# therefore gets its OWN SESSION (start_new_session), and every teardown path --
# timeout, exception, SIGINT, SIGTERM, interpreter exit -- kills the whole
# process GROUP rather than the one pid we happen to hold.
# ---------------------------------------------------------------------------
_GROUPS: set[int] = set()
_GROUPS_LOCK = threading.Lock()
_SHUTDOWN = threading.Event()

# Every probe THIS INVOCATION runs lives under its own run directory, and no
# other invocation's does.  The final teardown scans /proc for that path,
# because tracking groups is not enough on its own: a worker thread can be
# inside Popen when the signal lands, so a child may be born AFTER the handler
# has already walked the tracked set.
#
# The marker MUST be per-invocation.  A shared `temp/d3_sweep` marker makes the
# first driver to finish normally kill the live workers of every concurrent
# driver -- its atexit teardown cannot tell their probes from its own.  Two
# sweeps running side by side is the normal case here, so the run id is not a
# convenience.
RUN_ID = os.environ.get("D3_RUN_ID") or f"{time.strftime('%Y%m%d-%H%M%S')}-{os.getpid()}"
RUN_DIR = ROOT / "temp" / "d3_sweep" / "runs" / RUN_ID
_MARK = str(RUN_DIR)
# The cgroup-join wrapper, written per run beside the probes it launches.
CGEXEC_PATH = RUN_DIR / "cgexec.py"


def _killpg(pgid: int, sig: int) -> None:
    try:
        os.killpg(pgid, sig)
    except (ProcessLookupError, PermissionError):
        pass
    except OSError as e:
        if e.errno != errno.ESRCH:
            raise


def _reap(pgid: int, grace: float = 3.0) -> None:
    """TERM the group, give it `grace`, then KILL whatever is left."""
    _killpg(pgid, signal.SIGTERM)
    deadline = time.time() + grace
    while time.time() < deadline:
        try:
            os.killpg(pgid, 0)
        except OSError:
            return
        time.sleep(0.1)
    _killpg(pgid, signal.SIGKILL)


def _reap_all() -> None:
    with _GROUPS_LOCK:
        groups = list(_GROUPS)
        _GROUPS.clear()
    for g in groups:
        _reap(g, grace=1.0)


def _marked_pids() -> list:
    """Live pids whose command line names THIS worktree's sweep directory.

    Deliberately not a name match on `lean` or `lake`: another branch's sweep
    must not be touched, and the path is what distinguishes them.
    """
    out = []
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
        # `d3_sweep.py` excludes this driver itself; the run id already excludes
        # every other invocation, including a concurrent one.
        if _MARK in cl and "d3_sweep.py" not in cl and "d3_sweep_cancel_test" not in cl:
            out.append(pid)
    return out


def _sweep_orphans(deadline_s: float = 12.0) -> None:
    """Last resort: kill anything left that carries our marker.

    Runs after `_reap_all`, and repeatedly, because a child started during the
    handler would otherwise outlive the driver -- which is the exact failure
    this whole mechanism exists to prevent (nine reparented `lean` workers,
    ~70 GiB, no parent to collect them).
    """
    end = time.time() + deadline_s
    while time.time() < end:
        pids = _marked_pids()
        if not pids:
            return
        for pid in pids:
            try:
                _killpg(os.getpgid(pid), signal.SIGTERM)
            except (ProcessLookupError, PermissionError, OSError):
                pass
        time.sleep(0.5)
        pids = _marked_pids()
        if not pids:
            return
        for pid in pids:
            try:
                _killpg(os.getpgid(pid), signal.SIGKILL)
            except (ProcessLookupError, PermissionError, OSError):
                pass
        time.sleep(0.5)


def _teardown() -> None:
    _reap_all()
    _sweep_orphans()


atexit.register(_teardown)


def _on_signal(signum, _frame):
    # Stop handing out new children FIRST; otherwise a worker thread mid-launch
    # produces an orphan after the sweep below has already run.
    _SHUTDOWN.set()
    _teardown()
    # atexit does not run on a default-disposition signal death, and the work is
    # already done, so exit explicitly with the conventional code.
    os._exit(128 + signum)


for _s in (signal.SIGINT, signal.SIGTERM, signal.SIGHUP):
    signal.signal(_s, _on_signal)


# ---------------------------------------------------------------------------
# Memory budget.
#
# ORDERING IS NOT A CAP.  Running cheapest-first decides what runs when; it does
# nothing to stop the next target from exceeding the budget on its own, and
# `--defer-over-rss-kb` only acts on RSS values MEASURED IN AN EARLIER RUN.  A
# design whose cost moved, or one never measured, walks straight past both.
#
# So the live run is also sampled.  This machine is shared, so there are TWO
# limits, and they do different jobs:
#
#   --max-aggregate-rss-kb  SOFT.  Stops handing out NEW work.  Never KILLS a
#                           running worker: that would spend the memory and
#                           discard the row, the worst of both.  With `--prove`,
#                           a proof stage that has not started yet counts as new
#                           work and is suppressed, so the cap can affect a
#                           target already in flight -- its executable gates are
#                           kept and `proof` is `na`.
#   --kill-over-rss-kb      HARD, opt-in, default off.  TERMINATES the running
#                           probe.  The soft cap cannot help against one probe
#                           that grows past the budget by itself, and the two
#                           heaviest CORE-ET modules are exactly that case.
#                           Paying a discarded row is the point: on a shared
#                           machine, overrunning is someone else's problem.
# ---------------------------------------------------------------------------
_RSS_STOP = threading.Event()
# The HARD limit.  `_RSS_STOP` only declines to START more work, which cannot
# help against a single probe that grows past the budget on its own -- and the
# two heaviest CORE-ET modules are exactly that case.  `_RSS_KILL` terminates
# the running process group.  It is opt-in because killing a probe spends its
# memory and time and keeps no row; it is worth it only when exceeding the
# budget would hurt someone else on a shared machine.
_RSS_KILL = threading.Event()
# Set once the executor has drained, so the sampler can retire instead of
# sampling an empty process tree forever.
_WORK_DONE = threading.Event()
_RSS_PEAK = {"kb": 0, "cap_kb": 0, "tripped_at_kb": 0, "kill_kb": 0, "killed_at_kb": 0}
_RSS_LOCK = threading.Lock()
_PAGE_KB = os.sysconf("SC_PAGE_SIZE") // 1024


def _rss_kb_of(pid: int) -> int:
    try:
        return int(pathlib.Path(f"/proc/{pid}/statm").read_text().split()[1]) * _PAGE_KB
    except (FileNotFoundError, ProcessLookupError, PermissionError, OSError,
            IndexError, ValueError):
        return 0


def _descendants(root: int) -> set:
    """Every live descendant of `root`, by walking real ppid links.

    The marker scan alone is NOT enough for a memory budget.  `_marked_pids`
    matches on the command line, which catches `/usr/bin/time`, `lake` and the
    `lean` worker because the probe path is an argument to each -- but a process
    one of them forks need not mention that path at all, and an uncounted child
    is memory the cap cannot see.  Teardown can afford a command-line match
    because it only has to kill what it can find and then rescan; a budget
    cannot, because what it misses is spent either way.
    """
    kids = {}
    for entry in pathlib.Path("/proc").iterdir():
        if not entry.name.isdigit():
            continue
        try:
            stat = (entry / "stat").read_text()
            # comm can contain spaces and parentheses, so ppid is read from
            # AFTER the last ')' rather than by splitting the whole line.
            ppid = int(stat[stat.rindex(")") + 1:].split()[1])
        except (FileNotFoundError, ProcessLookupError, PermissionError, OSError,
                ValueError, IndexError):
            continue
        kids.setdefault(ppid, []).append(int(entry.name))
    out, stack = set(), [root]
    while stack:
        for c in kids.get(stack.pop(), []):
            if c not in out:
                out.add(c)
                stack.append(c)
    return out


# TEST SEAM.  A sequence of aggregate readings, consumed one per sample, so a
# monitor regression can pin an exact ORDER of crossings rather than racing a
# real process.  It replaces the MEASUREMENT, so a manifest run refuses outright
# when it is set -- the same rule as `D3_ARTIFACT_DIR`.
# Trip the budget BETWEEN the sim and proof stages, which is the one window a
# scripted RSS sequence cannot target reliably: the sampler runs on its own clock
# and the gap between the stages is microseconds.
#
#   `<module>`        both caps -- the HARD trip (`_RSS_KILL` + `_RSS_STOP`)
#   `<module>:soft`   the SOFT cap only (`_RSS_STOP`, `_RSS_KILL` CLEAR)
#
# The soft-only mode exists because the two produce different reasons and only
# the hard one was covered; a regression that could not tell them apart would
# not notice the soft path silently acquiring the hard path's wording.
_TEST_KILL_BETWEEN_STAGES = os.environ.get("D3_TEST_KILL_BETWEEN_STAGES") or ""
_TEST_KILL_SOFT_ONLY = _TEST_KILL_BETWEEN_STAGES.endswith(":soft")
if _TEST_KILL_SOFT_ONLY:
    _TEST_KILL_BETWEEN_STAGES = _TEST_KILL_BETWEEN_STAGES[: -len(":soft")]

_TEST_RSS_SEQ = [int(x) for x in
                 (os.environ.get("D3_TEST_RSS_SEQ") or "").replace(" ", "").split(",")
                 if x] or None


def _aggregate_rss_kb() -> int:
    if _TEST_RSS_SEQ:
        # The last value repeats, so a sequence does not have to predict exactly
        # how many samples a run will take.
        return _TEST_RSS_SEQ.pop(0) if len(_TEST_RSS_SEQ) > 1 else _TEST_RSS_SEQ[0]
    """Resident total of THIS run's process tree, plus any marked stragglers.

    The union matters in both directions: the ppid walk catches a grandchild
    that never names the probe path, and the marker catches a worker that has
    been REPARENTED away from this driver -- which is precisely the state the
    nine orphaned `lean` processes were in.  Scoping by this run's own marker
    also means a concurrent sweep's workers are not charged to this budget, and
    that this run cannot hide behind someone else's idle machine.
    """
    pids = _descendants(os.getpid()) | set(_marked_pids())
    return _rss_kb_of(os.getpid()) + sum(_rss_kb_of(pid) for pid in pids)



# ---------------------------------------------------------------------------
# cgroup v2 enforcement.
#
# The sampled caps are ADVISORY: they bound sustained memory, not instantaneous
# peaks.  Measured on real runs at a 5 s interval against a 19,000,000 kB limit,
# `frontend` peaked 161,996 kB OVER and was never killed, while `issue_stage` was
# caught 20,996 kB over -- same limit, opposite outcomes, decided by sampling
# phase alone.
#
# cgroup v2 `memory.max` is enforced by the kernel continuously, and
# `memory.peak` reports the true peak rather than a sampled lower bound.  Probed
# on this machine with a tiny allocator: OOM-killed at exactly the limit with
# exit 137, `memory.events` carrying an `oom_kill` counter that distinguishes it
# from any other kill, and `rmdir` succeeding with zero processes left.
#
# It is OPT-IN and falls back to sampling, because delegation is a property of
# the host: another machine may mount cgroup v2 without granting the memory
# controller to a user slice, and a sweep must still run there.
# ---------------------------------------------------------------------------
def cgroup_base() -> "pathlib.Path | None":
    """The delegated cgroup directory probes can actually be confined in.

    THE PREFLIGHT IS THE RUNTIME PATH.  It instantiates the same `Cgroup` class
    with the same configuration (exclusive naming, memory.max, swap, oom.group)
    and launches the same `CGEXEC_SRC` wrapper with the same Popen shape that
    `run_one` uses, against /bin/true.  An earlier version used a separate
    hand-rolled join probe, and the two DRIFTED: that probe succeeded while the
    real wrapper got PermissionError on `cgroup.procs`, so detection reported
    AVAILABLE and every probe then died before doing any work.  Duplicated
    preflight logic was the defect; sharing the code is the fix.

    `Cgroup` and `CGEXEC_SRC` are defined below this function.  That is legal:
    module-level names resolve at CALL time, and nothing calls this during module
    initialisation.

    A candidate is accepted only when ALL of these hold:
      * `memory` delegated AND present in `subtree_control`
      * the `Cgroup` constructor succeeds -- which already requires exclusive
        creation, a writable `memory.max` and a writable `memory.oom.group`
      * the wrapper exits 0: a real process MIGRATED IN and exec'd
      * accounting is readable and reports no OOM for /bin/true
      * `destroy()` returns True

    Acceptance happens OUTSIDE the try/finally, so a cleanup failure REJECTS the
    candidate instead of being discovered after it was already returned.
    """
    try:
        own = pathlib.Path("/proc/self/cgroup").read_text().strip()
    except OSError:
        return None
    if not own.startswith("0::"):
        return None                      # not a unified (v2) hierarchy
    for cand in (pathlib.Path("/sys/fs/cgroup") / own[3:].lstrip("/"),
                 pathlib.Path(f"/sys/fs/cgroup/user.slice/user-{os.getuid()}.slice"
                              f"/user@{os.getuid()}.service")):
        try:
            if "memory" not in (cand / "cgroup.controllers").read_text().split():
                continue
            if "memory" not in (cand / "cgroup.subtree_control").read_text().split():
                continue
        except OSError:
            continue

        cg, joined, removed = None, False, False
        try:
            cg = Cgroup(cand, f"detect-{uuid.uuid4().hex}", 64 * 1024)
            CGEXEC_PATH.parent.mkdir(parents=True, exist_ok=True)
            CGEXEC_PATH.write_text(CGEXEC_SRC)
            r = subprocess.run(
                [sys.executable, str(CGEXEC_PATH), str(cg.path), "/bin/true"],
                capture_output=True, text=True, timeout=30,
                start_new_session=True)          # same shape as run_group
            joined = (r.returncode == 0
                      and cg.peak_kb() is not None
                      and cg.oom_killed() is False)
        except (OSError, ValueError, subprocess.SubprocessError):
            joined = False
        finally:
            removed = cg.destroy() if cg is not None else True

        if joined and removed:
            return cand
    return None


# The probe joins its cgroup in a tiny wrapper process, NOT in a `preexec_fn`.
# CPython documents `preexec_fn` as unsafe in the presence of threads -- it runs
# arbitrary Python between fork and exec, where only async-signal-safe work is
# legal, and this runner always has threads (the RSS sampler plus a
# ThreadPoolExecutor).  The wrapper is a fresh single-threaded process, so the
# same two operations are safe, and `execvpe` replaces it immediately: the probe
# is inside the cgroup before it executes a single instruction.
CGEXEC_SRC = '''#!/usr/bin/env python3
"""Join a cgroup, then become the real command.  Single-threaded by construction."""
import os, sys
with open(os.path.join(sys.argv[1], "cgroup.procs"), "w") as fh:
    fh.write(str(os.getpid()))
os.execvpe(sys.argv[2], sys.argv[2:], os.environ)
'''


class Cgroup:
    """One cgroup per probe, so `memory.peak` is that probe's own peak."""

    def __init__(self, base: pathlib.Path, name: str, max_kb: int):
        # Hashed: a module name can carry characters a cgroup path must not, and
        # a collision would merge two probes' accounting into one.
        # EXCLUSIVE creation, with a unique suffix. `exist_ok=True` would let a
        # probe reuse a directory somebody else made, mixing two probes' memory
        # accounting into one `memory.peak`.
        safe = hashlib.sha256(name.encode()).hexdigest()[:32]
        self.path = base / f"d3-{safe}-{uuid.uuid4().hex[:8]}"
        self.path.mkdir()
        try:
            if max_kb:
                (self.path / "memory.max").write_text(str(max_kb * 1024))
            try:
                (self.path / "memory.swap.max").write_text("0")   # no swap escape
            except OSError:
                pass            # optional: swap may be off host-wide already
            # REQUIRED, not best-effort: the claim "a multi-process probe is
            # killed as a unit" rests on it, so a host that cannot set it is a
            # host where this mechanism does not apply. Swallowing the failure
            # would keep the claim while losing the property.
            (self.path / "memory.oom.group").write_text("1")
            # The baseline for the DELTA. An absolute count would misread a
            # pre-existing event, and exit 137 alone proves nothing: any SIGKILL
            # produces it, including our own teardown and an external `kill`.
            self.oom_before = self._oom_count()
            if self.oom_before is None:
                raise OSError("memory.events is unreadable, so an OOM could not "
                              "be distinguished from any other kill")
        except Exception:
            self.destroy()          # never leave a partially configured child
            raise

    def _oom_count(self):
        """The kernel's oom_kill counter, or None if it cannot be read.

        None is NOT zero. Returning 0 on a read error would report "no OOM
        happened" when the truth is "nothing is known", and that difference
        decides whether a row is `rss_killed` or a design result.
        """
        try:
            ev = dict(l.split() for l in
                      (self.path / "memory.events").read_text().splitlines() if l)
            return int(ev.get("oom_kill", 0))
        except (OSError, ValueError):
            return None

    def peak_kb(self):
        """cgroup-accounted peak memory in kB, or None if unreadable.

        NOT the same quantity as `/usr/bin/time`'s max RSS: this is the cgroup's
        own accounting of the whole subtree, including page cache charged to it.
        The results column is called `max_rss_kb` for schema compatibility, so
        the sidecar records which quantity actually filled it.
        """
        for f in ("memory.peak", "memory.max_usage_in_bytes"):
            try:
                return int((self.path / f).read_text().strip()) // 1024
            except (OSError, ValueError):
                continue
        return None

    def oom_killed(self):
        """True / False / None(unknown) from a DELTA in the kernel's counter.

        A delta is the only sound evidence: exit 137 is produced by ANY SIGKILL,
        and an absolute count could be inherited from an earlier event.
        """
        now = self._oom_count()
        if now is None:
            return None
        return now > self.oom_before

    def destroy(self) -> bool:
        """Kill anything left, remove the cgroup, and SAY whether it worked.

        Returns False on a leak rather than swallowing it: a cgroup that cannot
        be removed still holds its memory charge, and a sweep that quietly
        accumulates them would drift away from the budget it claims to keep.
        """
        try:
            (self.path / "cgroup.kill").write_text("1")   # v2: kill the subtree
        except OSError:
            pass
        for _ in range(50):
            try:
                self.path.rmdir()
                return True
            except FileNotFoundError:
                return True
            except OSError:
                time.sleep(0.1)
        return False


def _nonneg_int(text: str) -> int:
    """A memory budget in kB.  Zero means "no limit"; negative means nothing.

    Without this a negative value is TRUTHY, so it passed every `if limit:` guard
    and would have reached `memory.max` as a nonsensical write.
    """
    try:
        v = int(text)
    except ValueError:
        raise argparse.ArgumentTypeError(f"{text!r} is not an integer number of kB")
    if v < 0:
        raise argparse.ArgumentTypeError(
            f"{v} is negative; a memory budget cannot be. Use 0 to disable.")
    return v


def _positive_finite(text: str) -> float:
    """A sample interval must be a finite positive number.

    Rejected rather than clamped: zero or a negative turns the sampler into a
    busy loop that pins a core and floods /proc, and NaN makes every `sleep`
    comparison false in ways that are hard to attribute later.  `inf` would
    sleep forever, silently disabling the limit the caller asked for -- the
    worst of the four, because the run looks guarded and is not.
    """
    try:
        v = float(text)
    except ValueError:
        raise argparse.ArgumentTypeError(f"{text!r} is not a number")
    if v != v:
        raise argparse.ArgumentTypeError("NaN is not a usable sample interval")
    if v == float("inf") or v == float("-inf"):
        raise argparse.ArgumentTypeError(
            "an infinite sample interval would disable the limit while the run "
            "still appears to be guarded")
    if v <= 0:
        raise argparse.ArgumentTypeError(
            f"{v} is not positive; zero or negative turns the sampler into a busy "
            f"loop rather than sampling faster")
    return v


def _rss_monitor(cap_kb: int, interval: float = 5.0, kill_kb: int = 0) -> None:
    """Sample the aggregate and act on the two caps.

    BOTH CAPS ARE SAMPLED AND THEREFORE ADVISORY.  They bound SUSTAINED memory,
    not instantaneous peaks: between two samples a probe can rise above the hard
    limit and fall back, and nothing observes it.  Measured on real runs at a 5 s
    interval and a 19,000,000 kB limit -- `frontend` peaked at 19,161,996 kB
    (161,996 kB over) and was never killed, because the sampler's highest
    observation was 18,977,904 kB; `issue_stage` was caught at 19,020,996 kB,
    only 20,996 kB over.  Same limit, same interval, opposite outcomes, decided
    purely by sampling phase.

    A smaller interval narrows the window but cannot close it.  Only a kernel
    mechanism (cgroup v2 `memory.max`) enforces continuously.
    """
    # NO environment override.  `D3_RSS_INTERVAL` used to be honoured here while
    # the sidecar recorded `--rss-sample-seconds`, so a run could sample at 0.05 s
    # and certify 5.0 -- and the scheduler tests, which set that variable, were
    # certifying the mismatch. The interval now comes from the CLI alone, so the
    # recorded value IS the effective value. Tests pass --rss-sample-seconds.
    while not _SHUTDOWN.is_set() and not _RSS_KILL.is_set():
        total = _aggregate_rss_kb()
        with _RSS_LOCK:
            if total > _RSS_PEAK["kb"]:
                _RSS_PEAK["kb"] = total
        if kill_kb and total >= kill_kb:
            with _RSS_LOCK:
                _RSS_PEAK["killed_at_kb"] = total
            print(f"d3_sweep: AGGREGATE RSS {total} kB reached the HARD limit "
                  f"{kill_kb} kB -- terminating the running probe(s). The row is "
                  f"recorded as rss_killed, a SCHEDULING outcome and not a gate "
                  f"failure.", file=sys.stderr)
            _RSS_KILL.set()
            _RSS_STOP.set()
            _reap_all()
            return
        if _RSS_STOP.is_set() and not kill_kb:
            # Only when NO hard limit is armed.  Returning here unconditionally
            # is what let the SOFT cap silently end the HARD monitor: the cap
            # trips, the next sample sees `_RSS_STOP` already set, the thread
            # returns, and the probe that the hard limit exists to stop then
            # grows unwatched.  Measured once, for real: the cap tripped at
            # 18,002,680 kB and `vpu_mask` went on to 19,048,836 kB, past a
            # 19,000,000 kB hard limit, with nothing sampling.
            return
        if _WORK_DONE.is_set():
            # Nothing left to watch.  Without this the thread would keep
            # sampling after the executor drained, which is harmless but means
            # the sidecar's peak can keep moving after the last row is in.
            return
        if cap_kb and total >= cap_kb and not _RSS_STOP.is_set():
            # LATCHED on `_RSS_STOP` being unset, so this fires exactly ONCE.
            # Without the latch the branch re-ran on every sample after the
            # crossing: one real run logged ~300 identical lines, and each pass
            # overwrote `tripped_at_kb`, so the sidecar recorded the LAST
            # crossing when the FIRST is the meaningful one.
            with _RSS_LOCK:
                _RSS_PEAK["tripped_at_kb"] = total
            print(f"d3_sweep: AGGREGATE RSS {total} kB reached the cap {cap_kb} kB -- "
                  f"no further targets will be launched; the EXECUTABLE stage of the "
                  f"running one may finish, but a --prove proof stage that has not "
                  f"started yet is suppressed. (logged once; the sampler keeps "
                  f"watching)", file=sys.stderr)
            _RSS_STOP.set()
            # Deliberately NOT a return when a hard limit is armed: the probe
            # already running is the one that can still cross it, and it is
            # precisely the case the hard limit was added for.
            if not kill_kb:
                return
        time.sleep(interval)


# Everything whose value can change what `lean` loads or links.  Used for the
# sidecar digest; the FULL snapshot is what the probe actually runs under, but
# digesting all of it would make the digest move for reasons like `SSH_TTY`.
_ENV_KEYS = re.compile(r"^(LEAN|LAKE|ELAN)")


def _lean_env_subset(env: dict) -> dict:
    return {k: v for k, v in env.items()
            if _ENV_KEYS.match(k) or k in ("PATH", "LD_LIBRARY_PATH")}


def resolve_lean():
    """The exact `lean` binary and Lake's COMPLETE environment, resolved once.

    `lake env lean` FORKS: a `lake` parent sits at ~811,480 kB resident for the
    whole probe, long after its only job -- establishing the environment
    described below -- is finished.  At jobs=1 that is 811 MB of a 20 GB budget
    held per probe by a process that has nothing left to do, and it is the
    difference between the largest module fitting and not fitting.

    Resolution goes THROUGH Lake and takes the WHOLE environment, because Lake
    sets far more than `LEAN_PATH`: measured here it adds 17 variables (LEAN,
    LEAN_SYSROOT, LEAN_AR, LEAN_SRC_PATH, LEAN_GITHASH, LEAN_RECURSION_COUNT,
    LAKE_*, ELAN_*) and it MODIFIES both `PATH` and `LD_LIBRARY_PATH`.
    Reconstructing only `LEAN_PATH` would have run Lean against different shared
    libraries -- that is changing the execution environment, not removing a
    process.  The snapshot is taken once and passed verbatim, so the only thing
    that disappears is Lake's resident parent.

    The binary comes from the snapshot's own `LEAN`, falling back to `which lean`
    INSIDE that environment; it never picks a `lean` off the ambient PATH.

    Measured on three real certificates the output is byte-identical both ways,
    and the direct call is also slightly faster.
    """
    r = subprocess.run([LAKE, "env", "env", "-0"], cwd=LEAN_DIR,
                       capture_output=True, text=True)
    if r.returncode != 0:
        return "", {}, ""
    env = {}
    for item in r.stdout.split("\0"):
        if not item:
            continue
        k, _, v = item.partition("=")
        if k:
            env[k] = v
    binp = env.get("LEAN") or shutil.which("lean", path=env.get("PATH", ""))
    if not binp:
        return "", {}, ""
    sub = _lean_env_subset(env)
    digest = hashlib.sha256(
        "\n".join(f"{k}={sub[k]}" for k in sorted(sub)).encode()).hexdigest()
    return binp, env, digest


def run_group(cmd, cwd, timeout, env=None):
    """Run `cmd` in its own session; return (combined output, returncode).

    Returncode 124 means the timeout fired, matching `timeout(1)` so the
    classifier has one convention to read.
    """
    if _SHUTDOWN.is_set():
        return "", 143
    p = subprocess.Popen(
        cmd, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        text=True, start_new_session=True, env=env,
    )
    pgid = os.getpgid(p.pid)
    with _GROUPS_LOCK:
        _GROUPS.add(pgid)
    # Lost the race with a signal: the handler walked the tracked set before
    # this group joined it, so retire it here instead of leaking it.
    if _SHUTDOWN.is_set():
        _reap(pgid, grace=0.5)
        with _GROUPS_LOCK:
            _GROUPS.discard(pgid)
        return "", 143
    try:
        out, _ = p.communicate(timeout=timeout)
        return out or "", p.returncode
    except subprocess.TimeoutExpired:
        _reap(pgid)
        try:
            out, _ = p.communicate(timeout=10)
        except subprocess.TimeoutExpired:
            out = ""
        return out or "", 124
    finally:
        _reap(pgid, grace=0.5)
        with _GROUPS_LOCK:
            _GROUPS.discard(pgid)

PROBE_HEAD = """import LeanSemanticPrimitives.Compiler.ReifyGen
import LeanSemanticPrimitives.Compiler.D3Harness
"""

PROBE_TAIL = """
reify_design {m}_designCert as d3_fast

def d3_residual : ResidualProgram :=
  match compileDesign {m}_designCert with
  | .ok R    => R
  | .error _ => default

#eval Compiler.D3.report "{m}" {m}_designCert d3_fast d3_residual {samples}
"""

# `--prove` only.  Imports `ReifyProof`, which imports the Mathlib-bearing
# `CompileDesign` because that is where `compileAndRun_correct` lives -- so a
# proof probe costs ~6.9 GB where a sim probe costs ~1.5 GB.  That is the whole
# reason this is a separate head rather than always-on.
# NAMED mode. The SAME `reify_design_named` runs in the executable probe and in
# the proof probe, so the function that is simulated and the function that is
# proved are the same definition. Emitting a model separately in each stage is
# how a proof comes to be about something other than what ran.
PROBE_TAIL_NAMED = """
reify_design_named {m}_designCert as d3_fast

def d3_residual : ResidualProgram :=
  match compileDesign {m}_designCert with
  | .ok R    => R
  | .error _ => default

#eval Compiler.D3.report "{m}" {m}_designCert d3_fast d3_residual {samples}
"""

PROVE_TAIL_NAMED = """
reify_design_named {m}_designCert as d3_fast
{segopt}
prove_reified_incr {m}_designCert as d3_fast
d3_proof_gate d3_fast.correct
"""

PROVE_HEAD = """import LeanSemanticPrimitives.Compiler.ReifyProof
import LeanSemanticPrimitives.Compiler.D3Harness
"""

# `d3_proof_gate` audits the axioms and prints the gate line in ONE elaboration,
# so the line cannot appear for a theorem that failed the audit.  Lean keeps
# elaborating after an error, so a separate print would still have run.
PROVE_TAIL = """
reify_design {m}_designCert as d3_fast

prove_reified {m}_designCert as d3_fast
audit_axioms d3_fast.eq_compileAndRun
d3_proof_gate d3_fast.correct
"""


class Target:
    """One row of a target manifest, resolved to a certificate on disk.

    `key` is the manifest's own identity -- a CORE-ET module name, or a CVA6
    BLOCK name -- and `module` is the certificate's module name.  They differ
    for CVA6 (`ras` vs `ras_gate`), and both are carried so the results join can
    key on the manifest and still name the artifact that was actually run.
    """

    __slots__ = ("key", "module", "path", "sha256", "nodes")

    def __init__(self, key, module, path, sha256, nodes):
        self.key, self.module, self.path = key, module, path
        self.sha256, self.nodes = sha256, nodes


class ManifestError(Exception):
    pass


def sha256_of(path: pathlib.Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def read_manifest_rows(path: pathlib.Path):
    with path.open(encoding="utf-8") as fh:
        return list(csv.DictReader((l for l in fh if not l.startswith("#")), delimiter="\t"))


def load_manifest(man_path: pathlib.Path, certs_dirs):
    """Resolve a manifest to targets, or refuse.

    The canonical run must not be a glob plus a regex: a glob silently picks up
    whatever is in the directory, and `--limit` silently truncates.  The
    manifest names the targets, and every one is verified against the bytes
    recorded beside it before a single probe is launched -- a certificate
    regenerated since the manifest was written is a different experiment, and
    discovering that halfway through a ten-hour run is too late.

    The digest returned covers the manifest's BYTES, not the resolved targets.
    Hashing only the available rows would let an edit to an unavailable row, or
    to the denominator itself, pass a resume check -- and the denominator is the
    thing the milestone is stated over.
    """
    raw = man_path.read_bytes()
    manifest_digest = hashlib.sha256(raw).hexdigest()
    rows = read_manifest_rows(man_path)
    if not rows:
        raise ManifestError(f"{man_path} has no rows")
    is_cva6 = "block" in rows[0]
    key_col = "block" if is_cva6 else "module"

    keys = [r[key_col] for r in rows]
    dupes = sorted({k for k in keys if keys.count(k) > 1})
    if dupes:
        raise ManifestError(f"duplicate targets in {man_path.name}: {dupes}")

    # One certificate filename may live in exactly one of the --certs
    # directories.  `setdefault` would pick whichever was scanned first and hide
    # the ambiguity, which is how a run ends up measuring a corpus nobody named.
    index, seen_in = {}, {}
    for d in certs_dirs:
        for f in sorted(pathlib.Path(d).glob("*_Lgraph.lean")):
            if f.name in index:
                raise ManifestError(
                    f"{f.name} appears in two certificate directories: "
                    f"{seen_in[f.name]} and {f.parent}")
            index[f.name] = f
            seen_in[f.name] = f.parent

    targets, skipped, problems = [], [], []
    for r in rows:
        key = r[key_col]
        avail = str(r.get("cert_available", "")).strip()
        if avail not in ("0", "1"):
            problems.append(f"{key}: cert_available={avail!r} is not 0 or 1")
            continue
        if avail == "0":
            skipped.append((key, r.get("blocked_cause", "") or "no certificate"))
            continue
        cert_name = (r.get("certificate") or "").strip()
        if not cert_name:
            problems.append(f"{key}: cert_available=1 but no certificate name")
            continue
        want = (r.get("sha256") or "").strip()
        if not want:
            problems.append(f"{key}: cert_available=1 but no sha256 recorded")
            continue
        n = _as_int(r.get("nodes"))
        if n < 0:
            problems.append(f"{key}: nodes={r.get('nodes')!r} is missing or not a count")
            continue
        fname = cert_name if cert_name.endswith("_Lgraph.lean") else f"{cert_name}_Lgraph.lean"
        path = index.get(fname)
        if path is None:
            problems.append(f"{key}: {fname} not found in {[str(x) for x in certs_dirs]}")
            continue
        got = sha256_of(path)
        if want != got:
            problems.append(f"{key}: {fname} hash drift, manifest {want[:12]} != disk {got[:12]}")
            continue
        targets.append(Target(key, module_of(path), path, got, str(n)))

    if problems:
        raise ManifestError("manifest could not be resolved:\n  " + "\n  ".join(problems))

    mods = [t.module for t in targets]
    dup_mods = sorted({m for m in mods if mods.count(m) > 1})
    if dup_mods:
        raise ManifestError(f"two targets resolve to the same certificate module: {dup_mods}")
    return targets, skipped, manifest_digest


def selection_digest(targets) -> str:
    """Identity of the SELECTED set, after --tier/--only/--limit.

    The manifest digest says which denominator was used; this says which subset
    of it this run covers.  Resuming across a changed selection would merge two
    different experiments into one table.
    """
    h = hashlib.sha256()
    for t in sorted(targets, key=lambda t: t.key):
        h.update(f"{t.key}:{t.module}:{t.sha256}\n".encode())
    return h.hexdigest()


def order_by_rss(targets, path: pathlib.Path):
    """RUN ORDER only: smallest measured peak RSS first, largest last.

    This machine is shared, so a sweep is staged by MEMORY rather than by
    whatever `free -g` reports idle.  Cheap targets first means an interrupted
    run has already banked most of its rows, and it puts the targets that could
    approach the budget at the end, where they can be dropped without losing
    anything earned.

    This is SCHEDULING metadata and never evidence.  The file it reads may come
    from a run with a DIFFERENT artifact digest -- that is fine here and only
    here, because nothing from it reaches a result row.  `selection_digest`
    sorts by key, so the identity of the selected SET is unchanged by the order
    its members run in; the sidecar records this file's digest so the schedule
    is reproducible anyway.

    A target with no recorded RSS sorts LAST.  Unmeasured is not the same as
    small, and assuming it small is exactly the assumption that blows a budget.
    """
    rss = {}
    with path.open(encoding="utf-8") as fh:
        for r in csv.DictReader((l for l in fh if not l.startswith("#")), delimiter="\t"):
            try:
                v = int(r.get("max_rss_kb") or 0)
            except ValueError:
                continue
            if v > 0:
                rss[(r.get("module") or "").strip()] = v
    unknown = sorted(t.module for t in targets if not rss.get(t.module))
    if unknown:
        print(f"d3_sweep: {len(unknown)} target(s) have no recorded RSS and run LAST: "
              f"{', '.join(unknown[:8])}{' ...' if len(unknown) > 8 else ''}",
              file=sys.stderr)
    return sorted(targets, key=lambda t: (rss.get(t.module) or (1 << 62), t.key)), rss


# Everything whose CONTENT can change what a row means.  A worktree HEAD plus a
# dirty bit is not enough: two different dirty states share both, and an
# uncommitted edit to the reifier or the harness is exactly the kind of change
# that makes old rows incomparable.
TOOL_FILES = [
    pathlib.Path(__file__).resolve(),
    ROOT / "formal/lean/LeanSemanticPrimitives/Compiler/D3Harness.lean",
    ROOT / "formal/lean/LeanSemanticPrimitives/Compiler/ReifyGen.lean",
    ROOT / "formal/lean/LeanSemanticPrimitives/Compiler/Reify.lean",
    # The probe loads the *Defs* modules; the proof modules are kept here anyway
    # -- see CRITICAL_OLEANS for why.
    ROOT / "formal/lean/LeanSemanticPrimitives/Compiler/CompileDesignDefs.lean",
    ROOT / "formal/lean/LeanSemanticPrimitives/Compiler/CompileGraphDefs.lean",
    ROOT / "formal/lean/LeanSemanticPrimitives/Compiler/CompileOpDefs.lean",
    ROOT / "formal/lean/LeanSemanticPrimitives/Compiler/CompileDesign.lean",
    # The proof generator. Only `--prove` loads it, but it is part of the
    # harness's identity either way: a change to it changes what `proof=1` means.
    ROOT / "formal/lean/LeanSemanticPrimitives/Compiler/ReifyProof.lean",
    ROOT / "formal/lean/LeanSemanticPrimitives/Compiler/ResidualSemantics.lean",
    ROOT / "formal/lean/LeanSemanticPrimitives/Compiler/Runtime.lean",
    ROOT / "formal/lean/LeanSemanticPrimitives/Compiler/DesignCert.lean",
]


# The compiled artifacts the probes actually load.  `tool_digest` hashes SOURCE,
# which is not enough: a shared `.lake` let another worktree rebuild
# `CompileDesign.olean` fourteen minutes into a run while this branch's source
# sat unchanged, so every source-level guard reported "same tooling" while the
# compiler under test was swapped.  Hash what gets loaded.
# The *Defs modules are what a probe now LOADS.  The three proof modules are kept
# in the list even though a probe no longer imports them: `compileDesign_correct`
# is the entire reason an `agree` row means anything, so a run that continued
# across an edit to it would be reporting agreement against a correctness claim
# that had moved underneath it.  The cost is an abort when a proof is edited
# mid-run, which is exactly when a run SHOULD abort.
CRITICAL_OLEANS = ["CompileDesignDefs", "CompileGraphDefs", "CompileOpDefs",
                   "CompileDesign", "CompileGraph", "CompileOp",
                   "D3Harness", "ReifyGen", "Reify", "ReifyProof", "Runtime",
                   "DesignCert", "ResidualIR", "ResidualSemantics"]


def artifact_dir() -> pathlib.Path:
    """Where the loaded `.olean`s live.

    `D3_ARTIFACT_DIR` is a TEST SEAM, and a dangerous one: it changes what is
    HASHED without changing what Lean LOADS, so a run that inherited it from the
    environment would validate one set of files while the probes used another.
    `--runner-selftest` is required before it is honoured, and a canonical
    manifest run refuses outright if the variable is set at all.
    """
    env = os.environ.get("D3_ARTIFACT_DIR")
    if env and os.environ.get("D3_RUNNER_SELFTEST") == "1":
        return pathlib.Path(env)
    return (build_root() / "build" / "lib" / "lean" / "LeanSemanticPrimitives" / "Compiler")


def missing_oleans() -> list:
    base = artifact_dir()
    return [m for m in CRITICAL_OLEANS if not (base / f"{m}.olean").is_file()]


def build_root() -> pathlib.Path:
    return (LEAN_DIR / ".lake").resolve()


def build_root_is_external() -> tuple:
    """(external, why).  A build root outside this worktree is shared state, and
    shared mutable state is what invalidated the first pilot."""
    lake = LEAN_DIR / ".lake"
    if lake.is_symlink():
        return True, f"{lake} is a symlink to {lake.resolve()}"
    try:
        build_root().relative_to(ROOT.resolve())
    except ValueError:
        return True, f"{build_root()} is outside the worktree {ROOT.resolve()}"
    return False, ""


def artifact_digest() -> str:
    """Digest of the compiled closure the probes load.

    A missing object hashes as `<missing>` so drift is still detected if one
    disappears mid-run; a canonical run refuses up front when any is absent, so
    that placeholder never stands in for real evidence.
    """
    base = artifact_dir()
    h = hashlib.sha256()
    for m in CRITICAL_OLEANS:
        f = base / f"{m}.olean"
        # No is_file() probe first: that is a TOCTOU race, and this function runs
        # in the central post-result check where an escaping OSError would kill
        # the driver before it could write an aborted sidecar.  Absent and
        # unreadable get DIFFERENT deterministic markers, so either one shows up
        # as drift rather than as a silent equality.
        try:
            h.update(f"{m}:".encode() + hashlib.sha256(f.read_bytes()).digest())
        except FileNotFoundError:
            h.update(f"{m}:<absent>".encode())
        except OSError as e:
            h.update(f"{m}:<unreadable:{e.errno}>".encode())
        except Exception:  # noqa: BLE001
            h.update(f"{m}:<error>".encode())
    return h.hexdigest()


def tool_digest() -> str:
    h = hashlib.sha256()
    for f in TOOL_FILES:
        try:
            h.update(f.name.encode() + b":" + hashlib.sha256(f.read_bytes()).digest())
        except OSError:
            h.update(f.name.encode() + b":<missing>")
    return h.hexdigest()


def _atomic_write(path: pathlib.Path, text: str) -> None:
    """Write, fsync, rename.  The temp file sits BESIDE the destination.

    `os.replace` is atomic only within one filesystem, and the run directory and
    the output directory need not be on the same one.  The dot-prefixed name
    keeps a partial file from being mistaken for a result table.
    """
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.parent / f".{path.name}.partial"
    with tmp.open("w", encoding="utf-8") as fh:
        fh.write(text)
        fh.flush()
        os.fsync(fh.fileno())
    os.replace(tmp, path)


def write_rows_atomic(out_path: pathlib.Path, cols, rows) -> None:
    """Checkpoint.  A ten-hour run killed at hour nine must keep its rows."""
    body = ["\t".join(cols)]
    for r in sorted(rows, key=lambda r: str(r.get("target_key", r.get("module", "")))):
        body.append("\t".join(str(r.get(c, "")) for c in cols))
    _atomic_write(out_path, "\n".join(body) + "\n")


def run_config(a, manifest_digest: str) -> dict:
    """What a resumed run must match before it may reuse a row."""
    return {
        "manifest": str(pathlib.Path(a.manifest).resolve()) if a.manifest else "",
        "manifest_digest": manifest_digest,
        "samples": a.samples,
        "timeout": a.timeout,
        "native": bool(a.native),
        "tier": a.tier or "",
        # SEMANTIC: a segmented proof and a monolithic one are different
        # experiments over the same design, so they must not resume into or
        # merge with one another.
        "proof_segment_size": int(getattr(a, "proof_segment_size", 0) or 0),
        "lake": LAKE,
        "worktree_head": subprocess.run(
            ["git", "-C", str(ROOT), "rev-parse", "HEAD"],
            capture_output=True, text=True).stdout.strip(),
    }


def module_of(path: pathlib.Path) -> str:
    """`foo_Lgraph.lean` -> `foo`.  The emitter's own naming, not a guess."""
    return path.name[: -len("_Lgraph.lean")] if path.name.endswith("_Lgraph.lean") else path.stem


def make_probe(cert: pathlib.Path, m: str, samples: int, reifier: str = "legacy") -> str:
    """Certificate body + reify + gate report.

    The certificate's own `theorem` block is DROPPED: `<m>_compiles` is a
    `native_decide` over the whole literal, and paying it here would put the
    most expensive gate in front of the cheapest ones.  It is a separate claim
    and `--native` measures it separately.  Cutting at the first `theorem` also
    drops `<m>_step_correct` and the trailing `<m>_residual`/`#print axioms`,
    all of which this probe replaces or does not need.
    """
    tail = PROBE_TAIL_NAMED if reifier == "named" else PROBE_TAIL
    return _cert_body(cert, PROBE_HEAD) + tail.format(m=m, samples=samples)


def _cert_body(cert: pathlib.Path, head: str) -> str:
    """Shared by both probe builders, so they cannot trim the certificate
    differently and compare results that came from different text."""
    body = []
    for line in cert.read_text(encoding="utf-8", errors="replace").splitlines():
        if line.startswith("theorem "):
            break
        # the certificate imports CompileDesign; the probe head already pulls in
        # ReifyGen and D3Harness, both of which import it transitively.
        if line.startswith("import "):
            continue
        body.append(line)

    # Drop the doc comment that belonged to the theorem we just cut.  A `/-- -/`
    # left dangling attaches to whatever comes next, so `reify_design` is parsed
    # as the declaration the comment documents and fails with
    # "unexpected token 'reify_design'; expected 'lemma'" -- a parse error that
    # still lets every other gate pass, which is exactly the kind of silent
    # miscount this sweep exists to avoid.
    while body:
        while body and not body[-1].strip():
            body.pop()
        if not body:
            break
        last = body[-1].strip()
        if last.endswith("-/"):
            while body and not body[-1].lstrip().startswith(("/--", "/-")):
                body.pop()
            if body:
                body.pop()
            continue
        if last.startswith("--"):
            body.pop()
            continue
        break

    return head + "\n".join(body)


def make_proof_probe(cert: pathlib.Path, m: str, reifier: str = "legacy",
                     segment: int = 0) -> str:
    """The last-mile proof, in its OWN file and its OWN process.

    SEPARATE ON PURPOSE.  In one process a failed `prove_reified` or a failed
    `audit_axioms` puts `error:` in the log and a nonzero exit on the run, and
    `extract_gates` reads both as `clean_exit = False` -- so an executable design
    that genuinely passed cert..agree would be relabelled a `typecheck` failure
    by a proof that did not work out.  `proof` is the LAST gate and must not be
    able to retract an earlier one.
    """
    if reifier == "named":
        # `set_option d3.segment` goes HERE -- after the imports and the
        # certificate body, immediately before the command it affects, and in
        # the PROOF probe only.  The sim probe compiles the same certificate
        # body but must not carry it: segmentation is a property of how the
        # walk is PROVED, and putting it in the executable probe would change
        # the artifact the simulation ran against for no reason.
        segopt = f"set_option d3.segment {int(segment)}\n" if segment else ""
        return _cert_body(cert, PROVE_HEAD) + PROVE_TAIL_NAMED.format(m=m, segopt=segopt)
    if segment:
        # Unreachable from `main`, which refuses the combination up front; kept
        # so a direct caller cannot quietly get a legacy probe with the option
        # silently dropped.
        raise ValueError("segment size is meaningless for the legacy reifier: "
                         "`prove_reified` has no segmented path")
    return _cert_body(cert, PROVE_HEAD) + PROVE_TAIL.format(m=m)


_TIME_PATS = (("max_rss_kb", r"Maximum resident set size \(kbytes\): (\d+)"),
              ("user_s", r"User time \(seconds\): ([\d.]+)"),
              ("sys_s", r"System time \(seconds\): ([\d.]+)"),
              ("wall_s", r"Elapsed \(wall clock\) time .*?: (?:(\d+):)?(\d+):([\d.]+)"))


def read_time_report(path: pathlib.Path) -> dict:
    """One `/usr/bin/time -v` report -> {max_rss_kb, user_s, sys_s, wall_s}.

    Shared by both stages so they cannot parse the same format differently and
    then be compared as if they had.  Missing fields are simply absent: a report
    that was never written -- the process was SIGKILLed before it could flush --
    yields {} rather than zeros, because zero is a measurement and this is not.
    """
    out = {}
    try:
        tt = path.read_text()
    except OSError:
        return out
    for key, pat in _TIME_PATS:
        mo = re.search(pat, tt)
        if not mo:
            continue
        if key == "wall_s":
            h, mi, sec = mo.group(1), mo.group(2), mo.group(3)
            out[key] = f"{int(h or 0) * 3600 + int(mi) * 60 + float(sec):.2f}"
        else:
            out[key] = mo.group(1)
    return out


PROOF_GATE_RE = r"^D3GATE proof=1 thm=(\S+) axioms=(.*)$"

# The one theorem a proof probe is allowed to be reporting.
PROOF_THEOREM = "d3_fast.correct"


def extract_proof_gate(row, out: str, rc, expect_module=None, oom: bool = False,
                       sampled_kill_kb: int = 0, refused: str = "") -> None:
    """Credit `proof` from the SEPARATE proof probe.  Writes nothing else.

    Three outcomes, kept apart because they mean different things:

      `na` -- not attempted.  No `--prove`, or the executable probe did not
              reach `agree`, so there was nothing worth proving about.  An
              unattempted gate is not a failed one.
      `0`  -- attempted and did not come back clean.  That INCLUDES a
              proof-stage timeout, a proof-stage kernel OOM kill, and a
              proof-stage SAMPLED kill.  All three are RESOURCE outcomes rather
              than anything learned about the theorem, and `detail` says which.

    TERMINAL ON PURPOSE, and this is a real trade-off rather than an oversight.
    A proof-stage timeout or OOM could instead make the row NONTERMINAL so
    `--resume` retries it.  It must not: `d3_join.py` treats every nonterminal
    status as uncredited, so a target whose executable gates genuinely passed
    cert..agree would drop out of the milestone denominator because an OPTIONAL
    last gate ran out of memory.  That erases established evidence to report a
    failure of something else.  The row therefore stays `done` with its
    executable verdict intact, `proof=0`, and a `detail` naming the cause; a
    proof retry is an explicit `--prove` re-run, which is already opt-in.
      `1`  -- the marker is present exactly once AND the probe exited 0.

    The marker is emitted by `d3_proof_gate`, which audits the axioms and prints
    in ONE elaboration, so it cannot appear for a theorem resting on `sorryAx`
    or on `native_decide`'s `ofReduceBool`.  `rc == 0` is required on top of
    that, because Lean keeps elaborating after an error: a LATER failure in the
    file would leave the marker printed and the exit code nonzero.
    """
    if rc is None:
        row["proof"] = "na"
        if refused:
            # NOT ATTEMPTED, and the row says why.  `0` would be a claim that the
            # proof was tried and did not come back clean; nothing ran at all, so
            # nothing is known about the theorem either way.  The distinction
            # matters because `0` invites a reader to think the theorem is in
            # doubt, and it is not -- it is simply unexamined.
            row["detail"] = ((row.get("detail") or "")
                             + ("; " if row.get("detail") else "")
                             + f"proof not attempted: {refused}")
        return
    # The last gate rests on the others: proving a theorem about a design whose
    # simulation disagreed would be a claim about the wrong thing.
    if row.get("agree") != 1:
        row["proof"] = "na"
        return
    hits = re.findall(PROOF_GATE_RE, out, re.M)
    if oom or sampled_kill_kb or rc != 0 or len(hits) != 1:
        row["proof"] = "0"
        if oom:
            # The kernel killed the PROOF stage at memory.max.  The row stays
            # terminal with its executable verdict: see the docstring.
            # FIRST, because the kernel's verdict is exact and keeps precedence
            # over the sampled guard's whenever both could be read as applying.
            why = "proof stage killed by the kernel at the cgroup memory.max"
        elif sampled_kill_kb:
            # The SAMPLED guard killed the proof subprocess.  Said as a RESOURCE
            # outcome, not a proof failure: nothing was learned about the
            # theorem, and the figure quoted is a lower bound because the sampler
            # can miss a peak between two samples.
            why = (f"proof stage killed by the sampled aggregate hard limit at "
                   f"{sampled_kill_kb} kB (a SAMPLED LOWER BOUND, not a "
                   f"/usr/bin/time peak); the theorem was never decided")
        elif rc == 124:
            # A proof-stage TIMEOUT.  Deliberately NOT `_mark_timeout`: that sets
            # `run_status`/`verdict` to `timeout`, which would retract an
            # executable verdict this target had already earned.
            why = "proof stage timed out"
        elif rc != 0:
            why = f"proof probe exited {rc}"
        else:
            why = f"{len(hits)} proof markers in the log, expected exactly 1"
        row["detail"] = (row.get("detail") or "") + ("; " if row.get("detail") else "") + why
        return
    thm, _axioms = hits[0]
    if thm != PROOF_THEOREM:
        # EXACT, not a prefix.  `d3_fast.wrong` starts with `d3_fast` and would
        # have been credited; the marker has to name the theorem the probe was
        # asked to prove, not a neighbour of it.
        row["proof"] = "0"
        row["detail"] = ((row.get("detail") or "")
                         + ("; " if row.get("detail") else "")
                         + f"proof marker names {thm!r}, expected {PROOF_THEOREM!r}")
        return
    row["proof"] = "1"


ERR_RE = re.compile(r"^(?:[^\s:]+:\d+:\d+: )?error: (.*)$")


def classify(log: str, rc: int, timeout: int) -> str:
    """First real cause, not the last line printed."""
    if rc == 124:
        return f"timeout {timeout}s"
    for line in log.splitlines():
        mo = ERR_RE.match(line.strip())
        if mo:
            return mo.group(1)[:160]
    if rc != 0:
        return f"exit {rc}"
    return ""


def deferred_row(target: Target, samples: int, why: str) -> dict:
    """A target the SCHEDULER declined to start.

    Deliberately not an error row and not a `done` row.  It did not fail -- no
    gate was even attempted -- so every gate is 0 and the verdict says `deferred`
    rather than being recomputed from those zeros, which would relabel "we chose
    not to run this" as "it passed nothing".  `run_status` is `deferred`, which
    `--resume` does not treat as terminal, so a later run picks it up.
    """
    row = {g: 0 for g in GATES}
    row.update({
        "target_key": target.key, "module": target.module,
        "cert_sha256": target.sha256, "manifest_nodes": target.nodes,
        "requested_samples": samples, "run_status": "deferred",
        "verdict": "deferred", "proof": "na", "detail": why,
        "max_rss_kb": "", "max_rss_source": "", "cgroup_peak_kb": "",
        "user_s": "", "sys_s": "", "wall_s": "0.00",
        "sim_max_rss_kb": "", "sim_user_s": "", "sim_sys_s": "", "sim_wall_s": "",
        "proof_max_rss_kb": "", "proof_max_rss_source": "",
        "proof_user_s": "", "proof_sys_s": "", "proof_wall_s": "",
        "drift": "", "launched": "0",
    })
    for k in ("sources", "nodes", "outputs", "flops", "mems", "inputs", "bindings",
              "selftest_base", "mut_out", "mut_flop", "mut_mem",
              "mutable_out", "mutable_flop", "mutable_mem", "distinct_obs", "tier"):
        row.setdefault(k, "")
    return row


def _mark_timeout(row: dict, timeout: int) -> None:
    """Record a probe killed by `--timeout` as a LIMIT outcome.

    Idempotent, and called on EVERY path out of `run_one` that can see rc 124 --
    including the `--native` early return, which an earlier version of this fix
    skipped, leaving native timeouts `done` and terminal.

    Whatever gate output arrived before the kill is deliberately KEPT as raw
    observation: it is real, and discarding it would lose the only clue to where
    the time went.  What it must not become is a verdict.
    """
    row["run_status"], row["verdict"] = "timeout", "timeout"
    row["launched"] = "1"
    if "timeout" not in (row.get("detail") or ""):
        row["detail"] = f"timeout {timeout}s"


# Cgroup peaks read before the cgroup was destroyed, keyed by `target.key`.
#
# A side channel, because the peak is read in `run_one`'s `finally` while the row
# that reports it may be synthesized by `main` after an exception unwound past
# every `return` in between.  The kernel's reading is gone once the directory is
# removed, so losing it to an unrelated crash would mean losing it for good.
# Written once per probe from the worker that owns that target, and read only
# after that worker's future has completed.
_CG_PEAK: dict = {}

# The Cgroup of a probe whose removal FAILED, keyed by `target.key`.
#
# `main` retries removal after `_teardown()`, which is the first point at which
# every process holding the cgroup has certainly been killed.  The OBJECT is kept
# rather than just the path, so the retry runs the real `destroy()` -- its
# `cgroup.kill` write and its bounded rmdir loop -- instead of a second,
# divergent copy of that sequence at the call site.
_FATAL_CGROUP: dict = {}


def run_one(target: Target, samples: int, timeout: int, native: bool, baseline: str = ""):
    cert, m = target.path, target.module
    probe_dir = RUN_DIR / ("probes_native" if native else "probes")
    log_dir = RUN_DIR / ("logs_native" if native else "logs")
    probe_dir.mkdir(parents=True, exist_ok=True)
    log_dir.mkdir(parents=True, exist_ok=True)

    row = {g: 0 for g in GATES}
    row["target_key"] = target.key
    row["module"] = m
    row["cert_sha256"] = target.sha256
    # The MANIFEST's node count, kept apart from the one the probe log reports.
    # Selection and tiering use this one; a log that disagrees is a finding, not
    # a reason to re-tier the row after the fact.
    row["manifest_nodes"] = target.nodes
    row["requested_samples"] = samples
    row["run_status"] = "done"
    row["max_rss_kb"] = ""
    # Blank until something actually measures: an unmeasured row must not carry
    # a provenance, or "not measured" reads as "measured by that instrument".
    row["max_rss_source"] = ""
    row["cgroup_peak_kb"] = ""
    row["user_s"] = ""
    row["sys_s"] = ""
    for k in STAGE_COLS:
        row[k] = ""
    row["drift"] = ""
    row["launched"] = "0"
    row["proof"] = "na"  # never attempted by this pass; see the module docstring
    for k in ("sources", "nodes", "outputs", "flops", "mems", "inputs", "bindings",
              "selftest_base", "mut_out", "mut_flop", "mut_mem",
              "mutable_out", "mutable_flop", "mutable_mem", "distinct_obs"):
        row[k] = ""

    if _SHUTDOWN.is_set():
        # The run is already ending -- a fatal cleanup failure, a drift abort or
        # a signal.  `run_group` would return 143 and the gates would all read
        # zero, which `classify` would present as a design result; a deferred row
        # says what actually happened and stays nonterminal for `--resume`.
        # This is what stops a QUEUED target at jobs=1, where cancelling futures
        # cannot help: the next task is already submitted.
        return deferred_row(target, samples,
                            "the run was shutting down before this target launched")

    if _RSS_STOP.is_set():
        return deferred_row(target, samples,
                            f"aggregate RSS cap reached before launch "
                            f"({_RSS_PEAK['tripped_at_kb']} kB)")

    if not cert.is_file() or cert.stat().st_size == 0:
        row["detail"] = "certificate file missing or empty"
        row["wall_s"] = "0.00"
        return row
    text = cert.read_text(encoding="utf-8", errors="replace")
    if f"{m}_designCert" not in text:
        row["detail"] = f"no `{m}_designCert` declaration in the emitted file"
        row["wall_s"] = "0.00"
        return row
    row["cert"] = 1

    if native:
        probe = probe_dir / f"{m}.lean"
        probe.write_text(text, encoding="utf-8")
    else:
        probe = probe_dir / f"{m}.lean"
        probe.write_text(make_probe(cert, m, samples, reifier=REIFIER), encoding="utf-8")

    # /usr/bin/time -v, so job count and timeouts for the long tiers can be
    # chosen from MEASURED peak RSS rather than guessed.  Wrapping rather than
    # sampling: a sampler misses the peak of a short-lived elaboration.
    tv = log_dir / f"{m}.time"
    # Direct `lean` by default; `--via-lake` keeps the old two-process form so
    # the two can be compared on identical probes rather than assumed equal.
    if LEAN_BIN:
        cmd, penv = [LEAN_BIN, str(probe)], LEAN_ENV
    else:
        cmd, penv = [LAKE, "env", "lean", str(probe)], None
    if pathlib.Path("/usr/bin/time").exists():
        cmd = ["/usr/bin/time", "-v", "-o", str(tv)] + cmd
    # Immediately BEFORE launch.  A probe started after the compiler moved is
    # already measuring something else.
    if baseline and artifact_digest() != baseline:
        row["drift"] = "before"
        row["run_status"] = "aborted_artifact_drift"
        row["detail"] = "build artifacts changed before this module was launched"
        row["wall_s"] = "0.00"
        return row

    row["launched"] = "1"
    t0 = time.time()
    cg = None
    if CGROUP_BASE is not None:
        try:
            cg = Cgroup(CGROUP_BASE, f"{RUN_ID}-{m}", CGROUP_MAX_KB)
            # Outermost, so EVERYTHING -- /usr/bin/time included -- is inside the
            # cgroup and counted against it.
            cmd = [sys.executable, str(CGEXEC_PATH), str(cg.path)] + cmd
        except OSError as e:
            # Enforcement was requested and cannot be provided.  Refusing is the
            # only honest option: silently falling back would run the probe
            # unguarded while the sidecar claims otherwise.
            row["detail"] = f"cgroup enforcement unavailable at launch: {e}"
            row["run_status"], row["verdict"] = "error", "runner_error"
            row["wall_s"] = "0.00"
            return row
    cg_peak, cg_oom, cg_leaked = 0, False, False
    proof_out, proof_rc = "", None
    proof_refused = ""
    sim_oom, sim_peak = False, None
    try:
        out, rc = run_group(cmd, LEAN_DIR, timeout, env=penv)
        # The proof stage launches only on a FULL executable pass, judged by the
        # same parser that will credit the row.  `rc == 0` is not that: a probe
        # can exit cleanly having disagreed, and proving a theorem about a design
        # whose simulation disagreed is a claim about the wrong thing.  Running
        # `extract_gates` on a COPY keeps the decision and the final credit from
        # drifting apart -- they are literally the same function on the same log.
        _pre = dict(row)
        if PROVE and not native and rc == 0 and not _SHUTDOWN.is_set():
            extract_gates(_pre, out, rc, timeout, expect_module=m, expect_samples=samples)
        if _TEST_KILL_BETWEEN_STAGES == m:
            # The seam fires exactly where the race lives: after the sim stage
            # has exited and before the proof stage exists to be reaped.
            if not _TEST_KILL_SOFT_ONLY:
                _RSS_PEAK["killed_at_kb"] = _RSS_PEAK.get("killed_at_kb") or 1
                _RSS_KILL.set()
            else:
                _RSS_PEAK["tripped_at_kb"] = _RSS_PEAK.get("tripped_at_kb") or 1
            _RSS_STOP.set()
        # `_RSS_KILL` as well as `_SHUTDOWN`.  The hard monitor sets `_RSS_KILL`,
        # sets `_RSS_STOP`, reaps, and then RETURNS -- the thread is gone.  Trip
        # it in the window between the sim stage exiting and the proof stage's
        # process group being registered and `_reap_all` finds nothing to kill,
        # no monitor remains, and the proof probe would launch UNGUARDED: a
        # ~6.9 GB Mathlib import against a budget that has already been declared
        # exceeded.  Nor would it be caught afterwards -- if it exited 0,
        # `proof_sampled_kill` is false, because nothing killed it.
        # `_RSS_STOP` is refused too: the soft cap exists to stop ADDING load,
        # and a proof stage is new load for a target whose sim is already done.
        want_proof = (PROVE and not native and rc == 0 and not _SHUTDOWN.is_set()
                      and _pre.get("agree") == 1)
        if want_proof and (_RSS_KILL.is_set() or _RSS_STOP.is_set()):
            proof_refused = ("the aggregate RSS hard limit was reached before the "
                             "proof stage could launch" if _RSS_KILL.is_set() else
                             "the aggregate RSS cap latched before the proof stage "
                             "could launch")
            print(f"d3_sweep: not launching the proof stage for {m}: "
                  f"{proof_refused}", file=sys.stderr)
        elif want_proof:
            # Read the cgroup's OOM state BEFORE the proof stage, so a proof-stage
            # kill can be told apart from a sim-stage one.  Without this the two
            # share a cgroup and a proof OOM would come back as `rss_killed` for
            # the whole row -- zeroing executable gates that were already earned.
            if cg is not None:
                sim_oom, sim_peak = cg.oom_killed(), cg.peak_kb()
            # A SECOND process, so a failed proof cannot put `error:` in the
            # executable probe's log or a nonzero exit on its run -- either of
            # which `extract_gates` reads as `clean_exit = False` and turns into
            # a `typecheck` failure for a design that passed cert..agree.
            # Inside the same cgroup, so the memory cap still governs it; that
            # makes `cgroup_peak_kb` the max over BOTH stages, which is what the
            # cap is protecting anyway.  Per-stage RSS lives in the two
            # `/usr/bin/time` reports instead.
            pprobe = probe_dir / f"{m}.proof.lean"
            pprobe.write_text(make_proof_probe(cert, m, reifier=REIFIER,
                                               segment=PROOF_SEGMENT), encoding="utf-8")
            ptv = log_dir / f"{m}.proof.time"
            pcmd = ([LEAN_BIN, str(pprobe)] if LEAN_BIN
                    else [LAKE, "env", "lean", str(pprobe)])
            if pathlib.Path("/usr/bin/time").exists():
                pcmd = ["/usr/bin/time", "-v", "-o", str(ptv)] + pcmd
            if cg is not None:
                pcmd = [sys.executable, str(CGEXEC_PATH), str(cg.path)] + pcmd
            proof_out, proof_rc = run_group(pcmd, LEAN_DIR, timeout, env=penv)
            (log_dir / f"{m}.proof.log").write_text(proof_out, encoding="utf-8")
    finally:
        if cg is not None:
            # Read BEFORE destroying -- the counters vanish with the directory.
            # This runs on every path out, including a timeout and an exception,
            # so no cgroup survives its probe.
            cg_peak, cg_oom = cg.peak_kb(), cg.oom_killed()
            cg_leaked = not cg.destroy()
            if cg_peak is not None:
                # Recorded the INSTANT it is read, so every path out of here
                # already carries it -- including the ones that go on to return
                # `runner_error`.  A reading the kernel already took must not be
                # discarded because the semantic row turned out to be unusable:
                # `peak_kb()` can succeed while `oom_killed()` fails, and the
                # peak is the whole reason the cgroup existed.
                row["cgroup_peak_kb"] = str(cg_peak)
                # The one exit this function does not own: an exception below
                # unwinds past every `return`, and `main` synthesizes the row
                # instead.  Keyed by target so the synthesized row can recover
                # what was measured before the throw.
                _CG_PEAK[target.key] = cg_peak
    if os.environ.get("D3_TEST_RAISE_ON") == m:
        # Test seam, placed where the real hazard is: AFTER the probe ran, in the
        # window where timing capture, log writing and gate parsing happen.  main
        # converts an exception here into a `runner_error` row, and that row must
        # still be subject to the artifact check.
        raise RuntimeError("D3_TEST_RAISE_ON (post-probe)")
    if cg_leaked:
        # Fail CLOSED.  Cleanup is part of the enforcement guarantee rather than
        # housekeeping after it: a cgroup that will not die keeps its memory
        # charge, so the budget this run is scheduling against is now wrong and
        # the NEXT probe would launch under a limit that is already partly
        # consumed.  Warning and returning an ordinary verdict -- what this did
        # before -- let the run continue on a weakened guarantee while reporting
        # a design result.  `cgroup_peak_kb` is already set above, so the
        # measurement survives the error row.
        print(f"d3_sweep: WARNING: could not remove the cgroup for {m}; it still "
              f"holds a memory charge", file=sys.stderr)
        # BEFORE this worker returns, so that at jobs=1 the already-submitted
        # next task hits the guard at the top of `run_one` and never reaches
        # `run_group`.  Cancelling futures in `main` happens too late for a task
        # the executor has already picked up.
        _SHUTDOWN.set()
        # Handed to `main` so the removal can be retried once the teardown has
        # killed whatever was holding it.  Without this the object -- and with it
        # the path -- would be lost when this frame returns.
        _FATAL_CGROUP[target.key] = cg
        row["run_status"], row["verdict"] = "fatal_cleanup", "runner_error"
        row["detail"] = (f"the per-probe cgroup could not be removed, so its "
                         f"memory charge still counts against the budget and the "
                         f"next probe's limit would be weakened"
                         + (f" (an OOM kill was also observed)" if cg_oom else ""))
        row["wall_s"] = f"{time.time() - t0:.2f}"
        return row

    if cg is not None and (cg_oom is None or cg_peak is None):
        # The kernel's own accounting could not be read, so whether this probe
        # was OOM-killed is UNKNOWN. Guessing either way would be worse than
        # saying so: "not OOM" would turn a memory kill into a design result.
        row["run_status"], row["verdict"] = "error", "runner_error"
        row["detail"] = (f"cgroup accounting unreadable "
                         f"(oom={'unknown' if cg_oom is None else cg_oom}, "
                         f"peak={'unknown' if cg_peak is None else cg_peak}); "
                         f"cannot distinguish a memory kill from a design result")
        row["wall_s"] = f"{time.time() - t0:.2f}"
        return row

    if cg is not None and cg_oom and sim_oom is False and proof_rc is not None:
        # The kill happened AFTER the sim stage read a clean OOM state, so it was
        # the PROOF stage that hit memory.max.  Falling through to the
        # `rss_killed` row below would zero `cert`..`agree` -- discarding an
        # executable result that was already established -- to report a failure
        # of the optional last gate.  The proof simply does not get credited.
        proof_oom = True
    else:
        proof_oom = False

    if cg is not None and cg_oom and not proof_oom:
        # The KERNEL killed it at memory.max.  Continuous enforcement, and
        # `memory.peak` is a TRUE peak rather than a sampled lower bound.
        r = deferred_row(target, samples,
                         f"terminated by the kernel at the cgroup memory.max of "
                         f"{CGROUP_MAX_KB} kB (max_rss_kb holds cgroup memory.peak: "
                         f"an EXACT cgroup-ACCOUNTED memory peak for the whole "
                         f"subtree, which is NOT the same quantity as "
                         f"/usr/bin/time's max RSS)")
        r["run_status"], r["verdict"] = "rss_killed", "rss_killed"
        r["launched"], r["wall_s"] = "1", f"{time.time() - t0:.2f}"
        r["max_rss_kb"] = str(cg_peak)
        r["max_rss_source"] = SRC_CGROUP
        # The same figure, also in its own column, so a consumer reading
        # `cgroup_peak_kb` uniformly across every cgroup row does not have to
        # special-case the one status where it was promoted into `max_rss_kb`.
        r["cgroup_peak_kb"] = str(cg_peak)
        return r

    # STAGE-AWARE.  `rc` is the SIM stage's, so a sampled kill that landed on the
    # PROOF subprocess leaves `rc == 0` and only `proof_rc` nonzero -- and the
    # branch below, gated on `rc != 0`, used to skip it entirely.  The probe was
    # physically SIGKILLed by the budget and the row said `proof=0`, as if the
    # proof had simply not worked out.  Attributed here instead, which also keeps
    # the `cert`..`agree` the sim stage already earned.
    proof_sampled_kill = (_RSS_KILL.is_set() and rc == 0
                          and proof_rc is not None and proof_rc != 0
                          and not proof_oom)

    if _RSS_KILL.is_set() and rc != 0:
        # The SAMPLED guard acted.  Reached only when the cgroup branch above did
        # not: a kernel OOM raises an `oom_kill` event and is handled there, and
        # a SIGKILL from this monitor raises none, so the two are distinguishable
        # rather than racing for the same row.  Under hybrid enforcement the
        # kernel's verdict is checked first because it is exact.
        # Killed by the budget, not by anything the design did.  Recorded as a
        # nonterminal row so `--resume` picks it up, and never passed to
        # `classify`, which would read the SIGKILL as a compiler failure.
        killed_at = _RSS_PEAK["killed_at_kb"]
        r = deferred_row(target, samples,
                         f"terminated by the hard aggregate RSS limit at "
                         f"{killed_at} kB (max_rss_kb is this SAMPLED aggregate, a "
                         f"LOWER BOUND; /usr/bin/time was SIGKILLed and flushed "
                         f"nothing, so no true per-process peak exists for this row)")
        r["run_status"], r["verdict"] = "rss_killed", "rss_killed"
        r["launched"], r["wall_s"] = "1", f"{time.time() - t0:.2f}"
        # Populate from the sampled kill peak rather than leaving it blank.  The
        # kill destroys its own measurement: `/usr/bin/time` writes its report at
        # exit and never reaches that point under SIGKILL, so `max_rss_kb`,
        # `user_s` and `sys_s` came back EMPTY on the one real rss_killed row.
        # An empty cell is worse than a bounded one -- it reads as "not measured"
        # when the truth is "measured, by a different instrument, as at least
        # this much".  The provenance is spelled out in `detail` so the two
        # sources can never be confused; `user_s`/`sys_s` stay blank because
        # nothing measured them at all.
        r["max_rss_kb"] = str(killed_at)
        r["max_rss_source"] = SRC_SAMPLED
        # Normally blank: the sampled killer is DISARMED under cgroup
        # enforcement, so reaching here with a cgroup is not expected.  Filled
        # anyway if one exists, because discarding a reading the kernel already
        # took is what this whole change is fixing.
        if cg is not None and cg_peak is not None:
            r["cgroup_peak_kb"] = str(cg_peak)
        return r
    # WHOLE TARGET, measured end to end: with `--prove` this spans both stages.
    row["wall_s"] = f"{time.time() - t0:.2f}"
    sim_t = read_time_report(tv)
    for k, v in sim_t.items():
        row[f"sim_{k}"] = v
    # The generic columns start as the sim stage's and are widened to totals
    # below if a proof stage ran.  Without `--prove` that is the whole story.
    for k in ("max_rss_kb", "user_s", "sys_s"):
        if k in sim_t:
            row[k] = sim_t[k]
    if row["max_rss_kb"]:
        # Only once a figure is actually present.  A `/usr/bin/time` report that
        # could not be read leaves `max_rss_kb` blank, and a blank figure keeps
        # a blank provenance.
        row["max_rss_source"] = SRC_TIME
    if proof_rc is not None:
        # The proof stage's OWN report, in its OWN columns.  Its provenance is
        # definitionally `/usr/bin/time`: it is the only instrument that writes
        # these, and if the stage was killed before flushing they come back BLANK
        # rather than from some other source.  So there is no `proof_max_rss_source`
        # to disambiguate -- there is nothing it could say but one value or
        # "not measured", and blank already says the latter.
        proof_t = read_time_report(log_dir / f"{m}.proof.time")
        for k, v in proof_t.items():
            row[f"proof_{k}"] = v
        if row.get("proof_max_rss_kb"):
            row["proof_max_rss_source"] = SRC_TIME
        if proof_sampled_kill:
            # SIGKILL: `/usr/bin/time` writes its report at exit and never got
            # there, so `proof_max_rss_kb` is blank. The sampled peak at the kill
            # is a real measurement and a LOWER BOUND -- better than an empty
            # cell, which reads as "not measured" when the truth is "measured, by
            # a different instrument, as at least this much". Labelled as such so
            # it can never be mistaken for a /usr/bin/time peak.
            row["proof_max_rss_kb"] = str(_RSS_PEAK["killed_at_kb"])
            row["proof_max_rss_source"] = SRC_SAMPLED
        # Widen the generic columns from "the sim stage" to "the whole target":
        # max over stages for RSS, SUM over stages for CPU.  `wall_s` already
        # spanned both.  Leaving `user_s`/`sys_s` sim-only -- which is what they
        # were -- put three columns side by side with two different scopes.
        if row.get("proof_max_rss_kb") and row.get("max_rss_kb"):
            row["max_rss_kb"] = str(max(int(row["max_rss_kb"]),
                                        int(row["proof_max_rss_kb"])))
            row["max_rss_source"] = SRC_TIME_STAGES
        elif row.get("proof_max_rss_kb"):
            row["max_rss_kb"] = row["proof_max_rss_kb"]
            row["max_rss_source"] = SRC_TIME_STAGES
        if proof_sampled_kill:
            # A max over stages one of which is a LOWER BOUND is itself a lower
            # bound: the true proof peak is at least `killed_at`. The weaker
            # claim is the only honest one, so the whole-target provenance drops
            # to it rather than keeping the stronger label.
            row["max_rss_source"] = SRC_SAMPLED
        for k in ("user_s", "sys_s"):
            a_, b_ = row.get(f"sim_{k}"), row.get(f"proof_{k}")
            if a_ and b_:
                row[k] = f"{float(a_) + float(b_):.2f}"
            elif b_ and not a_:
                row[k] = b_

    if cg is not None:
        # The point of the change: retained on the SUCCESSFUL path too, not just
        # where the kernel killed the probe.  `cg_peak` is read in the `finally`
        # above and was, until now, discarded here on every non-OOM path -- so a
        # green cgroup run kept no exact peak at all.  Reaching this line proves
        # `cg_peak is not None`; the unreadable case returned `runner_error`.
        row["cgroup_peak_kb"] = str(cg_peak)
    (log_dir / f"{m}.log").write_text(out, encoding="utf-8")

    if native:
        # The kernel-checked claim, plus the axiom list it depends on.
        row["compile"] = 1 if ("error" not in out and rc == 0) else 0
        ax = re.search(r"depends on axioms: \[([^\]]*)\]", out, re.S)
        row["detail"] = classify(out, rc, timeout) or (
            " ".join(ax.group(1).split()) if ax else "")
        if rc == 124:
            _mark_timeout(row, timeout)   # after `detail`, which classify rewrote
        return row

    extract_gates(row, out, rc, timeout, expect_module=m, expect_samples=samples)
    # STRICTLY after `extract_gates`, and it only ever writes `row["proof"]`.
    # `proof` is the last gate: it can be 0 on a design whose earlier gates are
    # all 1, and it can never pull an earlier gate down.
    extract_proof_gate(row, proof_out, proof_rc, expect_module=m, oom=proof_oom,
                       sampled_kill_kb=(_RSS_PEAK["killed_at_kb"]
                                        if proof_sampled_kill else 0),
                       refused=proof_refused)
    if rc == 124:
        # After `extract_gates`, which writes `detail` and the raw gates.
        _mark_timeout(row, timeout)
    return row


def _as_int(v, default=-1) -> int:
    try:
        return int(v)
    except (TypeError, ValueError):
        return default


def _one(pat, out):
    """Exactly one match, or None.

    Strictness is the point: a probe that printed two conflicting gate lines --
    a stale one plus a fresh one, say -- must not have whichever `re.search`
    happens to find first silently believed.
    """
    ms = re.findall(pat, out, re.M)
    return ms[0] if len(ms) == 1 else None


def check_gate(row) -> tuple:
    """Is this row's agreement result worth anything?  (ok, reason)

    Applicability is judged against `mutable_*`, which the harness derives from
    the `DesignCert` DESCRIPTOR widths -- never from the mutators themselves.
    Asking a mutator whether it applies and then checking its answer against
    itself is circular: a `mutateOutput` regressed to `none` everywhere would
    report `mut_out=na, mutable_out=0` and pass.

    Everything is required and range-checked.  A field that is missing, or an
    applicability flag that is not exactly "0" or "1", fails the gate: the usual
    way to lose a gate is for its line never to be printed, and a flag of "2"
    would otherwise fall through to the `na` branch and be credited.
    """
    if row.get("selftest_base") != "1":
        return False, f"selftest_base={row.get('selftest_base') or 'missing'}"

    mutable_any = False
    for field, mut_key, count_key in (("mut_out", "mutable_out", "outputs"),
                                      ("mut_flop", "mutable_flop", "flops"),
                                      ("mut_mem", "mutable_mem", "mems")):
        got = row.get(field)
        flag = row.get(mut_key)
        if flag not in ("0", "1"):
            return False, f"{mut_key}={flag or 'missing'} is not 0 or 1"
        n = _as_int(row.get(count_key))
        if n < 0:
            return False, f"{count_key} missing or non-numeric, so the row's shape is unusable"
        # The descriptor-derived flag and the raw count must be consistent: an
        # absent observable cannot be mutable, and a mutable one cannot be absent.
        if flag == "1" and n == 0:
            return False, f"{mut_key}=1 but {count_key}=0"
        want = "rejected" if flag == "1" else "na"
        if got != want:
            return False, f"{field}={got or 'missing'} but {mut_key}={flag} requires {want}"
        mutable_any = mutable_any or flag == "1"

    if not mutable_any:
        # Nothing could be mutated, so nothing proved the checker can reject a
        # wrong answer on THIS design.  Agreement here is untested machinery.
        return False, "no mutable observable: the checker was not exercised on this design"
    return True, ""


def extract_gates(row, out, rc, timeout, expect_module=None, expect_samples=None) -> None:
    """Pure function of the probe log: every gate this run actually passed.

    TRUST ORDER.  Nothing from the log is credited until the log is shown to
    describe THIS certificate.  An earlier version set compile/reify/typecheck
    from the text first and validated identity afterwards, so a stale clean log
    for a different module still earned `verdict=typecheck`.  `cert` is the only
    gate established independently of the log -- it comes from reading the
    certificate file -- so it is the only one that may survive an identity or
    shape failure.
    """
    def only_cert(detail: str) -> None:
        for g in ("compile", "reify", "typecheck", "sim", "checker", "agree"):
            row[g] = 0
        row["detail"] = detail

    if "compileDesign refused" in out:
        only_cert("compileDesign refused the certificate")
        return

    shape = _one(r"^D3GATE module=\S+ (.*)$", out)
    # either reifier: the two emit the same line shape on purpose, so the gate
    # does not have to know which mode the run is in.
    emitted = _one(r"^reify_design(?:_named)?: \S+ emitted, \d+ sources, (\d+) bindings$", out)
    clean_exit = rc == 0 and not ERR_RE.search(out) and "error:" not in out

    if shape is None:
        # No identity anchor in the log.  The run died before reporting, which is
        # a legitimate early failure rather than a wrong-log hazard: this log was
        # written by this run into its own per-run directory.  Credit only the
        # gates the text supports and stop.
        if emitted is not None:
            row["compile"] = 1
            row["reify"] = 1
        row["typecheck"] = 1 if clean_exit else 0
        row["detail"] = classify(out, rc, timeout) or (
            "no D3GATE shape line, or more than one" if emitted is not None else "")
        return

    # ---- identity and shape validation, before any gate is credited --------
    shape_fields, dup_keys = {}, []
    for kv in shape.split():
        k, _, v = kv.partition("=")
        if k in shape_fields:
            dup_keys.append(k)
        shape_fields[k] = v
    if dup_keys:
        only_cert(f"shape line repeats key(s) {sorted(set(dup_keys))}: it cannot be read")
        return
    for k in ("sources", "nodes", "outputs", "flops", "mems", "inputs", "bindings"):
        if _as_int(shape_fields.get(k)) < 0:
            only_cert(f"shape field {k}={shape_fields.get(k)!r} is missing or not a count")
            return
    logged = _one(r"^D3GATE module=(\S+) ", out)
    if expect_module is not None and logged != expect_module:
        only_cert(f"log reports module {logged!r}, expected {expect_module!r}: "
                  "this log does not describe this certificate")
        return
    if emitted is not None:
        em = re.search(r"^reify_design(?:_named)?: \S+ emitted, (\d+) sources, (\d+) bindings$",
                       out, re.M)
        if em and (em.group(1) != shape_fields.get("sources")
                   or em.group(2) != shape_fields.get("bindings")):
            only_cert(f"reifier reported {em.group(1)} sources/{em.group(2)} bindings "
                      f"but the shape line says {shape_fields.get('sources')}/"
                      f"{shape_fields.get('bindings')}")
            return

    # ---- the log is ours and well formed; now credit gates ----------------
    for k, v in shape_fields.items():
        if k in row:
            row[k] = v
    if emitted is not None:
        # reify_design runs compileDesign at elaboration time and throws when it
        # refuses, so reaching "emitted" implies acceptance.
        row["compile"] = 1
        row["reify"] = 1
    row["typecheck"] = 1 if clean_exit else 0
    row["sim"] = 1 if len(re.findall(r"^D3GATE sim_exec=1 ", out, re.M)) == 1 else 0
    for k, pat in (("selftest_base", r"\bselftest_base=(\d)"),
                   ("mut_out", r"\bmut_out=(\S+)"),
                   ("mut_flop", r"\bmut_flop=(\S+)"),
                   ("mut_mem", r"\bmut_mem=(\S+)"),
                   ("mutable_out", r"\bmutable_out=(\d)"),
                   ("mutable_flop", r"\bmutable_flop=(\d)"),
                   ("mutable_mem", r"\bmutable_mem=(\d)"),
                   ("distinct_obs", r"\bdistinct_obs=(\d+)")):
        v = _one(pat, out)
        if v is not None:
            row[k] = v

    ag_raw = _one(r"^D3GATE agree=(\S+) ", out)
    row["agree"] = 1 if ag_raw == "1" else 0
    agree_malformed = ag_raw not in ("0", "1")

    got_samples = _one(r"\bsamples=(\d+)", out)
    if expect_samples is not None and got_samples != str(expect_samples):
        row["checker"] = 0
        row["agree"] = 0
        row["detail"] = (f"agree line reports samples={got_samples}, "
                         f"requested {expect_samples}")
        return

    # A nonzero Lean exit invalidates everything the log appeared to show: the
    # gate lines may have been printed before the failure that killed the run.
    if not clean_exit:
        row["checker"] = 0
        row["agree"] = 0
        row["detail"] = classify(out, rc, timeout) or f"nonzero exit {rc} after gate output"
        return

    ok, why = check_gate(row)
    row["checker"] = 1 if ok else 0
    if not ok:
        row["detail"] = f"CHECKER SELF-TEST FAILED: {why}"
        return
    if (_as_int(row.get("outputs")) == 0 and _as_int(row.get("flops")) == 0
            and _as_int(row.get("mems")) == 0):
        row["checker"] = 0
        row["detail"] = "NO OBSERVABLE BEHAVIOUR: no outputs, flops or memories to compare"
        return
    if agree_malformed:
        row["detail"] = f"malformed or missing agree value: {ag_raw!r}"
        return
    row["detail"] = classify(out, rc, timeout)
    if row["agree"] == 0 and not row["detail"]:
        row["detail"] = "reified def disagrees with denoteResidual on sampled stimuli"


def verdict(row) -> str:
    """The last gate actually passed."""
    last = "none"
    for g in ("cert", "compile", "reify", "typecheck", "sim", "checker", "agree"):
        if row.get(g) == 1:
            last = g
        else:
            break
    return last


TIERS = [("tiny", 0, 1000), ("small", 1000, 5000), ("mid", 5000, 20000),
         ("large", 20000, 60000), ("huge", 60000, 10 ** 9)]


def tier_of(nodes) -> str:
    n = _as_int(nodes)
    if n < 0:
        return "unknown"
    for name, lo, hi in TIERS:
        if lo < n <= hi or (lo == 0 and n <= hi):
            return name
    return "huge"


RESULT_COLS = (["target_key", "module", "verdict"] + GATES + [
    "selftest_base", "mut_out", "mut_flop", "mut_mem",
    "mutable_out", "mutable_flop", "mutable_mem", "distinct_obs",
    "sources", "nodes", "outputs", "flops", "mems", "inputs", "bindings",
    "tier", "manifest_nodes", "requested_samples", "cert_sha256", "run_status",
    "drift", "launched",
    "max_rss_kb", "max_rss_source", "cgroup_peak_kb",
    "user_s", "sys_s", "wall_s",
    "sim_max_rss_kb", "sim_user_s", "sim_sys_s", "sim_wall_s",
    "proof_max_rss_kb", "proof_max_rss_source",
    "proof_user_s", "proof_sys_s", "proof_wall_s",
    "detail"])

# COLUMN SCOPES, so no generic column quietly mixes them.
#
#   max_rss_kb / user_s / sys_s / wall_s   WHOLE TARGET: every stage that ran.
#       `max_rss_kb` is the max over stages, `user_s`/`sys_s` are sums, `wall_s`
#       is measured end to end across both.
#   sim_*                                  the executable probe alone.
#   proof_*                                the `--prove` probe alone.
#   cgroup_peak_kb                         the probe's cgroup, over both stages.
#
# Without `--prove` only one stage runs, so the generic columns and `sim_*` hold
# the same figures and `proof_*` is blank.  Before this, `wall_s` spanned both
# stages while `user_s`/`sys_s` were silently sim-only -- three columns side by
# side with two different scopes.
STAGE_COLS = ("sim_max_rss_kb", "sim_user_s", "sim_sys_s", "sim_wall_s",
              "proof_max_rss_kb", "proof_max_rss_source",
              "proof_user_s", "proof_sys_s", "proof_wall_s")

# Provenance for `max_rss_kb`, and the cgroup's own peak beside it.
#
# Three different instruments can fill `max_rss_kb`, and they measure three
# DIFFERENT quantities: `/usr/bin/time`'s max over the wait tree, the sampled
# aggregate over the marked descendant tree (a lower bound), and the cgroup's
# `memory.peak` (an exact cgroup-ACCOUNTED peak for the whole subtree).  The
# column alone never said which, and the sidecar's `rss_killed_max_rss_source`
# describes killed rows only -- so a SUCCESSFUL cgroup run retained no exact
# peak at all.  `cgroup_peak_kb` is a separate column rather than a relabelling
# of `max_rss_kb`, because the two are not substitutes and a run wants both.
#
# CHARGE IS NOT RSS, and the gap is large enough to mislead a reader who treats
# the columns as comparable.  `memory.peak` counts what THIS cgroup was charged;
# a file page already in the page cache is not re-charged to a second cgroup
# that maps it.  Lean mmaps ~5.8 GB of Mathlib oleans, so on a warm cache those
# pages are charged to whoever faulted them in first and not to the probe.
# Measured on `tima_adder`: max_rss_kb 1,566,244 kB against cgroup_peak_kb
# 168,212 kB, with ZERO major page faults -- the probe read nothing from disk.
# The synthetic allocator, which uses ANONYMOUS memory, shows near agreement
# instead (38,232 vs 44,176 kB), because anon is always charged to the allocator.
# Consequence for limit-setting: `--kill-over-rss-kb N` becomes `memory.max = N`,
# which bounds CHARGE, not RSS, and is therefore more permissive in RSS terms on
# a warm cache than the same N as a sampled RSS cap.  It still bounds what
# actually blows up here, since elaboration heap is anonymous.
#
# Part of the schema `--resume` requires in full, like every other column.  An
# earlier revision made them resume-optional and backfilled reused rows with
# `unknown-pre-schema`, which was unreachable dead code: `TOOL_FILES` includes
# this file, so a table genuinely written before these columns necessarily
# carries a different `tool_digest` and is refused by the CONFIG comparison long
# before any column is inspected.  The only way to reach the exception was to
# fabricate a table that cannot exist.  Migrating a real old run is an offline
# job for a migration tool, not something `--resume` should do silently.
PROVENANCE_COLS = ("max_rss_source", "cgroup_peak_kb")

# `max_rss_kb` came from `/usr/bin/time -v`: the maximum over the wait tree.
SRC_TIME = "time-max-rss"
# `max_rss_kb` holds the cgroup's `memory.peak`: exact, cgroup-accounted, whole
# subtree.  Only ever set where the kernel killed the probe at `memory.max`.
SRC_CGROUP = "cgroup-memory-peak-exact-accounted"
# `max_rss_kb` holds the sampled aggregate at the moment of the kill: a LOWER
# BOUND, because the sampler can miss a peak between two samples.
SRC_SAMPLED = "sampled-aggregate-lower-bound"
# `max_rss_kb` is the MAX over the stages that ran, each measured by its own
# `/usr/bin/time`. Only a `--prove` run that reached the proof stage uses it.
# Named distinctly so a reader cannot mistake a two-stage maximum for one
# process's peak; the per-stage figures are in `user_s`/`sys_s`/`wall_s` and the
# `proof_*` columns.
SRC_TIME_STAGES = "time-max-rss-max-of-stages"


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--certs", nargs="+", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--manifest", default="",
                    help="canonical target list (coreet_122.tsv or cva6_30.tsv). "
                         "Without it the run is exploratory, not canonical.")
    ap.add_argument("--jobs", type=int, default=4)
    ap.add_argument("--timeout", type=int, default=900)
    ap.add_argument("--samples", type=int, default=32)
    ap.add_argument("--limit", type=int, default=0)
    ap.add_argument("--only", default="")
    ap.add_argument("--tier", default="",
                    help="run only this size tier: " + ",".join(t[0] for t in TIERS))
    ap.add_argument("--one-per-tier", action="store_true",
                    help="the LARGEST design in each tier, to bound resources before a long run")
    ap.add_argument("--resume", action="store_true",
                    help="reuse terminal rows from an existing --out written by the SAME config")
    ap.add_argument("--runner-selftest", action="store_true",
                    help="honour D3_ARTIFACT_DIR (runner tests only; never canonical)")
    ap.add_argument("--allow-dirty", action="store_true",
                    help="permit a manifest run from a dirty worktree (exploratory only: "
                         "the result cannot be reproduced from a commit)")
    ap.add_argument("--force", action="store_true",
                    help="overwrite an existing --out that was not produced by --resume")
    ap.add_argument("--order-by", default="",
                    help="TSV with module/max_rss_kb columns; run order becomes "
                         "cheapest-RSS-first (scheduling only, never evidence)")
    ap.add_argument("--via-lake", action="store_true",
                    help="run each probe as `lake env lean` (two processes) instead "
                         "of exec'ing the resolved lean binary directly. Slower and "
                         "~811 MB more resident per probe; kept so the two forms can "
                         "be compared rather than assumed equal.")
    ap.add_argument("--defer-over-rss-kb", type=int, default=0,
                    help="do not START targets whose recorded peak RSS exceeds this "
                         "(or that have no recorded RSS); they are written as "
                         "run_status=deferred and picked up by a later --resume. "
                         "Requires --order-by. Scheduling only.")
    ap.add_argument("--kill-over-rss-kb", type=_nonneg_int, default=0,
                    help="HARD limit, SAMPLED AND ADVISORY: terminate the running "
                         "probe(s) once a sample of this run's aggregate resident "
                         "total reaches it. 0 disables. Unlike "
                         "--max-aggregate-rss-kb this stops a probe that is ALREADY "
                         "RUNNING; the row becomes rss_killed and resumable. It "
                         "bounds SUSTAINED memory, NOT instantaneous peaks: a peak "
                         "between samples passes unobserved. Measured: one run "
                         "exceeded a 19,000,000 kB limit by 161,996 kB and was never "
                         "killed, while another was caught 20,996 kB over. Use "
                         "--rss-sample-seconds to trade overhead for tightness.")
    ap.add_argument("--max-aggregate-rss-kb", type=_nonneg_int, default=20_000_000,
                    help="SOFT cap, sampled: stop LAUNCHING new work once a sample "
                         "of this run's aggregate resident total reaches it; 0 "
                         "disables. It never KILLS anything: a running executable "
                         "probe is left to finish. With --prove it does affect a "
                         "running target, because a proof stage that has not started "
                         "yet is new work and is suppressed (the row keeps its "
                         "executable gates and reports proof=na). Without --prove, "
                         "at --jobs 1 with one target, it has no effect. Logged "
                         "once, at the first crossing.")
    ap.add_argument("--enforce", choices=("sampled", "cgroup", "auto"),
                    default="sampled",
                    help="how --kill-over-rss-kb is enforced. `sampled` (default) "
                         "polls every --rss-sample-seconds and is ADVISORY: a peak "
                         "between samples passes unobserved. `cgroup` uses cgroup v2 "
                         "memory.max, which the kernel enforces continuously and "
                         "which reports an EXACT memory.peak; it REFUSES to run if "
                         "delegation is unavailable rather than silently degrading. "
                         "`auto` uses cgroup when available and sampled otherwise.")
    ap.add_argument("--rss-sample-seconds", type=_positive_finite, default=5.0,
                    help="how often the aggregate RSS sampler runs. Smaller tightens "
                         "the advisory hard limit at the cost of more /proc reads; it "
                         "cannot make the limit exact.")
    ap.add_argument("--dry-run", action="store_true",
                    help="print the selected targets IN RUN ORDER and exit, "
                         "writing nothing")
    ap.add_argument("--proof-segment-size", type=int, default=0, metavar="N",
                    help="emit the incremental walk in segments of at most N "
                         "bindings (0 = one monolithic walk, the default). Sets "
                         "`d3.segment` in the PROOF probe only, and requires "
                         "--reifier named.")
    ap.add_argument("--reifier", choices=("legacy", "named"), default="legacy",
                    help="which reifier BOTH stages use. `legacy` (default) is "
                         "`reify_design` + `prove_reified`, unchanged. `named` is "
                         "`reify_design_named` + `prove_reified_incr`, where the "
                         "executable and proof stages share ONE emitted model. The "
                         "setting drives both stages together: simulating one "
                         "function while proving another would credit a theorem "
                         "that says nothing about what ran.")
    ap.add_argument("--prove", action="store_true",
                    help="after a target reaches `agree`, run a SECOND probe that "
                         "generates and kernel-checks the last-mile theorem "
                         "`d3_fast.correct` and audits its axioms. Off by default: "
                         "it imports Mathlib (~6.9 GB against the sim probe's "
                         "~1.5 GB), and `proof` must stay `na` wherever it was "
                         "not attempted")
    ap.add_argument("--native", action="store_true",
                    help="run the UNMODIFIED certificate: compilesOk by native_decide + axioms")
    a = ap.parse_args()

    # ARGUMENT VALIDATION FIRST: cheap, and the same answer on every host.
    # Ordering host detection before this made the diagnostic for a plainly
    # invalid command line vary by machine -- an unavailable host reported "not
    # available" while an available one reported "needs --kill-over-rss-kb" for
    # the identical argv. A bad command line is bad everywhere.
    if a.enforce in ("cgroup", "auto") and a.kill_over_rss_kb <= 0:
        print(f"REFUSING: --enforce {a.enforce} needs a POSITIVE "
              f"--kill-over-rss-kb to set memory.max; got {a.kill_over_rss_kb}. "
              f"Zero leaves the cgroup with no limit at all.", file=sys.stderr)
        return 2

    if a.runner_selftest:
        os.environ["D3_RUNNER_SELFTEST"] = "1"
    elif os.environ.get("D3_ARTIFACT_DIR"):
        # Inherited from the environment without the flag: refuse rather than
        # silently ignore, because the two readings differ in what gets checked.
        print("REFUSING: D3_ARTIFACT_DIR is set but --runner-selftest was not passed. "
              "That variable changes what is hashed, not what Lean loads, so honouring "
              "it outside the runner tests would validate files the probes never use.",
              file=sys.stderr)
        return 2

    if _TEST_KILL_BETWEEN_STAGES and a.manifest:
        # Same reasoning as the scripted sampler below: it fabricates the budget
        # state the scheduler acts on, so a canonical run must never see it.
        print("REFUSING a manifest run: D3_TEST_KILL_BETWEEN_STAGES is set, which "
              "fabricates a budget trip between the probe stages (hard, or soft "
              "with the `:soft` suffix). It exists for the guard regressions and "
              "can never produce evidence.", file=sys.stderr)
        return 2
    if _TEST_RSS_SEQ and a.manifest:
        # It replaces the MEASUREMENT the memory budget is enforced from, so a
        # canonical run must never see it -- a sweep that believes a scripted
        # sequence is not measuring this machine at all.
        print("REFUSING a manifest run: D3_TEST_RSS_SEQ is set, which replaces the "
              "aggregate RSS measurement with a scripted sequence. It exists for the "
              "monitor regressions and can never produce evidence.", file=sys.stderr)
        return 2

    out_path = pathlib.Path(a.out)
    meta_path = out_path.with_suffix(out_path.suffix + ".meta.json")
    manifest_digest = ""

    if a.manifest:
        try:
            targets, skipped, manifest_digest = load_manifest(
                pathlib.Path(a.manifest), a.certs)
        except ManifestError as e:
            print(f"MANIFEST ERROR: {e}", file=sys.stderr)
            return 2
    else:
        certs = []
        for d in a.certs:
            certs += sorted(pathlib.Path(d).glob("*_Lgraph.lean"))
        targets, skipped = [Target(module_of(c), module_of(c), c, "", "") for c in certs], []

    if a.only:
        rx = re.compile(a.only)
        targets = [t for t in targets if rx.search(t.key) or rx.search(t.module)]
    if a.tier:
        targets = [t for t in targets if tier_of(t.nodes) == a.tier]
    if a.one_per_tier:
        # LARGEST per tier.  The smallest is a useless resource bound -- a tiny
        # design can be zero nodes -- and the point of this mode is to choose
        # jobs and timeouts from a conservative measurement.
        best = {}
        for t in targets:
            tn = tier_of(t.nodes)
            if tn not in best or _as_int(t.nodes) > _as_int(best[tn].nodes):
                best[tn] = t
        # In TIER order, smallest first.  `sorted` over tier NAMES is alphabetical
        # -- "mid" < "small" < "tiny" -- so at jobs=1 the largest design ran first
        # and two cheap results sat behind it for 46 minutes, leaving an
        # interrupted run with nothing to show.
        rank = {name: i for i, (name, _, _) in enumerate(TIERS)}
        targets = [best[k] for k in sorted(best, key=lambda n: rank.get(n, len(TIERS)))]
    if a.limit:
        targets = targets[: a.limit]
    rss_map, deferred_targets = {}, []
    if a.order_by:
        # AFTER --limit on purpose: --limit still selects its subset the way it
        # always did, and this only changes the order that subset runs in.
        targets, rss_map = order_by_rss(targets, pathlib.Path(a.order_by))
    elif a.defer_over_rss_kb:
        print("--defer-over-rss-kb needs --order-by: the threshold is compared "
              "against the RSS table that option reads", file=sys.stderr)
        return 2
    if a.defer_over_rss_kb:
        # `targets` deliberately keeps ALL of them. The selection digest, the
        # sidecar and the denominator describe the SET under study; which of its
        # members this particular invocation chose to start is a schedule, and a
        # schedule must not be able to shrink the experiment silently.
        keep = []
        for t in targets:
            seen_rss = rss_map.get(t.module)
            if seen_rss is None:
                deferred_targets.append(
                    (t, "no recorded peak RSS, so it cannot be shown to fit the "
                        "memory budget"))
            elif seen_rss > a.defer_over_rss_kb:
                deferred_targets.append(
                    (t, f"recorded peak RSS {seen_rss} kB exceeds the "
                        f"{a.defer_over_rss_kb} kB launch threshold"))
            else:
                keep.append(t)
        run_now = keep
    else:
        run_now = list(targets)
    if not targets:
        print("no targets selected", file=sys.stderr)
        return 2

    global LEAN_BIN, LEAN_ENV, LEAN_ENV_DIGEST
    if not a.via_lake:
        LEAN_BIN, LEAN_ENV, LEAN_ENV_DIGEST = resolve_lean()
        if not LEAN_BIN:
            print("REFUSING: could not resolve the lean binary through lake; "
                  "pass --via-lake to use the two-process form deliberately",
                  file=sys.stderr)
            return 2

    global CGROUP_BASE, CGROUP_MAX_KB
    if a.enforce in ("cgroup", "auto"):
        CGROUP_BASE = cgroup_base()
        CGROUP_MAX_KB = a.kill_over_rss_kb
        if CGROUP_BASE is None and a.enforce == "auto":
            print("d3_sweep: --enforce auto: cgroup v2 memory delegation is not "
                  "usable here (a child could not be migrated into a test cgroup), "
                  "falling back to the SAMPLED advisory limit. The sidecar records "
                  "sampled-advisory as the actual enforcement.", file=sys.stderr)
        if CGROUP_BASE is None and a.enforce == "cgroup":
            print("REFUSING: --enforce cgroup was requested but cgroup v2 memory "
                  "delegation is not available here. Running under the sampled "
                  "advisory limit while claiming kernel enforcement would be a "
                  "false guarantee; pass --enforce sampled to accept the weaker "
                  "one deliberately.", file=sys.stderr)
            return 2
        if CGROUP_BASE is not None:
            CGEXEC_PATH.parent.mkdir(parents=True, exist_ok=True)
            CGEXEC_PATH.write_text(CGEXEC_SRC)

    # Decided here, beside the enforcement choice, because the sidecar records it
    # long before the sampler thread starts. When the KERNEL enforces the limit
    # the sampled killer is disarmed: left armed it can win the race and emit
    # SAMPLED kill evidence -- an rss_killed row whose max_rss_kb is a sampled
    # lower bound -- under a sidecar claiming `cgroup-memory-peak-exact`. The two
    # would disagree about the same row. Sampling still runs for the soft cap and
    # the aggregate measurement; only the kill moves to the kernel.
    # HYBRID under cgroup enforcement: the kernel's `memory.max` AND the sampled
    # aggregate hard kill, both armed at the same ceiling.
    #
    # The sampled killer used to be disarmed here, so that it could not win a
    # race and write a sampled LOWER BOUND into `max_rss_kb` under a sidecar
    # claiming an exact cgroup peak.  That reasoning was about provenance and is
    # answered by recording WHICH guard acted; what it missed is that the two
    # guards do not measure the same thing.  `memory.max` bounds cgroup CHARGE,
    # and a page already in the page cache is not re-charged to a second cgroup
    # that maps it -- so a probe mapping warm Mathlib oleans is nearly invisible
    # to it.  Measured on the tima_adder proof probe: 6,897,312 kB RSS against a
    # cgroup peak of 694,948 kB, with zero major page faults.  A 10,000,000 kB
    # `memory.max` was therefore not enforcing anything close to a 10 GB RSS
    # ceiling, which is the quantity that matters on a shared machine.
    #
    # So: keep the kernel guard for anonymous charge, where it is exact and
    # continuous, and arm the sampled guard for resident set, where it is the
    # only instrument that sees the mapping at all.  Sampled remains ADVISORY --
    # it bounds sustained memory, not instantaneous peaks -- which is why it is
    # the SECONDARY guard and not a replacement.
    sampled_kill_kb = a.kill_over_rss_kb

    sel_digest = selection_digest(targets)
    cfg = run_config(a, manifest_digest)
    cfg["selection_digest"] = sel_digest
    # Part of `config`, not of `scheduling`: WHICH lean binary ran a probe can
    # change what the probe reports, so a resumed run must not mix the two.
    cfg["lean_bin"] = LEAN_BIN
    cfg["via_lake"] = bool(a.via_lake)
    # The resolved EXECUTION ENVIRONMENT, not just the binary: two runs that
    # differ in LD_LIBRARY_PATH or LEAN_PATH ran different code even with the
    # same `lean` on disk, so a resume must not mix them.
    cfg["lean_env_digest"] = LEAN_ENV_DIGEST
    # SCHEDULING metadata lives beside `config`, never inside it.  `--resume`
    # refuses a run whose `config` differs, and it SHOULD: samples, timeout and
    # the manifest change what a row means.  A schedule does not.  Resuming the
    # deferred remainder WITHOUT `--defer-over-rss-kb` has to be allowed, so
    # these keys must not be part of that comparison.
    sched = {
        "jobs": a.jobs,
        "order_by": a.order_by,
        "order_by_digest": (hashlib.sha256(pathlib.Path(a.order_by).read_bytes()).hexdigest()
                            if a.order_by else ""),
        # Recorded so a merge can DOCUMENT which selection controls differed
        # between stages instead of inferring it from the selection digest alone.
        "only": a.only,
        "limit": a.limit,
        "one_per_tier": bool(a.one_per_tier),
        "defer_over_rss_kb": a.defer_over_rss_kb,
        "max_aggregate_rss_kb": a.max_aggregate_rss_kb,
        "kill_over_rss_kb": a.kill_over_rss_kb,
        "rss_sample_seconds": a.rss_sample_seconds,
        # The EFFECTIVE interval, because nothing else can change it: there is no
        # environment override. A reader can take this as what actually ran.
        "rss_sample_seconds_effective": a.rss_sample_seconds,
        # Machine-readable provenance for `max_rss_kb` on an rss_killed row.
        # `detail` says the same thing in prose, but prose is not checkable.
        # Run-wide and KILLED-ROW-ONLY, which is why the per-row
        # `max_rss_source` column exists beside it: this field says nothing about
        # a green row, and a green cgroup row's exact peak now lives in
        # `cgroup_peak_kb`.  The same constants as the row column, so the two can
        # never drift into describing the same thing differently.
        # Under HYBRID enforcement either guard can be the one that acts, and
        # they write different quantities into `max_rss_kb`, so a single
        # run-wide answer would be a guess. The row says which acted, in its own
        # `max_rss_source`; this states that it is per-row rather than naming a
        # mechanism that may not have been the one.
        "rss_killed_max_rss_source": (
            "per-row: see max_rss_source" if (CGROUP_BASE is not None and sampled_kill_kb)
            else SRC_CGROUP if CGROUP_BASE is not None
            else SRC_SAMPLED),
        # Both caps are SAMPLED: they bound sustained memory, not instantaneous
        # peaks.  Recorded so a reader of these rows knows which guarantee applies.
        # What ACTUALLY enforced the limit, not what was asked for.
        "rss_enforcement": ("cgroup-memory-max" if CGROUP_BASE is not None
                            else "sampled-advisory"),
        "rss_enforcement_requested": a.enforce,
        "sampled_kill_armed": bool(sampled_kill_kb),
        # What is enforcing, in full.  `rss_enforcement` names the primary
        # mechanism; this names every guard that is actually armed, because
        # under cgroup mode there are now two and they bound DIFFERENT
        # quantities (cgroup charge vs resident set).
        "rss_guards": ([g for g in (
            "cgroup-memory-max" if CGROUP_BASE is not None else "",
            "sampled-aggregate-hard-kill" if sampled_kill_kb else "",
            "sampled-aggregate-soft-stop" if a.max_aggregate_rss_kb else "",
        ) if g]),
        "cgroup_base": str(CGROUP_BASE) if CGROUP_BASE is not None else "",
        "deferred": [{"module": t.module, "why": why} for t, why in deferred_targets],
    }
    cfg["tool_digest"] = tool_digest()
    cfg["worktree_dirty"] = bool(subprocess.run(
        ["git", "-C", str(ROOT), "status", "--porcelain"],
        capture_output=True, text=True).stdout.strip())
    # The toolchain is part of the experiment: a Lean or Lake upgrade can change
    # what elaborates, and TOOL_FILES cannot see outside the repository.
    #
    # Probed with cwd=LEAN_DIR, which is where the probes actually run.  elan
    # selects a toolchain from the nearest `lean-toolchain`, and the repository
    # ROOT has none -- asking there answered with elan's DEFAULT (4.34.1) while
    # every worker ran under the pinned 4.31.0.  The sidecar then carried a
    # version that had executed nothing, and since `--resume` authenticates
    # against the sidecar, two runs on different toolchains could have been
    # merged as one experiment.
    def _probe(args):
        """The version, or "unknown" -- and "unknown" is never good enough for a
        canonical run.  A nonzero exit or empty output means the toolchain was
        not identified, and two DIFFERENT failed probes both record "unknown",
        so resume would happily match them and merge the runs."""
        try:
            r = subprocess.run(args, cwd=LEAN_DIR, capture_output=True, text=True, timeout=120)
            if r.returncode != 0:
                return "unknown"
            text = (r.stdout + r.stderr).strip()
            return text.splitlines()[0] if text else "unknown"
        except Exception:  # noqa: BLE001
            return "unknown"

    cfg["lake_version"] = _probe([LAKE, "--version"])
    cfg["lean_version"] = _probe([LAKE, "env", "lean", "--version"])
    # The version that was PREFLIGHTED must be the version that RUNS.  Probing
    # `lake env lean` while the probes exec a separately resolved binary would
    # authenticate a toolchain nothing used -- the same shape as the earlier bug
    # where `lake --version` was probed from the repository root and reported
    # 4.34.1 while every worker ran 4.31.0.
    if LEAN_BIN:
        cfg["lean_bin_version"] = _probe([LEAN_BIN, "--version"])
    tc = LEAN_DIR / "lean-toolchain"
    cfg["lean_toolchain"] = tc.read_text().strip() if tc.is_file() else "unknown"

    if a.manifest:
        # Ordered after the `unidentified` refusal below: comparing against an
        # `unknown` version would report a toolchain MISMATCH when the real
        # finding is that a version probe failed outright.
        unidentified = [k for k in ("lake_version", "lean_version", "lean_toolchain")
                        if cfg[k] == "unknown"]
        if unidentified:
            # NOT waived by --allow-dirty: that flag concedes an unreproducible
            # worktree, which is a different thing from not knowing which
            # compiler produced the rows.
            print(f"REFUSING a manifest run: toolchain not identified ({', '.join(unidentified)}). "
                  f"A canonical result must name the compiler that produced it, and 'unknown' "
                  f"is a value two different failures share.", file=sys.stderr)
            return 2

        # The version PREFLIGHTED must be the version that RUNS.  Probing
        # `lake env lean` while each probe execs a separately resolved binary
        # would authenticate a toolchain nothing used -- the same shape as the
        # earlier bug where `lake --version` was probed from the repository root
        # and reported 4.34.1 while every worker ran 4.31.0.
        if LEAN_BIN and cfg.get("lean_bin_version") != cfg["lean_version"]:
            print(f"REFUSING: the resolved lean binary reports "
                  f"{cfg.get('lean_bin_version')!r} but `lake env lean` reports "
                  f"{cfg['lean_version']!r}; these must be the same toolchain",
                  file=sys.stderr)
            return 2

    external, why = build_root_is_external()
    global PROVE, REIFIER, PROOF_SEGMENT
    PROVE = bool(a.prove)
    REIFIER = a.reifier
    if a.proof_segment_size < 0:
        print("REFUSING: --proof-segment-size must be >= 0.", file=sys.stderr)
        return 2
    if a.proof_segment_size and not a.prove:
        print("REFUSING: --proof-segment-size without --prove. Segmentation only "
              "affects the proof stage, which this run would not execute, so the "
              "option would be accepted and silently do nothing.", file=sys.stderr)
        return 2
    if a.proof_segment_size and a.reifier != "named":
        print("REFUSING: --proof-segment-size requires --reifier named. The legacy "
              "reifier proves through `prove_reified`, which has no segmented path, "
              "so the option would be accepted and silently do nothing.",
              file=sys.stderr)
        return 2
    PROOF_SEGMENT = int(a.proof_segment_size)
    cfg["runner_selftest"] = bool(a.runner_selftest)
    # SEMANTIC, not scheduling: a `--prove` run and a plain one are different
    # experiments, so they must not resume into or merge with one another.
    cfg["prove"] = bool(a.prove)
    # SEMANTIC: a named-model row and a legacy row describe different emitted
    # functions, so they must not resume into or merge with one another.
    cfg["reifier"] = a.reifier
    cfg["proof_segment_size"] = int(a.proof_segment_size)
    if a.runner_selftest:
        # Branded in the metadata rather than forbidden: the drift regressions
        # must exercise the manifest path.  The brand is what stops the result
        # being mistaken for evidence -- d3_join.py refuses a self-test sidecar,
        # and `runner_selftest` is part of the resume config, so such rows can
        # never be extended by a canonical run.
        print("NOTE: --runner-selftest — the artifact digest is redirected away from the "
              "build the probes load. This run is NOT canonical evidence.", file=sys.stderr)
    cfg["build_root"] = str(build_root())
    cfg["build_root_external"] = external
    cfg["artifact_digest"] = artifact_digest()

    if a.manifest and external:
        print(f"REFUSING a manifest run: the Lean build root is shared or outside this "
              f"worktree ({why}). Another worktree can rebuild an .olean mid-run, and a "
              f"source-level digest cannot see it -- that is how a pilot got three rows "
              f"from two different compilers.", file=sys.stderr)
        return 2

    absent = missing_oleans()
    if a.manifest and absent:
        print(f"REFUSING a manifest run: critical build artifact(s) absent from "
              f"{artifact_dir()}: {absent}. A digest over missing objects is not "
              f"evidence that the right compiler was loaded.", file=sys.stderr)
        return 2

    if a.manifest and cfg["worktree_dirty"] and not a.allow_dirty:
        # A canonical run must be reproducible from a commit.  TOOL_FILES covers
        # the files this script knows about; a dirty worktree can carry semantic
        # changes outside that list, and a result nobody can regenerate is not a
        # measurement.
        print("REFUSING a manifest run from a dirty worktree: commit first, or pass "
              "--allow-dirty for an exploratory run (its rows are not reproducible)",
              file=sys.stderr)
        return 2

    if out_path.exists() and not (a.resume or a.force):
        print(f"REFUSING to overwrite {out_path}: pass --resume to continue it, "
              f"or --force to discard it", file=sys.stderr)
        return 2

    # ---- resume ------------------------------------------------------------
    done = {}
    if a.resume:
        have_out, have_meta = out_path.is_file(), meta_path.is_file()
        if have_out != have_meta:
            # Half a run is not a run. One without the other means a partial or
            # hand-edited state, and guessing which to trust is how two
            # configurations end up merged in one table.
            print(f"--resume REFUSED: {'output' if have_out else 'sidecar'} exists without "
                  f"the other ({out_path.name} / {meta_path.name})", file=sys.stderr)
            return 2
        if not have_out:
            print("--resume: no previous output and sidecar; starting fresh", file=sys.stderr)
        else:
            try:
                prev = json.loads(meta_path.read_text())
            except (json.JSONDecodeError, OSError) as e:
                print(f"--resume REFUSED: sidecar is unreadable ({e}); "
                      f"re-run without --resume to start fresh", file=sys.stderr)
                return 2
            if not isinstance(prev, dict) or "config" not in prev:
                print("--resume REFUSED: sidecar has no config block", file=sys.stderr)
                return 2
            if prev["config"].get("aborted_artifact_drift"):
                print(f"--resume REFUSED: {meta_path.name} is marked "
                      f"aborted_artifact_drift (at {prev['config'].get('aborted_at_target')!r}, "
                      f"phase {prev['config'].get('aborted_phase')!r}). Those rows came from a "
                      f"run whose compiler changed; they cannot be extended.", file=sys.stderr)
                return 2
            if prev["config"].get("aborted_run"):
                # The GENERIC marker, which covers every abort reason including
                # ones added after this code was written.  Checked separately
                # from the drift key above so that a reason this branch has never
                # heard of still refuses rather than falling through.
                print(f"--resume REFUSED: {meta_path.name} is marked aborted_run "
                      f"(reason {prev['config'].get('aborted_reason')!r}, at "
                      f"{prev['config'].get('aborted_at_target')!r}). The run was "
                      f"void, so its rows cannot be extended.", file=sys.stderr)
                return 2
            mismatch = {k: (prev["config"].get(k), v)
                        for k, v in cfg.items() if prev["config"].get(k) != v}
            if mismatch:
                print("--resume REFUSED: configuration differs from the previous run:",
                      file=sys.stderr)
                for k, (was, now) in mismatch.items():
                    print(f"    {k}: was {was!r}, now {now!r}", file=sys.stderr)
                return 2
            # The rows must be the rows this sidecar was written for.  A table
            # edited between runs is not a table to resume from.
            prev_results_sha = prev.get("results_sha256", "")
            if not prev_results_sha and a.manifest:
                # Checking only when present would silently resume an UNBOUND
                # table, and the next checkpoint would then write a fresh
                # `results_sha256` over rows nothing ever vouched for -- blessing
                # them. A canonical run refuses instead.
                print(f"--resume REFUSED: {meta_path.name} carries no "
                      f"`results_sha256`, so the rows in {out_path.name} are not "
                      f"bound to it. Resuming would re-bind rows that nothing "
                      f"vouches for. Rerun the stage, or resume without "
                      f"--manifest for an exploratory run.", file=sys.stderr)
                return 2
            if prev_results_sha:
                actual = hashlib.sha256(out_path.read_bytes()).hexdigest()
                if actual != prev_results_sha:
                    print(f"--resume REFUSED: {out_path.name} hashes to "
                          f"{actual[:16]} but its sidecar records "
                          f"{prev_results_sha[:16]}. The results table changed "
                          f"after the run that wrote it.", file=sys.stderr)
                    return 2

            prev_hashes = prev.get("cert_sha256", {})
            live = {t.key: t for t in targets}
            seen = set()
            try:
                prev_rows = read_manifest_rows(out_path)
            except (OSError, csv.Error) as e:
                print(f"--resume REFUSED: previous output is unreadable ({e})", file=sys.stderr)
                return 2
            # The FULL schema, not a handful of identity columns.  A row with
            # blank gate fields would otherwise be reused as `done` and counted
            # as a measurement that never happened.
            with out_path.open(encoding="utf-8") as fh:
                header = next((l for l in fh if not l.startswith("#")), "")
            have_cols = header.rstrip("\n").split("\t")
            missing_cols = set(RESULT_COLS) - set(have_cols)
            if missing_cols:
                print(f"--resume REFUSED: previous output is missing column(s) "
                      f"{sorted(missing_cols)}", file=sys.stderr)
                return 2
            for r in prev_rows:
                if any(r.get(c) is None for c in RESULT_COLS):
                    print(f"--resume REFUSED: truncated row in {out_path.name} "
                          f"(target_key={r.get('target_key')!r})", file=sys.stderr)
                    return 2
                if r.get("run_status") != "done":
                    # Covers NONTERMINAL_SCHEDULER_STATUSES and every error row:
                    # anything not `done` is re-run rather than reused, so a
                    # scheduler outcome can never be mistaken for finished work.
                    continue
                bad = [g for g in GATES if g != "proof" and r.get(g) not in ("0", "1")]
                if bad:
                    print(f"--resume REFUSED: row {r.get('target_key')!r} has "
                          f"non-boolean gate value(s) {bad}", file=sys.stderr)
                    return 2
                if not (r.get("verdict") or "").strip():
                    print(f"--resume REFUSED: row {r.get('target_key')!r} has no verdict",
                          file=sys.stderr)
                    return 2
                # The verdict must follow from the gates it ships with, or the
                # row was edited after the fact.
                recomputed = verdict({g: int(r[g]) for g in GATES if g != "proof"})
                if r["verdict"] not in (recomputed, "native_ok"):
                    print(f"--resume REFUSED: row {r.get('target_key')!r} claims verdict "
                          f"{r['verdict']!r} but its gates give {recomputed!r}",
                          file=sys.stderr)
                    return 2
                key = (r.get("target_key") or "").strip()
                if not key:
                    continue
                if key in seen:
                    print(f"--resume REFUSED: duplicate target_key {key!r} in {out_path.name}",
                          file=sys.stderr)
                    return 2
                seen.add(key)
                if r.get("run_status") != "done":
                    continue
                t = live.get(key)
                if t is None:
                    continue
                # The row, the sidecar and the disk must all name the same bytes,
                # and the row must name the module this target resolves to.
                if r.get("cert_sha256") != t.sha256 or prev_hashes.get(key) != t.sha256:
                    continue
                if (r.get("module") or "") != t.module:
                    print(f"--resume REFUSED: row for {key!r} names module "
                          f"{r.get('module')!r}, manifest resolves {t.module!r}",
                          file=sys.stderr)
                    return 2
                done[key] = r
            print(f"--resume: reusing {len(done)} terminal row(s)", file=sys.stderr)

    todo = [t for t in run_now if t.key not in done]
    deferred_now = [(t, why) for t, why in deferred_targets if t.key not in done]

    print(f"d3_sweep: run_id={RUN_ID} dir={RUN_DIR}", file=sys.stderr)
    print(f"d3_sweep: manifest={a.manifest or '(none: exploratory)'} "
          f"digest={manifest_digest[:16] or '-'} selection={sel_digest[:16]}", file=sys.stderr)
    print(f"d3_sweep: {len(targets)} selected target(s), {len(todo)} to run, "
          f"{len(done)} reused, {len(skipped)} manifest row(s) without a certificate "
          f"(NOT written here -- the join owns them), jobs={a.jobs}, "
          f"timeout={a.timeout}s, samples={a.samples}, tier={a.tier or 'all'}, "
          f"native={a.native}", file=sys.stderr)

    if a.dry_run:
        # Printed from the SAME lists the executor would consume, so the order
        # shown is the order that would run -- not a second computation of it.
        def _rss_s(m):
            kb = rss_map.get(m)
            return (f"{kb} kB ({kb / 1048576:.2f} GiB)" if kb else "unknown")
        print("d3_sweep: DRY RUN -- nothing executed, nothing written")
        print(f"  selection covers {len(targets)} target(s); {len(todo)} would run, "
              f"{len(deferred_now)} deferred, {len(done)} reused")
        for i, t in enumerate(todo, 1):
            print(f"  {i:>3}. {t.module:<34} prior_rss={_rss_s(t.module):<26} "
                  f"nodes={t.nodes or '?':>7} tier={tier_of(t.nodes)}")
        for t, why in deferred_now:
            print(f"  DEFER {t.module:<32} prior_rss={_rss_s(t.module):<26} {why}")
        return 0

    # Only selected, attempted targets appear in this table.  Appending the
    # manifest's no-certificate rows to every tier slice would make the slices
    # overlap and give each one a false denominator; the exact-denominator join
    # is what owns those rows.
    rows = list(done.values())
    for r in rows:
        r.setdefault("proof", "na")
    for t, why in deferred_now:
        rows.append(deferred_row(t, a.samples, why))

    lock = threading.Lock()

    def checkpoint():
        write_rows_atomic(out_path, RESULT_COLS, rows)
        # BIND the results bytes to the sidecar, written in this order so the
        # digest describes the file that now exists.  Without it a results table
        # can be edited after the run and nothing contradicts it: the sidecar
        # vouches for the certificates and the toolchain, but says nothing about
        # the ROWS.  `--resume` and `d3_merge.py` both check this.
        _atomic_write(meta_path, json.dumps(
            {"config": cfg, "scheduling": sched, "run_id": RUN_ID,
             "results_sha256": hashlib.sha256(out_path.read_bytes()).hexdigest(),
             "aggregate_rss_peak_kb": _RSS_PEAK["kb"],
             "aggregate_rss_tripped_kb": _RSS_PEAK["tripped_at_kb"],
             "aggregate_rss_killed_kb": _RSS_PEAK["killed_at_kb"],
             "lean_env": _lean_env_subset(LEAN_ENV) if LEAN_ENV else {},
             "cert_sha256": {t.key: t.sha256 for t in targets},
             "modules": {t.key: t.module for t in targets},
             "targets": len(targets), "updated": time.strftime("%Y-%m-%d %H:%M:%S")},
            indent=2) + "\n")

    baseline_artifacts = cfg["artifact_digest"]
    aborted = False

    checkpoint()
    _RSS_PEAK["cap_kb"] = a.max_aggregate_rss_kb
    _RSS_PEAK["kill_kb"] = sampled_kill_kb
    if a.max_aggregate_rss_kb or sampled_kill_kb:
        threading.Thread(target=_rss_monitor,
                         args=(a.max_aggregate_rss_kb, a.rss_sample_seconds,
                               sampled_kill_kb),
                         daemon=True).start()
    with concurrent.futures.ThreadPoolExecutor(max_workers=a.jobs) as ex:
        futs = {ex.submit(run_one, t, a.samples, a.timeout, a.native, baseline_artifacts): t
                for t in todo}
        for i, fut in enumerate(concurrent.futures.as_completed(futs), 1):
            t = futs[fut]
            launched_hint, synthesized = "unknown", False
            try:
                r = fut.result()
                launched_hint = r.get("launched", "0")
            except concurrent.futures.CancelledError:
                continue
            except Exception as e:  # noqa: BLE001
                synthesized = True
                # One module's failure must not strand the batch or discard the
                # checkpoint; it becomes an explicit terminal row.
                r = {g: 0 for g in GATES}
                r.update({"target_key": t.key, "module": t.module, "proof": "na",
                          "cert_sha256": t.sha256, "manifest_nodes": t.nodes,
                          "requested_samples": a.samples, "run_status": "error",
                          "verdict": "runner_error", "wall_s": "",
                          "drift": "", "launched": "unknown",
                          "detail": f"{type(e).__name__}: {e}"[:200]})
                # Recover anything the probe's cgroup told us before the throw.
                # The exception unwound past the returns that would normally
                # carry it, but the reading itself was taken successfully and
                # the cgroup it came from no longer exists to be re-read.
                if t.key in _CG_PEAK:
                    r["cgroup_peak_kb"] = str(_CG_PEAK[t.key])

            # UNCONDITIONAL, for every outcome: a normal result, a --native
            # result, and a synthesized `runner_error`.  Doing this inside
            # run_one missed the native early return and every exception raised
            # after the probe, either of which could have ended a drifted run on
            # a row that looked ordinary.
            if not r.get("drift"):
                now_d = artifact_digest()
                if now_d != baseline_artifacts:
                    # `between` only when the row proves no worker ran; the phase
                    # is diagnostic, the abort is not conditional on it.
                    r["drift"] = {"1": "after", "0": "between"}.get(launched_hint, "unknown")

            if (not r.get("drift") and not synthesized
                    and r.get("run_status") == "done"):
                # Only a row that actually COMPLETED the gate pipeline has gates
                # worth reading a verdict from.  Previously this excluded just
                # `NONTERMINAL_SCHEDULER_STATUSES`, which let every `run_status`
                # of `error` through: a `runner_error` returned by `run_one` --
                # unreadable cgroup accounting, or a cgroup that would not die --
                # was silently relabelled `cert` from the gates it had passed
                # before the failure, turning a runner fault into a design
                # result.  Requiring `done` names the real condition instead of
                # enumerating the statuses that must be kept out, so a status
                # added later is excluded by default rather than by memory.
                r["verdict"] = ("native_ok" if (a.native and r.get("compile") == 1)
                                else verdict(r))

            if r.get("drift"):
                # The compiler moved. The WHOLE run is invalid, not just this
                # module, so the tainted row is NOT appended -- crediting it as
                # ordinary evidence is exactly the failure this guard exists for.
                aborted = True
                now = artifact_digest()
                _SHUTDOWN.set()
                for f2 in futs:
                    f2.cancel()
                _teardown()          # kill every live process group, not just flag it
                cfg["artifact_digest_end"] = now
                cfg["aborted_artifact_drift"] = True
                cfg["aborted_at_target"] = t.key
                cfg["aborted_phase"] = r["drift"]
                # The GENERIC marker alongside the specific one, so a consumer
                # has a single key to refuse on and does not have to learn every
                # abort reason that will ever exist.  Drift keeps its own key for
                # the readers that already know it.
                cfg["aborted_run"] = True
                cfg["aborted_reason"] = "artifact_drift"
                with lock:
                    checkpoint()
                print(f"\nABORTED: build artifacts changed {r['drift']} {t.key} "
                      f"({baseline_artifacts[:16]} -> {now[:16]}). The run is void: rows "
                      f"already written are preserved for diagnosis but must NOT be "
                      f"reported, and the tainted row was not recorded.",
                      file=sys.stderr, flush=True)
                break

            if r.get("run_status") in FATAL_RUN_STATUSES:
                # Same treatment as drift -- the run is void from here on -- with
                # one deliberate difference: the failing row IS kept, because
                # unlike a drifted row it is not tainted evidence about a design,
                # it is the diagnosis of what broke the run.  `_SHUTDOWN` was
                # already set by the worker, so nothing new can launch; this
                # cancels what is queued and kills what is still live.
                aborted = True
                for f2 in futs:
                    f2.cancel()
                _teardown()
                cfg["aborted_run"] = True
                cfg["aborted_reason"] = r["run_status"]
                cfg["aborted_at_target"] = t.key
                # Marking the evidence void does not give the shared machine its
                # memory back.  The worker's `destroy()` had already spent its 5 s
                # and failed, most plausibly because the probe's own children were
                # still holding the cgroup -- and `_teardown()` above is the first
                # moment every one of them has certainly been killed.  So retry
                # HERE, where the first attempt could not have succeeded and this
                # one can.  Bounded, because a cgroup that will not die after
                # teardown will not die by being asked more often.
                cg_left = _FATAL_CGROUP.get(t.key)
                if cg_left is not None:
                    cfg["cleanup_path"] = str(cg_left.path)
                    freed = False
                    for attempt in range(2):
                        if cg_left.destroy():
                            freed = True
                            break
                        if attempt == 0:
                            time.sleep(0.5)
                    cfg["cleanup_retry_succeeded"] = freed
                    # The residual is named, not summarised as a boolean: whoever
                    # reads this has to be able to go and remove it by hand.
                    cfg["cleanup_residual"] = "" if freed else str(cg_left.path)
                r["tier"] = tier_of(t.nodes)
                with lock:
                    rows.append(r)
                    checkpoint()
                print(f"\nABORTED: {r.get('detail') or r['run_status']} at {t.key}. "
                      f"The run is void: the failing row is preserved for diagnosis "
                      f"and rows already written are kept, but the output must NOT "
                      f"be reported as evidence.", file=sys.stderr, flush=True)
                if cg_left is not None:
                    if cfg.get("cleanup_retry_succeeded"):
                        print(f"  cleanup: the leaked cgroup was removed on retry "
                              f"after teardown ({cfg['cleanup_path']}); no residual "
                              f"charge remains.", file=sys.stderr, flush=True)
                    else:
                        # Never "no leak": it is still there, it still holds a
                        # charge against a shared machine, and saying otherwise
                        # would be the one claim nobody could check.
                        print(f"  cleanup: FAILED AGAIN after teardown. The cgroup "
                              f"{cfg['cleanup_residual']} SURVIVES and still holds a "
                              f"memory charge; remove it by hand before the next run.",
                              file=sys.stderr, flush=True)
                break

            r["tier"] = tier_of(t.nodes)
            with lock:
                rows.append(r)
                # Checkpoint after EVERY completed module.
                checkpoint()
            print(f"[{i}/{len(todo)}] {r['target_key']:<44} {r['verdict']:<13} "
                  f"{r.get('wall_s','')}s rss={r.get('max_rss_kb','?')}kB "
                  f"{r.get('detail','')[:60]}", file=sys.stderr, flush=True)

    _WORK_DONE.set()

    if aborted:
        print(f"wrote {out_path} ({len(rows)} rows) and {meta_path.name} "
              f"-- MARKED ABORTED, not usable as evidence", file=sys.stderr)
        return 3

    checkpoint()
    print(f"\nwrote {out_path} ({len(rows)} rows) and {meta_path.name}", file=sys.stderr)
    for g in GATES:
        # `proof` is a STRING ("na"/"0"/"1"), every other gate an int, so a bare
        # `== 1` reported `proof 0/1` for a run whose row said `proof='1'` -- the
        # summary contradicting its own table.
        n = sum(1 for r in rows if r.get(g) in (1, "1"))
        extra = ""
        if g == "proof":
            na = sum(1 for r in rows if r.get(g) == "na")
            # `na` spelled out rather than folded into the denominator: not
            # attempted is not the same as attempted and failed.
            extra = f"  ({na} na, {len(rows) - n - na} failed)"
        print(f"  {g:<10} {n}/{len(rows)}{extra}", file=sys.stderr)
    ntmo = sum(1 for r in rows if r.get("run_status") == "timeout")
    if ntmo:
        print(f"  timeout    {ntmo}/{len(rows)} ran past --timeout and were killed; "
              f"resumable, and NOT a gate failure", file=sys.stderr)
    nkill = sum(1 for r in rows if r.get("run_status") == "rss_killed")
    if nkill:
        print(f"  rss_killed {nkill}/{len(rows)} started and then terminated by the "
              f"hard aggregate limit ({a.kill_over_rss_kb} kB); resumable, and NOT a "
              f"gate failure", file=sys.stderr)
    ndef = sum(1 for r in rows if r.get("run_status") == "deferred")
    if ndef:
        # Stated as its own line rather than folded into the gate counts: a
        # deferred target is not a target that failed every gate, and the two
        # must not be read off the same number.
        print(f"  deferred   {ndef}/{len(rows)} not started by the scheduler "
              f"(threshold {a.defer_over_rss_kb} kB); rerun with --resume and no "
              f"--defer-over-rss-kb to pick them up", file=sys.stderr)
    peak = _RSS_PEAK["kb"]
    print(f"  aggregate RSS peak {peak} kB ({peak / 1048576:.2f} GiB) "
          f"against a {a.max_aggregate_rss_kb} kB cap"
          + ("  -- CAP TRIPPED" if _RSS_PEAK["tripped_at_kb"] else ""), file=sys.stderr)
    if skipped:
        print(f"  {len(skipped)} manifest target(s) have no certificate; join with "
              f"d3_join.py for the exact denominator", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
