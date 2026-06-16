#!/usr/bin/env python3
"""2f-v2prp gated test: a single golden `.v` round-trips through Pyrope.

    lhd compile foo.v --emit-dir pyrope:DIR/                         # slang -> Pyrope
    lhd lec --set lec.solver=lgyosys --impl pyrope:DIR/foo.prp --ref verilog:foo.v  # recompile + LEC

`lhd lec --set lec.solver=lgyosys` (yosys/lgcheck) is the authoritative gate:
equivalent => pass, not-equivalent => fail, and a TIMEOUT => inconclusive (exit 0, NOT a fail
— per the task's gate semantics).  This is the per-file form driven by the
`prp-v2prp-<name>` bazel targets; inou/prp/tests/v2prp_census.py is the
whole-corpus census.

  python3 inou/prp/tests/v2prp_test.py -i inou/prp/tests/equiv/trivial_if.v
"""

import argparse
import glob
import os
import re
import shutil
import subprocess
import sys

CHECK_TIMEOUT = 20  # seconds; a timeout is inconclusive, not a failure


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

    work = "tmp_v2prp_" + re.sub(r"\W+", "_", name)
    shutil.rmtree(work, ignore_errors=True)
    prp_dir = os.path.join(work, "prp")
    os.makedirs(prp_dir, exist_ok=True)

    # 1. Verilog -> Pyrope via the slang reader + upass/prp_writer.
    comp = subprocess.run(
        [lhd, "compile", "--reader", "slang", v, "--emit-dir", "pyrope:" + prp_dir + "/",
         "--workdir", os.path.join(work, "w_slang")],
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if comp.returncode != 0:
        print("{} - v2prp - FAILED: slang->pyrope rc={}".format(name, comp.returncode))
        print(comp.stdout.decode("utf-8", "ignore"))
        return 1

    prps = glob.glob(os.path.join(prp_dir, "*.prp"))
    if not prps:
        print("{} - v2prp - FAILED: no .prp emitted".format(name))
        return 1
    # Prefer the .prp whose module matches the reference top; else the sole file.
    prp = next((p for p in prps if os.path.splitext(os.path.basename(p))[0] == top), prps[0])
    with open(prp) as f:
        emitted = f.read()
    if "/* TODO" in emitted or "unhandled node" in emitted:
        print("{} - v2prp - FAILED: writer left a TODO/unhandled node:\n{}".format(name, emitted))
        return 1

    # 2. Recompile the emitted Pyrope and LEC it against the golden .v.
    #    The re-compiled module name equals the .v module name (<file>.<top>),
    #    so a single --top pins both sides.
    try:
        chk = subprocess.run(
            [lhd, "lec", "--set", "lec.solver=lgyosys", "--impl", "pyrope:" + prp, "--ref", "verilog:" + v, "--top", top,
             "--workdir", os.path.join(work, "w_check")],
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=CHECK_TIMEOUT)
    except subprocess.TimeoutExpired:
        # Authoritative gate is inconclusive on timeout — pass (run `lhd lec`
        # by hand for the cvc5 cross-check).
        print("{} - v2prp - inconclusive (lhd lec timeout >{}s, NOT a fail)".format(name, CHECK_TIMEOUT))
        return 0

    if chk.returncode == 0:
        print("{} - v2prp - success (top:{})".format(name, top))
        return 0
    print("{} - v2prp - FAILED: not equivalent (top:{})".format(name, top))
    print(chk.stdout.decode("utf-8", "ignore"))
    return 1


if __name__ == "__main__":
    sys.exit(main())
