#!/usr/bin/env python3
"""Parser tests for `d3_sweep.extract_gates` / `verdict`.

Every case here is a way a worthless agreement result could have been credited
as a pass.  One of them actually happened: the self-test failure path wrote a
note into `detail`, left `agree=1` untouched, and `verdict()` duly returned
`agree`.  A sweep that reports that is worse than one that reports nothing.

The single invariant under test: **no log may yield `verdict == "agree"` unless
the checker proved, in that same run, that it rejects a wrong answer at every
observable the design actually has.**

Run: python3 pass/lean/scripts/d3_sweep_parser_test.py
"""

import importlib.util
import pathlib
import sys

HERE = pathlib.Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("d3_sweep", HERE / "d3_sweep.py")
d3 = importlib.util.module_from_spec(spec)
spec.loader.exec_module(d3)


def log(shape: str, selftest: str | None,
        agree: str | None = "agree=1 samples=32 distinct_obs=9",
        extra: list | None = None) -> str:
    parts = [
        "reify_design: d3_fast emitted, 10 sources, 12 bindings",
        f"D3GATE module=m {shape}",
        "D3GATE sim_exec=1 obs=123",
    ]
    if selftest is not None:
        parts.append(f"D3GATE {selftest}")
    if agree is not None:
        parts.append(f"D3GATE {agree}")
    parts += extra or []
    return "\n".join(parts) + "\n"


def run(out: str, rc: int = 0, expect_module=None, expect_samples=None):
    row = {g: 0 for g in d3.GATES}
    row["cert"] = 1
    row["proof"] = "na"
    for k in ("sources", "nodes", "outputs", "flops", "mems", "inputs", "bindings",
              "selftest_base", "mut_out", "mut_flop", "mut_mem",
              "mutable_out", "mutable_flop", "mutable_mem", "distinct_obs"):
        row[k] = ""
    d3.extract_gates(row, out, rc, 900, expect_module=expect_module,
                     expect_samples=expect_samples)
    return row, d3.verdict(row)


FULL = "sources=10 nodes=12 outputs=2 flops=1 mems=1 inputs=3 bindings=12"
MUT_ALL = "mutable_out=1 mutable_flop=1 mutable_mem=1"
GOOD = f"selftest_base=1 mut_out=rejected mut_flop=rejected mut_mem=rejected {MUT_ALL}"

