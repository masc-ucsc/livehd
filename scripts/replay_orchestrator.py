#!/usr/bin/env python3
"""Run the configured replay, separating EXPORT from PROOF.

WHY THIS WAS REWRITTEN. The first version ran with `RUN_LEAN=false` and marked a
job `passed` when the export exited 0 and a `_Lgraph.lean` file existed. Driven
with a worker that wrote `THIS IS NOT VALID LEAN AND HAS NO PROOF`, every mode
came back `passed`. An export is not a proof, and a file's existence is not its
contents.

So a job now carries TWO states:

  export_state   EXPORTED / REFUSED / FAILED / SKIPPED -- did the artifact get
                 produced, and if not, was that a real upstream disposition?
  proof_state    PROVEN / FAILED / NOT_RUN -- did Lean typecheck it and did the
                 exact-obligation axiom audit pass?

`PROVEN` requires the audited serial queue AND `axiom_audit.py` against the
artifact. Static gates, a COMPLETE marker and an older artifact establish none
of it.

THREE OTHER THINGS THE FIRST VERSION GOT WRONG, all measured:

  * it set `EMIT_FAST_BRIDGE`, which the runner ignores -- the runner reads
    `LEAN_EMIT_FAST_BRIDGE` and `LEAN_CERT_WF`. The intended flags are now set
    by name, and a `cert_wf=skip` job can never be labelled full-WF;
  * it keyed jobs on (family, top, mode). The manifest holds BOTH `cva6_tlb`
    and `legacy_cva6_tlb_gate` with top `cva6_tlb_gate`, so their output paths
    collided. Jobs are keyed on CASE;
  * it rewrote one shared inventory after every job, so two parallel launches
    would overwrite each other. Each job writes its own immutable result file
    and the inventory is assembled from those.

Usage:
  replay_orchestrator.py status <inventory.json>
  replay_orchestrator.py run    <inventory.json> --family F --mode M
                                [--limit N] [--export-only]
"""

import argparse
import contextlib
import fcntl
import hashlib
import re
import json
import os
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
# One id per invocation: it names the output directory and is recorded in every
# result, so a result can be tied to the run that produced it.
RUN_ID = os.environ.get("REPLAY_RUN_ID") or time.strftime("%Y%m%dT%H%M%SZ", time.gmtime()) + f"-{os.getpid()}"
REPLAY = ROOT / "generated" / "master_port" / "replay"
RESULTS = REPLAY / "results"
JOBS = REPLAY / "jobs"
FORMAL = ROOT / "formal" / "lean"

EXPORT_STATES = ["EXPORTED", "REFUSED", "FAILED", "SKIPPED"]
# TYPECHECKED is NOT a proof: a legacy fast model elaborates without carrying a
# refinement obligation, so calling it PROVEN would overstate it.
# UNVERIFIABLE: the run's evidence is gone, so the verdict cannot be re-checked.
# It is neither a proof nor a failure, and it counts as NOT ACCEPTED.
PROOF_STATES = ["PROVEN", "TYPECHECKED", "FAILED", "UNVERIFIABLE", "NOT_RUN"]

# What each REQUESTED mode must demonstrate. Supplied to the auditor explicitly;
# running it in `auto` mode lets the ARTIFACT choose the obligations, so a
# verified certificate satisfied a legacy chunked-WF request. Measured.
# The value is (minimum obligations, success state). The minimum is ADDED to
# what the auditor derives from the artifact's structure -- it never replaces
# it. An earlier version passed `--required _comb_refines_fast` for the bridge
# modes, which SUBSTITUTED for the derivation, so a stateful model with no
# next/step obligations came back PROVEN. A mode may strengthen the requirement;
# it may not weaken it.
MODE_OBLIGATIONS = {
    "verified_compiler":  (["_step_correct", "_step_legacy_correct"], "PROVEN"),
    # Legacy fast is TYPECHECKED under its declared scope: it carries no
    # refinement obligation, and demanding one it was never asked to emit would
    # fail a correct export.
    "legacy":             ([], "TYPECHECKED"),
    "legacy_bridge":      (["_comb_refines_fast"], "PROVEN"),
    "legacy_full_bridge": (["_comb_refines_fast"], "PROVEN"),
    "legacy_chunked_wf":  (["_graphCert_wf", "_comb_refines_fast"], "PROVEN"),
}


