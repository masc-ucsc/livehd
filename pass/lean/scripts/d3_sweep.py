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
import errno
import os
import pathlib
import re
import signal
import subprocess
import sys
import threading
import time

ROOT = pathlib.Path(__file__).resolve().parents[3]
LEAN_DIR = ROOT / "formal" / "lean"
LAKE = os.environ.get("LAKE", "/mada/users/czeng14/.elan/bin/lake")

# `checker` sits BETWEEN sim and agree deliberately.  An agreement result from a
# checker that cannot reject a wrong answer is not weak evidence, it is no
# evidence, so it must not be able to earn a verdict.  Ordering it here means
# `verdict()` stops at `sim` whenever the self-test fails, whatever `agree` says.
GATES = ["cert", "compile", "reify", "typecheck", "sim", "checker", "agree", "proof"]

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


def run_group(cmd, cwd, timeout):
    """Run `cmd` in its own session; return (combined output, returncode).

    Returncode 124 means the timeout fired, matching `timeout(1)` so the
    classifier has one convention to read.
    """
    if _SHUTDOWN.is_set():
        return "", 143
    p = subprocess.Popen(
        cmd, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        text=True, start_new_session=True,
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


def module_of(path: pathlib.Path) -> str:
    """`foo_Lgraph.lean` -> `foo`.  The emitter's own naming, not a guess."""
    return path.name[: -len("_Lgraph.lean")] if path.name.endswith("_Lgraph.lean") else path.stem


def make_probe(cert: pathlib.Path, m: str, samples: int) -> str:
    """Certificate body + reify + gate report.

    The certificate's own `theorem` block is DROPPED: `<m>_compiles` is a
    `native_decide` over the whole literal, and paying it here would put the
    most expensive gate in front of the cheapest ones.  It is a separate claim
    and `--native` measures it separately.  Cutting at the first `theorem` also
    drops `<m>_step_correct` and the trailing `<m>_residual`/`#print axioms`,
    all of which this probe replaces or does not need.
    """
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

    return PROBE_HEAD + "\n".join(body) + PROBE_TAIL.format(m=m, samples=samples)


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


def run_one(cert: pathlib.Path, samples: int, timeout: int, native: bool):
    m = module_of(cert)
    probe_dir = RUN_DIR / ("probes_native" if native else "probes")
    log_dir = RUN_DIR / ("logs_native" if native else "logs")
    probe_dir.mkdir(parents=True, exist_ok=True)
    log_dir.mkdir(parents=True, exist_ok=True)

    row = {g: 0 for g in GATES}
    row["module"] = m
    row["proof"] = "na"  # never attempted by this pass; see the module docstring
    for k in ("sources", "nodes", "outputs", "flops", "mems", "inputs", "bindings",
              "selftest_base", "mut_out", "mut_flop", "mut_mem",
              "mutable_out", "mutable_flop", "mutable_mem", "distinct_obs"):
        row[k] = ""

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
        probe.write_text(make_probe(cert, m, samples), encoding="utf-8")

    t0 = time.time()
    out, rc = run_group([LAKE, "env", "lean", str(probe)], LEAN_DIR, timeout)
    row["wall_s"] = f"{time.time() - t0:.2f}"
    (log_dir / f"{m}.log").write_text(out, encoding="utf-8")

    if native:
        # The kernel-checked claim, plus the axiom list it depends on.
        row["compile"] = 1 if ("error" not in out and rc == 0) else 0
        ax = re.search(r"depends on axioms: \[([^\]]*)\]", out, re.S)
        row["detail"] = classify(out, rc, timeout) or (
            " ".join(ax.group(1).split()) if ax else "")
        return row

    extract_gates(row, out, rc, timeout)
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


def extract_gates(row, out, rc, timeout) -> None:
    """Pure function of the probe log: every gate this run actually passed.

    Separated from process handling so the parser can be tested directly -- an
    earlier version recorded a self-test failure in `detail` only, left
    `agree=1`, and still produced `verdict=agree`.
    """
    if "reify_design: compileDesign refused" in out:
        row["detail"] = "compileDesign refused the certificate"
        return
    shape = _one(r"^D3GATE module=\S+ (.*)$", out)
    emitted = _one(r"^reify_design: \S+ emitted, \d+ sources, (\d+) bindings$", out)
    if emitted is not None:
        # reify_design runs compileDesign at elaboration time and throws when it
        # refuses, so reaching "emitted" implies acceptance.
        row["compile"] = 1
        row["reify"] = 1
    clean_exit = rc == 0 and not ERR_RE.search(out) and "error:" not in out
    row["typecheck"] = 1 if clean_exit else 0
    if shape is None:
        row["detail"] = classify(out, rc, timeout) or (
            "no D3GATE shape line, or more than one" if emitted is not None else "")
        return
    for kv in shape.split():
        k, _, v = kv.partition("=")
        if k in row:
            row[k] = v
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
        row["agree"] = row["agree"]  # recorded as observed; verdict() stops at sim
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


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--certs", nargs="+", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--jobs", type=int, default=4)
    ap.add_argument("--timeout", type=int, default=900)
    ap.add_argument("--samples", type=int, default=32)
    ap.add_argument("--limit", type=int, default=0)
    ap.add_argument("--only", default="")
    ap.add_argument("--native", action="store_true",
                    help="run the UNMODIFIED certificate: compilesOk by native_decide + axioms")
    a = ap.parse_args()

    certs = []
    for d in a.certs:
        certs += sorted(pathlib.Path(d).glob("*_Lgraph.lean"))
    if a.only:
        rx = re.compile(a.only)
        certs = [c for c in certs if rx.search(module_of(c))]
    if a.limit:
        certs = certs[: a.limit]
    if not certs:
        print("no certificates matched", file=sys.stderr)
        return 2
    print(f"d3_sweep: run_id={RUN_ID} dir={RUN_DIR}", file=sys.stderr)
    print(f"d3_sweep: {len(certs)} certificates, jobs={a.jobs}, timeout={a.timeout}s, "
          f"samples={a.samples}, native={a.native}", file=sys.stderr)

    cols = ["module", "verdict"] + GATES + [
        "selftest_base", "mut_out", "mut_flop", "mut_mem",
        "mutable_out", "mutable_flop", "mutable_mem", "distinct_obs",
        "sources", "nodes", "outputs", "flops", "mems", "inputs", "bindings",
        "wall_s", "detail"]
    rows = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=a.jobs) as ex:
        futs = {ex.submit(run_one, c, a.samples, a.timeout, a.native): c for c in certs}
        for i, f in enumerate(concurrent.futures.as_completed(futs), 1):
            r = f.result()
            r["verdict"] = "native_ok" if (a.native and r.get("compile") == 1) else verdict(r)
            rows.append(r)
            print(f"[{i}/{len(certs)}] {r['module']:<44} {r['verdict']:<10} "
                  f"{r.get('wall_s','')}s {r.get('detail','')[:80]}", file=sys.stderr, flush=True)

    rows.sort(key=lambda r: r["module"])
    with open(a.out, "w", encoding="utf-8") as fh:
        fh.write("\t".join(cols) + "\n")
        for r in rows:
            fh.write("\t".join(str(r.get(c, "")) for c in cols) + "\n")

    print(f"\nwrote {a.out}", file=sys.stderr)
    for g in GATES:
        n = sum(1 for r in rows if r.get(g) == 1)
        print(f"  {g:<10} {n}/{len(rows)}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
