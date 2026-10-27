#!/usr/bin/env python3
"""Direction 2 sweep: classify every generated `DesignCert` with the DIRECT
simulator's own admission test, and run it for a few cycles.

Why this exists.  `checkDesign` is an INDEPENDENT, source-level acceptance
predicate (`pass/lean/../formal/lean/LeanSemanticPrimitives/Compiler/DirectCheck.lean`)
whose accepted-operator table was derived from the exporter's `cert_node_expr`,
not copied from `compileOp`'s refusal list.  A table derived that way is only
worth anything if it is checked against the certificates real designs actually
produce -- that is this sweep.  Every design is classified ACCEPTED or REFUSED
WITH A REASON; a refusal that is not explainable is a bug in the table.

The probe it generates imports `DirectTrace`, NOT `CompileDesign`, so the direct
path elaborates without Mathlib.  That is not an optimisation detail: it is the
point.  The direct simulator does not need the verified compiler to exist.

Usage:
  direct_sweep.py --out SWEEP.tsv [--jobs 4] [--cycles 4] [--limit N]
                  [--filter REGEX] ROOT [ROOT...]
"""

import argparse
import hashlib
import json
import os
import re
import shutil
import signal
import subprocess
import sys
import tempfile
import time
from concurrent.futures import ThreadPoolExecutor, as_completed

# The residual-half cut is shared with make_sim_launcher.py: both tools redirect
# the same import and cut at the same boundary, and both carried the same two
# defects independently. One copy, so a repair cannot drift.
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cert_cut

LEAN_DIR = os.path.realpath(
    os.path.join(os.path.dirname(__file__), "..", "..", "..", "..", "formal", "lean"))

CERT_RE = re.compile(r"^def\s+(\w+)_designCert\s*:\s*DesignCert", re.M)
TAIL_MARKERS = ("/-- Compile-and-run.", "\ndef ")  # cut before the residual half

PROBE_TAIL = r"""
open Compiler Compiler.Direct in
#eval show IO Unit from do
  let D := @BASE@_designCert
  IO.println s!"SHAPE sources={D.sources.size} nodes={D.nodes.size} outputs={D.outputs.size} flops={D.flops.size} mems={D.memories.size} inputs={inputArity D} clocks={D.clocks.size}"
  let t0 ← IO.monoMsNow
  let verdict := match designErrors D with
    | none   => "ACCEPTED"
    | some e => "REFUSED\t" ++ e.tag ++ "\t" ++ e.render
  let t1 ← IO.monoMsNow
  IO.println s!"VERDICT\t{verdict}"
  IO.println s!"CHECK_MS {t1 - t0}"
  if (designErrors D).isNone then
    let s0 := zeroState D
    let i0 := zeroInput D
    let t2 ← IO.monoMsNow
    let e0 := allEdges D
    let r1 := directStepRaw D e0 i0 s0
    let h1 := r1.outputs.foldl (fun a b => a + (bv_uint b).toNat) 0
    let t3 ← IO.monoMsNow
    IO.println s!"STEP1_MS {t3 - t2} outsum {h1}"
    let t4 ← IO.monoMsNow
    let mut st := s0
    let mut acc := 0
    for _ in [0:@CYCLES@] do
      let r := directStepRaw D e0 i0 st
      acc := acc + r.outputs.foldl (fun a b => a + (bv_uint b).toNat) 0
      st := r.nextState
    let t5 ← IO.monoMsNow
    IO.println s!"RUN_MS {t5 - t4} cycles @CYCLES@ outsum {acc}"
    -- the CHECKED public entry point must agree that this is runnable
    match runDirect D s0 (ticksAll D (List.replicate @CYCLES@ i0)) with
    | .error e => IO.println s!"RUNDIRECT\tREFUSED\t{e.tag}"
    | .ok t    => IO.println s!"RUNDIRECT\tOK\t{t.steps.length}"
"""


