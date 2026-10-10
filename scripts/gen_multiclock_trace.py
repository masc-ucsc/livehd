#!/usr/bin/env python3
"""Build a checked multi-cycle direct-vs-compiled trace for a REAL export.

Everything before this used a fixture built to be testable. This drives an
actual CORE-ET artifact and reads its input and clock ordinals FROM ITS SIDECAR
and its state widths FROM THE CERTIFICATE'S OWN DESCRIPTORS -- which is what
those files are for, and makes the run a test of them too.

Three things it deliberately avoids:

  * WRONG STATE WIDTHS. The first version built every flop as `mk_bv 1`,
    whatever the design declared -- txfma_f4's flops are [1,52,1,32,1,1].
    `checkRuntime` compares array LENGTHS, not value widths, so it accepted that
    and the trace "passed" against state no hardware would produce. The checker
    is not changed; the generator is.
  * ALL-ZERO inputs. A design fed zeros can hold state for reasons unrelated to
    the clock, so a hold would prove nothing.
  * UNFORCED probes. `diffStep ... = none` passes if both implementations are
    equally wrong, so the trace also pins values an independent source predicts.

Usage: gen_multiclock_trace.py <artifact.lean> <sidecar.json> <out.lean>
         [--oracle FLOP,INPUT,OUTPUT,CLOCK]
`--oracle` names a flop whose RTL behaviour is known independently: on CLOCK's
edge it loads INPUT, and its value is visible at OUTPUT on the FOLLOWING tick.
"""

import json
import re
import sys
from pathlib import Path


