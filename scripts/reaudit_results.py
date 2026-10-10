#!/usr/bin/env python3
"""Re-audit retained typecheck logs against the CURRENT obligation rule.

When the obligation set is strengthened, results recorded under the weaker rule
do not have to be re-proved: the Lean run already happened and its full log was
kept. What has to happen is the AUDIT, against the stronger set.

This re-runs `axiom_audit.py` over each retained log and rewrites the result's
proof verdict. A record that no longer passes becomes FAILED -- it is not left
at its old verdict with a footnote.

Usage: reaudit_results.py <inventory.json> [--family F] [--mode M]
"""

import argparse
import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))


def main() -> int:
    import importlib.util
    spec = importlib.util.spec_from_file_location("orch", ROOT / "scripts" / "replay_orchestrator.py")
    orch = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(orch)

    ap = argparse.ArgumentParser()
    ap.add_argument("inventory", type=Path)
    ap.add_argument("--family")
    ap.add_argument("--mode")
    ap.add_argument("--results", type=Path,
                    help="results directory (default: the orchestrator's). Lets a "
                         "control drive this CLI against its own fixtures instead "
                         "of testing a reimplementation of it.")
    a = ap.parse_args()
    if a.results:
        orch.RESULTS = a.results
    inv = json.loads(a.inventory.read_text())
    changed = rechecked = 0
    for j in inv["jobs"]:
        if a.family and j["family"] != a.family:
            continue
        if a.mode and j["mode"] != a.mode:
            continue
        rp = orch.result_path(j)
        if not rp.exists():
            continue
        rec = json.loads(rp.read_text())
        # FAILED records are re-audited too: a verdict recorded under a rule
        # that has since been CORRECTED (not merely strengthened) may have been
        # wrong in either direction. Leaving them out would make the fix
        # one-way.
        if rec.get("proof_state") == "NOT_RUN":
            continue
        art = Path(rec.get("artifact", ""))
        log = Path(rec.get("out_dir", "")) / "typecheck.log" if rec.get("out_dir") else None
        if log is None or not log.exists() or not art.exists():
            # Without the retained log there is nothing to re-audit against.
            rec["reaudit"] = "log or artifact missing; verdict left as recorded"
            rp.write_text(json.dumps(rec, indent=1) + "\n")
            continue
        rechecked += 1
        top, mode = j["top"], j["mode"]
        required, success = orch.MODE_OBLIGATIONS[mode]
        before = rec["proof_state"]

        # A RE-AUDIT MAY NOT PROMOTE A RUN THAT DID NOT SUCCEED.
        #
        # The first version set the verdict purely from axiom_audit.py's exit
        # code, ignoring the recorded Lean execution entirely. Measured: a
        # record whose artifact ends `example : False := by rfl` -- Lean exit 1,
        # correctly FAILED when it ran -- was promoted to PROVEN, because its
        # three bridge theorems were printed before the error and the axiom
        # report alone looked clean. An axiom audit describes what a proof
        # DEPENDS on; it does not establish that the file compiled.
        blockers = []
        if rec.get("lean_exit") not in (0, None):
            blockers.append(f"recorded lean_exit {rec['lean_exit']}")
        if rec.get("lean_errors"):
            blockers.append(f"{rec['lean_errors']} recorded Lean error(s)")
        if "timeout" in str(rec.get("detail", "")).lower():
            blockers.append("the recorded run timed out")
        # The log is the evidence; read it rather than trusting the summary.
        log_text = log.read_text(errors="replace")
        n_err = log_text.count("error:")
        if n_err:
            blockers.append(f"{n_err} `error:` line(s) in the retained log")
        if "sorryAx" in log_text:
            blockers.append("`sorryAx` in the retained log")
        # PROVENANCE: the evidence must still be the evidence that was recorded.
        if rec.get("artifact_sha256") and orch.sha(art) != rec["artifact_sha256"]:
            blockers.append(f"artifact hash changed since the run "
                            f"({rec['artifact_sha256']} -> {orch.sha(art)})")
        if rec.get("typecheck_log_sha256") and orch.sha(log) != rec["typecheck_log_sha256"]:
            blockers.append(f"typecheck log hash changed since the run")
        if rec.get("lean_exit") is None and "lean_exit" not in rec:
            blockers.append("no recorded Lean exit status -- cannot confirm the run succeeded")

        if blockers:
            rec["proof_state"] = "FAILED"
            rec["reaudit"] = "refused promotion: " + "; ".join(blockers)
            rp.write_text(json.dumps(rec, indent=1) + "\n")
            if rec["proof_state"] != before:
                changed += 1
                print(f"  {j['family']}/{j['mode']}/{j['case']}: {before} -> FAILED "
                      f"({blockers[0]})")
            continue

        cmd = ["python3", "-I", str(ROOT / "pass" / "lean" / "scripts" / "axiom_audit.py"),
               str(log), top, "--artifact", str(art)]
        if mode == "verified_compiler":
            cmd += ["--required", ",".join(required)]
        elif required:
            cmd += ["--min-required", ",".join(required), "--witnesses", "auto"]
        elif mode == "legacy":
            # Legacy FAST is TYPECHECKED under its declared scope. Demanding
            # bridge obligations it was never asked to emit would fail a correct
            # export, so auto scope applies and nothing is added.
            cmd += ["--witnesses", "auto"]
        r = subprocess.run(cmd, capture_output=True, text=True)
        rec["proof_state"] = success if r.returncode == 0 else "FAILED"
        rec["axiom_audit"] = "ok" if r.returncode == 0 else "FAIL"
        rec["axiom_detail"] = (r.stdout + r.stderr).strip().splitlines()[:6]
        rec["reaudit"] = "re-audited against the current obligation rule"
        src = art.read_text(errors="replace")
        eff = list(required)
        if mode != "verified_compiler" and f"structure {top}_state" in src:
            eff += ["_next_refines_fast", "_step_refines_fast"]
        if f"theorem {top}_graphCert_wf" in src and "_graphCert_wf" not in eff:
            eff.append("_graphCert_wf")
        rec["requested_obligations"] = [top + o for o in eff]
        rp.write_text(json.dumps(rec, indent=1) + "\n")
        if rec["proof_state"] != before:
            changed += 1
            print(f"  {j['family']}/{j['mode']}/{j['case']}: {before} -> {rec['proof_state']}")
    print(f"re-audited {rechecked} retained result(s); {changed} verdict(s) changed")
    bad = sum(1 for j in inv["jobs"]
              if (a.family is None or j["family"] == a.family)
              and (a.mode is None or j["mode"] == a.mode)
              and orch.result_path(j).exists()
              and json.loads(orch.result_path(j).read_text()).get("proof_state") == "FAILED")
    if bad:
        print(f"{bad} record(s) remain FAILED", file=sys.stderr)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
