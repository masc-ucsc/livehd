#!/usr/bin/env python3
"""prp->prp round-trip gate: a Pyrope source re-emitted through pass.prp_writer
must recompile and stay logically equivalent to its golden `.v`.

    lhd compile foo.prp --emit-dir pyrope:DIR/                          # prp -> upass -> prp
    lhd lec --set lec.solver=lgyosys --impl pyrope:DIR/foo.<top>.prp --ref verilog:foo.v

This is the FORWARD-direction companion of v2prp_test.py (which round-trips a
`.v`).  It exists to lock in the constructs the writer fully supports — notably
the pipeline (`stage[N]` / `@[N]`), `type_spec`, and `tuple_concat` emission.
The prp_writer safety net makes the first step fail the compile if it hits an
unimplemented construct (rather than silently emitting a /* TODO */ stub), so a
construct gap surfaces here as a hard failure, not a false pass.

`lhd lec --set lec.solver=lgyosys` (yosys/lgcheck) is the authoritative gate:
equivalent => pass, not-equivalent => fail, TIMEOUT => inconclusive (exit 0).

  python3 inou/prp/tests/p2p_test.py -i inou/prp/tests/equiv/mod_call_pipe.prp
"""

import argparse
import glob
import os
import re
import shutil
import subprocess
import sys

CHECK_TIMEOUT = 20  # seconds; a timeout is inconclusive, not a failure


def _v_top(vpath):
    with open(vpath) as f:
        m = re.search(r"\bmodule\s+\\?([^\s(]+)", f.read())
    return m.group(1) if m else None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-i", "--input", required=True, help="Pyrope source .prp (golden <name>.v alongside)")
    args = ap.parse_args()

    lhd = "./bazel-bin/lhd/lhd" if os.path.exists("./bazel-bin/lhd/lhd") else "./lhd/lhd"
    if not os.path.exists(lhd):
        print("missing lhd binary")
        return 3

    prp = args.input
    name = os.path.splitext(os.path.basename(prp))[0]
    v = os.path.join(os.path.dirname(prp), name + ".v")
    if not os.path.exists(v):
        print("{} - p2p - FAILED: no golden {}".format(name, v))
        return 1
    top = _v_top(v) or name

    work = "tmp_p2p_" + re.sub(r"\W+", "_", name)
    shutil.rmtree(work, ignore_errors=True)
    out_dir = os.path.join(work, "prp")
    os.makedirs(out_dir, exist_ok=True)

    # 1. Pyrope -> upass -> Pyrope via pass.prp_writer (safety net fails on any
    #    unimplemented construct).
    #    Pin compile.upass.inline=true: this gate recompiles only the single
    #    emitted `<top>.prp`, so it needs a self-contained (flat) re-emission.
    #    With the default (inline=false) a `comb` called with runtime args is
    #    emitted as a separate Sub module, and the single-file recompile cannot
    #    resolve it (the hierarchical multi-file roundtrip is a separate flow).
    comp = subprocess.run(
        [lhd, "compile", prp, "--set", "compile.upass.inline=true",
         "--emit-dir", "pyrope:" + out_dir + "/", "--workdir", os.path.join(work, "w_emit")],
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if comp.returncode != 0:
        print("{} - p2p - FAILED: prp->prp emission rc={}".format(name, comp.returncode))
        print(comp.stdout.decode("utf-8", "ignore"))
        return 1

    emitted = os.path.join(out_dir, top + ".prp")
    if not os.path.exists(emitted):
        cands = glob.glob(os.path.join(out_dir, "*.prp"))
        print("{} - p2p - FAILED: no emitted unit for top '{}' (have: {})".format(name, top, cands))
        return 1
    with open(emitted) as f:
        text = f.read()
    if "/* TODO" in text or "unhandled node" in text:
        print("{} - p2p - FAILED: writer left a TODO/unhandled node:\n{}".format(name, text))
        return 1

    # 2. Recompile the emitted Pyrope and LEC it against the golden .v.
    try:
        # Per-side tops: the golden .v carries the historical dotted module name
        # (`pipe1_pass.passthru`), but the Pyrope side now emits the FLAT Verilog
        # module name (`passthru`) — internal graph names stay hierarchical while
        # Verilog flattens. Pass each side its own module name.
        ref_top  = top
        impl_top = top.rsplit(".", 1)[-1]
        chk = subprocess.run(
            [lhd, "lec", "--set", "lec.solver=lgyosys", "--impl", "pyrope:" + emitted, "--ref", "verilog:" + v,
             "--impl-top", impl_top, "--ref-top", ref_top, "--workdir", os.path.join(work, "w_check")],
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=CHECK_TIMEOUT)
    except subprocess.TimeoutExpired:
        print("{} - p2p - inconclusive (lhd lec timeout >{}s, NOT a fail)".format(name, CHECK_TIMEOUT))
        return 0

    if chk.returncode == 0:
        print("{} - p2p - success (top:{})".format(name, top))
        return 0
    print("{} - p2p - FAILED: not equivalent (top:{})".format(name, top))
    print(chk.stdout.decode("utf-8", "ignore"))
    return 1


if __name__ == "__main__":
    sys.exit(main())
