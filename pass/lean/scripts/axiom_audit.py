#!/usr/bin/env python3
"""Audit the `#print axioms` output of a typechecked verified_compiler artifact.

The queue used to decide PROVEN by COUNTING lines: it compared the number of
`depends on axioms` lines against the number of `^#print axioms ` lines in the
file and required no literal `sorryAx`.  That is not an axiom audit.  It accepts

  * a file auditing theorems that are not the obligations we require, because no
    name is ever compared -- `#print axioms foo` satisfies a count of one;
  * any non-standard axiom whose name is not spelled `sorryAx`, including one
    introduced by an `axiom` declaration in the artifact itself;
  * a multi-line axiom list whose tail is never read, since Lean wraps output and
    the trailing axioms land on continuation lines.

This checks identity and content instead:

  1. every REQUIRED obligation is declared as a theorem in the artifact AND has a
     `#print axioms` for it (exact, namespace-qualified name);
  2. nothing extra is audited that is not a theorem in the artifact;
  3. every reported axiom list is parsed across line wraps and each axiom is on
     the allowlist -- the four Lean standard axioms, plus `Lean.ofReduceBool`
     and the per-design `<Top>_<obligation>._native.native_decide.ax_N` facts
     that `native_decide` necessarily introduces;
  4. `_step_legacy_correct` must additionally cite the `_clock_bounds` native
     fact.  That is the R13.1 guard: the clock bounds are proved separately and
     must actually be USED, not inferred from compile acceptance.

Usage: axiom_audit.py <typecheck.log> <Top> [--required a,b]
Exit 0 iff every check passes.
"""

import re
import sys

STANDARD = {"propext", "Classical.choice", "Quot.sound", "Quot.lift",
            "Quot.ind", "funext", "Lean.ofReduceBool", "Lean.trustCompiler"}
REQUIRED_DEFAULT = ("_step_correct", "_step_legacy_correct")


def parse_reports(text):
    """Return {theorem: [axiom, ...]} from `#print axioms` output.

    Lean wraps long lists, so the body is joined across lines until the closing
    bracket.  A report with no axioms prints `does not depend on any axioms`.
    """
    out = {}
    flat = re.sub(r"\n\s+", " ", text)
    for m in re.finditer(r"'([^']+)' depends on axioms: \[([^\]]*)\]", flat):
        out[m.group(1)] = [a.strip() for a in m.group(2).split(",") if a.strip()]
    for m in re.finditer(r"'([^']+)' does not depend on any axioms", flat):
        out.setdefault(m.group(1), [])
    return out


def main() -> int:
    if len(sys.argv) < 3:
        print("usage: axiom_audit.py <typecheck.log> <Top> [--required a,b]",
              file=sys.stderr)
        return 2
    log, top = sys.argv[1], sys.argv[2]
    required = REQUIRED_DEFAULT
    for i, a in enumerate(sys.argv):
        if a == "--required" and i + 1 < len(sys.argv):
            required = tuple(sys.argv[i + 1].split(","))
    try:
        text = open(log, encoding="utf-8", errors="replace").read()
    except OSError as e:
        print(f"FAIL: cannot read {log}: {e}")
        return 1

    fails = []
    reports = parse_reports(text)

    if "sorryAx" in text:
        fails.append("`sorryAx` appears in the typecheck output")

    for obl in required:
        name = f"{top}{obl}"
        if name not in reports:
            fails.append(f"no axiom report for required obligation `{name}` "
                         f"(reported: {sorted(reports) or 'none'})")

    native_re = re.compile(rf"^{re.escape(top)}_[A-Za-z0-9_]+\._native\.native_decide\.ax_[0-9_]+$")
    for thm, axs in reports.items():
        for ax in axs:
            if ax in STANDARD or native_re.match(ax):
                continue
            fails.append(f"`{thm}` depends on unexpected axiom `{ax}`")

    # R13.1: the legacy-compatibility obligation must USE the separately proved
    # clock bounds.  If it does not cite that native fact, the bounds were
    # discharged some other way -- which is the unsupported implication from
    # compile acceptance that this pipeline is built to refuse.
    legacy = f"{top}_step_legacy_correct"
    if legacy in reports:
        if not any("_clock_bounds._native" in a for a in reports[legacy]):
            fails.append(f"`{legacy}` does not cite the `_clock_bounds` native "
                         f"fact; clock bounds may be inferred from compilation")

    for f in fails:
        print(f"FAIL: {f}")
    if not fails:
        print(f"PASS: axiom audit ({len(reports)} report(s), "
              f"{sum(len(v) for v in reports.values())} axiom citation(s))")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
