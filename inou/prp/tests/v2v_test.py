#!/usr/bin/env python3
"""Verilog ROUND-TRIP gated test: a golden `.v` is compiled by lhd (native
slang reader, default O1 recipe) and re-emitted as Verilog, and
inou/yosys/lgcheck proves the emitted netlist equivalent to the ORIGINAL `.v`.

    lhd compile foo.v --emit-dir verilog:DIR/ --workdir W      # slang -> lg -> cgen
    lgcheck --reference foo.v --implementation DIR/all.v \
            --reference_top <vtop> --implementation_top <flat vtop>

Coverage this adds over the sibling harnesses: prp-v2prp-* LECs the emitted
Pyrope against the hand-written .prp with the in-process cvc5 engine — LiveHD
reads BOTH sides, so a slang-reader misread of the .v is invisible to it.
run_equiv_slang compares two cgen-emitted netlists — same blindness. Here the
reference side is the original `.v` read INDEPENDENTLY by yosys read_verilog
(or yosys-slang via the sibling .prp's `:gold_reader: slang` header), so a
misread OR a lowering miscompile that reaches the emitted netlist refutes the
miter. This is the harness that caught the clocked/comb array-store gaps
(see lhdsuite array_problem.md).

Per-side tops: the reference top is the sibling .prp's `:verilog_top:` header
(the golden module name) or the first module declared in the .v. The
implementation top is the same name FLATTENED to its entity (cgen emits flat
module names: `file.entity` -> `entity`).

Gate semantics (same policy as v2prp_test.py):
  lgcheck exit 0 -> pass
  lgcheck exit 1 -> FAIL (bounded counterexample or a hard yosys error: the
                    designs are not equivalent, or cannot even be compared)
  lgcheck exit 2 -> INCONCLUSIVE: pass with an honest note (no proof, but no
                    counterexample either)
  wall-clock timeout -> inconclusive, NOT a fail

  python3 inou/prp/tests/v2v_test.py -i inou/prp/tests/equiv/trivial_if.v
"""

import argparse
import glob
import os
import re
import shutil
import subprocess
import sys

CHECK_TIMEOUT = 240  # seconds for the whole lgcheck cascade; expiry is inconclusive


def _header(prp_path, key):
    """Return the `:key: value` header field from the sibling Pyrope, or None."""
    try:
        with open(prp_path) as f:
            m = re.search(r"^:%s:\s*([^\s*]+)" % re.escape(key), f.read(), re.M)
            return m.group(1).strip() if m else None
    except OSError:
        return None


def _modules(vpath):
    try:
        with open(vpath) as f:
            return re.findall(r"\bmodule\s+\\?([^\s(]+)", f.read())
    except OSError:
        return []


def _slang_plugin():
    # yosys-slang plugin for `:gold_reader: slang` goldens (same probes as
    # prplib._yosys_slang_plugin; cwd is the runfiles _main dir under bazel,
    # the repo root on manual runs).
    for cand in ("../+http_archive+yosys_slang/slang.so",
                 "../+_repo_rules+yosys_slang/slang.so",
                 "bazel-bin/external/+http_archive+yosys_slang/slang.so",
                 "bazel-bin/external/+_repo_rules+yosys_slang/slang.so"):
        path = os.path.normpath(cand)
        if os.path.exists(path):
            return path
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-i", "--input", required=True, help="golden .v file")
    args = ap.parse_args()

    lhd = "./bazel-bin/lhd/lhd" if os.path.exists("./bazel-bin/lhd/lhd") else "./lhd/lhd"
    if not os.path.exists(lhd):
        print("missing lhd binary")
        return 3

    v = args.input
    name = os.path.splitext(os.path.basename(v))[0]
    ref_prp = os.path.join(os.path.dirname(v), name + ".prp")

    vtop = _header(ref_prp, "verilog_top")
    if not vtop:
        mods = _modules(v)
        if not mods:
            print("{} - v2v - FAILED: no module declared in {}".format(name, v))
            return 1
        vtop = mods[0]
    # cgen emits FLAT module names (`file.entity` graph -> `entity` module).
    impl_top = vtop.rsplit(".", 1)[-1]

    work = "tmp_v2v_" + re.sub(r"\W+", "_", name)
    shutil.rmtree(work, ignore_errors=True)
    odir = os.path.join(work, "v")
    os.makedirs(odir, exist_ok=True)

    # 1. Verilog -> LGraph -> Verilog (native slang reader, default recipe).
    # `flat_top_io`: a struct is a BUNDLE everywhere inside LiveHD, but this
    # check miters the emitted netlist against the ORIGINAL .v, so the emitted
    # TOP interface must be the source module's packed port list. Only the top
    # needs it — yosys' miter compares the top, and submodule interfaces are
    # internal to each netlist (measured: a pair whose internal child differs
    # bus-vs-leaf still proves).
    comp = subprocess.run(
        [lhd, "compile", v, "--emit-dir", "verilog:" + odir + "/",
         "--set", "compile.slang.flat_top_io=true",
         "--workdir", os.path.join(work, "w")],
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if comp.returncode != 0:
        print("{} - v2v - FAILED: slang->verilog rc={}".format(name, comp.returncode))
        print(comp.stdout.decode("utf-8", "ignore"))
        return 1

    gen_vs = sorted(glob.glob(os.path.join(odir, "*.v")))
    if not gen_vs:
        print("{} - v2v - FAILED: no verilog emitted in {}".format(name, odir))
        return 1
    impl = os.path.join(work, "all_impl.v")
    with open(impl, "w") as out:
        for g in gen_vs:
            with open(g) as f:
                out.write(f.read())
                out.write("\n")

    # 2. lgcheck: reference = the ORIGINAL .v (independent yosys read).
    cmd = ["./inou/yosys/lgcheck", "--reference", v, "--implementation", impl,
           "--reference_top", vtop, "--implementation_top", impl_top]
    if (_header(ref_prp, "gold_reader") or "") == "slang":
        plugin = _slang_plugin()
        if not plugin:
            print("{} - v2v - FAILED: :gold_reader: slang but yosys-slang plugin not found".format(name))
            return 1
        cmd += ["--gold_reader", "slang", "--slang_plugin", plugin]
    try:
        chk = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                             timeout=CHECK_TIMEOUT)
    except subprocess.TimeoutExpired:
        print("{} - v2v - inconclusive (lgcheck timeout >{}s, NOT a fail)".format(name, CHECK_TIMEOUT))
        return 0

    out = chk.stdout.decode("utf-8", "ignore")
    if chk.returncode == 0:
        print("{} - v2v - success (ref_top:{} impl_top:{})".format(name, vtop, impl_top))
        return 0
    if chk.returncode == 2:
        # lgcheck's explicit INCONCLUSIVE: no proof but NO counterexample.
        print("{} - v2v - inconclusive (no proof, no counterexample; ref_top:{} impl_top:{})".format(
            name, vtop, impl_top))
        return 0
    print("{} - v2v - FAILED: round-trip not equivalent (ref_top:{} impl_top:{})".format(
        name, vtop, impl_top))
    print(out)
    return 1


if __name__ == "__main__":
    sys.exit(main())