def sha(p: Path) -> str:
    try:
        return hashlib.sha256(p.read_bytes()).hexdigest()[:16]
    except OSError:
        return "-"


def job_key(j: dict) -> str:
    return f"{j['family']}__{j['case']}__{j['mode']}"


def result_path(j: dict) -> Path:
    return RESULTS / f"{job_key(j)}.json"


def load_results(inv: dict) -> dict:
    """Per-job immutable results, assembled fresh. Never a shared writer."""
    out = {}
    for j in inv["jobs"]:
        p = result_path(j)
        if p.exists():
            try:
                out[job_key(j)] = json.loads(p.read_text())
            except json.JSONDecodeError:
                out[job_key(j)] = {"export_state": "FAILED", "proof_state": "NOT_RUN",
                                   "detail": "unreadable result file"}
    return out


def status(inv: dict) -> int:
    res = load_results(inv)
    rows: dict = {}
    for j in inv["jobs"]:
        r = res.get(job_key(j), {})
        k = (j["family"], j["mode"])
        rows.setdefault(k, {"total": 0})
        rows[k]["total"] += 1
        e = r.get("export_state", "queued")
        p = r.get("proof_state", "-")
        rows[k][f"e:{e}"] = rows[k].get(f"e:{e}", 0) + 1
        if e == "EXPORTED":
            rows[k][f"p:{p}"] = rows[k].get(f"p:{p}", 0) + 1

    print("EXPORT state (an export is not a proof)")
    cols = EXPORT_STATES + ["queued"]
    print(f"{'family':9} {'mode':20} " + " ".join(f"{c:>9}" for c in cols))
    tot = {c: 0 for c in cols}
    for k in sorted(rows):
        r = rows[k]
        print(f"{k[0]:9} {k[1]:20} " + " ".join(f"{r.get('e:' + c, 0):>9}" for c in cols))
        for c in cols:
            tot[c] += r.get("e:" + c, 0)
    print(f"{'TOTAL':9} {'':20} " + " ".join(f"{tot[c]:>9}" for c in cols))

    print("\nPROOF state, over EXPORTED jobs only")
    print(f"{'family':9} {'mode':20} " + " ".join(f"{c:>9}" for c in PROOF_STATES))
    ptot = {c: 0 for c in PROOF_STATES}
    for k in sorted(rows):
        r = rows[k]
        print(f"{k[0]:9} {k[1]:20} " + " ".join(f"{r.get('p:' + c, 0):>9}" for c in PROOF_STATES))
        for c in PROOF_STATES:
            ptot[c] += r.get("p:" + c, 0)
    print(f"{'TOTAL':9} {'':20} " + " ".join(f"{ptot[c]:>9}" for c in PROOF_STATES))
    print(f"\nconfigurations: {inv['configurations']}")
    print(f"independently audited PROOFS: {ptot['PROVEN']} of {len(inv['jobs'])} jobs")
    return 0


def mode_env(mode: str) -> dict:
    """The runner's REGISTERED variable names, set deliberately."""
    if mode == "verified_compiler":
        return {"LEAN_MODE": "verified_compiler", "LEAN_EMIT_FAST_BRIDGE": "false",
                "LEAN_CERT_WF": "skip"}
    if mode == "legacy":
        # THE STANDALONE FAST MODEL: no bridge, no WF.
        #
        # This asked for LEAN_EMIT_FAST_BRIDGE=true, which is the FULL BRIDGE
        # emission -- so the "fast model" job requested exactly what
        # legacy_bridge requests, and was then judged against bridge
        # obligations it had not been asked for as a standalone mode. A full
        # bridge does include the fast model, but running one is not running
        # the other.
        return {"LEAN_MODE": "legacy", "LEAN_EMIT_FAST_BRIDGE": "false",
                "LEAN_CERT_WF": "skip"}
    if mode == "legacy_bridge":  # full bridge
        return {"LEAN_MODE": "legacy", "LEAN_EMIT_FAST_BRIDGE": "true",
                "LEAN_CERT_WF": "skip"}
    if mode == "legacy_chunked_wf":
        return {"LEAN_MODE": "legacy", "LEAN_EMIT_FAST_BRIDGE": "true",
                "LEAN_CERT_WF": "chunked"}
    if mode == "legacy_full_bridge":
        return {"LEAN_MODE": "legacy", "LEAN_EMIT_FAST_BRIDGE": "true",
                "LEAN_CERT_WF": "skip"}
    raise ValueError(f"unregistered mode {mode}")


