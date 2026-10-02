#!/usr/bin/env python3
"""Tests for the `proof` gate -- the LAST gate, and the one that must never
retract an earlier one.

Two layers, because the hazards live in different places:

  * `extract_proof_gate` is a pure function and is tested as one.  The property
    that matters most is negative: on every failure path the executable gates
    (`cert`..`agree`) come out byte-identical to what they were.  The reason the
    proof runs in its own process at all is that a failed proof in the sim
    probe's process puts `error:` in that log and a nonzero exit on that run,
    both of which `extract_gates` reads as `clean_exit = False` -- turning a
    design that genuinely passed into a `typecheck` failure.
  * `audit_axioms` / `d3_proof_gate` are Lean commands, so they are tested by
    running Lean.  A gate that cannot fail is not a gate: `sorryAx` and
    `native_decide`'s `ofReduceBool` must both be REFUSED, and the marker must
    not be printed when they are.

The Lean cases import Mathlib (~6.9 GB each) and run serially, one at a time.

Run: python3 pass/lean/scripts/d3_proof_test.py
"""

import importlib.util
import os
import pathlib
import re
import subprocess
import sys
import tempfile
import time

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[2]
SWEEP = HERE / "d3_sweep.py"
LEAN_DIR = ROOT / "formal/lean"
CERT = pathlib.Path("/soe/czeng14/projects/livehd-new/generated/vc_sweep2/lean/"
                    "tima_adder_Lgraph.lean")

spec = importlib.util.spec_from_file_location("d3_sweep", SWEEP)
sweep = importlib.util.module_from_spec(spec)
spec.loader.exec_module(sweep)

GOOD = "D3GATE proof=1 thm=d3_fast.correct axioms=[propext, Classical.choice, Quot.sound]"

# Every executable gate passing: the state a proof failure must leave alone.
def agree_row():
    r = {g: 1 for g in sweep.GATES if g != "proof"}
    r["proof"] = "na"
    r["detail"] = ""
    return r


