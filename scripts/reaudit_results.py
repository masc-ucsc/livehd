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
import re
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
        before = rec.get("proof_state")
        # FAILED records are re-audited too: a verdict recorded under a rule
        # that has since been CORRECTED (not merely strengthened) may have been
        # wrong in either direction. Leaving them out would make the fix
        # one-way.
        if rec.get("proof_state") == "NOT_RUN":
            continue
        art = Path(rec.get("artifact", ""))
        # RECOVER out_dir WHERE IT IS JUSTIFIED, do not invent it.
        #
        # The records migrated from the pre-rewrite run predate `out_dir`, so a
        # re-audit keyed on it skipped their retained logs entirely -- the files
        # are there, beside artifact.parent.parent. Recovering that one mapping
        # is legitimate: it is the layout that run produced, and the log must
        # still exist and belong to this artifact. Anything not found this way
        # stays unresolved rather than being guessed at.
        log = None
        if rec.get("out_dir"):
            log = Path(rec["out_dir"]) / "typecheck.log"
        elif art.parts:
            cand = art.parent.parent / "typecheck.log"
            if cand.exists():
                log = cand
                rec["out_dir"] = str(art.parent.parent)
                rec["out_dir_recovered"] = ("derived from artifact.parent.parent; this record "
                                            "predates out_dir and the log was found there")
        if log is None or not log.exists() or not art.exists():
            # UNAVAILABLE EVIDENCE IS NOT ACCEPTED EVIDENCE.
            #
            # Leaving the recorded verdict standing meant a PROVEN record whose
            # artifact or log had since vanished stayed PROVEN, and the run
            # exited 0. A verdict that cannot be re-checked is UNVERIFIABLE --
            # distinct both from a proof and from a failure, and it must not be
            # counted as the former.
            rec["proof_state"] = "UNVERIFIABLE"
            rec["reaudit"] = ("evidence missing: "
                              + ("no retained log" if log is None or not log.exists()
                                 else "artifact absent"))
            rp.write_text(json.dumps(rec, indent=1) + "\n")
            if before != "UNVERIFIABLE":
                changed += 1
                print(f"  {j['family']}/{j['mode']}/{j['case']}: {before} -> UNVERIFIABLE "
                      f"({rec['reaudit']})")
            continue
        rechecked += 1
        top, mode = j["top"], j["mode"]
        required, success = orch.MODE_OBLIGATIONS[mode]

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
        # EVERY REQUIRED FIELD MUST BE PRESENT AND GOOD. The first version wrote
        # `not in (0, None)`, which treats a null exit as acceptable, and tested
        # the hashes only `if rec.get(...)` -- so deleting a field removed its
        # check. Absent evidence is not clean evidence, and a re-audit must not
        # mint the missing value from the file as it stands now: that would
        # record what is true after the edit, not what the run checked.
        # PRESENT IS NOT THE SAME AS VALID. The first version rejected only
        # `None`, so an empty string or a blank digest satisfied the check.
        for field in ("artifact_sha256", "typecheck_log_sha256"):
            v = rec.get(field)
            if v is None:
                blockers.append(f"no recorded {field} -- cannot confirm what the run checked")
            elif not isinstance(v, str) or not re.fullmatch(r"[0-9a-f]{16}", v):
                blockers.append(f"recorded {field} is {v!r}, not a digest")
        if rec.get("lean_exit") is None:
            blockers.append("no recorded lean_exit -- cannot confirm the run succeeded")
        if rec.get("lean_exit") is not None and rec["lean_exit"] != 0:
            blockers.append(f"recorded lean_exit {rec['lean_exit']}")
        if rec.get("lean_errors"):
            blockers.append(f"{rec['lean_errors']} recorded Lean error(s)")
        if "timeout" in str(rec.get("detail", "")).lower():
            blockers.append("the recorded run timed out")
        # The timing file is part of the promised evidence, and its own exit
        # status matters: 137 is a kill, which /usr/bin/time records while the
        # Lean log may look truncated-but-clean.
        tfile = Path(rec["out_dir"]) / "typecheck.time" if rec.get("out_dir") else None
        if tfile is None or not tfile.exists():
            blockers.append("no retained timing file")
        else:
            ttext = tfile.read_text(errors="replace")
            if not ttext.strip():
                blockers.append("the retained timing file is empty")
            # A SUCCESSFUL TERMINAL RESULT MUST BE PRESENT, not merely
            # unobjectionable. Scanning for a bad line accepts a file whose exit
            # line has been REMOVED -- absence of evidence read as evidence.
            exits = [l for l in ttext.splitlines() if "Exit status" in l]
            if not exits:
                blockers.append("the timing file records no exit status")
            elif any(l.split(":")[-1].strip() != "0" for l in exits):
                blockers.append(f"timing records {exits[0].strip()}")
            if any("Command terminated by signal" in l for l in ttext.splitlines()):
                blockers.append("timing records a termination by signal")
            # IDENTITY: the timing must describe THIS artifact's run. A
            # substituted file was accepted while naming a different command.
            cmdline = next((l for l in ttext.splitlines() if "Command being timed" in l), "")
            if cmdline and art.name not in cmdline and str(art) not in cmdline:
                blockers.append(f"timing names a different command: {cmdline.strip()[:90]}")
            # ...and the reported wall/RSS must be the ones recorded.
            for key, label in (("wall_clock", "Elapsed (wall clock)"),
                               ("peak_rss_kib", "Maximum resident set size")):
                if rec.get(key):
                    line = next((l for l in ttext.splitlines() if label in l), "")
                    if line and str(rec[key]) not in line:
                        blockers.append(f"recorded {key}={rec[key]} is not in the retained timing")
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
        elif mode == "legacy":
            # Legacy FAST is TYPECHECKED under its declared scope: an EXPLICITLY
            # empty obligation set, the same thing the orchestrator requests.
            # Auto derivation here would demand comb (and next/step when the
            # model has state) of an export that was never asked to emit them.
            cmd += ["--required", ""]
        elif required:
            cmd += ["--min-required", ",".join(required), "--witnesses", "auto"]
        r = subprocess.run(cmd, capture_output=True, text=True)
        rec["proof_state"] = success if r.returncode == 0 else "FAILED"
        rec["axiom_audit"] = "ok" if r.returncode == 0 else "FAIL"
        rec["axiom_detail"] = (r.stdout + r.stderr).strip().splitlines()[:6]
        rec["reaudit"] = "re-audited against the current obligation rule"
        src = art.read_text(errors="replace")
        eff = list(required)
        # Legacy FAST is excluded, matching the typecheck rule: it demands no
        # refinement obligation, so recording next/step as "requested" would
        # claim a scope the run never had. (A fast-only re-audit said
        # TYPECHECKED while listing next and step it had neither demanded nor
        # found.)
        if mode not in ("verified_compiler", "legacy") and f"structure {top}_state" in src:
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
              and json.loads(orch.result_path(j).read_text()).get("proof_state")
                  in ("FAILED", "UNVERIFIABLE"))
    if bad:
        print(f"{bad} record(s) are FAILED or UNVERIFIABLE", file=sys.stderr)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
