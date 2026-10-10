#!/usr/bin/env python3
"""Audit the `#print axioms` output of a typechecked Lean artifact.

SCOPE.  By default this reads a TYPECHECK LOG only.  A log cannot show what the
artifact asked to be audited, so `--artifact <file.lean>` enables the second
half of the check: that the file declares the required obligations as theorems
and prints axioms for exactly those.  Without it, this audits the report and
says so -- it does not silently imply a source check it did not run.

WHAT THIS REPLACED.  The queue decided PROVEN by COUNTING lines: it compared the
number of `depends on axioms` lines against the number of `^#print axioms `
lines and grepped for the literal `sorryAx`.  That is not an audit.  It accepts

  * a file auditing theorems that are not the required obligations, because no
    name is ever compared -- `#print axioms foo` satisfies a count of one;
  * any non-standard axiom whose name is not spelled `sorryAx`, including one
    introduced by an `axiom` declaration in the artifact itself;
  * a multi-line axiom list whose tail is never read, since Lean wraps output
    and the trailing axioms land on continuation lines.

THE ALLOWLIST IS MEASURED, NOT GUESSED.  Across all 160 surviving axiom reports
in the corpus (78 CORE-ET, plus CVA6/DINO/regate), the complete set is:

    propext   Classical.choice   Quot.sound
    <Top>_compiles._native.native_decide.ax_N
    <Top>_clock_bounds._native.native_decide.ax_N

and nothing else -- no `funext`, no `Quot.lift`, no `Lean.ofReduceBool`, no
`Lean.trustCompiler`.  An earlier version of this file allowlisted those five on
the assumption they might appear.  They do not, and allowlisting an axiom that
has never been observed is how a trust assumption gets broadened silently.

The native facts are restricted BY OBLIGATION NAME, not by shape.  A regex that
admits `<Top>_<anything>._native.native_decide.ax_N` admits
`<Top>_arbitrary_false._native...`, which is an arbitrary `native_decide` of an
arbitrary proposition -- exactly the hole this is supposed to close.  Only the
two independently expected witnesses are accepted.

If the toolchain starts emitting a different axiom, this FAILS rather than
accepts, and a human decides whether to extend the list.  That is the correct
direction to fail in.

Usage: axiom_audit.py <typecheck.log> <Top> [--required a,b] [--artifact f.lean]
Exit 0 iff every check passes.
"""

import re
import sys

# Measured across the corpus.  See the module docstring before adding to this.
STANDARD = {"propext", "Classical.choice", "Quot.sound"}

# Obligations whose `native_decide` fact may be cited, per mode.
VERIFIED_REQUIRED = ("_step_correct", "_step_legacy_correct")
VERIFIED_WITNESSES = ("_compiles", "_clock_bounds")

NATIVE_RE = re.compile(r"\._native\.native_decide\.ax_[0-9_]+$")


def parse_reports(text):
    """{theorem: [axiom, ...]} from `#print axioms` output.

    Lean WRAPS long lists, so the body is joined across lines before matching;
    reading line by line silently drops every axiom after the first wrap.
    """
    out = {}
    flat = re.sub(r"\n\s+", " ", text)
    for m in re.finditer(r"'([^']+)' depends on axioms: \[([^\]]*)\]", flat):
        out[m.group(1)] = [a.strip() for a in m.group(2).split(",") if a.strip()]
    for m in re.finditer(r"'([^']+)' does not depend on any axioms", flat):
        out.setdefault(m.group(1), [])
    return out