LEAN_LOCK = ROOT / "generated" / "pass_lean_runtime_tmp" / "lean_slot.lock"


@contextlib.contextmanager
def lean_slot():
    """One Lean typecheck at a time AMONG PROCESSES THAT TAKE THIS LOCK.

    Scope, stated accurately: this serialises orchestrator runs that reach this
    code. It does NOT serialise against `run_lean_queue.sh`, the oracle scripts,
    a bare `lake env lean`, or any launcher started before this existed -- none
    of them acquire it. Two queues launched earlier were still running Lean
    concurrently after this was added, and that is expected, not a failure of
    the lock.

    Why serialise at all: each job is multi-GB, and on a shared NFS box
    concurrent ones starve its clients and can OOM each other -- where an
    OOM-killed run looks exactly like a proof failure. An already-running
    legitimate job is WAITED FOR, never killed, so a long proof in flight is
    never thrown away to acquire the slot.

    To make it general, every proof launcher would have to take the same lock;
    until then its scope is this file.
    """
    LEAN_LOCK.parent.mkdir(parents=True, exist_ok=True)
    with LEAN_LOCK.open("w") as fh:
        fcntl.flock(fh, fcntl.LOCK_EX)
        try:
            yield
        finally:
            fcntl.flock(fh, fcntl.LOCK_UN)


def typecheck(artifact: Path, top: str, out: Path, mode: str) -> dict:
    """Typecheck and audit against the REQUESTED mode's obligations.

    The obligation set comes from `mode`, not from the artifact. Running the
    auditor in `auto` mode lets the file decide what it has to prove, which is
    how a verified certificate came back PROVEN for a `legacy_chunked_wf`
    request -- with no bridge and no graphCert_wf anywhere in it.
    """
    # ABSOLUTE paths: this runs with cwd=formal/lean, so a relative `-o` lands
    # nowhere and /usr/bin/time exits 125 before Lean is ever invoked.
    out = out.resolve()
    log = out / "typecheck.log"
    tv = out / "typecheck.time"
    out.mkdir(parents=True, exist_ok=True)
    cmd = ["/usr/bin/time", "-v", "-o", str(tv), "nice", "-n", "19", "ionice", "-c", "3",
           "lake", "env", "lean", str(artifact.resolve())]
    env = dict(os.environ)
    env["PATH"] = str(Path.home() / ".elan" / "bin") + os.pathsep + env.get("PATH", "")
    env["LEAN_NUM_THREADS"] = env.get("LEAN_NUM_THREADS", "8")
    env["TMPDIR"] = str(ROOT / "generated" / "pass_lean_runtime_tmp")
    with lean_slot():
        with log.open("w") as fh:
            rc = subprocess.run(cmd, cwd=str(FORMAL), env=env, stdout=fh,
                                stderr=subprocess.STDOUT, timeout=7200).returncode
    text = log.read_text(errors="replace")
    timing = tv.read_text(errors="replace") if tv.exists() else ""
    wall = next((l.split(": ")[-1].strip() for l in timing.splitlines()
                 if "Elapsed (wall clock)" in l), None)
    rss = next((l.split(": ")[-1].strip() for l in timing.splitlines()
                if "Maximum resident set size" in l), None)
    required, success_state = MODE_OBLIGATIONS[mode]
    cmd_audit = ["python3", "-I", str(ROOT / "pass" / "lean" / "scripts" / "axiom_audit.py"),
                 str(log), top, "--artifact", str(artifact)]
    if mode == "verified_compiler":
        cmd_audit += ["--required", ",".join(required)]
    elif mode == "legacy":
        # A fast-only export carries NO refinement theorem, so the auditor's
        # structural derivation (which owes comb, and next/step when the model
        # has state) would fail a correct one. The declared scope here is "it
        # elaborates", so the obligation set is explicitly EMPTY.
        cmd_audit += ["--required", ""]
    elif required:
        # MIN, not required: the structural derivation (next/step when the model
        # has state, graphCert_wf when it has a graph certificate) still applies.
        cmd_audit += ["--min-required", ",".join(required), "--witnesses", "auto"]
    audit = subprocess.run(cmd_audit, capture_output=True, text=True)
    errs = text.count("error:")
    # A requested obligation that the ARTIFACT does not even declare is a scope
    # failure, not an audit the file happens to pass.
    src = artifact.read_text(errors="replace")
    effective = list(required)
    if mode not in ("verified_compiler", "legacy") \
            and re.search(rf"^structure {re.escape(top)}_state\b", src, re.M):
        # Same rule the auditor derives: a model WITH STATE owes next and step.
        effective += ["_next_refines_fast", "_step_refines_fast"]
    absent = [o for o in effective if f"theorem {top}{o}" not in src]
    ok = rc == 0 and errs == 0 and audit.returncode == 0 and not absent
    return {"proof_state": success_state if ok else "FAILED",
            "requested_obligations": [top + o for o in effective],
            "obligations_absent_from_artifact": [top + o for o in absent],
            "lean_exit": rc, "lean_errors": errs,
            "axiom_audit": "ok" if audit.returncode == 0 else "FAIL",
            "axiom_detail": (audit.stdout + audit.stderr).strip().splitlines()[:6],
            "wall_clock": wall, "peak_rss_kib": rss,
            "typecheck_log_sha256": sha(log)}


