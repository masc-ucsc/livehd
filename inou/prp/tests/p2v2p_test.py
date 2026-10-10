#!/usr/bin/env python3
"""Pyrope -> Verilog -> Pyrope round-trip gate (the random-fuzz direction).

    lhd compile foo.prp --emit-dir verilog:V/                      # cgen Verilog
    lhd lec --impl V/foo.v --ref foo.prp                           # 1. P -> V (lg: if several)
    lhd compile verilog V/*.v --emit-dir pyrope:P2/                # slang -> prp writer
    lhd lec --impl pyrope:P2/ --ref foo.prp                        # 2. P -> V -> P

Both must be PROVEN (a timeout or refusal fails). The fixtures under
tests/p2v2p/ are minimized cases from the random Pyrope round-trip fuzzer: each
one was a miscompile in the Verilog writer or in width inference, so the first
check is the one that guards them; the second keeps the whole round trip honest.
The top is the fixture's file name unless a `:top:` header names another.

  python3 inou/prp/tests/p2v2p_test.py -i inou/prp/tests/p2v2p/sum_signed_const_div.prp
"""

import argparse
import glob
import os
import re
import shutil
import subprocess
import sys

from lec import run_lec, verdict

CHECK_TIMEOUT = 20  # internal seconds; the external watchdog allows twice this


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-i", "--input", required=True, help="Pyrope source .prp")
    args = ap.parse_args()

    lhd = "./bazel-bin/lhd/lhd" if os.path.exists("./bazel-bin/lhd/lhd") else "./lhd/lhd"
    if not os.path.exists(lhd):
        print("missing lhd binary")
        return 3

    prp = args.input
    name = os.path.splitext(os.path.basename(prp))[0]
    with open(prp) as f:
        m = re.search(r"^:top:\s*(\S+)", f.read(), re.M)
    top = m.group(1) if m else name

    work = "tmp_p2v2p_" + re.sub(r"\W+", "_", name)
    shutil.rmtree(work, ignore_errors=True)
    vdir = os.path.join(work, "v")
    pdir = os.path.join(work, "p2")
    os.makedirs(vdir, exist_ok=True)
    os.makedirs(pdir, exist_ok=True)

    comp = subprocess.run([lhd, "compile", prp, "--top", top, "--emit-dir", "verilog:" + vdir + "/",
                           "--workdir", os.path.join(work, "w1")], stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if comp.returncode != 0:
        print("{} - p2v2p - FAILED: prp->verilog rc={}".format(name, comp.returncode))
        print(comp.stdout.decode("utf-8", "ignore"))
        return 1
    vs = sorted(glob.glob(os.path.join(vdir, "*.v")))
    # A lec side is ONE source: a multi-module emission is elaborated to lg: first.
    vside = vs[0] if len(vs) == 1 else "lg:" + os.path.join(work, "vlg")
    if len(vs) > 1:
        comp = subprocess.run([lhd, "compile", "verilog"] + vs + ["--top", top, "--emit-dir", vside + "/",
                               "--workdir", os.path.join(work, "w_vlg")], stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        if comp.returncode != 0:
            print("{} - p2v2p - FAILED: verilog->lg rc={}".format(name, comp.returncode))
            print(comp.stdout.decode("utf-8", "ignore"))
            return 1

    chk = run_lec([lhd, "lec", "--impl", vside, "--ref", prp, "--top", top,
                  "--workdir", os.path.join(work, "w_v")], timeout=CHECK_TIMEOUT)
    if verdict(chk) != "proven":
        print("{} - p2v2p - FAILED: emitted Verilog not equivalent to the Pyrope source".format(name))
        print(chk.stdout.decode("utf-8", "ignore"))
        return 1

    comp = subprocess.run([lhd, "compile", "verilog"] + vs + ["--top", top, "--emit-dir", "pyrope:" + pdir + "/",
                           "--set", "compile.slang.flat_top_io=true", "--workdir", os.path.join(work, "w2")],
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if comp.returncode != 0:
        print("{} - p2v2p - FAILED: verilog->prp rc={}".format(name, comp.returncode))
        print(comp.stdout.decode("utf-8", "ignore"))
        return 1
    chk = run_lec([lhd, "lec", "--impl", "pyrope:" + pdir + "/", "--ref", prp, "--top", top,
                  "--workdir", os.path.join(work, "w_p")], timeout=CHECK_TIMEOUT)
    if verdict(chk) != "proven":
        print("{} - p2v2p - FAILED: regenerated Pyrope not equivalent to the source".format(name))
        print(chk.stdout.decode("utf-8", "ignore"))
        return 1
    print("{} - p2v2p - success (top:{})".format(name, top))
    return 0


if __name__ == "__main__":
    sys.exit(main())
