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

    # ---- segmented-proof probe text -------------------------------------
    # Pure text, so it is checked here rather than by running Lean.  The hazard
    # is placement: the option must reach the command it affects and NOTHING
    # else.  If it leaked into the sim probe, the executable stage would compile
    # a different artifact than the one it is credited for.
    _c = tmp / "segtext_Lgraph.lean"
    _c.write_text("import LeanSemanticPrimitives.Compiler.CompileDesign\n"
                  "open Compiler\n"
                  "def segtext_designCert : DesignCert := default\n", encoding="utf-8")
    pr16 = sweep.make_proof_probe(_c, "segtext", reifier="named", segment=16)
    pr0  = sweep.make_proof_probe(_c, "segtext", reifier="named", segment=0)
    sim  = sweep.make_probe(_c, "segtext", 32, reifier="named",
                            phase_file=str(tmp / "segtext.phase"))
    check("seg_option_in_proof_probe", "set_option d3.segment 16" in pr16,
          "the proof probe carries `set_option d3.segment 16`")
    check("seg_option_before_the_command",
          "set_option d3.segment 16" in pr16
          and pr16.index("set_option d3.segment 16") < pr16.index("prove_reified_incr"),
          "and it precedes `prove_reified_incr`, which is what reads it")
    check("seg_option_after_cert_body",
          "set_option d3.segment 16" in pr16
          and pr16.index("set_option d3.segment 16") > pr16.index("segtext_designCert"),
          "and follows the imports and the certificate body")
    check("seg_zero_emits_nothing", "d3.segment" not in pr0,
          "size 0 emits no option at all, so the default probe is unchanged")
    check("seg_option_never_in_sim_probe", "d3.segment" not in sim,
          "the SIM probe never carries it -- segmentation is how the walk is PROVED")
    check("seg_gate_still_last",
          pr16.rstrip().endswith("d3_proof_gate d3_fast.correct"),
          "and the gate is still the last command in the proof probe")
    _legacy_refused = False
    try:
        sweep.make_proof_probe(_c, "segtext", reifier="legacy", segment=16)
    except ValueError:
        _legacy_refused = True
    check("seg_refused_for_legacy_reifier", _legacy_refused,
          "a legacy probe with a segment size raises rather than dropping it")

    # The option only affects the PROOF stage, so the CLI must refuse every way
    # of asking for it without one, rather than accepting it and doing nothing.
    for _label, _extra in (("no_prove", ["--reifier", "named"]),
                           ("legacy_reifier", ["--prove"]),
                           ("negative", ["--reifier", "named", "--prove"])):
        _size = "-1" if _label == "negative" else "16"
        _p = subprocess.run(
            [sys.executable, str(SWEEP), "--certs", str(tmp), "--out",
             str(tmp / f"refuse_{_label}.tsv"), "--only", "^nothing$",
             "--proof-segment-size", _size] + _extra,
            capture_output=True, text=True)
        check(f"seg_cli_refuses_{_label}",
              _p.returncode == 2 and not (tmp / f"refuse_{_label}.tsv").exists(),
              f"--proof-segment-size {_size} with {' '.join(_extra)} exits 2 and writes nothing",
              f"rc={_p.returncode} err={_p.stderr.strip()[:200]}")

    class _A:
        manifest = ""
        samples = 32
        timeout = 900
        native = False
        tier = ""
        proof_segment_size = 16
    check("seg_in_resume_key",
          sweep.run_config(_A(), "d")["proof_segment_size"] == 16
          and sweep.run_config(_A(), "d") != (lambda a: (setattr(a, "proof_segment_size", 0),
                                                         sweep.run_config(a, "d"))[1])(_A()),
          "and a segmented run cannot resume into or merge with a monolithic one")

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
            # A PREFIX of the right name. The earlier check was `startswith`,
            # so this was credited as a pass for a theorem nobody asked for.
            ("prefix_theorem", "D3GATE proof=1 thm=d3_fast.wrong axioms=[propext]", 0),
            ("proof_timeout", "", 124),
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

        r = agree_row()
        sweep.extract_proof_gate(r, "", 124, expect_module="tima_adder")
        check("timeout_names_the_stage", r["proof"] == "0"
              and "proof stage timed out" in r["detail"]
              and r.get("run_status") is None,
              "a proof-stage timeout is proof=0 and does NOT set run_status: a "
              "nonterminal row would make d3_join drop the executable result "
              "from the denominator over an optional gate", r["detail"])

        r = agree_row()
        sweep.extract_proof_gate(r, GOOD, 0, expect_module="tima_adder", oom=True)
        check("proof_oom_keeps_executable_gates", r["proof"] == "0"
              and "memory.max" in r["detail"],
              "a proof-stage OOM is proof=0 with the cause named", r["detail"])
        intact(r, "proof_oom")

        # ---- 3a. a sampled kill that lands on the PROOF stage -----------------
        # The gap: the sampled hard-kill branch is gated on the SIM stage's rc,
        # so a kill during the proof subprocess left rc == 0 and only proof_rc
        # nonzero -- the probe was physically SIGKILLed by the budget and the row
        # said `proof=0`, as if the proof had merely not worked out.
        r = agree_row()
        sweep.extract_proof_gate(r, "", -9, expect_module="tima_adder",
                                 sampled_kill_kb=8800000)
        check("proof_sampled_kill_is_a_resource_outcome",
              r["proof"] == "0" and "sampled aggregate hard limit" in r["detail"]
              and "8800000" in r["detail"] and "LOWER BOUND" in r["detail"],
              "a sampled kill on the proof stage says so, quotes the killed_at "
              "figure, and marks it a lower bound", r["detail"])
        check("proof_sampled_kill_says_nothing_was_decided",
              "never decided" in r["detail"],
              "and states the theorem was never decided -- a resource outcome is "
              "not evidence about the theorem")
        intact(r, "proof_sampled_kill")

        # Kernel precedence: when both could be read as applying, the exact
        # verdict wins and the sampled one is not reported.
        r = agree_row()
        sweep.extract_proof_gate(r, "", -9, expect_module="tima_adder",
                                 oom=True, sampled_kill_kb=8800000)
        check("kernel_oom_outranks_sampled_on_the_proof_stage",
              r["proof"] == "0" and "memory.max" in r["detail"]
              and "sampled aggregate hard limit" not in r["detail"],
              "an oom_kill event outranks the sampled guard", r["detail"])
        intact(r, "proof_oom_over_sampled")

        # ---- 3a2. refused-before-launch is NOT ATTEMPTED, not failed ----------
        # The race: the hard monitor sets _RSS_KILL, reaps, and RETURNS. Trip it
        # between the sim stage exiting and the proof stage being registered and
        # `_reap_all` finds nothing, no monitor remains, and the proof would
        # launch unguarded -- then exit 0, so nothing downstream would notice.
        r = agree_row()
        sweep.extract_proof_gate(r, "", None, expect_module="tima_adder",
                                 refused="the aggregate RSS hard limit was reached "
                                         "before the proof stage could launch")
        check("refused_launch_is_na_not_zero",
              r["proof"] == "na" and "not attempted" in r["detail"]
              and "hard limit" in r["detail"],
              "a proof that never launched is `na` with the reason, not `0`: "
              "`0` would read as the theorem being in doubt when it is simply "
              "unexamined", r["detail"])
        intact(r, "refused_launch")

        # ---- 3b. the schema carries the proof stage's own figures -------------
        for c in ("sim_max_rss_kb", "sim_user_s", "sim_sys_s", "sim_wall_s",
                  "proof_max_rss_kb", "proof_max_rss_source",
                  "proof_user_s", "proof_sys_s", "proof_wall_s"):
            check(f"schema_has_{c}", c in sweep.RESULT_COLS,
                  f"{c} is part of RESULT_COLS, so merge and resume see it")
        # Every stage-scoped column is named for its stage, and the generic ones
        # are whole-target. The bug this pins: `wall_s` spanned both stages while
        # `user_s`/`sys_s` were silently sim-only.
        check("no_generic_column_is_stage_scoped",
              all(c.startswith(("sim_", "proof_")) for c in sweep.STAGE_COLS)
              and not any(c.startswith(("sim_", "proof_"))
                          for c in ("max_rss_kb", "user_s", "sys_s", "wall_s")),
              f"the {len(sweep.STAGE_COLS)} stage columns are all prefixed, and the "
              f"generic four are whole-target")
        check("schema_has_stage_provenance",
              sweep.SRC_TIME_STAGES in ("time-max-rss-max-of-stages",)
              and sweep.SRC_TIME != sweep.SRC_TIME_STAGES,
              "a two-stage max_rss_kb is labelled distinctly from a one-process peak")

        # ---- 3c. the reifier mode drives BOTH stages from one setting ---------
        # The hazard this pins: if the executable probe and the proof probe could
        # pick different reifiers, the row would credit a theorem about a
        # function other than the one that ran.
        for mode, simtok, provetok in (("legacy", "reify_design ", "prove_reified "),
                                       ("named", "reify_design_named ",
                                        "prove_reified_incr ")):
            simp = sweep.make_probe(CERT, "tima_adder", 32, reifier=mode,
                                    phase_file=str(tmp / f"{mode}.phase"))
            prvp = sweep.make_proof_probe(CERT, "tima_adder", reifier=mode)
            check(f"mode_{mode}_sim_stage",
                  simtok in simp and (mode == "legacy") == ("reify_design_named" not in simp),
                  f"the {mode} executable probe uses {simtok.strip()}")
            check(f"mode_{mode}_proof_stage",
                  provetok in prvp,
                  f"and the {mode} proof probe uses {provetok.strip()}")
            # the model each stage emits must be the SAME reifier
            sim_named = "reify_design_named" in simp
            prv_named = "reify_design_named" in prvp
            check(f"mode_{mode}_stages_agree", sim_named == prv_named,
                  f"and both stages emit the same model kind "
                  f"(sim named={sim_named}, proof named={prv_named})")
        # the marker the runner credits names exactly the generated theorem
        check("marker_theorem_is_exact",
              sweep.PROOF_THEOREM == "d3_fast.correct"
              and "d3_proof_gate d3_fast.correct" in
                  sweep.make_proof_probe(CERT, "tima_adder", reifier="named"),
              "the gate in the named proof probe names exactly the theorem the "
              "runner credits")

        # Both reifiers must report in the SAME line shape, or the gate parser
        # reads a named run as "the reifier never ran" and zeroes compile/reify.
        # That is exactly what the first guarded named run did.
        import re as _re
        _pat = r"^reify_design(?:_named)?: \S+ emitted, \d+ sources, (\d+) bindings$"
        for mode in ("legacy", "named"):
            probe = sweep.make_probe(CERT, "tima_adder", 32, reifier=mode,
                                    phase_file=str(tmp / f"{mode}.phase"))
            tok = "reify_design_named" if mode == "named" else "reify_design"
            check(f"gate_line_shape_{mode}",
                  _re.search(sweep.PROOF_GATE_RE, "") is None and tok in probe,
                  f"the {mode} probe calls {tok}")
        check("gate_regex_accepts_both",
              _re.match(_pat, "reify_design: x emitted, 3 sources, 4 bindings") is not None
              and _re.match(_pat, "reify_design_named: x emitted, 3 sources, 4 bindings")
                  is not None,
              "and the gate parser accepts either reifier's emitted line")

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