# Which environment variable each family's inputs come from. Set them in the
# environment or pass --root; nothing here names a filesystem path.
FAMILY_ROOT = {"CORE-ET": "COREET_ROOT", "CVA6": "CVA6_ROOT", "DINO": "HAGENT_BUILD"}


def require_root(env: dict, fam: str) -> str:
    """Empty when the family's input root is set and exists; else why not."""
    var = FAMILY_ROOT.get(fam)
    if var is None:
        return f"no input root is defined for family {fam}"
    val = env.get(var)
    if not val:
        return f"{var} is not set (pass --root {fam}=PATH or export {var})"
    if not Path(val).exists():
        return f"{var}={val} does not exist"
    return ""


def run_one(j: dict, lhd: Path, export_only: bool) -> dict:
    top, mode, fam, case = j["top"], j["mode"], j["family"], j["case"]
    # A FRESH directory per invocation.
    #
    # Hashing the artifact after the worker does not establish that the worker
    # made it. Measured: pre-seed a valid artifact, have the worker exit 0
    # without writing anything, and the job came back EXPORTED and then PROVEN.
    # Exporting into a directory that did not exist a moment ago removes the
    # question -- anything found there was produced by this run.
    #
    # CASE, not top: the manifest holds two cases sharing top `cva6_tlb_gate`.
    base = JOBS / fam / mode / case
    out = base / RUN_ID
    if out.exists():
        raise RuntimeError(f"run directory already exists: {out}")
    out.mkdir(parents=True)
    # A stable pointer to the newest run, so paths stay predictable without
    # letting an older run's output stand in for this one.
    latest = base / "latest"
    if latest.is_symlink() or latest.exists():
        latest.unlink()
    latest.symlink_to(RUN_ID)
    log = out / "job.log"
    rec = {"family": fam, "case": case, "top": top, "mode": mode,
           "run_id": RUN_ID, "out_dir": str(out.relative_to(ROOT)),
           "lhd_sha256": sha(lhd), "requested_scope": mode_env(mode),
           "requested_obligations": [top + o for o in MODE_OBLIGATIONS[mode][0]],
           "export_state": "FAILED", "proof_state": "NOT_RUN"}

    env = dict(os.environ)
    env.update(mode_env(mode))
    # BENCHMARK ROOTS ARE SUPPLIED, NOT BAKED IN. A default pointing at one
    # person's home directory is both a privacy leak in a committed file and a
    # silent failure for anyone else: the job would "run" against a path that
    # does not exist. Missing roots are an unresolved prerequisite, reported by
    # name.
    missing_root = require_root(env, fam)
    if missing_root:
        rec["export_state"] = "SKIPPED"
        rec["detail"] = f"unresolved prerequisite: {missing_root}"
        return rec
    env["LHD"] = str(lhd)
    env["OUT"] = str(out)
    env["RUN_LEAN"] = "false"        # the audited queue does the typecheck
    env["RUN_LEC_GATE"] = "false"
    if fam == "CORE-ET":
        env["COREET_TOP"] = top
        cmd = ["bash", str(ROOT / "scripts" / "run_coreet_module_lean.sh")]
    elif fam == "CVA6":
        # The wrapper is the prerequisite, and a MISSING one stays visibly
        # unresolved rather than becoming a quiet pass. Regenerated wrappers
        # first, then the committed hand-written ones for the legacy-only
        # configurations that have no elaborated instance in the core.
        # SELECT BY CASE, not by top. The manifest holds two configurations
        # sharing top `cva6_tlb_gate`: `cva6_tlb` (verified+legacy, from the
        # core's own elaborated instance) and `legacy_cva6_tlb_gate` (the
        # historical legacy-only procedure, whose hand-written wrapper is
        # committed). The two wrapper FILES differ, so preferring the
        # regenerated one for both would silently run the historical case
        # against the wrong input -- a unique output directory does not
        # preserve a procedure.
        hand = ROOT / "scripts" / "cva6_module_wrappers" / f"{top}.sv"
        gen = REPLAY / "cva6_wrappers" / f"{top}.sv"
        historical = case.startswith("legacy_")
        order = (hand, gen) if historical else (gen, hand)
        wrapper = next((c for c in order if c.exists()), None)
        rec["wrapper_selection"] = ("historical hand-written (case is legacy_*)"
                                    if historical else "regenerated from the retained AST")
        if wrapper is None:
            rec["export_state"] = "SKIPPED"
            rec["detail"] = f"no wrapper for {top} -- unresolved prerequisite"
            return rec
        fl = ROOT / "generated" / "cva6_filelists" / \
            "cv64a6_imafdc_sv39_hpdcache_wb.top_cva6.flistplus.f"
        if not fl.exists():
            rec["export_state"] = "SKIPPED"
            rec["detail"] = "no CVA6 filelist -- unresolved prerequisite"
            return rec
        env["CVA6_TOP"] = top
        env["CVA6_FILELIST"] = str(fl)
        env["CVA6_WRAPPER_FILE"] = str(wrapper)
        rec["wrapper"] = str(wrapper.relative_to(ROOT))
        rec["wrapper_sha256"] = sha(wrapper)
        rec["filelist_sha256"] = sha(fl)
        cmd = ["bash", str(ROOT / "scripts" / "run_cva6_module_lean_stress.sh")]
    elif fam == "DINO":
        hb = Path(env["HAGENT_BUILD"])  # presence already checked by require_root
        # The DINO driver exports all three designs in one invocation, selected
        # by SC_DIR/PIPE_DIR/DUAL_DIR. Pointing the other two at a nonexistent
        # path would be a silent skip, so instead each job runs the driver with
        # only ITS design's directory set -- the others are left unset, which
        # the driver treats as "not requested".
        env["HAGENT_BUILD"] = str(hb)
        subdir = {"SingleCycleCPU": ("SC_DIR", "build_singlecyclecpu_d"),
                  "PipelinedCPU": ("PIPE_DIR", "build_pipelined_d"),
                  "PipelinedDualIssueCPU": ("DUAL_DIR", "build_dualissue_d")}[top]
        for var in ("SC_DIR", "PIPE_DIR", "DUAL_DIR"):
            env.pop(var, None)
        env[subdir[0]] = str(hb / subdir[1])
        env["RUN_LEC_GATE"] = "false"
        cmd = ["bash", str(ROOT / "scripts" / "run_dino_lgraph_lean.sh")]
        # A SECOND STAGE for DINO, because its driver cannot express these modes.
        #
        # run_dino_lgraph_lean.sh passes only strict/emit_cert/mode/max_width --
        # it has no cert_wf and no emit_fast_bridge option, so a chunked-WF or
        # full-bridge request exported cleanly with none of the obligations it
        # asked for. Rather than weaken the scope check, the driver is used for
        # what it does well (building the LGraph from Chisel output) and `lhd`
        # is then invoked directly with the full option set.
        rec["two_stage"] = "driver builds the LGraph; lhd re-emits with the requested scope"
    else:
        rec["export_state"] = "SKIPPED"
        rec["detail"] = f"{fam} adapter not wired -- unresolved prerequisite"
        return rec

    t0 = time.time()
    with log.open("w") as fh:
        rc = subprocess.run(cmd, env=env, stdout=fh, stderr=subprocess.STDOUT,
                            cwd=str(ROOT), timeout=3600).returncode
    if fam == "DINO" and rc == 0:
        lg = out / "lgdb" / top
        if not lg.exists():
            rec["detail"] = f"DINO driver left no LGraph at {lg}"
            return rec
        want = mode_env(mode)
        stage2 = [str(lhd), "compile", f"lg:{lg}", "--top", top,
                  "--workdir", str(out / "w2"),
                  "--emit-dir", f"lean:{out / 'lean'}",
                  "--set", "formal.lean.mode=legacy",
                  "--set", "formal.lean.emit_cert=true",
                  "--set", f"formal.lean.emit_fast_bridge={want['LEAN_EMIT_FAST_BRIDGE']}",
                  "--set", f"formal.lean.cert_wf={want['LEAN_CERT_WF']}",
                  "--set", "formal.lean.strict=true"]
        with (out / "stage2.log").open("w") as fh:
            rc = subprocess.run(stage2, env=env, stdout=fh, stderr=subprocess.STDOUT,
                                cwd=str(ROOT), timeout=3600).returncode
        rec["stage2_log_sha256"] = sha(out / "stage2.log")
    rec["export_wall_s"] = round(time.time() - t0, 2)
    rec["export_log_sha256"] = sha(log)
    text = log.read_text(errors="replace")
    artifact = next(iter(out.rglob(f"{top}_Lgraph.lean")), None)

    # DID THE REQUESTED SCOPE ACTUALLY TAKE EFFECT?
    #
    # Setting a mode variable is not the same as the runner honouring it.
    # Measured: a DINO chunked-WF job exported cleanly and produced an artifact
    # with NO graphCert_wf obligation at all, because
    # run_dino_lgraph_lean.sh passes only strict/emit_cert/mode/max_width and
    # has no cert_wf option. Labelling that EXPORTED-as-chunked-WF would assert
    # a scope the artifact does not carry.
    def scope_mismatch(art: Path) -> str:
        want = mode_env(mode)
        txt = art.read_text(errors="replace")
        if want.get("LEAN_CERT_WF") == "chunked" and f"theorem {top}_graphCert_wf" not in txt:
            return "cert_wf=chunked requested but the artifact has no graphCert_wf obligation"
        if want.get("LEAN_MODE") == "verified_compiler" and f"{top}_designCert" not in txt:
            return "verified_compiler requested but the artifact has no DesignCert"
        if want.get("LEAN_EMIT_FAST_BRIDGE") == "true" and want.get("LEAN_MODE") == "legacy" \
                and f"theorem {top}_comb_refines_fast" not in txt:
            return "fast bridge requested but the artifact has no _comb_refines_fast"
        return ""

    if rc == 0 and artifact is not None:
        mismatch = scope_mismatch(artifact)
        if mismatch:
            rec["export_state"] = "SKIPPED"
            rec["detail"] = f"unresolved: {mismatch}"
            rec["artifact_produced"] = str(artifact.relative_to(ROOT))
            return rec
        rec["export_state"] = "EXPORTED"
        rec["artifact"] = str(artifact.relative_to(ROOT))
        rec["artifact_sha256"] = sha(artifact)
        sidecar = artifact.with_name(f"{top}_io.json")
        if sidecar.exists():
            rec["sidecar_sha256"] = sha(sidecar)
    elif "pass.single_edge refused" in text or "combinational loop" in text:
        rec["export_state"] = "REFUSED"
        rec["detail"] = "upstream refusal (single_edge / combinational loop)"
    else:
        # THE GATE WRITES TO ITS OWN LOG, not to stdout. Searching the job's
        # stdout for "static gate" missed every real refusal: fpu_wrap,
        # frontend, issue_read_operands and issue_stage were recorded FAILED
        # when they had in fact been emitted and then REFUSED by the
        # const-width gate -- a real disposition, and one whose diagnostics are
        # the open mask investigation (R14.4).
        gate = next(iter(out.rglob("logs/static_gates.log")), None)
        gate_fails = []
        if gate is not None:
            gate_fails = [l.strip() for l in gate.read_text(errors="replace").splitlines()
                          if l.startswith("FAIL:")]
        if gate_fails:
            rec["export_state"] = "REFUSED"
            rec["detail"] = f"static gate refused after emission ({len(gate_fails)} flag(s))"
            rec["gate_failures"] = gate_fails[:8]
            rec["gate_log_sha256"] = sha(gate)
            # The artifact EXISTS and is retained with its digest -- a refused
            # candidate keeps its evidence. It must not enter prove as an
            # admitted export.
            emitted = next(iter(out.rglob(f"{top}_Lgraph.lean")), None)
            if emitted is not None:
                rec["artifact_produced_then_refused"] = str(emitted.relative_to(ROOT))
                rec["artifact_produced_sha256"] = sha(emitted)
        else:
            rec["detail"] = f"export exit {rc}"

    if rec["export_state"] == "EXPORTED" and not export_only:
        rec.update(typecheck(Path(rec["artifact"]), top, out, mode))
    return rec