def main() -> int:
    fails = []
    tmp = pathlib.Path(tempfile.mkdtemp(prefix="d3_proof_test_", dir=str(ROOT / "temp")))
    t0 = time.time()

    def check(name, cond, note, detail=""):
        if cond:
            print(f"ok   {name:<34} {note}")
        else:
            print(f"FAIL {name:<34} {note}")
            if detail:
                print(f"     {detail[:500]}")
            fails.append(name)

    EXEC_GATES = [g for g in sweep.GATES if g != "proof"]

    def intact(r, label):
        """The executable gates survived whatever the proof did."""
        check(f"prior_gates_intact_{label}",
              all(r[g] == 1 for g in EXEC_GATES),
              f"cert..agree all still 1 after {label}",
              str({g: r[g] for g in EXEC_GATES}))

    try:
        # ---- 1. not attempted stays `na`, which is not a failure --------------
        r = agree_row(); sweep.extract_proof_gate(r, "", None, expect_module="tima_adder")
        check("not_attempted_is_na", r["proof"] == "na",
              "no proof probe ran, so proof is `na` -- an unattempted gate is "
              f"neither passed nor failed (got {r['proof']!r})")
        intact(r, "no_attempt")

        # A design that never reached `agree` has nothing worth proving about,
        # so the gate is `na` rather than 0 even though a probe could have run.
        r2 = agree_row(); r2["agree"] = 0
        sweep.extract_proof_gate(r2, GOOD, 0, expect_module="tima_adder")
        check("no_agree_is_na", r2["proof"] == "na",
              f"a design that did not reach agree gets `na` (got {r2['proof']!r})")

        # ---- 2. the success path ---------------------------------------------
        r = agree_row(); sweep.extract_proof_gate(r, GOOD, 0, expect_module="tima_adder")
        check("success_credits_proof", r["proof"] == "1",
              f"marker present once and rc 0 credits proof=1 (got {r['proof']!r})")
        intact(r, "success")

        # ---- 3. every failure path, and NONE of them touches an earlier gate --
        cases = [
            ("missing_marker", "", 0),
            ("nonzero_exit", GOOD, 1),
            ("nonzero_exit_with_marker", GOOD + "\nerror: something later broke", 1),
            ("duplicate_marker", GOOD + "\n" + GOOD, 0),
            ("wrong_theorem", "D3GATE proof=1 thm=other.thm axioms=[propext]", 0),
        ]
        for label, out, rc in cases:
            r = agree_row()
            sweep.extract_proof_gate(r, out, rc, expect_module="tima_adder")
            check(f"failure_{label}", r["proof"] == "0",
                  f"{label} gives proof=0 (got {r['proof']!r})")
            intact(r, label)

        # The duplicate case deserves its own statement: two markers means the
        # log describes more than one proof, and crediting the first would be
        # choosing which one to believe.
        r = agree_row(); sweep.extract_proof_gate(r, GOOD + "\n" + GOOD, 0)
        check("duplicate_marker_not_credited", r["proof"] == "0"
              and "2 proof markers" in r["detail"],
              "a duplicated marker is refused and says why", r["detail"])

        # ---- 4. Lean: the audit must REFUSE sorryAx and ofReduceBool ----------
        if not CERT.is_file():
            check("lean_cases", False, f"certificate not found at {CERT}")
            return 1 if fails else 0

        env = dict(os.environ)
        env["PATH"] = "/mada/users/czeng14/.elan/bin:" + env.get("PATH", "")
        env["TMPDIR"] = str(tmp)

        def run_lean(name, text, timeout=900):
            f = tmp / f"{name}.lean"
            f.write_text(text, encoding="utf-8")
            p = subprocess.run(["lake", "env", "lean", str(f)], cwd=str(LEAN_DIR),
                               env=env, capture_output=True, text=True, timeout=timeout)
            return p

        head = "import LeanSemanticPrimitives.Compiler.ReifyProof\nopen Compiler\n"
        p = run_lean("sorry_case", head
                     + "theorem bogus : (1:Nat) = 1 := by sorry\n"
                     + "d3_proof_gate bogus\n")
        out = p.stdout + p.stderr
        check("lean_refuses_sorryAx",
              p.returncode != 0 and "DISALLOWED" in out and "sorryAx" in out
              and "D3GATE proof=1" not in out,
              "a sorry-backed theorem is refused AND prints no marker", out[-300:])

        p = run_lean("native_case", head
                     + "theorem bogus2 : (List.range 5).length = 5 := by native_decide\n"
                     + "d3_proof_gate bogus2\n")
        out = p.stdout + p.stderr
        check("lean_refuses_ofReduceBool",
              p.returncode != 0 and "DISALLOWED" in out
              and "D3GATE proof=1" not in out,
              "native_decide's axiom is refused AND prints no marker", out[-300:])

        # ---- 5. Lean: the real design, end to end ----------------------------
        p = run_lean("tima_case", sweep.make_proof_probe(CERT, "tima_adder"))
        out = p.stdout + p.stderr
        hits = re.findall(sweep.PROOF_GATE_RE, out, re.M)
        check("lean_proves_tima_adder",
              p.returncode == 0 and len(hits) == 1,
              f"the generated proof probe for tima_adder exits 0 with exactly one "
              f"marker ({len(hits)})", out[-400:])
        if hits:
            check("lean_axioms_are_the_allowed_three",
                  set(re.findall(r"\w+(?:\.\w+)*", hits[0][1]))
                  <= {"propext", "Classical", "choice", "Classical.choice",
                      "Quot", "sound", "Quot.sound"},
                  f"and depends on nothing outside the allowed set: {hits[0][1]}")
            # The row the runner would write from that log.
            r = agree_row()
            sweep.extract_proof_gate(r, out, p.returncode, expect_module="tima_adder")
            check("runner_credits_the_real_proof", r["proof"] == "1",
                  "and the runner's parser credits proof=1 from it", str(r)[:200])
    finally:
        pass

    print()
    if fails:
        print(f"PROOF-TEST FAILED: {fails}  [{time.time() - t0:.1f}s]")
        return 1
    print(f"PROOF-TEST OK  [{time.time() - t0:.1f}s]")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
