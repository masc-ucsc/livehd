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
    a = ap.parse_args()
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
        cmd = ["python3", "-I", str(ROOT / "pass" / "lean" / "scripts" / "axiom_audit.py"),
               str(log), top, "--artifact", str(art)]
        if mode == "verified_compiler":
            cmd += ["--required", ",".join(required)]
        elif required:
            cmd += ["--min-required", ",".join(required), "--witnesses", "auto"]
        r = subprocess.run(cmd, capture_output=True, text=True)
        before = rec["proof_state"]
        rec["proof_state"] = success if r.returncode == 0 else "FAILED"
        rec["axiom_audit"] = "ok" if r.returncode == 0 else "FAIL"
        rec["axiom_detail"] = (r.stdout + r.stderr).strip().splitlines()[:6]
        rec["reaudit"] = "re-audited against the current obligation rule"
        # Record what was actually demanded this time.
        src = art.read_text(errors="replace")
        eff = list(required)
        if mode != "verified_compiler" and f"structure {top}_state" in src:
            eff += ["_next_refines_fast", "_step_refines_fast"]
        rec["requested_obligations"] = [top + o for o in eff]
        rp.write_text(json.dumps(rec, indent=1) + "\n")
        if rec["proof_state"] != before:
            changed += 1
            print(f"  {j['family']}/{j['mode']}/{j['case']}: {before} -> {rec['proof_state']}")
    print(f"re-audited {rechecked} retained result(s); {changed} verdict(s) changed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