def build_probe(src_text, base, cycles, max_rec_depth=0):
    """Swap the import for the Mathlib-free one, drop the residual half, append the probe.

    `max_rec_depth` overrides the `set_option maxRecDepth` the exporter emits.
    The emitted 1,000,000 is not enough for the two 14 MB CVA6 hpdcache
    certificates: Lean's front end exhausts it while ELABORATING the
    `sources := #[..]` literal, the definition becomes noncomputable, and the
    probe fails before `checkDesign` is ever called.  20,000,000 clears it (at
    ~4 h and 16.6 GB for a 107,213-node design, essentially all of it
    elaboration -- the semantic check itself is under two seconds).
    """
    text = src_text.replace(
        "import LeanSemanticPrimitives.Compiler.CompileDesign",
        "import LeanSemanticPrimitives.Compiler.DirectTrace")
    if max_rec_depth:
        text = re.sub(r"^set_option maxRecDepth \d+$",
                      f"set_option maxRecDepth {max_rec_depth}", text, count=1, flags=re.M)
    # A BOUNDARY THAT CANNOT BE FOUND IS AN ERROR, not `len(text)`.
    #
    # The fallback kept EVERYTHING -- having already redirected the import the
    # kept code needs -- and the probe was appended after it, so the module
    # could not elaborate and the failure was recorded against the MODULE
    # rather than against this function. The `_step` fallback cut at the `def`
    # line, leaving that definition's two-line doc comment dangling with the
    # probe's `#eval` after it, which Lean also rejects. Both live in
    # cert_cut.residual_cut now, shared with make_sim_launcher.py.
    cut, why = cert_cut.residual_cut(text)
    if cut is None:
        raise ValueError(why)
    kept = text[:cut]
    why = cert_cut.check_kept(kept, CERT_RE)
    if why:
        raise ValueError(why)
    return kept + PROBE_TAIL.replace("@BASE@", base).replace("@CYCLES@", str(cycles))


def kill_process_group(proc, grace=10.0):
    """SIGTERM the whole process group, then SIGKILL whatever is left.

    `subprocess.run(timeout=...)` kills only the process it started.  Here that
    is `/usr/bin/time`, whose child `lean` is NOT killed: it is reparented to
    init and keeps running -- measured, a leaked probe sat at 100% CPU and 8 GB
    RSS while the sweep moved on to the next module, so the next module's
    numbers were taken on a machine that was secretly still busy.  Killing the
    GROUP is what reaches the descendants.
    """
    try:
        pgid = os.getpgid(proc.pid)
    except (ProcessLookupError, PermissionError):
        return
    if pgid == os.getpgrp():
        # The child is in OUR OWN group, which means it was not started with
        # start_new_session.  Killing the group here would take down the sweep
        # -- and whatever launched it -- instead of the probe.  Fall back to the
        # single process and say so, loudly: the leak this function exists to
        # prevent is back, and silently doing the old wrong thing is worse than
        # a line on stderr.
        print(f"direct_sweep: WARNING pid {proc.pid} shares this process group; "
              f"killing only it, so its descendants may leak", file=sys.stderr)
        try:
            proc.kill()
        except OSError:
            pass
        return
    for sig, wait_s in ((signal.SIGTERM, grace), (signal.SIGKILL, 5.0)):
        try:
            os.killpg(pgid, sig)
        except ProcessLookupError:
            return
        except PermissionError:
            return
        deadline = time.time() + wait_s
        while time.time() < deadline:
            try:
                os.killpg(pgid, 0)
            except ProcessLookupError:
                return
            time.sleep(0.2)


def run_timed(cmd, timeout, cwd=None, env=None):
    """-> (stdout, stderr, timed_out, returncode).

    The command runs in its OWN session, so a timeout can take down every
    descendant rather than just the wrapper.

    The RETURN CODE is part of the result on purpose: `run_one` promotes an
    ACCEPTED verdict to LEAN_ERROR when the probe exited non-zero after
    printing it, and a result shape that dropped the status would have turned
    that guard into a NameError on every ordinary run.
    """
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                            text=True, cwd=cwd, env=env, start_new_session=True)
    try:
        out, err = proc.communicate(timeout=timeout)
        return out, err, False, proc.returncode
    except subprocess.TimeoutExpired:
        kill_process_group(proc)
        try:
            out, err = proc.communicate(timeout=30)   # reap; pipes are closed by now
        except subprocess.TimeoutExpired:
            proc.kill()
            out, err = "", ""
        return out, err, True, (proc.returncode if proc.returncode is not None else -signal.SIGKILL)


