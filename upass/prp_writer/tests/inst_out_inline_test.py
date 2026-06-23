#!/usr/bin/env python3
"""Regression: the v->prp writer inlines a submodule's output-port reads.

A multi-output instance's outputs are read AFTER the instance (backward-only),
so the writer must:
  * read each output inline as `u_sub.<port>` (dot field access),
  * NOT emit `u_sub["<port>"]` bracket-string reads, and
  * NOT keep a per-output extraction temp / `wire` for the inlined outputs.

This exercises only the slang->Pyrope emit (no LEC), so it is unaffected by the
pre-existing multi-module library-read gaps.

    python3 inst_out_inline_test.py -i inst_out_inline.v
"""

import argparse
import glob
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

    work = "tmp_inst_out_inline"
    shutil.rmtree(work, ignore_errors=True)
    prp_dir = os.path.join(work, "prp")
    os.makedirs(prp_dir, exist_ok=True)

    comp = subprocess.run(
        [lhd, "compile", "--top", "top", "--reader", "slang", args.input,
         "--emit-dir", "pyrope:" + prp_dir + "/", "--workdir", os.path.join(work, "w")],
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if comp.returncode != 0:
        print("FAILED: slang->pyrope rc={}".format(comp.returncode))
        print(comp.stdout.decode("utf-8", "ignore"))
        return 1

    top = os.path.join(prp_dir, "top.prp")
    if not os.path.exists(top):
        print("FAILED: no top.prp emitted; got {}".format(glob.glob(prp_dir + "/*.prp")))
        return 1
    text = open(top).read()

    ok = True

    def need(cond, msg):
        nonlocal ok
        if not cond:
            ok = False
            print("FAILED: " + msg)

    # The instance call must survive, carrying the call-site instance name so the
    # Sub keeps the bound variable's hierarchical name (`submod` is stateful).
    need("u_sub = submod::[name=u_sub](" in text, "instance call `u_sub = submod::[name=u_sub](` missing")
    # Both outputs read inline with dot notation.
    need("u_sub.s" in text, "expected inlined `u_sub.s`")
    need("u_sub.d" in text, "expected inlined `u_sub.d`")
    # No bracket-string extraction reads of the instance outputs.
    need('u_sub["s"]' not in text and 'u_sub["d"]' not in text,
         "bracket-string extraction `u_sub[\"...\"]` should have been inlined")
    # No leftover per-output extraction temp / wire for the inlined ports.
    need("_u_sub_s" not in text and "_u_sub_d" not in text,
         "per-output extraction temp `_u_sub_*` should have been dropped")

    if ok:
        print("inst_out_inline - PASSED")
        print(text)
        return 0
    print("--- emitted top.prp ---")
    print(text)
    return 1


if __name__ == "__main__":
    sys.exit(main())