# (name, shape, selftest, agree, rc, extra, must_not_be_agree, note)
CASES = [
    ("healthy", FULL, GOOD, None, 0, None, False,
     "positive control: everything present and correct"),
    ("accepted_out", FULL,
     f"selftest_base=1 mut_out=ACCEPTED mut_flop=rejected mut_mem=rejected {MUT_ALL}",
     None, 0, None, True, "checker failed to reject a wrong output"),
    ("accepted_flop", FULL,
     f"selftest_base=1 mut_out=rejected mut_flop=ACCEPTED mut_mem=rejected {MUT_ALL}",
     None, 0, None, True, "checker failed to reject a wrong flop"),
    ("accepted_mem", FULL,
     f"selftest_base=1 mut_out=rejected mut_flop=rejected mut_mem=ACCEPTED {MUT_ALL}",
     None, 0, None, True, "checker failed to reject a wrong memory value"),
    ("base_false", FULL,
     f"selftest_base=0 mut_out=rejected mut_flop=rejected mut_mem=rejected {MUT_ALL}",
     None, 0, None, True, "a result did not even agree with itself"),
    ("missing_selftest_line", FULL, None, None, 0, None, True,
     "the self-test line never printed"),
    ("missing_mut_field", FULL,
     f"selftest_base=1 mut_out=rejected mut_flop=rejected {MUT_ALL}",
     None, 0, None, True, "mut_mem absent while the memory is mutable"),
    ("missing_mutable_field", FULL,
     "selftest_base=1 mut_out=rejected mut_flop=rejected mut_mem=rejected "
     "mutable_out=1 mutable_flop=1",
     None, 0, None, True, "mutable_mem absent, so mut_mem cannot be judged"),
    ("wrong_na_out", FULL,
     f"selftest_base=1 mut_out=na mut_flop=rejected mut_mem=rejected {MUT_ALL}",
     None, 0, None, True, "`na` claimed for a mutable output"),
    ("rejected_on_immutable", FULL,
     "selftest_base=1 mut_out=rejected mut_flop=rejected mut_mem=rejected "
     "mutable_out=1 mutable_flop=1 mutable_mem=0",
     None, 0, None, True, "rejection claimed for a memory that cannot be mutated"),
    # The case raw counts get wrong: the observable is PRESENT but width 0.
    ("width0_output_present", "sources=10 nodes=12 outputs=1 flops=1 mems=0 inputs=3 bindings=12",
     "selftest_base=1 mut_out=na mut_flop=rejected mut_mem=na "
     "mutable_out=0 mutable_flop=1 mutable_mem=0",
     None, 0, None, False,
     "width-0 output is present but immutable; `na` is correct and must pass"),
    ("nothing_mutable", "sources=10 nodes=12 outputs=1 flops=1 mems=1 inputs=3 bindings=12",
     "selftest_base=1 mut_out=na mut_flop=na mut_mem=na "
     "mutable_out=0 mutable_flop=0 mutable_mem=0",
     None, 0, None, True, "no mutable observable, so the checker was never exercised"),
    ("no_observables", "sources=10 nodes=12 outputs=0 flops=0 mems=0 inputs=3 bindings=12",
     "selftest_base=1 mut_out=na mut_flop=na mut_mem=na "
     "mutable_out=0 mutable_flop=0 mutable_mem=0",
     None, 0, None, True, "nothing to observe, so agreement is vacuous"),
    ("shape_missing_counts", "sources=10 nodes=12 inputs=3 bindings=12", GOOD,
     None, 0, None, True, "counts absent"),
    ("mutable_two", FULL,
     "selftest_base=1 mut_out=rejected mut_flop=rejected mut_mem=rejected "
     "mutable_out=1 mutable_flop=1 mutable_mem=2",
     None, 0, None, True,
     "mutable_mem=2 is out of range; it must not fall through to the `na` branch"),
    ("mutable_nonnumeric", FULL,
     "selftest_base=1 mut_out=rejected mut_flop=rejected mut_mem=rejected "
     "mutable_out=1 mutable_flop=1 mutable_mem=yes",
     None, 0, None, True, "non-numeric applicability flag"),
    ("mutable_contradicts_count",
     "sources=10 nodes=12 outputs=2 flops=0 mems=1 inputs=3 bindings=12",
     "selftest_base=1 mut_out=rejected mut_flop=rejected mut_mem=rejected "
     "mutable_out=1 mutable_flop=1 mutable_mem=1",
     None, 0, None, True, "mutable_flop=1 while flops=0"),
    ("duplicate_selftest", FULL, GOOD, None, 0,
     ["D3GATE selftest_base=0 mut_out=na mut_flop=na mut_mem=na "
      "mutable_out=0 mutable_flop=0 mutable_mem=0"],
     True, "two conflicting self-test lines"),
    # --- item 3 ---------------------------------------------------------------
    ("agree_zero", FULL, GOOD, "agree=0 samples=32 distinct_obs=9", 0, None, True,
     "a real disagreement must stop at checker, not read as agree"),
    ("agree_malformed", FULL, GOOD, "agree=x samples=32 distinct_obs=9", 0, None, True,
     "a non-boolean agree value must not be credited"),
    ("agree_missing", FULL, GOOD, None, 0, None, True,
     "the agree line never printed"),
    ("duplicate_shape", FULL, GOOD, None, 0,
     ["D3GATE module=m sources=99 nodes=99 outputs=9 flops=9 mems=9 inputs=9 bindings=9"],
     True, "two conflicting shape lines; neither may be silently believed"),
    ("duplicate_agree", FULL, GOOD, None, 0,
     ["D3GATE agree=0 samples=32 distinct_obs=1"], True,
     "two conflicting agree lines"),
    ("nonzero_exit", FULL, GOOD, None, 1, None, True,
     "a nonzero Lean exit invalidates the gate lines printed before it"),
    ("error_in_log", FULL, GOOD, None, 0, ["probes/m.lean:9:0: error: boom"], True,
     "an error line invalidates the run even at exit 0"),
]