def main() -> int:
    args = sys.argv[1:]
    if len(args) < 3:
        print(__doc__, file=sys.stderr)
        return 2
    art, side, out_path = (Path(a) for a in args[:3])
    oracle = None
    if "--oracle" in args:
        oracle = [int(x) for x in args[args.index("--oracle") + 1].split(",")]

    meta = json.loads(side.read_text())
    top, clocks = meta["top"], meta["clocks"]
    if len(clocks) < 2:
        print(f"FAIL: {top} has {len(clocks)} clock(s); this gate needs a multi-clock design",
              file=sys.stderr)
        return 1
    src = art.read_text()

    flop_w = [int(m) for m in re.findall(r"\{ width := (\d+), din := \d+,[^}]*resetActiveLow", src)]
    flop_clk = [int(m) for m in re.findall(r"resetActiveLow := (?:true|false), clock := (\d+)", src)]
    mem_w = [int(m) for m in re.findall(r"\{ aw := \d+, dw := (\d+), nextImg", src)]
    n = len(clocks)
    if len(flop_w) != len(flop_clk):
        print("FAIL: could not read flop widths and domains consistently", file=sys.stderr)
        return 1

    # VALIDATE the sidecar against the artifact rather than trusting it.
    n_src_inputs = len(re.findall(r"SourceDesc\.input \d+", src))
    if max((c["ordinal"] for c in clocks), default=-1) != n - 1:
        print("FAIL: sidecar clock ordinals are not dense", file=sys.stderr)
        return 1
    for i, p in enumerate(meta["inputs"]):
        if p["ordinal"] != i:
            print(f"FAIL: sidecar input {i} has ordinal {p['ordinal']}", file=sys.stderr)
            return 1
    if any(f >= n for f in flop_clk):
        print("FAIL: a flop names a clock ordinal the sidecar does not declare", file=sys.stderr)
        return 1

    def ivalue(p, bump=0):
        if not p["used_by_model"]:
            return 0
        w = p["width"]
        return ((p["ordinal"] * 7 + 3 + bump) % (1 << w)) or 1

    def input_vec(bump=0):
        return "#[" + ", ".join(f"mk_bv {p['width']} {ivalue(p, bump)}" for p in meta["inputs"]) + "]"

    # State at the DECLARED widths. One-bit elements cannot all differ, so they
    # alternate; wider ones get distinct values.
    flops = ", ".join(f"mk_bv {w} {(i * 37 + 5) % (1 << w) if w > 1 else i % 2}"
                      for i, w in enumerate(flop_w))
    mems = ", ".join(f"fun _ => mk_bv {w} 0" for w in mem_w)
    state = f"(⟨#[{flops}], #[{mems}]⟩ : RuntimeState)"
    addrs = "[0,1,2,3]" if mem_w else "[]"

    def edges(bits):
        return "#[" + ", ".join("true" if b else "false" for b in bits) + "]"

    vectors = {"all quiet": [False] * n, "all firing (= allEdges)": [True] * n}
    for i, c in enumerate(clocks):
        b = [False] * n
        b[i] = True
        vectors[f"only {c['name']} (ordinal {i})"] = b

    L = [src.rstrip(), "",
         f"-- Trace for the REAL exported artifact `{top}`.",
         f"-- clocks: " + ", ".join(f"{c['ordinal']}={c['name']}" for c in clocks),
         f"-- flop widths {flop_w} on domains {flop_clk}; {len(mem_w)} memory/memories.",
         f"-- runtime_input_arity {meta['runtime_input_arity']}, {n_src_inputs} input source(s).",
         "-- Input ordinals come from the sidecar; STATE WIDTHS from the",
         "-- certificate's own descriptors.",
         "",
         f"def {top}_in0 : RuntimeInput := {input_vec(0)}",
         f"def {top}_in1 : RuntimeInput := {input_vec(17)}",
         f"def {top}_st0 : RuntimeState := {state}",
         ""]

    L.append(f"example : (checkDesign {top}_designCert).isOk := by native_decide")
    for label, b in vectors.items():
        L.append(f"-- checked admission: {label}")
        L.append(f"example : (checkRuntime {top}_designCert {edges(b)} {top}_in0 {top}_st0).isOk"
                 " := by native_decide")
    L.append("")
    for label, b in vectors.items():
        L.append(f"-- direct vs compiled: {label}")
        L.append(f"example : diffStep {top}_designCert {top}_residual {edges(b)} {top}_in0 "
                 f"{top}_st0 {addrs} = none := by native_decide")
    L.append("")
    L.append("-- FORCED: every domain quiet, nothing moves. Agreement alone would pass")
    L.append("-- even if both implementations committed.")
    L.append(f"example : (compileAndRun {top}_designCert {edges([False]*n)} {top}_in0 "
             f"{top}_st0).nextState.flops = {top}_st0.flops := by native_decide")
    L.append("")
    L.append(f"example : allEdges {top}_designCert = {edges([True]*n)} := by native_decide")
    L.append(f"example : {top}_step {top}_in0 {top}_st0"
             f" = interpretDesignLegacy {top}_designCert {top}_in0 {top}_st0"
             f" := {top}_step_legacy_correct _ _")
    fq = [True] * n
    fq[0] = False
    L.append("-- ...and a QUIET domain is NOT that schedule.")
    L.append(f"example : (compileAndRun {top}_designCert {edges(fq)} {top}_in0 {top}_st0).nextState.flops")
    L.append(f"        ≠ (interpretDesignLegacy {top}_designCert {top}_in0 "
             f"{top}_st0).nextState.flops := by native_decide")
    L.append("")

    # ---- the independently sourced oracle -----------------------------------
    if oracle:
        f_ord, i_ord, o_ord, c_ord = oracle
        w = flop_w[f_ord]
        loaded = ivalue(meta["inputs"][i_ord])
        if flop_clk[f_ord] != c_ord:
            print(f"FAIL: flop {f_ord} is on domain {flop_clk[f_ord]}, not {c_ord}", file=sys.stderr)
            return 1
        fire = [False] * n
        fire[c_ord] = True
        other = [False] * n
        other[1 - c_ord if n == 2 else (c_ord + 1) % n] = True
        L += ["-- INDEPENDENTLY SOURCED ORACLE.",
              f"-- The RTL assigns output {o_ord} <= input {i_ord} on "
              f"{clocks[c_ord]['name']}'s edge; the certificate puts that in flop "
              f"{f_ord} ({w}-bit, domain {c_ord}).",
              "-- This is predicted from the source, not read off the model.",
              "",
              f"-- it COMMITS the input on {clocks[c_ord]['name']}'s edge:",
              f"example : ((compileAndRun {top}_designCert {edges(fire)} {top}_in0 "
              f"{top}_st0).nextState.flops[{f_ord}]!) = mk_bv {w} {loaded} := by native_decide",
              f"-- it HOLDS when only the OTHER clock fires:",
              f"example : ((compileAndRun {top}_designCert {edges(other)} {top}_in0 "
              f"{top}_st0).nextState.flops[{f_ord}]!) = {top}_st0.flops[{f_ord}]!"
              " := by native_decide",
              "-- ...and when nothing fires:",
              f"example : ((compileAndRun {top}_designCert {edges([False]*n)} {top}_in0 "
              f"{top}_st0).nextState.flops[{f_ord}]!) = {top}_st0.flops[{f_ord}]!"
              " := by native_decide",
              "",
              "-- VISIBILITY: the output reads the flop's CURRENT value, so the",
              "-- committed value appears on the FOLLOWING tick. The pre-state",
              "-- timing is not adjusted to make this pass.",
              f"def {top}_afterCommit : RuntimeState :=",
              f"  (compileAndRun {top}_designCert {edges(fire)} {top}_in0 {top}_st0).nextState",
              f"example : ((compileAndRun {top}_designCert {edges([False]*n)} {top}_in0 "
              f"{top}_afterCommit).outputs[{o_ord}]!) = mk_bv {w} {loaded} := by native_decide",
              f"-- ...and on the commit tick itself the output still shows the OLD value.",
              f"example : ((compileAndRun {top}_designCert {edges(fire)} {top}_in0 "
              f"{top}_st0).outputs[{o_ord}]!) = {top}_st0.flops[{f_ord}]! := by native_decide",
              ""]

    # ---- multi-cycle, state carried forward ---------------------------------
    L.append("-- MULTI-CYCLE. Each tick starts from the previous tick's nextState, so a")
    L.append("-- commit that should not have happened diverges later even when the step")
    L.append("-- it happened on looked fine on its own.")
    ticks = [([False] * n, f"{top}_in0"),
             ([True] + [False] * (n - 1), f"{top}_in1"),
             ([False] * (n - 1) + [True], f"{top}_in0"),
             ([True] * n, f"{top}_in1")]
    lits = ",\n             ".join(f"⟨{edges(b)}, {i}⟩" for b, i in ticks)
    L.append(f"example : diffTrace {top}_designCert {top}_residual {addrs} {top}_st0")
    L.append(f"            [{lits}] 0 = none := by native_decide")

    text = "\n".join(L)
    text = text.replace("import LeanSemanticPrimitives.Compiler.CompileDesign",
                        "import LeanSemanticPrimitives.Compiler.CompileDesign\n"
                        "import LeanSemanticPrimitives.Compiler.DirectCheck\n"
                        "import LeanSemanticPrimitives.Compiler.DirectVsCompiled", 1)
    text = text.replace("open Compiler\n", "open Compiler\nopen Compiler.Direct\n", 1)
    out_path.write_text(text + "\n")
    print(f"{top}: {n} clocks, flop widths {flop_w} on domains {flop_clk} -> {out_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
