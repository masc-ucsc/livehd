#!/usr/bin/env python3
"""Build a checked direct-vs-compiled trace for a REAL exported artifact.

Everything before this was a fixture built to be testable. This drives an actual
CORE-ET export, and it reads the INPUT AND CLOCK ORDINALS FROM ITS SIDECAR rather
than hard-coding them -- which is the sidecar's whole purpose, and makes the run
a test of that file as well.

Two things it deliberately avoids:

  * ALL-ZERO inputs. A design fed zeros can hold state for reasons that have
    nothing to do with the clock, so a hold proves nothing. Every used input gets
    a distinct non-zero value derived from its ordinal and width.
  * UNFORCED probes. Asserting only `diffStep … = none` would pass if both
    implementations were equally wrong. The emitted cases also pin that a quiet
    domain's state is UNCHANGED and that a firing one is reached, so the trace
    has to distinguish them.

Usage: gen_multiclock_trace.py <artifact.lean> <sidecar.json> <out.lean>
"""

import json
import sys
from pathlib import Path


def main() -> int:
    if len(sys.argv) != 4:
        print(__doc__, file=sys.stderr)
        return 2
    art, side, out_path = (Path(a) for a in sys.argv[1:4])
    meta = json.loads(side.read_text())
    top = meta["top"]
    clocks = meta["clocks"]
    if len(clocks) < 2:
        print(f"FAIL: {top} has {len(clocks)} clock(s); this gate needs a multi-clock design",
              file=sys.stderr)
        return 1

    src = art.read_text()
    n_flops = src.count("resetActiveLow")
    n_mems = src.count("nextImg :=")

    # A distinct non-zero value per used input, inside its declared width.
    def value(p):
        if not p["used_by_model"]:
            return 0
        w = p["width"]
        return (p["ordinal"] * 7 + 3) % (1 << w) or 1

    args = ", ".join(f"mk_bv {p['width']} {value(p)}" for p in meta["inputs"])
    # The runtime state: every flop distinct, so a hold is visible per element.
    flops = ", ".join(f"mk_bv 1 {i % 2}" for i in range(n_flops))
    mems = ", ".join("fun _ => mk_bv 1 0" for _ in range(n_mems))
    st = f"(⟨#[{flops}], #[{mems}]⟩ : RuntimeState)"

    def edges(bits):
        return "#[" + ", ".join("true" if b else "false" for b in bits) + "]"

    n = len(clocks)
    vectors = {
        "all quiet": [False] * n,
        "all firing (= allEdges)": [True] * n,
    }
    for i, c in enumerate(clocks):
        bits = [False] * n
        bits[i] = True
        vectors[f"only {c['name']} (ordinal {i})"] = bits

    lines = [src.rstrip(), "",
             "import LeanSemanticPrimitives.Compiler.DirectVsCompiled" if False else "",
             f"-- Trace for the REAL exported artifact `{top}`.",
             f"-- {len(clocks)} clock(s): " + ", ".join(f"{c['ordinal']}={c['name']}" for c in clocks),
             f"-- {n_flops} flop(s), {n_mems} memory/memories, "
             f"runtime_input_arity {meta['runtime_input_arity']}.",
             "-- Input values come from the SIDECAR's ordinals and widths, not from",
             "-- hard-coded positions, so this exercises that file too.",
             ""]
    lines.append(f"def {top}_traceInput : RuntimeInput := #[{args}]")
    lines.append(f"def {top}_traceState : RuntimeState := {st}")
    lines.append("")
    # checkDesign/checkRuntime admission, on the real artifact.
    lines.append(f"example : (checkDesign {top}_designCert).isOk := by native_decide")
    for label, bits in vectors.items():
        lines.append(f"-- checked admission: {label}")
        lines.append(f"example : (checkRuntime {top}_designCert {edges(bits)} "
                     f"{top}_traceInput {top}_traceState).isOk := by native_decide")
    lines.append("")
    # direct vs compiled on every edge vector.
    addrs = "[0,1,2,3]" if n_mems else "[]"
    for label, bits in vectors.items():
        lines.append(f"-- direct vs compiled: {label}")
        lines.append(f"example : diffStep {top}_designCert {top}_residual {edges(bits)} "
                     f"{top}_traceInput {top}_traceState {addrs} = none := by native_decide")
    lines.append("")
    # The forced part: a fully quiet step must leave the state ALONE.
    lines.append("-- FORCED: with every domain quiet, no state element moves. Asserting")
    lines.append("-- only agreement would pass even if both implementations committed.")
    lines.append(f"example : (compileAndRun {top}_designCert {edges([False]*n)} "
                 f"{top}_traceInput {top}_traceState).nextState.flops = {top}_traceState.flops"
                 " := by native_decide")
    lines.append("")
    lines.append("-- allEdges IS the all-firing vector, and the emitted step agrees with the")
    lines.append("-- preserved interpreter on this real design.")
    lines.append(f"example : allEdges {top}_designCert = {edges([True]*n)} := by native_decide")
    lines.append(f"example : {top}_step {top}_traceInput {top}_traceState"
                 f" = interpretDesignLegacy {top}_designCert {top}_traceInput {top}_traceState"
                 f" := {top}_step_legacy_correct _ _")
    lines.append("")
    lines.append("-- ...and a QUIET domain is NOT that schedule, on this design.")
    first_quiet = [True] * n
    first_quiet[0] = False
    lines.append(f"example : (compileAndRun {top}_designCert {edges(first_quiet)} "
                 f"{top}_traceInput {top}_traceState).nextState.flops")
    lines.append(f"        ≠ (interpretDesignLegacy {top}_designCert {top}_traceInput "
                 f"{top}_traceState).nextState.flops := by native_decide")

    text = "\n".join(l for l in lines if l is not None)
    text = text.replace("import LeanSemanticPrimitives.Compiler.CompileDesign",
                        "import LeanSemanticPrimitives.Compiler.CompileDesign\n"
                        "import LeanSemanticPrimitives.Compiler.DirectCheck\n"
                        "import LeanSemanticPrimitives.Compiler.DirectVsCompiled", 1)
    text = text.replace("open Compiler\n", "open Compiler\nopen Compiler.Direct\n", 1)
    out_path.write_text(text + "\n")
    print(f"{top}: {len(clocks)} clocks, {n_flops} flops, {n_mems} mems -> {out_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