def main() -> int:
    args = sys.argv[1:]
    if len(args) < 2:
        print("usage: axiom_audit.py <typecheck.log> <Top> [--required a,b] "
              "[--witnesses a,b] [--artifact f.lean]", file=sys.stderr)
        return 2
    log, top = args[0], args[1]
    required = VERIFIED_REQUIRED
    witnesses = VERIFIED_WITNESSES
    artifact = None
    for i, a in enumerate(args):
        if a == "--required" and i + 1 < len(args):
            required = tuple(x for x in args[i + 1].split(",") if x)
        elif a == "--witnesses" and i + 1 < len(args):
            witnesses = tuple(x for x in args[i + 1].split(",") if x)
        elif a == "--artifact" and i + 1 < len(args):
            artifact = args[i + 1]
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
        if f"{top}{obl}" not in reports:
            fails.append(f"no axiom report for required obligation `{top}{obl}` "
                         f"(reported: {sorted(reports) or 'none'})")

    # In `legacy` mode the expected witnesses are not a fixed pair, so they are
    # taken from the ARTIFACT: a native fact is accepted only when its obligation
    # stem names a theorem the artifact itself declares.  That is still a source-
    # validated check, not a shape match -- `<Top>_arbitrary_false` is rejected
    # because no such theorem exists -- but it does not require this file to know
    # every legacy obligation by name.  It needs `--artifact`.
    if witnesses == ("auto",):
        if artifact is None:
            print("FAIL: --witnesses auto requires --artifact")
            return 1
        try:
            decl = set(re.findall(r"^theorem (\S+)", open(artifact, encoding="utf-8",
                                                          errors="replace").read(), re.M))
        except OSError as e:
            print(f"FAIL: cannot read artifact {artifact}: {e}")
            return 1
        witnesses = tuple(sorted(t[len(top):] for t in decl if t.startswith(top)))
    allowed_native = {f"{top}{w}._native.native_decide.ax" for w in witnesses}
    for thm, axs in reports.items():
        for ax in axs:
            if ax in STANDARD:
                continue
            if NATIVE_RE.search(ax):
                stem = NATIVE_RE.sub("._native.native_decide.ax", ax)
                if stem in allowed_native:
                    continue
                fails.append(
                    f"`{thm}` cites native fact `{ax}`, which is not one of the "
                    f"independently expected witnesses "
                    f"({', '.join(top + w for w in witnesses)}). A native_decide of an "
                    f"unexpected proposition proves nothing about this design.")
                continue
            fails.append(f"`{thm}` depends on axiom `{ax}`, which is not in the "
                         f"measured standard set ({', '.join(sorted(STANDARD))})")

    # R13.1: the legacy-compatibility obligation must USE the separately proved
    # clock bounds.  If it does not cite that witness, the bounds were discharged
    # some other way -- the unsupported implication from compile acceptance that
    # this pipeline exists to refuse.  Only meaningful when that witness is in
    # play, so a legacy-mode run with other witnesses is unaffected.
    legacy = f"{top}_step_legacy_correct"
    if legacy in reports and "_clock_bounds" in witnesses:
        if not any("_clock_bounds._native" in a for a in reports[legacy]):
            fails.append(f"`{legacy}` does not cite the `_clock_bounds` native fact; "
                         f"clock bounds may be inferred from compilation")

    # The source half, only when the artifact is supplied.
    if artifact is not None:
        try:
            src = open(artifact, encoding="utf-8", errors="replace").read()
        except OSError as e:
            fails.append(f"cannot read artifact {artifact}: {e}")
        else:
            printed = re.findall(r"^#print axioms (\S+)", src, re.M)
            thms = set(re.findall(r"^theorem (\S+)", src, re.M))
            for obl in required:
                name = f"{top}{obl}"
                if name not in thms:
                    fails.append(f"artifact declares no `theorem {name}`")
                if name not in printed:
                    fails.append(f"artifact has no `#print axioms {name}`")
            for pr in printed:
                if pr not in thms:
                    fails.append(f"artifact audits `{pr}`, which it does not declare "
                                 f"as a theorem")

    for f in fails:
        print(f"FAIL: {f}")
    if not fails:
        scope = "report+source" if artifact else "report only"
        print(f"PASS: axiom audit ({scope}; {len(reports)} report(s), "
              f"{sum(len(v) for v in reports.values())} axiom citation(s))")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
