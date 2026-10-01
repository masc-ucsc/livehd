#!/usr/bin/env python3
"""Differential: source RTL (iverilog) vs the NORMALIZED Lean certificate.

WHY THIS AND NOT lhd lec OR THE VERILOG DIFFSIM.

Edge normalization is not cycle-preserving -- one source period becomes P
sub-steps -- so no cycle-accurate equivalence checker can validate it, and the
existing single_edge_memory_slot_test answers that by running source Verilog
against NORMALIZED Verilog at period boundaries. That route is unavailable for
a mixed-timing memory: both cgen emitters refuse it by name (they would emit a
stateless combinational array). The Lean certificate is the only model of the
normalized design that exists, so it is the one to compare against.

OBSERVATION PHASE -- stated exactly, because "at the period boundary" is not
precise enough to reproduce:
  * one source period = ONE iverilog clock period = P Lean microsteps;
  * inputs are held across the microsteps of a period, as the RTL holds them
    across a clock period;
  * `directStepRaw` returns PRE-TRANSITION outputs -- the design BEFORE the
    step's LAST slot commits.  Which RTL phase matches depends on P, and
    `--sample` selects it:
      P=2, `--sample after-first-edge` (the default, and the historical one):
        the posedge is slot 0, so the model's pre-slot-1 sample is the state
        AFTER the posedge and BEFORE the negedge, and a slot-1 effect (a
        negedge element) is observed on the NEXT sample, not the current one;
      P=1, `--sample before-first-edge`: there is a single slot, so the
        matching RTL sample is BEFORE the posedge.  Sampling after it would
        compare Lean's f(state_k, in_k) against the RTL's f(state_k+1, in_k)
        and report an off-by-one as a value mismatch.
A post-state peek would be the other defensible convention; this one is kept
because the negative controls discriminate under it.

The positional stimulus is derived from the <Top>_io.json sidecar, so the
named scenario below and the Lean row vectors cannot drift: values are padded
to `runtime_input_arity`, not to the port count, because DirectSim ignores
anything beyond it.
"""
import argparse
import json
import os
import re
import shutil
import subprocess
import sys

PROBE = """
open Compiler Compiler.Direct in
#eval show IO Unit from do
  let D := @BASE@_designCert
  let ws : Array Nat := #[@WIDTHS@]
  let lines ← IO.FS.lines "@STIM@"
  let mut st := zeroState D
  let mut n : Nat := 0
  for ln in lines do
    let fs := ((ln.splitOn " ").filter (fun s => s != "")).toArray
    if fs.size != 0 then
      let vals : Array Nat := fs.map (fun s => (s.toNat?).getD 0)
      let inp : RuntimeInput := Array.zipWith (fun (w : Nat) (v : Nat) => mk_bv w v) ws vals
      let r := directStepRaw D (allEdges D) inp st
      st := r.nextState
      n := n + 1
      -- One sample per source period (P=@P@ microsteps). directStepRaw's
      -- outputs are PRE-transition: the design BEFORE the step's last slot
      -- commits. At P=2 that is after slot 0 and before slot 1, so a slot-1
      -- effect shows up on the next sample; at P=1 there is one slot, so the
      -- sample is simply before it.
      if n % @P@ == 0 then
        IO.println (String.intercalate " " (r.outputs.toList.map (fun b => toString (bv_uint b))))
"""


def build_probe(cert_text, base, widths, stim, p):
    text = cert_text.replace("import LeanSemanticPrimitives.Compiler.CompileDesign",
                             "import LeanSemanticPrimitives.Compiler.DirectTrace")
    cut = text.find("/-- Compile-and-run.")
    if cut < 0:
        m = re.search(r"^def\s+\w+_step\s*:", text, re.M)
        cut = m.start() if m else len(text)
    tail = (PROBE.replace("@BASE@", base)
                 .replace("@WIDTHS@", ",".join(str(w) for w in widths))
                 .replace("@STIM@", stim)
                 .replace("@P@", str(p)))
    return text[:cut] + tail


