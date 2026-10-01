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


# Everything whose CONTENT can change what a row means.  A worktree HEAD plus a
# dirty bit is not enough: two different dirty states share both, and an
# uncommitted edit to the reifier or the harness is exactly the kind of change
# that makes old rows incomparable.
TOOL_FILES = [
    pathlib.Path(__file__).resolve(),
    ROOT / "formal/lean/LeanSemanticPrimitives/Compiler/D3Harness.lean",
    ROOT / "formal/lean/LeanSemanticPrimitives/Compiler/ReifyGen.lean",
    ROOT / "formal/lean/LeanSemanticPrimitives/Compiler/Reify.lean",
    ROOT / "formal/lean/LeanSemanticPrimitives/Compiler/CompileDesign.lean",
    ROOT / "formal/lean/LeanSemanticPrimitives/Compiler/ResidualSemantics.lean",
    ROOT / "formal/lean/LeanSemanticPrimitives/Compiler/Runtime.lean",
    ROOT / "formal/lean/LeanSemanticPrimitives/Compiler/DesignCert.lean",
]


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
        "lake": LAKE,
        "worktree_head": subprocess.run(
            ["git", "-C", str(ROOT), "rev-parse", "HEAD"],
            capture_output=True, text=True).stdout.strip(),
    }


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


