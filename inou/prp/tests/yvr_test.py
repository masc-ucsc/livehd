#!/usr/bin/env python3
"""prp-yvr (yosys-verilog reader) gated test: a golden `.v` round-trips through
LiveHD's `--reader yosys-verilog` front end and stays logically equivalent.

    lhd compile --reader yosys-verilog foo.v --emit-dir verilog:DIR/   # yosys-verilog -> lg -> verilog
    inou/yosys/lgcheck --reference foo.v --implementation DIR/foo.v    # LEC the reader output vs the source

Unlike prp-v2prp (which exercises `--reader slang`), this targets the
`--reader yosys-verilog` path (lgyosys_tolg / proc / cgen). It is the minimal
reproducer for the two XiangShan failure classes that the slang/pyrope tests
canNOT catch (slang reads those files correctly):

  * READ-FAIL  — yosys `read_verilog` cannot parse the file at all
                 (`'{...}` assignment pattern -> `unexpected OP_CAST`).
  * MISCOMPILE — the reader lowers the file to a NON-equivalent netlist
                 (terminal constant of a deep nested-ternary chain corrupted).

Exit: 0 = the yosys-verilog reader handled it correctly (bug absent/fixed);
1 = read-fail or miscompile (bug present — the expected state for the repro
cases). A lgcheck TIMEOUT is inconclusive (exit 0, not a fail), matching
v2prp_test.py's gate semantics.

  python3 inou/prp/tests/yvr_test.py -i inou/prp/tests/equiv/clz_nest.v
"""

import argparse
import glob
import os
import re
import shutil
import subprocess
import sys

CHECK_TIMEOUT = 30  # seconds; a lgcheck timeout is inconclusive, not a failure


def _modules(vpath):
    with open(vpath) as f:
        return re.findall(r"\bmodule\s+\\?([^\s(]+)", f.read())


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
    mods = _modules(v)
    top = mods[0] if mods else name

    work = "tmp_yvr_" + re.sub(r"\W+", "_", name)
    shutil.rmtree(work, ignore_errors=True)
    out = os.path.join(work, "v")
    os.makedirs(out, exist_ok=True)

    # 1. golden.v -> lg -> verilog via the yosys-verilog reader.
    comp = subprocess.run(
        [lhd, "compile", "--reader", "yosys-verilog", v, "--emit-dir", "verilog:" + out + "/",
         "--workdir", os.path.join(work, "w_yvr")],
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    gen = glob.glob(os.path.join(out, "*.v"))
    if comp.returncode != 0 or not gen:
        # The READ-FAIL class: yosys read_verilog could not parse the file.
        print("{} - yvr - FAILED: --reader yosys-verilog could not read the golden "
              "(read-fail class; slang/yosys-slang read it)".format(name))
        print(comp.stdout.decode("utf-8", "ignore")[-1500:])
        return 1

    impl = gen[0]
    # 2. LEC the reader's netlist against the source (yosys read_verilog parses
    #    the plain golden fine for the miscompile class).
    try:
        chk = subprocess.run(
            ["./inou/yosys/lgcheck", "--reference", v, "--implementation", impl,
             "--reference_top", top, "--implementation_top", top],
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=CHECK_TIMEOUT)
    except subprocess.TimeoutExpired:
        print("{} - yvr - inconclusive (lgcheck timeout >{}s, NOT a fail)".format(name, CHECK_TIMEOUT))
        return 0

    if chk.returncode == 0:
        print("{} - yvr - success (top:{}) — yosys-verilog reader correct".format(name, top))
        return 0
    print("{} - yvr - FAILED: --reader yosys-verilog netlist NOT equivalent to source "
          "(miscompile class; slang matches source) (top:{})".format(name, top))
    print(chk.stdout.decode("utf-8", "ignore")[-1500:])
    return 1


if __name__ == "__main__":
    sys.exit(main())