def file_sha(p):
    h = hashlib.sha256()
    try:
        with open(p, "rb") as fh:
            for blk in iter(lambda: fh.read(1 << 20), b""):
                h.update(blk)
    except OSError:
        return ""
    return h.hexdigest()


def provenance_error(path, entry):
    """Why this input is not the input the manifest describes, or "".

    CHECKED IMMEDIATELY BEFORE THE PROBE, against the bytes about to be read.
    Measured externally: flipping one 5-bit source constant from 20 to 19 in an
    accepted certificate, leaving the manifest untouched, produced ACCEPTED and
    a result row carrying the ORIGINAL artifact_sha256 -- a run attributed to
    source and proof evidence that did not describe it. A direct evaluator may
    legitimately admit the changed model; it may not borrow the original's
    identity to report it.
    """
    want = entry.get("artifact_sha256")
    if not want:
        return "the manifest carries no artifact digest for this entry"
    got = file_sha(path)
    if got[: len(want)] != want:
        return (f"artifact digest {got[:16]}... does not match the manifest's "
                f"{want[:16]}...; the bytes on disk are not the bytes the manifest "
                f"describes")
    side = entry.get("sidecar")
    if side:
        sw = entry.get("sidecar_sha256")
        sg = file_sha(side)
        if not sg:
            return f"the manifest names a sidecar that is not readable: {side}"
        if sw and sg[: len(sw)] != sw:
            return (f"sidecar digest {sg[:16]}... does not match the manifest's "
                    f"{sw[:16]}...")
    for f in ("family", "case", "mode"):
        if not entry.get(f):
            return f"the manifest entry carries no {f}"
    return ""