def main() -> int:
    failures = []
    for name, shape, selftest, agree, rc, extra, must_not_agree, note in CASES:
        kwargs = {} if agree is None and name in ("agree_missing",) else {}
        text = log(shape, selftest,
                   agree if agree is not None else (
                       None if name == "agree_missing" else "agree=1 samples=32 distinct_obs=9"),
                   extra)
        row, v = run(text, rc)
        bad = (v == "agree") if must_not_agree else (v != "agree")
        status = "FAIL" if bad else "ok"
        if bad:
            failures.append(name)
        print(f"{status:<4} {name:<24} verdict={v:<10} checker={row['checker']} "
              f"agree={row['agree']}  {note}")
        if bad:
            print(f"       detail={row.get('detail')!r}")

    # --- trust order: an identity or shape failure leaves ONLY `cert` --------
    #
    # Gates must not be credited from a log before the log is shown to describe
    # this certificate. An earlier version set compile/reify/typecheck first and
    # validated identity afterwards, so a stale clean log for another module
    # still earned verdict=typecheck.
    ident = [
        ("wrong_module",
         log(FULL, GOOD).replace("D3GATE module=m ", "D3GATE module=someone_else "),
         {"expect_module": "m"},
         "a clean log for a DIFFERENT module"),
        ("emitted_count_mismatch",
         log(FULL, GOOD).replace("emitted, 10 sources, 12 bindings",
                                 "emitted, 99 sources, 77 bindings"),
         {"expect_module": "m"},
         "the reifier's counts contradict the shape line"),
        ("duplicate_shape_key",
         log(FULL + " sources=20", GOOD), {"expect_module": "m"},
         "the shape line repeats `sources`"),
        ("shape_value_not_a_count",
         log("sources=ten nodes=12 outputs=2 flops=1 mems=1 inputs=3 bindings=12", GOOD),
         {"expect_module": "m"},
         "a shape value that is not a number"),
    ]
    for name, text, kw, note in ident:
        row, v = run(text, 0, **kw)
        gates_set = [g for g in ("compile", "reify", "typecheck", "sim", "checker", "agree")
                     if row.get(g) == 1]
        if v != "cert" or gates_set:
            print(f"FAIL {name:<24} verdict={v} (want cert), still-set gates={gates_set}")
            print(f"       detail={row.get('detail')!r}")
            failures.append(name)
        else:
            print(f"ok   {name:<24} verdict=cert, no gate credited  — {note}")

    # the sample count actually run must match the one requested
    row, v = run(log(FULL, GOOD, "agree=1 samples=8 distinct_obs=9"),
                 0, expect_module="m", expect_samples=32)
    if v == "agree":
        print("FAIL sample_count_mismatch   a run with 8 samples credited against a request for 32")
        failures.append("sample_count_mismatch")
    else:
        print(f"ok   sample_count_mismatch   verdict={v}, samples=8 != requested 32")

    # agree=0 with a sound checker must land exactly on `checker`.
    row, v = run(log(FULL, GOOD, "agree=0 samples=32 distinct_obs=9"))
    if v != "checker" or row["checker"] != 1:
        print(f"FAIL agree_zero_lands_on_checker: verdict={v} checker={row['checker']}")
        failures.append("agree_zero_lands_on_checker")
    else:
        print("ok   agree_zero_verdict       a real disagreement stops at verdict=checker")

    # The raw agreement value is still recorded; it just cannot set the verdict.
    row, v = run(log(FULL,
                     f"selftest_base=1 mut_out=ACCEPTED mut_flop=rejected mut_mem=rejected {MUT_ALL}"))
    if row["agree"] != 1 or v == "agree":
        print("FAIL raw_agree_recorded: observed value lost or verdict granted")
        failures.append("raw_agree_recorded")
    else:
        print("ok   raw_agree_recorded      observed agree=1 kept, verdict withheld")

    print("\nPARSER-TEST", "OK" if not failures else f"FAILED: {failures}")
    return 0 if not failures else 1


if __name__ == "__main__":
    sys.exit(main())
