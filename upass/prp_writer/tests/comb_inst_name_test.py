#!/usr/bin/env python3
"""Regression: a stateless multi-output COMB instance keeps its hierarchical name.

A multi-output `comb` is emitted by the writer as a whole-bind
`mut fwd = fwdunit::[name=fwd](args)` with `fwd.port` output reads (the old
destructure form `(t = C.p, …) = C(args)` silently dropped every output
binding once `hdl` units stopped being runner-inlined — the INT2FP
fracRounded miscompile). This test checks that the call-site instance name
survives: the `::[name=…]` annotation is emitted so the Sub keeps the source
instance name (not a synthesised `u_fwdunit_<tmp>`) when re-compiled with
`upass.inline=false`.

The loop is closed by re-compiling the emitted Pyrope with inline=false and
confirming the hierarchy tree shows `fwd : fwdunit`.

    python3 comb_inst_name_test.py -i comb_inst_name.v
"""

import argparse
import os
import shutil
import subprocess
import sys


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-i", "--input", required=True, help="fixture .v file")
    args = ap.parse_args()

    lhd = "./bazel-bin/lhd/lhd" if os.path.exists("./bazel-bin/lhd/lhd") else "./lhd/lhd"
    if not os.path.exists(lhd):
        print("missing lhd binary")
        return 3

    work = "tmp_comb_inst_name"
    shutil.rmtree(work, ignore_errors=True)
    prp_dir = os.path.join(work, "prp")
    os.makedirs(prp_dir, exist_ok=True)

    # 1. slang -> pyrope
    comp = subprocess.run(
        [lhd, "compile", "--top", "top", "--reader", "slang", args.input,
         "--emit-dir", "pyrope:" + prp_dir + "/", "--workdir", os.path.join(work, "w1")],
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if comp.returncode != 0:
        print("FAILED: slang->pyrope rc={}".format(comp.returncode))
        print(comp.stdout.decode("utf-8", "ignore"))
        return 1

    top = os.path.join(prp_dir, "top.prp")
    if not os.path.exists(top):
        print("FAILED: no top.prp emitted")
        return 1
    text = open(top).read()

    ok = True

    def need(cond, msg):
        nonlocal ok
        if not cond:
            ok = False
            print("FAILED: " + msg)

    # The stateless multi-output comb must carry the call-site instance name on
    # the whole-bind callee (`mut fwd = fwdunit::[name=fwd](…)`).
    need("= fwdunit::[name=fwd](" in text,
         "call `= fwdunit::[name=fwd](` missing — comb instance lost its name")

    # 2. re-compile the emitted pyrope (inline=false keeps combs as Sub instances)
    prp_files = sorted(
        os.path.join(prp_dir, f) for f in os.listdir(prp_dir) if f.endswith(".prp"))
    lg_dir = os.path.join(work, "lg")
    rec = subprocess.run(
        [lhd, "compile", "--set", "compile.upass.inline=false", "--top", "top",
         "--emit-dir", "lg:" + lg_dir] + prp_files + ["--workdir", os.path.join(work, "w2")],
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if rec.returncode != 0:
        print("FAILED: pyrope re-compile rc={} (parser must accept the named whole-bind)".format(rec.returncode))
        print(rec.stdout.decode("utf-8", "ignore"))
        print("--- emitted top.prp ---")
        print(text)
        return 1

    # 3. the re-compiled hierarchy must show the named instance
    tree = subprocess.run([lhd, "tool", "tree", "lg:" + lg_dir],
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    tree_txt = tree.stdout.decode("utf-8", "ignore")
    need("fwd" in tree_txt and "fwdunit" in tree_txt,
         "re-compiled tree missing `fwd : fwdunit` instance:\n" + tree_txt)

    if ok:
        print("comb_inst_name - PASSED")
        return 0
    print("--- emitted top.prp ---")
    print(text)
    return 1


if __name__ == "__main__":
    sys.exit(main())