def run_one(path, workdir, cycles, timeout, lean_env, max_rec_depth=0, entry=None,
            manifest_mode=False, evidence_dir=None):
    # A MISSING IDENTITY IS A REFUSAL, NOT AN EMPTY DICT.
    #
    # `entry or {}` turned "no manifest entry for this payload" into "no
    # provenance to check", and the guard below then skipped itself. Measured
    # on the real CLI: a manifest with a valid `to_run` path and `entries: []`
    # returned ACCEPTED, exit 0, four cycles -- with blank family, case, mode
    # and artifact_sha256. The row could not say what it had run.
    if manifest_mode and not entry:
        return {"module": os.path.basename(path).replace("_Lgraph.lean", ""), "path": path,
                "verdict": "PROVENANCE_MISMATCH", "family": "", "case": "", "mode": "",
                "reason": "the manifest selected this payload but carries no identity entry "
                          "for it", "artifact_sha256": "", "aliases": "", "ran_sha256": "",
                "probe_sha256": "", "log_sha256": "", "evidence": "",
                "sources": "", "nodes": "", "outputs": "", "flops": "", "mems": "",
                "inputs": "", "check_ms": "", "step1_ms": "", "run_ms": "", "cycles": "",
                "wall_s": "", "rss_kb": ""}
    entry = entry or {}
    name = os.path.basename(path).replace("_Lgraph.lean", "")
    row = {"module": name, "path": path, "verdict": "NO_CERT", "reason": "",
           "family": entry.get("family", ""), "case": entry.get("case", ""),
           "mode": entry.get("mode", ""),
           "artifact_sha256": entry.get("artifact_sha256", ""),
           "aliases": ",".join(entry.get("aliases", [])),
           "ran_sha256": "", "probe_sha256": "", "log_sha256": "", "evidence": "",
           "sources": "", "nodes": "", "outputs": "", "flops": "", "mems": "",
           "inputs": "", "check_ms": "", "step1_ms": "", "run_ms": "",
           # NOT prefilled from the request: `cycles` must be what the run
           # actually reported, or it is not evidence that any cycle ran.
           "cycles": "", "wall_s": "", "rss_kb": ""}
    # The manifest is checked BEFORE anything is read for the probe.
    if entry:
        why = provenance_error(path, entry)
        if why:
            row["verdict"] = "PROVENANCE_MISMATCH"
            row["reason"] = why
            return row
    src = open(path, encoding="utf-8", errors="replace").read()
    base_m = CERT_RE.search(src)
    if not base_m:
        return row
    base = base_m.group(1)
    # THE HASH OF THE BYTES ACTUALLY USED, not the one the manifest predicted.
    row["ran_sha256"] = file_sha(path)
    probe = os.path.join(workdir, f"{name}_probe.lean")
    try:
        probe_text = build_probe(src, base, cycles, max_rec_depth)
    except ValueError as e:
        # A NAMED ROW, not a traceback. This runs inside a ThreadPoolExecutor,
        # so an exception here surfaces as `f.result()` raising while every
        # other row still in flight is lost.
        row["verdict"] = "PROBE_REFUSED"
        row["reason"] = str(e)
        return row
    with open(probe, "w", encoding="utf-8") as f:
        f.write(probe_text)
    row["probe_sha256"] = hashlib.sha256(probe_text.encode()).hexdigest()
    cmd = ["/usr/bin/time", "-f", "%e %M", "nice", "-n", "19", "ionice", "-c", "3",
           "lean", probe]
    t0 = time.time()
    out, err, timed_out, exit_code = run_timed(cmd, timeout, cwd=LEAN_DIR, env=lean_env)
    if timed_out:
        # A TIMEOUT KEEPS ITS EVIDENCE: fall through to the retention block
        # below rather than returning here, which is what the first version
        # did -- the slowest and most interesting runs left nothing behind.
        row["verdict"] = "TIMEOUT"
        row["reason"] = f"no result within {timeout}s (harness limit, not a model refusal)"
    row["wall_s"] = f"{time.time() - t0:.1f}"
    # THE RETAINED LOG IS RETAINED. The previous version hashed `out + err` and
    # threw the string away, while its own comment called it a retained log --
    # a digest of nothing anyone can read. The log, the command and the exit
    # status are written to project-local evidence under a name built from the
    # CONFIGURED IDENTITY and the digest of the bytes that ran, so two
    # configurations sharing a payload do not overwrite each other.
    saw_rundirect_ok = False
    tail = err.strip().splitlines()
    if tail:
        m = re.match(r"^([\d.]+)\s+(\d+)$", tail[-1])
        if m:
            row["rss_kb"] = m.group(2)
            tail = tail[:-1]
    for line in out.splitlines():
        if line.startswith("SHAPE "):
            for kv in line[6:].split():
                k, _, v = kv.partition("=")
                if k in row:
                    row[k] = v
        elif line.startswith("VERDICT\t"):
            parts = line.split("\t")
            row["verdict"] = parts[1].strip('"')
            if len(parts) > 2:
                row["reason"] = parts[2] + (": " + parts[3].rstrip('"') if len(parts) > 3 else "")
        elif line.startswith("CHECK_MS "):
            row["check_ms"] = line.split()[1]
        elif line.startswith("STEP1_MS "):
            row["step1_ms"] = line.split()[1]
        elif line.startswith("RUN_MS "):
            row["run_ms"] = line.split()[1]
        elif line.startswith("RUNDIRECT\t"):
            parts = line.split("\t")
            if parts[1] != "OK":
                row["verdict"] = "RUN_REFUSED"
                row["reason"] = parts[2] if len(parts) > 2 else ""
            else:
                saw_rundirect_ok = True
                row["cycles"] = parts[2] if len(parts) > 2 else ""
    # A VERDICT line is printed before execution finishes, so an ACCEPTED that
    # is followed by a crash, a truncated run, or a missing RUNDIRECT would
    # otherwise survive into the TSV as a pass. Require the process to have
    # exited 0 AND every execution marker to be present with the requested
    # cycle count.
    if row["verdict"] == "ACCEPTED":
        missing = [k for k in ("check_ms", "step1_ms", "run_ms") if not row[k]]
        if exit_code != 0:
            row["verdict"] = "LEAN_ERROR"
            row["reason"] = (f"probe exited {exit_code} after printing ACCEPTED"
                             + (" | " + " | ".join(t[:120] for t in tail[:2]) if tail else ""))
        elif missing or not saw_rundirect_ok:
            row["verdict"] = "SIM_INCOMPLETE"
            row["reason"] = ("missing " + ",".join(missing + ([] if saw_rundirect_ok else ["RUNDIRECT/OK"])))
        elif str(row["cycles"]) != str(cycles):
            row["verdict"] = "SIM_INCOMPLETE"
            row["reason"] = f"ran {row['cycles']} cycle(s), expected {cycles}"
    if row["verdict"] == "NO_CERT" and tail:
        row["verdict"] = "LEAN_ERROR"
        row["reason"] = " | ".join(t[:200] for t in tail[:3])
    # WRITTEN AFTER THE OUTPUT IS PARSED.
    #
    # This block sat immediately after `run_timed`, so the verdict it recorded
    # was whatever `row` held before the probe's output had been read --
    # measured: a run whose TSV row says ACCEPTED left an evidence file saying
    # NO_CERT. Evidence that contradicts the row it describes is worse than no
    # evidence.
    # HASH THE EXACT BYTES THAT ARE SAVED.
    #
    # The previous version hashed `out + err` and then wrote a file with a
    # "--- stderr ---" separator in the middle, so the reported digest was of a
    # string that exists nowhere. Measured on a real run: reported
    # 794b9e87...4854 against a retained file hashing 887a3e49...d045.
    #
    # stdout and stderr are now saved SEPARATELY, each hashed over exactly the
    # bytes written, so there is no concatenation to disagree about.
    if evidence_dir:
        tag = "__".join(x for x in (row.get("family") or "nofam", row.get("case") or name,
                                    row.get("mode") or "nomode",
                                    (row.get("ran_sha256") or "nohash")[:12]) if x)
        tag = re.sub(r"[^A-Za-z0-9_.-]", "_", tag)
        try:
            os.makedirs(evidence_dir, exist_ok=True)
            written = {}
            for what, text in (("out", out), ("err", err)):
                fp = os.path.join(evidence_dir, f"{tag}.{what}.log")
                data = text.encode("utf-8", errors="replace")
                with open(fp, "wb") as fh:
                    fh.write(data)
                written[what] = (os.path.basename(fp),
                                 hashlib.sha256(data).hexdigest(), len(data))
            # THE GENERATED PROBE IS RETAINED BESIDE ITS LOG. The workdir is
            # removed at the end of the sweep unless --keep, so the recorded
            # command named a file that no longer existed and probe_sha256
            # could not be checked against anything.
            probe_copy = ""
            if os.path.exists(probe):
                probe_copy = os.path.join(evidence_dir, f"{tag}.probe.lean")
                with open(probe, "rb") as src_fh, open(probe_copy, "wb") as dst_fh:
                    dst_fh.write(src_fh.read())
                row["probe_sha256"] = file_sha(probe_copy)
                probe_copy = os.path.basename(probe_copy)
            row["log_sha256"] = written["out"][1]
            with open(os.path.join(evidence_dir, tag + ".json"), "w") as fh:
                json.dump({"path": path, "family": row.get("family"), "case": row.get("case"),
                           "mode": row.get("mode"), "verdict": row.get("verdict"),
                           "command": cmd, "command_probe": probe,
                           "retained_probe": probe_copy,
                           "exit_code": exit_code, "timed_out": timed_out,
                           "wall_s": row["wall_s"],
                           "artifact_sha256": row.get("artifact_sha256"),
                           "ran_sha256": row.get("ran_sha256"),
                           "probe_sha256": row.get("probe_sha256"),
                           "stdout_file": written["out"][0],
                           "stdout_sha256": written["out"][1],
                           "stdout_bytes": written["out"][2],
                           "stderr_file": written["err"][0],
                           "stderr_sha256": written["err"][1],
                           "stderr_bytes": written["err"][2],
                           "cycles_requested": cycles, "timeout_s": timeout}, fh, indent=1)
                fh.write("\n")
            row["evidence"] = tag
        except OSError as e:
            row["evidence"] = f"(unwritable: {e})"
    else:
        row["log_sha256"] = hashlib.sha256(
            (out + err).encode(errors="replace")).hexdigest()
    return row


