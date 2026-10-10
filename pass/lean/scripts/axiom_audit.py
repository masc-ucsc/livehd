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

NAMESPACES.  Lean reports a theorem by its FULL name, and these artifacts wrap
their declarations in `namespace <Top>_Lgraph`, so the report says
`tiny_and_Lgraph.tiny_and_comb_refines_fast` while the obligation is spelled
`tiny_and_comb_refines_fast`.  The namespace is read from the artifact's own
`namespace` declaration -- exactly, never by matching a suffix, because a
suffix match is how `evil.tiny_and_comb_refines_fast` would be accepted.  Native
fact names are NOT namespaced (Lean mangles them from the unqualified name), so
they are matched unqualified.

MODE AND SCOPE ARE INDEPENDENT OF THE OBLIGATIONS.  Which obligations are
required is decided by what the DESIGN is, read from structural markers in the
artifact:

  * a verified export proves `_step_correct` / `_step_legacy_correct`;
  * a legacy export always proves `_comb_refines_fast`, and additionally
    `_next_refines_fast` / `_step_refines_fast` when the design HAS STATE
    (`structure <Top>_state`);
  * any artifact defining `<Top>_graphCert` must also prove
    `<Top>_graphCert_wf`.

Deciding the scope from which obligations happen to be PRESENT would let a file
pick the easiest mode it can satisfy -- an artifact missing `_next_refines_fast`
would simply be judged combinational.  An EMPTY required set is a hard failure
for the same reason: an artifact that proves only `Unrecognized : True` must not
pass by having nothing asked of it.

`--required` REPLACES the derived set and is for callers that know exactly what
they want. `--min-required` ADDS to it, which is what a replay mode wants: the
structural derivation still applies, and the mode can only strengthen it.

Usage: axiom_audit.py <typecheck.log> <Top> [--required a,b] [--min-required a,b]
                      [--witnesses a,b] [--artifact f.lean] [--namespace N]
                      [--mode verified|legacy|auto]
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


def read_artifact(path):
    try:
        return open(path, encoding="utf-8", errors="replace").read()
    except OSError as e:
        return e


def derive_scope(src, top):
    """(required, witnesses_hint) from STRUCTURAL markers, not from obligations."""
    if re.search(rf"^theorem {re.escape(top)}_step_correct\b", src, re.M) or \
       re.search(rf"^def {re.escape(top)}_designCert\b", src, re.M):
        return ("verified", ["_step_correct", "_step_legacy_correct"])
    req = ["_comb_refines_fast"]
    # HAS STATE, read from the design, not from which theorems exist.
    if re.search(rf"^structure {re.escape(top)}_state\b", src, re.M):
        req += ["_next_refines_fast", "_step_refines_fast"]
    # A graph certificate carries a well-formedness obligation.
    if re.search(rf"^def {re.escape(top)}_graphCert\b", src, re.M):
        req += ["_graphCert_wf"]
    return ("legacy", req)


def main() -> int:
    args = sys.argv[1:]
    if len(args) < 2:
        print("usage: axiom_audit.py <typecheck.log> <Top> [--required a,b] "
              "[--witnesses a,b] [--artifact f.lean] [--namespace N] "
              "[--mode verified|legacy|auto]", file=sys.stderr)
        return 2
    log, top = args[0], args[1]
    required = None
    min_required = ()
    witnesses = None
    artifact = None
    namespace = None
    mode = "auto"
    for i, a in enumerate(args):
        if a == "--required" and i + 1 < len(args):
            required = tuple(x for x in args[i + 1].split(",") if x)
        elif a == "--witnesses" and i + 1 < len(args):
            witnesses = tuple(x for x in args[i + 1].split(",") if x)
        elif a == "--artifact" and i + 1 < len(args):
            artifact = args[i + 1]
        elif a == "--namespace" and i + 1 < len(args):
            namespace = args[i + 1]
        elif a == "--mode" and i + 1 < len(args):
            mode = args[i + 1]
    try:
        text = open(log, encoding="utf-8", errors="replace").read()
    except OSError as e:
        print(f"FAIL: cannot read {log}: {e}")
        return 1

    fails = []
    src = None
    if artifact is not None:
        src = read_artifact(artifact)
        if isinstance(src, OSError):
            print(f"FAIL: cannot read artifact {artifact}: {src}")
            return 1
        if namespace is None:
            m = re.search(r"^namespace (\S+)", src, re.M)
            namespace = m.group(1) if m else None

    # Scope. Derived from the artifact's structure when one is given; otherwise
    # the caller must say, because guessing from the report is exactly the
    # "pick the easiest satisfiable mode" hole.
    if required is None:
        if src is not None:
            detected, required = derive_scope(src, top)
            # A caller's MINIMUM is added to the derived set, never substituted
            # for it. Passing `--required _comb_refines_fast` for a bridge mode
            # REPLACED the derivation, so a stateful model with no next/step
            # obligations came back PROVEN -- measured on a fixture whose two
            # next-state functions provably differ. A caller may demand more
            # than the artifact's structure implies; it may not demand less.
            required = tuple(required) + tuple(o for o in min_required if o not in required)
            if mode not in ("auto", detected):
                fails.append(f"artifact looks like a {detected} export but --mode {mode} was requested")
        elif mode == "verified":
            required = VERIFIED_REQUIRED
        else:
            print("FAIL: no obligation set: pass --required, or --artifact so the "
                  "scope can be derived, or --mode verified")
            return 1
    if not required:
        print("FAIL: the required obligation set is EMPTY. An artifact cannot pass "
              "an audit that asks nothing of it.")
        return 1

    if witnesses is None:
        witnesses = VERIFIED_WITNESSES if tuple(required) == VERIFIED_REQUIRED else ("auto",)

    reports = parse_reports(text)

    def reported(obl):
        """The report key for an obligation, fully qualified when namespaced."""
        bare = f"{top}{obl}"
        if namespace and f"{namespace}.{bare}" in reports:
            return f"{namespace}.{bare}"
        return bare if bare in reports else None

    if "sorryAx" in text:
        fails.append("`sorryAx` appears in the typecheck output")

    for obl in required:
        if reported(obl) is None:
            fails.append(f"no axiom report for required obligation `{top}{obl}`"
                         + (f" (namespace `{namespace}`)" if namespace else "")
                         + f" (reported: {sorted(reports) or 'none'})")

    if witnesses == ("auto",):
        if src is None:
            print("FAIL: --witnesses auto requires --artifact")
            return 1
        decl = set(re.findall(r"^theorem (\S+)", src, re.M))
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
                    f"independently expected witnesses. A native_decide of an "
                    f"unexpected proposition proves nothing about this design.")
                continue
            fails.append(f"`{thm}` depends on axiom `{ax}`, which is not in the "
                         f"measured standard set ({', '.join(sorted(STANDARD))})")

    # R13.1: the legacy-compatibility obligation must USE the separately proved
    # clock bounds, not infer them from compile acceptance.
    legacy = reported("_step_legacy_correct")
    if legacy is not None and "_clock_bounds" in witnesses:
        if not any("_clock_bounds._native" in a for a in reports[legacy]):
            fails.append(f"`{legacy}` does not cite the `_clock_bounds` native fact; "
                         f"clock bounds may be inferred from compilation")

    if src is not None:
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
        print(f"PASS: axiom audit ({scope}; {len(required)} obligation(s); "
              f"{len(reports)} report(s), "
              f"{sum(len(v) for v in reports.values())} axiom citation(s))")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
