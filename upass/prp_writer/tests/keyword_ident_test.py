#!/usr/bin/env python3
"""Regression: emitted Pyrope escapes identifiers that collide with keywords.

A Verilog signal is free to be named for any Pyrope reserved word.  The writer
must backtick-escape those names, or its own output stops re-parsing and the
recompile / LEC leg of the flow dies on it.  The escape set used to be a
hand-kept copy of the parser's keyword table and had drifted 13 words behind it;
`stage` was the expensive miss -- a bare `stage[0] = a` re-lexes as a `stage[N]`
pipelining declaration, so the file fails with "expected an expression".

Closes the loop: Verilog -> Pyrope -> re-compile the emitted Pyrope.

    python3 keyword_ident_test.py -i keyword_ident.v
"""

import argparse
import os
import shutil
import subprocess
import sys

# Names in the fixture that are Pyrope keywords and so must come back quoted.
RESERVED = ["stage", "tick", "formal"]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-i", "--input", required=True, help="fixture .v file")
    args = ap.parse_args()

    lhd = "./bazel-bin/lhd/lhd" if os.path.exists("./bazel-bin/lhd/lhd") else "./lhd/lhd"
    if not os.path.exists(lhd):
        print("missing lhd binary")
        return 3

    work = "tmp_keyword_ident"
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
    for name in RESERVED:
        if "`" + name + "`" not in text:
            print("FAILED: reserved word '{}' not backtick-escaped in the emit".format(name))
            ok = False

    # 2. the emitted Pyrope must re-parse -- the point of the escaping
    recomp = subprocess.run(
        [lhd, "compile", top, "--workdir", os.path.join(work, "w2")],
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if recomp.returncode != 0:
        print("FAILED: emitted Pyrope does not re-compile rc={}".format(recomp.returncode))
        print(recomp.stdout.decode("utf-8", "ignore"))
        ok = False

    if not ok:
        print("--- emitted top.prp ---")
        print(text)
        return 1

    print("PASS: reserved-word identifiers escaped and the emit re-compiles")
    shutil.rmtree(work, ignore_errors=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