def scratch_dir():
    """Where the generated probes live.

    A FUNCTION so a control can CALL it. Asserting this by grepping the source
    for a path is the mistake an earlier control suite here made: it passes on
    a comment and fails on a rename.
    """
    return os.path.realpath(os.environ.get("D2SWEEP_TMPDIR") or os.path.join(
        os.path.dirname(LEAN_DIR), "..", "generated", "pass_lean_runtime_tmp"))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("roots", nargs="*")
    ap.add_argument("--manifest", type=str, default="",
                    help="a sweep_manifest.py manifest. The sweep then runs EXACTLY the "
                         "payloads it names, with their configured family/case/mode in the "
                         "output -- not whatever a directory walk happens to find.")
    ap.add_argument("--manifest-set", default="to_run", choices=["to_run", "investigate_only"],
                    help="which manifest set to run. `investigate_only` is the gate-REFUSED "
                         "candidates: a result there describes their payload and may NOT "
                         "promote the export or any RTL-alignment disposition.")
    ap.add_argument("--out", required=True)
    ap.add_argument("--jobs", type=int, default=4)
    ap.add_argument("--cycles", type=int, default=4)
    ap.add_argument("--limit", type=int, default=0)
    ap.add_argument("--filter", default="",
                    help="regex matched against the MODULE NAME (not the path), "
                         "e.g. '^(txfma_f0|txfma_e5)$'")
    ap.add_argument("--timeout", type=int, default=1800)
    ap.add_argument("--max-rec-depth", type=int, default=0,
                    help="override the emitted `set_option maxRecDepth`; the emitted\n1000000 is not enough for the largest CVA6 certificates (try 20000000)")
    ap.add_argument("--evidence", default="",
                    help="where to retain each payload's probe log, command and exit status "
                         "(default: <out>.evidence)")
    ap.add_argument("--keep", action="store_true", help="keep the generated probes")
    args = ap.parse_args()

    pat = re.compile(args.filter) if args.filter else None
    seen, files = set(), []
    ident = {}        # path -> configured identity, when a manifest supplied it

    if args.manifest:
        import json as _json
        man = _json.loads(open(args.manifest).read())
        want = set(man.get(args.manifest_set, []))
        missing_files = [a for a in sorted(want) if not os.path.exists(a)]
        if missing_files:
            print(f"direct_sweep: {len(missing_files)} manifest payload(s) are not on disk, "
                  f"e.g. {missing_files[0]}", file=sys.stderr)
            return 2
        # EXACTLY ONE ELIGIBLE ENTRY PER SELECTED PAYLOAD.
        #
        # Taking the first match quietly accepted both "no entry at all" and
        # "two entries disagreeing about what this payload is". A payload the
        # manifest cannot identify may not run, and one it identifies twice,
        # differently, is ambiguous rather than resolved by arrival order.
        # Explicit `aliases` are not ambiguity: that is ONE entry recording the
        # other configurations that share its payload.
        by_path = {}
        for e in man.get("entries", []):
            a = e.get("artifact")
            if a in want:
                by_path.setdefault(a, []).append(e)
        missing = sorted(a for a in want if not by_path.get(a))
        ambiguous = sorted(a for a, es in by_path.items()
                           if len({(x.get("family"), x.get("case"), x.get("mode"),
                                    x.get("artifact_sha256")) for x in es}) > 1)
        if missing or ambiguous:
            for a in missing:
                print(f"direct_sweep: manifest selects {a} but carries no identity entry "
                      f"for it", file=sys.stderr)
            for a in ambiguous:
                print(f"direct_sweep: manifest carries {len(by_path[a])} disagreeing "
                      f"identity entries for {a}", file=sys.stderr)
            return 2
        ident = {a: es[0] for a, es in by_path.items()}
        files = sorted(want, key=lambda q: os.path.getsize(q))
        if args.roots:
            print("direct_sweep: --manifest given, so the positional roots are ignored",
                  file=sys.stderr)
    for root in args.roots if not args.manifest else []:
        for dirpath, _dirs, names in os.walk(root):
            for n in sorted(names):
                if not n.endswith("_Lgraph.lean"):
                    continue
                p = os.path.join(dirpath, n)
                # Match the MODULE NAME, not the path. Matching the path made
                # `--filter '^(a|b)$'` match nothing at all (every candidate is
                # an absolute path), so a caller selecting an explicit module
                # set silently swept zero certificates and every column read
                # "not-run".
                if pat and not pat.search(n.replace("_Lgraph.lean", "")):
                    continue
                try:
                    h = hashlib.md5(open(p, "rb").read()).hexdigest()
                except OSError:
                    continue
                if h in seen:
                    continue
                if b"_designCert" not in open(p, "rb").read(1 << 20):
                    continue
                seen.add(h)
                files.append(p)
    files.sort(key=lambda p: os.path.getsize(p))

    # A sweep that found NOTHING is a configuration error, not a vacuous pass:
    # without this every caller gets "ACCEPTED 0/0" and a filter typo reads as
    # success. (A --filter regex matched against the path instead of the module
    # name did exactly that here.)
    if not files:
        print(f"direct_sweep: no certificates matched (roots={args.roots} "
              f"filter={args.filter!r})", file=sys.stderr)
        return 2
    if args.limit:
        files = files[:args.limit]
    print(f"# {len(files)} unique certificates", file=sys.stderr)

    lean_env = dict(os.environ)
    lean_env["LEAN_NUM_THREADS"] = "1"
    # `lake env` once, reused for every job
    try:
        lp = subprocess.run(["lake", "env", "printenv", "LEAN_PATH"], cwd=LEAN_DIR,
                            capture_output=True, text=True)
        if lp.returncode == 0 and lp.stdout.strip():
            lean_env["LEAN_PATH"] = lp.stdout.strip()
    except (FileNotFoundError, NotADirectoryError):
        pass  # no toolchain here; every probe will then fail by name, not by traceback

    # PROJECT-LOCAL SCRATCH. The default was $TMPDIR or /tmp, which parallel
    # runs share and clobber; this workspace requires temp directories under
    # the project. The probes are megabytes of certificate literal, so they do
    # not belong in a shared /tmp either way.
    scratch = scratch_dir()
    os.makedirs(scratch, exist_ok=True)
    workdir = tempfile.mkdtemp(prefix="d2sweep_", dir=scratch)
    cols = ["family", "case", "mode", "module", "verdict", "reason", "sources", "nodes",
            "outputs", "flops", "mems", "inputs", "check_ms", "step1_ms", "run_ms",
            "cycles", "wall_s", "rss_kb", "artifact_sha256", "ran_sha256",
            "probe_sha256", "log_sha256", "evidence", "aliases", "path"]
    rows = []
    # EVERY FINISHED ROW IS WRITTEN AS IT FINISHES, to `<out>.partial`.
    #
    # The sorted `<out>` is written once at the end, so a sweep that is stopped
    # -- a harness time limit, a kill, a crash -- used to leave NOTHING, however
    # many modules had already run.  Measured: minion_dcache_top ACCEPTED after
    # 4261s and its timings were lost because the next module was still going
    # when the run was stopped, and a 71-minute probe is not something to repeat
    # for a column.
    #
    # A SEPARATE FILE, not `<out>` itself: a half-written `<out>` would be read
    # by every consumer as a complete sweep. `.partial` is evidence, and its
    # absence next to a present `<out>` is what says the run finished.
    # Project-local evidence, beside the table.
    evidence_dir = args.evidence or (args.out + ".evidence")
    os.makedirs(evidence_dir, exist_ok=True)
    partial = args.out + ".partial"
    try:
        with open(partial, "w", encoding="utf-8") as pf:
            pf.write("\t".join(cols) + "\n")
            pf.flush()
            with ThreadPoolExecutor(max_workers=args.jobs) as ex:
                futs = [ex.submit(run_one, p, workdir, args.cycles, args.timeout, lean_env,
                                  args.max_rec_depth, ident.get(p), bool(args.manifest),
                                  evidence_dir)
                        for p in files]
                # AS THEY COMPLETE, not in submission order.  Iterating
                # `futs` and calling `f.result()` blocks on the FIRST module,
                # so with jobs>1 a short module that finished half an hour ago
                # is still not on disk -- which is exactly what "rows as they
                # finish" is supposed to prevent.  (At jobs=1 the two orders
                # coincide, which is why this was not visible.)
                for i, f in enumerate(as_completed(futs)):
                    r = f.result()
                    # The CONFIGURED identity travels with the row. A sweep keyed
                    # only on a module name cannot say which configuration it ran,
                    # and two configurations can share a payload.
                    rows.append(r)
                    pf.write("\t".join(str(r.get(c, "")) for c in cols) + "\n")
                    pf.flush()
                    os.fsync(pf.fileno())   # survive a kill, not just an exit
                    print(f"[{i+1}/{len(files)}] {r['module']:40s} {r['verdict']:12s} "
                          f"nodes={r['nodes']:>7s} wall={r['wall_s']:>7s}s {r['reason'][:60]}",
                          file=sys.stderr, flush=True)
    finally:
        if not args.keep:
            shutil.rmtree(workdir, ignore_errors=True)
        else:
            print(f"# probes kept in {workdir}", file=sys.stderr)

    rows.sort(key=lambda r: (r["verdict"] != "ACCEPTED", r.get("family", ""),
                             r.get("mode", ""), r["module"]))
    # ATOMIC PUBLISH.  Writing `args.out` in place means an interruption during
    # the final write leaves a TRUNCATED file that every consumer reads as a
    # complete sweep -- the same failure the `.partial` split exists to avoid,
    # one step later. Sibling temp (same directory, so `os.replace` is a rename
    # within one filesystem), fsync, then replace.
    tmp = args.out + ".tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        f.write("\t".join(cols) + "\n")
        for r in rows:
            f.write("\t".join(str(r.get(c, "")) for c in cols) + "\n")
        f.flush()
        os.fsync(f.fileno())
    os.replace(tmp, args.out)
    # TOOL AND LIBRARY CONTEXT, beside the table rather than implied by it. A
    # sweep's rows say what ran; this says what it ran UNDER.
    ctx = {
        "written_at": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "manifest": os.path.abspath(args.manifest) if args.manifest else None,
        "manifest_sha256": (hashlib.sha256(open(args.manifest, "rb").read()).hexdigest()
                            if args.manifest else None),
        "manifest_set": args.manifest_set if args.manifest else None,
        "cycles": args.cycles, "timeout_s": args.timeout, "jobs": args.jobs,
        "max_rec_depth": args.max_rec_depth,
        "lean_dir": LEAN_DIR,
        "lean_toolchain": (open(os.path.join(LEAN_DIR, "lean-toolchain")).read().strip()
                           if os.path.exists(os.path.join(LEAN_DIR, "lean-toolchain")) else None),
        "lean_path": lean_env.get("LEAN_PATH", ""),
        "rows": len(rows),
        "verdicts": {k: sum(1 for r in rows if r["verdict"] == k)
                     for k in sorted({r["verdict"] for r in rows})},
    }
    # `dirname(LEAN_DIR)` is `<root>/formal`, not `<root>`: the writer looked for
    # formal/generated/... and silently found nothing, so every context file
    # omitted the fingerprint while the real one sat beside it.
    lib = os.path.join(os.path.dirname(os.path.dirname(LEAN_DIR)), "generated",
                       "master_port", "d2_runtime", "library_fingerprint.txt")
    if os.path.exists(lib):
        ctx["library_fingerprint_file"] = lib
        ctx["library_fingerprint_mtime"] = time.strftime(
            "%Y-%m-%dT%H:%M:%SZ", time.gmtime(os.path.getmtime(lib)))
        ctx["library_fingerprint"] = open(lib).read().splitlines()
    else:
        ctx["library_fingerprint"] = None
        ctx["library_fingerprint_note"] = f"not found at {lib}"
    ctx["evidence_dir"] = evidence_dir
    with open(args.out + ".context.json", "w") as f:
        json.dump(ctx, f, indent=1)
        f.write("\n")
    # The complete table exists now, so the partial is no longer evidence of
    # anything -- leaving it would make a finished run look interrupted.
    try:
        os.remove(partial)
    except OSError:
        pass
    n_ok = sum(1 for r in rows if r["verdict"] == "ACCEPTED")
    print(f"# ACCEPTED {n_ok}/{len(rows)} -> {args.out}", file=sys.stderr)
    return 0 if n_ok == len(rows) else 1


if __name__ == "__main__":
    sys.exit(main())