def exit_status(sel: list) -> int:
    """Nonzero when any selected job failed or is unresolved.

    FAILED and SKIPPED both count: a missing prerequisite is an open question,
    not a pass, and a caller checking only the exit code should see that.
    REFUSED does not -- it is a real upstream disposition.
    """
    bad = 0
    for j in sel:
        p = result_path(j)
        if not p.exists():
            bad += 1
            continue
        r = json.loads(p.read_text())
        if r.get("export_state") in ("FAILED", "SKIPPED"):
            bad += 1
        elif r.get("proof_state") in ("FAILED", "UNVERIFIABLE"):
            bad += 1
    if bad:
        print(f"{bad} job(s) failed or are unresolved", file=sys.stderr)
    return 1 if bad else 0


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("command", choices=["status", "run", "prove"])
    ap.add_argument("inventory", type=Path)
    ap.add_argument("--family")
    ap.add_argument("--mode")
    ap.add_argument("--limit", type=int)
    ap.add_argument("--replay-dir", type=Path,
                    help="relocate the whole replay tree -- results AND job output. "
                         "A control suite must pass this: the env var the controls used "
                         "to set, REPLAY_RESULTS, was never read, so every control run "
                         "wrote its fixtures into the live results directory.")
    ap.add_argument("--case", action="append", default=[],
                    help="restrict to these case names (repeatable). A name that is "
                         "not in the inventory is an error, not an empty selection.")
    ap.add_argument("--redo", action="store_true",
                    help="re-run selected cases that already have a result. Without it "
                         "an existing record is kept, which is what makes a plain rerun "
                         "cheap; with it the old record is REPLACED.")
    ap.add_argument("--export-only", action="store_true")
    ap.add_argument("--lhd", type=Path, default=ROOT / "bazel-bin" / "lhd" / "lhd")
    ap.add_argument("--root", action="append", default=[],
                    metavar="FAMILY=PATH",
                    help="input root for a family, e.g. --root CORE-ET=/path/to/core-et. "
                         "May be repeated; also readable from COREET_ROOT / CVA6_ROOT / "
                         "HAGENT_BUILD.")
    a = ap.parse_args()
    for spec in a.root:
        fam, _, path = spec.partition("=")
        var = FAMILY_ROOT.get(fam)
        if var is None:
            print(f"unknown family in --root: {fam}", file=sys.stderr)
            return 2
        os.environ[var] = path
    # RELOCATE BEFORE DISPATCH, NOT AFTER IT.
    #
    # This block sat below the `status` return, so `status --replay-dir X` read
    # the DEFAULT results directory while reporting on X. Measured externally:
    # a relocated tree holding two SKIPPED records was reported as two queued
    # jobs, from the empty default tree. A relocation that only some commands
    # honour is worse than none -- it reads as a report about the tree that was
    # named.
    if a.replay_dir:
        global REPLAY, RESULTS, JOBS
        REPLAY = a.replay_dir.resolve()
        RESULTS = REPLAY / "results"
        JOBS = REPLAY / "jobs"

    inv = json.loads(a.inventory.read_text())
    if a.command == "status":
        return status(inv)

    RESULTS.mkdir(parents=True, exist_ok=True)

    # A --case NAME THAT MATCHES NOTHING IS AN ERROR.
    #
    # Silently selecting zero jobs for a misspelled name would print
    # "running 0 job(s)" and exit 0 -- a rerun that did not happen, reported as
    # a success. The whole inventory is checked, not the post-filter selection,
    # so a case that exists but is excluded by --family/--mode says so.
    if a.case:
        known = {j["case"] for j in inv["jobs"]}
        unknown = [c for c in a.case if c not in known]
        if unknown:
            print(f"no such case in {a.inventory}: {', '.join(sorted(unknown))}",
                  file=sys.stderr)
            return 2
    case_ok = (lambda j: True) if not a.case else (lambda j: j["case"] in a.case)
    if a.command == "prove":
        # Typecheck jobs that are already EXPORTED. Their exports are not rerun:
        # a valid artifact with recorded provenance does not need regenerating
        # to acquire a proof.
        done = load_results(inv)
        sel = [j for j in inv["jobs"]
               if done.get(job_key(j), {}).get("export_state") == "EXPORTED"
               and done[job_key(j)].get("proof_state") == "NOT_RUN"
               and (a.family is None or j["family"] == a.family)
               and (a.mode is None or j["mode"] == a.mode)
               and case_ok(j)]
        if a.limit:
            sel = sel[: a.limit]
        print(f"proving {len(sel)} exported job(s)", flush=True)
        for i, j in enumerate(sel, 1):
            rec = done[job_key(j)]
            art = Path(rec["artifact"])
            out = art.parent.parent
            if not art.exists():
                rec.update(proof_state="FAILED", detail="artifact missing at prove time")
            elif sha(art) != rec.get("artifact_sha256"):
                # A STALE artifact: present, but not the one that was exported.
                rec.update(proof_state="FAILED",
                           detail=f"artifact hash changed since export "
                                  f"({rec.get('artifact_sha256')} -> {sha(art)})")
            else:
                try:
                    rec.update(typecheck(art, j["top"], out, j["mode"]))
                except subprocess.TimeoutExpired:
                    rec.update(proof_state="FAILED", detail="typecheck timeout")
                except Exception as e:
                    rec.update(proof_state="FAILED", detail=f"{type(e).__name__}: {e}")
            result_path(j).write_text(json.dumps(rec, indent=1) + "\n")
            print(f"  [{i}/{len(sel)}] {j['family']}/{j['mode']}/{j['case']}: "
                  f"{rec['proof_state']} wall={rec.get('wall_clock', '-')} "
                  f"rss={rec.get('peak_rss_kib', '-')}", flush=True)
        return exit_status(sel)

    done = load_results(inv)
    # --redo REPLACES a recorded result; without it a recorded job is skipped.
    # Stated plainly because the skip is what keeps a rerun cheap, and because a
    # record that was wrong (a gate that refused for the wrong reason, say) can
    # only be corrected by asking for it.
    sel = [j for j in inv["jobs"]
           if (a.redo or job_key(j) not in done)
           and (a.family is None or j["family"] == a.family)
           and (a.mode is None or j["mode"] == a.mode)
           and case_ok(j)]
    if a.redo and not a.case:
        print("--redo without --case would replace every selected record; "
              "name the cases to redo", file=sys.stderr)
        return 2
    if a.limit:
        sel = sel[: a.limit]
    print(f"running {len(sel)} job(s) with {a.lhd} (sha {sha(a.lhd)})", flush=True)
    for i, j in enumerate(sel, 1):
        try:
            rec = run_one(j, a.lhd, a.export_only)
        except subprocess.TimeoutExpired:
            rec = {"family": j["family"], "case": j["case"], "top": j["top"],
                   "mode": j["mode"], "export_state": "FAILED",
                   "proof_state": "NOT_RUN", "detail": "timeout"}
        except Exception as e:
            rec = {"family": j["family"], "case": j["case"], "top": j["top"],
                   "mode": j["mode"], "export_state": "FAILED",
                   "proof_state": "NOT_RUN", "detail": f"{type(e).__name__}: {e}"}
        # One immutable file per job: no shared writer to race.
        result_path(j).write_text(json.dumps(rec, indent=1) + "\n")
        print(f"  [{i}/{len(sel)}] {j['family']}/{j['mode']}/{j['case']}: "
              f"export={rec['export_state']} proof={rec.get('proof_state')} "
              f"({rec.get('export_wall_s', '-')}s)", flush=True)
    # A run with failed or unresolved jobs must not exit 0: a caller that only
    # checks the status code would otherwise treat it as a clean sweep.
    return exit_status(sel)


if __name__ == "__main__":
    sys.exit(main())