def build_tb(top, ins, outs, nper, stim, p, init="", sample="after-first-edge"):
    """One clock period per stimulus period, sampled at the period boundary.

    TWO things this has to get right, both of which produced wrong traces on
    the first attempt:

    * STATE INITIALISATION. The Lean model starts from `zeroState`; Verilog
      regs and an uninferred memory start as X. Comparing those is comparing a
      model to noise, and an X column is not even a value the comparison can
      read. The caller supplies an `init` snippet that zeroes the DUT state
      hierarchically, which is the iverilog counterpart of verilator's
      `--x-assign 0 --x-initial 0`.
    * NOT DRIVING ON THE EDGE. Applying stimulus immediately after
      `@(negedge clk)` races the nonblocking updates scheduled by that very
      edge: the negedge flop `nq` sampled the NEW value instead of the old one,
      which read as a one-period offset in the trace. Inputs are applied a
      delta after the negedge and sampled a delta before the next one.
    """
    decl = "".join(f"  reg {'' if w == 1 else f'[{w-1}:0] '}{n};\n"
                   for n, w in ins if n != "clk")
    odecl = "".join(f"  wire {'' if w == 1 else f'[{w-1}:0] '}{n};\n" for n, w in outs)
    conn = ", ".join(f".{n}({n})" for n, _ in ins + outs)
    read = "".join(f'      $fscanf(f, "%d", {n});\n' for n, _ in ins if n != "clk")
    show = ", ".join(n for n, _ in outs)
    fmt = " ".join("%0d" for _ in outs)
    init = init or "    // (no DUT state initialisation supplied)\n"
    # WHICH EDGE THE SAMPLE SITS AFTER.  `directStepRaw` returns PRE-transition
    # outputs, so what it reports is the design BEFORE the step's last slot
    # commits.  At P=2 the posedge is slot 0 and the pre-slot-1 sample is
    # therefore AFTER the posedge -- the historical default, kept so the P=2
    # fixture's evidence is unchanged.  At P=1 there is only ONE slot, so the
    # matching RTL sample is BEFORE the posedge; taking it after would compare
    # Lean's f(state_k, input_k) against the RTL's f(state_k+1, input_k) and
    # report an off-by-one as a value mismatch.
    if sample == "before-first-edge":
        sample_body = ("      #3;                      // inputs settled, BEFORE the only edge\n"
                       f'      $display("{fmt}", {show});\n'
                       "      @(posedge clk);\n"
                       "      @(negedge clk);")
    elif sample == "after-first-edge":
        sample_body = ("      @(posedge clk);\n"
                       "      #3;                      // after the posedge settles, before the negedge\n"
                       f'      $display("{fmt}", {show});\n'
                       "      @(negedge clk);")
    else:
        raise SystemExit(f"FATAL: unknown --sample {sample!r}")
    zero_in = "".join(f"    {n} = 0;\n" for n, _ in ins if n != "clk")
    # The generated file must describe the timing it ACTUALLY has. A fixed
    # header saying "after the posedge" sat on top of a before-the-posedge
    # loop under --sample before-first-edge, which is the kind of artifact
    # someone reads once and then debugs against for an hour.
    if sample == "before-first-edge":
        phase_note = ("// One source period per stimulus row. Outputs are sampled BEFORE THE\n"
                      "// POSEDGE, which is the phase the Lean side reports: directStepRaw yields\n"
                      "// PRE-transition outputs, and at P=1 there is a single slot, so the model's\n"
                      "// sample is the state before that slot commits.")
    else:
        phase_note = ("// One source period per stimulus row. Outputs are sampled AFTER THE POSEDGE\n"
                      "// AND BEFORE THE NEGEDGE, which is the phase the Lean side reports:\n"
                      "// directStepRaw yields PRE-transition outputs, so at P=2 its sample is the\n"
                      "// state after slot 0 and before slot 1. A slot-1 effect appears on the NEXT\n"
                      "// sample.")
    return f"""// GENERATED by scripts/mem_lean_diffsim.py -- do not edit.
// sample={sample}, P={p}
{phase_note}
module tb;
  reg clk;
{decl}{odecl}  integer f, i;
  {top} dut({conn});
  initial clk = 1'b0;
  always #5 clk = ~clk;
  initial begin
    // Match the Lean model's zeroState; otherwise the RTL starts at X and the
    // comparison is against noise.
    //
    // AFTER a delta, not at t=0: `clk` is X until `initial clk = 1'b0` runs,
    // and that X->0 transition IS a negedge, so every negedge-triggered
    // element fires once at time zero with X inputs and overwrites anything
    // initialised at t=0. (Observed: `nq` came out X despite being zeroed.)
    #1;
    // Driven inputs start at 0 as well. Without this the pre-loop
    // `@(negedge clk)` fires every negedge element while the inputs are still
    // X -- `nq <= nd` captured X one edge before the first stimulus row was
    // ever applied, and the first observation reported X for a DUT whose
    // state had been zeroed correctly.
{zero_in}{init}  end
  initial begin
    f = $fopen("{stim}", "r");
    if (f == 0) begin $display("TB-FATAL: no stimulus"); $finish; end
    @(negedge clk);
    for (i = 0; i < {nper}; i = i + 1) begin
      #1;                      // off the edge: do not race the negedge NBAs
{read}{sample_body}
    end
    $finish;
  end
endmodule
"""


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cert", required=True)
    ap.add_argument("--sidecar", required=True)
    ap.add_argument("--rtl", required=True)
    ap.add_argument("--top", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--scenario", required=True, help="JSON list of {name: value} per source period")
    ap.add_argument("--p", type=int, default=2)
    ap.add_argument("--sample", default="after-first-edge",
                    choices=("after-first-edge", "before-first-edge"),
                    help="which edge the RTL observation sits after; see build_tb")
    ap.add_argument("--rtl-init", default="",
                    help="file holding Verilog that zeroes the DUT state, to match zeroState")
    ap.add_argument("--lean-dir", default="formal/lean")
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)

    io = json.load(open(a.sidecar))
    arity = io["runtime_input_arity"]
    by_name = {i["name"]: i for i in io["inputs"]}
    ins = [(i["name"], i["width"]) for i in sorted(io["inputs"], key=lambda i: i["ordinal"])]
    outs = [(o["name"], o["width"]) for o in sorted(io["outputs"], key=lambda o: o["ordinal"])]
    widths = [0] * arity
    for i in io["inputs"]:
        if i["ordinal"] < arity:
            widths[i["ordinal"]] = i["width"]

    periods = json.load(open(a.scenario))
    unknown = {k for p in periods for k in p} - set(by_name)
    if unknown:
        sys.exit(f"FATAL: scenario names not in the sidecar: {sorted(unknown)}")

    # --- Lean stimulus: P identical rows per source period, padded to arity --
    lean_stim = os.path.join(a.out, "stim_lean.txt")
    with open(lean_stim, "w") as fh:
        for per in periods:
            row = [0] * arity
            for nm, v in per.items():
                o = by_name[nm]["ordinal"]
                if o < arity:
                    row[o] = v
            for _ in range(a.p):
                fh.write(" ".join(str(v) for v in row) + "\n")

    # --- the SAME values for iverilog, by name ------------------------------
    rtl_stim = os.path.join(a.out, "stim_rtl.txt")
    drive = [n for n, _ in ins if n != "clk"]
    with open(rtl_stim, "w") as fh:
        for per in periods:
            fh.write(" ".join(str(per.get(n, 0)) for n in drive) + "\n")

    # --- Lean side ----------------------------------------------------------
    probe = os.path.join(a.out, "probe.lean")
    open(probe, "w").write(build_probe(open(a.cert).read(), a.top,
                                       widths, os.path.abspath(lean_stim), a.p))
    lp = os.path.join(a.out, "lean.out")
    try:
        with open(lp, "w") as fh:
            rc = subprocess.run(["lake", "env", "lean", os.path.abspath(probe)],
                                stdout=fh, stderr=subprocess.STDOUT, cwd=a.lean_dir).returncode
    except (FileNotFoundError, NotADirectoryError) as e:
        # Diagnosed, not a traceback: the Lean project or toolchain is simply
        # not reachable from here (this is what happens inside a bazel
        # sandbox, where formal/lean is not a runfile).
        print(f"BEHAVIORAL_DIFF_FAIL cannot run the Lean toolchain in {a.lean_dir}: {e}")
        return 2
    lean_rows = [l.split() for l in open(lp) if re.fullmatch(r"[\d ]+\n", l)]
    if rc != 0 or not lean_rows:
        print(f"LEAN-FAILED rc={rc}; see {lp}")
        return 2

    # --- iverilog side ------------------------------------------------------
    if shutil.which("iverilog") is None:
        print("BEHAVIORAL_DIFF_SKIPPED no-iverilog")
        return 3
    tb = os.path.join(a.out, "tb.v")
    init = open(a.rtl_init).read() if a.rtl_init else ""
    open(tb, "w").write(build_tb(a.top, ins, outs, len(periods), os.path.abspath(rtl_stim), a.p, init,
                                 a.sample))
    # ABSOLUTE: the simulation runs with cwd=a.out, which would otherwise
    # re-resolve this relative path against that same directory.
    exe = os.path.abspath(os.path.join(a.out, "sim"))
    bl = os.path.join(a.out, "iverilog_build.log")
    with open(bl, "w") as fh:
        if subprocess.run(["iverilog", "-g2012", "-o", exe, tb, a.rtl],
                          stdout=fh, stderr=subprocess.STDOUT).returncode != 0:
            # ONLY A MISSING TOOL MAY SKIP. An installed iverilog that cannot
            # elaborate the design is a FAILURE: calling it "skipped" let the
            # wrapper report a structural pass while the behavioural gate had
            # silently collapsed.
            print(f"BEHAVIORAL_DIFF_FAIL iverilog could not elaborate the design; see {bl}")
            for line in open(bl).read().splitlines()[:4]:
                print("  " + line)
            return 1
    rp = os.path.join(a.out, "rtl.out")
    with open(rp, "w") as fh:
        src = subprocess.run([exe], stdout=fh, stderr=subprocess.STDOUT, cwd=a.out).returncode
    if src != 0:
        print(f"BEHAVIORAL_DIFF_FAIL the RTL simulation exited {src}; see {rp}")
        return 1
    raw_rtl = [l for l in open(rp) if re.fullmatch(r"[\dxzXZ ]+\n", l)]
    if any(re.search(r"[xzXZ]", l) for l in raw_rtl):
        # Filtering X rows out would have silently shortened the trace and
        # reported a length mismatch instead of the real problem.
        print(f"BEHAVIORAL_DIFF_FAIL the RTL trace contains X/Z, so DUT state was not "
              f"initialised to match zeroState (see {rp})")
        for l in [x for x in raw_rtl if re.search(r"[xzXZ]", x)][:3]:
            print("  " + l.rstrip())
        return 1
    rtl_rows = [l.split() for l in raw_rtl]

    # --- compare ------------------------------------------------------------
    names = [n for n, _ in outs]
    n = min(len(rtl_rows), len(lean_rows))
    if n != len(periods):
        print(f"BEHAVIORAL_DIFF_FAIL expected {len(periods)} observations, "
              f"rtl={len(rtl_rows)} lean={len(lean_rows)}")
        return 1
    bad = []
    for k in range(n):
        if rtl_rows[k] != lean_rows[k]:
            bad.append((k, rtl_rows[k], lean_rows[k]))
    if bad:
        print(f"BEHAVIORAL_DIFF_FAIL {len(bad)}/{n} period(s) differ  (traces: {rp}, {lp})")
        for k, r, l in bad[:6]:
            diff = [f"{names[j]}: rtl={r[j]} lean={l[j]}"
                    for j in range(min(len(r), len(l), len(names))) if r[j] != l[j]]
            print(f"  period {k}: " + "; ".join(diff))
        return 1
    print(f"BEHAVIORAL_DIFF_PASS {n} period(s), P={a.p}, sample={a.sample}, "
          f"outputs {','.join(names)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