def run_one(target: Target, samples: int, timeout: int, native: bool):
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
    row["user_s"] = ""
    row["sys_s"] = ""
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

    # /usr/bin/time -v, so job count and timeouts for the long tiers can be
    # chosen from MEASURED peak RSS rather than guessed.  Wrapping rather than
    # sampling: a sampler misses the peak of a short-lived elaboration.
    tv = log_dir / f"{m}.time"
    cmd = [LAKE, "env", "lean", str(probe)]
    if pathlib.Path("/usr/bin/time").exists():
        cmd = ["/usr/bin/time", "-v", "-o", str(tv)] + cmd
    t0 = time.time()
    out, rc = run_group(cmd, LEAN_DIR, timeout)
    row["wall_s"] = f"{time.time() - t0:.2f}"
    try:
        tt = tv.read_text()
        for key, pat in (("max_rss_kb", r"Maximum resident set size \(kbytes\): (\d+)"),
                         ("user_s", r"User time \(seconds\): ([\d.]+)"),
                         ("sys_s", r"System time \(seconds\): ([\d.]+)")):
            mo = re.search(pat, tt)
            if mo:
                row[key] = mo.group(1)
    except OSError:
        pass
    (log_dir / f"{m}.log").write_text(out, encoding="utf-8")

    if native:
        # The kernel-checked claim, plus the axiom list it depends on.
        row["compile"] = 1 if ("error" not in out and rc == 0) else 0
        ax = re.search(r"depends on axioms: \[([^\]]*)\]", out, re.S)
        row["detail"] = classify(out, rc, timeout) or (
            " ".join(ax.group(1).split()) if ax else "")
        return row

    extract_gates(row, out, rc, timeout, expect_module=m, expect_samples=samples)
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

    if "reify_design: compileDesign refused" in out:
        only_cert("compileDesign refused the certificate")
        return

    shape = _one(r"^D3GATE module=\S+ (.*)$", out)
    emitted = _one(r"^reify_design: \S+ emitted, \d+ sources, (\d+) bindings$", out)
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
        em = re.search(r"^reify_design: \S+ emitted, (\d+) sources, (\d+) bindings$", out, re.M)
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
    "max_rss_kb", "user_s", "sys_s", "wall_s", "detail"])


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
    ap.add_argument("--force", action="store_true",
                    help="overwrite an existing --out that was not produced by --resume")
    ap.add_argument("--native", action="store_true",
                    help="run the UNMODIFIED certificate: compilesOk by native_decide + axioms")
    a = ap.parse_args()

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
        targets = [best[k] for k in sorted(best)]
    if a.limit:
        targets = targets[: a.limit]
    if not targets:
        print("no targets selected", file=sys.stderr)
        return 2

    sel_digest = selection_digest(targets)
    cfg = run_config(a, manifest_digest)
    cfg["selection_digest"] = sel_digest
    cfg["tool_digest"] = tool_digest()
    cfg["worktree_dirty"] = bool(subprocess.run(
        ["git", "-C", str(ROOT), "status", "--porcelain"],
        capture_output=True, text=True).stdout.strip())

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
            mismatch = {k: (prev["config"].get(k), v)
                        for k, v in cfg.items() if prev["config"].get(k) != v}
            if mismatch:
                print("--resume REFUSED: configuration differs from the previous run:",
                      file=sys.stderr)
                for k, (was, now) in mismatch.items():
                    print(f"    {k}: was {was!r}, now {now!r}", file=sys.stderr)
                return 2
            prev_hashes = prev.get("cert_sha256", {})
            live = {t.key: t for t in targets}
            seen = set()
            try:
                prev_rows = read_manifest_rows(out_path)
            except (OSError, csv.Error) as e:
                print(f"--resume REFUSED: previous output is unreadable ({e})", file=sys.stderr)
                return 2
            required = {"target_key", "module", "cert_sha256", "run_status", "verdict"}
            with out_path.open(encoding="utf-8") as fh:
                header = next((l for l in fh if not l.startswith("#")), "")
            missing_cols = required - set(header.rstrip("\n").split("\t"))
            if missing_cols:
                print(f"--resume REFUSED: previous output is missing column(s) "
                      f"{sorted(missing_cols)}", file=sys.stderr)
                return 2
            for r in prev_rows:
                if any(r.get(c) is None for c in required):
                    print(f"--resume REFUSED: truncated row in {out_path.name}: "
                          f"{ {k: r.get(k) for k in sorted(required)} }", file=sys.stderr)
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

    todo = [t for t in targets if t.key not in done]

    print(f"d3_sweep: run_id={RUN_ID} dir={RUN_DIR}", file=sys.stderr)
    print(f"d3_sweep: manifest={a.manifest or '(none: exploratory)'} "
          f"digest={manifest_digest[:16] or '-'} selection={sel_digest[:16]}", file=sys.stderr)
    print(f"d3_sweep: {len(targets)} selected target(s), {len(todo)} to run, "
          f"{len(done)} reused, {len(skipped)} manifest row(s) without a certificate "
          f"(NOT written here -- the join owns them), jobs={a.jobs}, "
          f"timeout={a.timeout}s, samples={a.samples}, tier={a.tier or 'all'}, "
          f"native={a.native}", file=sys.stderr)

    # Only selected, attempted targets appear in this table.  Appending the
    # manifest's no-certificate rows to every tier slice would make the slices
    # overlap and give each one a false denominator; the exact-denominator join
    # is what owns those rows.
    rows = list(done.values())
    for r in rows:
        r.setdefault("proof", "na")

    lock = threading.Lock()

    def checkpoint():
        write_rows_atomic(out_path, RESULT_COLS, rows)
        _atomic_write(meta_path, json.dumps(
            {"config": cfg, "run_id": RUN_ID,
             "cert_sha256": {t.key: t.sha256 for t in targets},
             "modules": {t.key: t.module for t in targets},
             "targets": len(targets), "updated": time.strftime("%Y-%m-%d %H:%M:%S")},
            indent=2) + "\n")

    checkpoint()
    with concurrent.futures.ThreadPoolExecutor(max_workers=a.jobs) as ex:
        futs = {ex.submit(run_one, t, a.samples, a.timeout, a.native): t for t in todo}
        for i, f in enumerate(concurrent.futures.as_completed(futs), 1):
            t = futs[f]
            try:
                r = f.result()
                r["verdict"] = "native_ok" if (a.native and r.get("compile") == 1) else verdict(r)
            except Exception as e:  # noqa: BLE001
                # One module's failure must not strand the batch or discard the
                # checkpoint; it becomes an explicit terminal row.
                r = {g: 0 for g in GATES}
                r.update({"target_key": t.key, "module": t.module, "proof": "na",
                          "cert_sha256": t.sha256, "manifest_nodes": t.nodes,
                          "requested_samples": a.samples, "run_status": "error",
                          "verdict": "runner_error", "wall_s": "",
                          "detail": f"{type(e).__name__}: {e}"[:200]})
            r["tier"] = tier_of(t.nodes)
            with lock:
                rows.append(r)
                # Checkpoint after EVERY completed module.
                checkpoint()
            print(f"[{i}/{len(todo)}] {r['target_key']:<44} {r['verdict']:<13} "
                  f"{r.get('wall_s','')}s rss={r.get('max_rss_kb','?')}kB "
                  f"{r.get('detail','')[:60]}", file=sys.stderr, flush=True)

    checkpoint()
    print(f"\nwrote {out_path} ({len(rows)} rows) and {meta_path.name}", file=sys.stderr)
    for g in GATES:
        n = sum(1 for r in rows if r.get(g) == 1)
        print(f"  {g:<10} {n}/{len(rows)}", file=sys.stderr)
    if skipped:
        print(f"  {len(skipped)} manifest target(s) have no certificate; join with "
              f"d3_join.py for the exact denominator", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
