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
import hashlib
import json
import os
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
REPLAY = ROOT / "generated" / "master_port" / "replay"
RESULTS = REPLAY / "results"
JOBS = REPLAY / "jobs"
FORMAL = ROOT / "formal" / "lean"

EXPORT_STATES = ["EXPORTED", "REFUSED", "FAILED", "SKIPPED"]
PROOF_STATES = ["PROVEN", "FAILED", "NOT_RUN"]


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
    if mode == "legacy":  # legacy fast model
        return {"LEAN_MODE": "legacy", "LEAN_EMIT_FAST_BRIDGE": "true",
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


def typecheck(artifact: Path, top: str, out: Path) -> dict:
    """Serial audited queue + exact-obligation axiom audit. /usr/bin/time -v."""
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
    with log.open("w") as fh:
        rc = subprocess.run(cmd, cwd=str(FORMAL), env=env, stdout=fh,
                            stderr=subprocess.STDOUT, timeout=7200).returncode
    text = log.read_text(errors="replace")
    timing = tv.read_text(errors="replace") if tv.exists() else ""
    wall = next((l.split(": ")[-1].strip() for l in timing.splitlines()
                 if "Elapsed (wall clock)" in l), None)
    rss = next((l.split(": ")[-1].strip() for l in timing.splitlines()
                if "Maximum resident set size" in l), None)
    audit = subprocess.run(
        ["python3", "-I", str(ROOT / "pass" / "lean" / "scripts" / "axiom_audit.py"),
         str(log), top, "--artifact", str(artifact)],
        capture_output=True, text=True)
    errs = text.count("error:")
    ok = rc == 0 and errs == 0 and audit.returncode == 0
    return {"proof_state": "PROVEN" if ok else "FAILED",
            "lean_exit": rc, "lean_errors": errs,
            "axiom_audit": "ok" if audit.returncode == 0 else "FAIL",
            "axiom_detail": (audit.stdout + audit.stderr).strip().splitlines()[:6],
            "wall_clock": wall, "peak_rss_kib": rss,
            "typecheck_log_sha256": sha(log)}


def run_one(j: dict, lhd: Path, export_only: bool) -> dict:
    top, mode, fam, case = j["top"], j["mode"], j["family"], j["case"]
    out = JOBS / fam / mode / case          # CASE, not top: the manifest has
    out.mkdir(parents=True, exist_ok=True)  # two cases sharing one top.
    log = out / "job.log"
    rec = {"family": fam, "case": case, "top": top, "mode": mode,
           "lhd_sha256": sha(lhd), "requested_scope": mode_env(mode),
           "export_state": "FAILED", "proof_state": "NOT_RUN"}

    env = dict(os.environ)
    env.update(mode_env(mode))
    env["COREET_ROOT"] = env.get("COREET_ROOT", "/mada/users/czeng14/projects/core-et")
    env["LHD"] = str(lhd)
    env["OUT"] = str(out)
    env["RUN_LEAN"] = "false"        # the audited queue does the typecheck
    env["RUN_LEC_GATE"] = "false"
    if fam == "CORE-ET":
        env["COREET_TOP"] = top
        cmd = ["bash", str(ROOT / "scripts" / "run_coreet_module_lean.sh")]
    else:
        # A missing adapter is UNRESOLVED, not an acceptance disposition.
        rec["export_state"] = "SKIPPED"
        rec["detail"] = f"{fam} adapter not wired -- unresolved prerequisite"
        return rec

    t0 = time.time()
    with log.open("w") as fh:
        rc = subprocess.run(cmd, env=env, stdout=fh, stderr=subprocess.STDOUT,
                            cwd=str(ROOT), timeout=3600).returncode
    rec["export_wall_s"] = round(time.time() - t0, 2)
    rec["export_log_sha256"] = sha(log)
    text = log.read_text(errors="replace")
    artifact = next(iter(out.rglob(f"{top}_Lgraph.lean")), None)

    if rc == 0 and artifact is not None:
        rec["export_state"] = "EXPORTED"
        rec["artifact"] = str(artifact.relative_to(ROOT))
        rec["artifact_sha256"] = sha(artifact)
        sidecar = artifact.with_name(f"{top}_io.json")
        if sidecar.exists():
            rec["sidecar_sha256"] = sha(sidecar)
    elif "pass.single_edge refused" in text or "combinational loop" in text:
        rec["export_state"] = "REFUSED"
        rec["detail"] = "upstream refusal (single_edge / combinational loop)"
    elif "static gate" in text.lower() and "FAIL:" in text:
        rec["export_state"] = "REFUSED"
        rec["detail"] = "static gate"
    else:
        rec["detail"] = f"export exit {rc}"

    if rec["export_state"] == "EXPORTED" and not export_only:
        rec.update(typecheck(Path(rec["artifact"]), top, out))
    return rec


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("command", choices=["status", "run", "prove"])
    ap.add_argument("inventory", type=Path)
    ap.add_argument("--family")
    ap.add_argument("--mode")
    ap.add_argument("--limit", type=int)
    ap.add_argument("--export-only", action="store_true")
    ap.add_argument("--lhd", type=Path, default=ROOT / "bazel-bin" / "lhd" / "lhd")
    a = ap.parse_args()
    inv = json.loads(a.inventory.read_text())
    if a.command == "status":
        return status(inv)

    RESULTS.mkdir(parents=True, exist_ok=True)
    if a.command == "prove":
        # Typecheck jobs that are already EXPORTED. Their exports are not rerun:
        # a valid artifact with recorded provenance does not need regenerating
        # to acquire a proof.
        done = load_results(inv)
        sel = [j for j in inv["jobs"]
               if done.get(job_key(j), {}).get("export_state") == "EXPORTED"
               and done[job_key(j)].get("proof_state") == "NOT_RUN"
               and (a.family is None or j["family"] == a.family)
               and (a.mode is None or j["mode"] == a.mode)]
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
                    rec.update(typecheck(art, j["top"], out))
                except subprocess.TimeoutExpired:
                    rec.update(proof_state="FAILED", detail="typecheck timeout")
                except Exception as e:
                    rec.update(proof_state="FAILED", detail=f"{type(e).__name__}: {e}")
            result_path(j).write_text(json.dumps(rec, indent=1) + "\n")
            print(f"  [{i}/{len(sel)}] {j['family']}/{j['mode']}/{j['case']}: "
                  f"{rec['proof_state']} wall={rec.get('wall_clock', '-')} "
                  f"rss={rec.get('peak_rss_kib', '-')}", flush=True)
        return 0

    done = load_results(inv)
    sel = [j for j in inv["jobs"]
           if job_key(j) not in done
           and (a.family is None or j["family"] == a.family)
           and (a.mode is None or j["mode"] == a.mode)]
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
    return 0


if __name__ == "__main__":
    sys.exit(main())
