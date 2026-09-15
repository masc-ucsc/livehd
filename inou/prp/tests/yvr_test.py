#!/usr/bin/env python3
"""Cross-check the explicit Yosys importer against native Slang using default LEC.

Compile the golden through Yosys to an LGraph, then compare that graph and its
emitted Verilog with the source read by native Slang. Both checks must pass.
"""

import argparse
import glob
import os
import re
import shutil
import subprocess
import sys

CHECK_TIMEOUT = 30  # seconds; each native LEC comparison must complete


def _modules(vpath):
    with open(vpath) as f:
        # Anchored at the START OF A LINE: an unanchored `\bmodule\s+` also matches the
        # word inside a golden's own prose comment. See prplib.PrpRunner._verilog_modules.
        return re.findall(r"^\s*module\s+\\?([^\s(]+)", f.read(), re.M)


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
         "--emit-dir", "lg:" + os.path.join(work, "lg"), "--workdir", os.path.join(work, "w_yvr")],
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    gen = glob.glob(os.path.join(out, "*.v"))
    if comp.returncode != 0 or not gen:
        # The READ-FAIL class: yosys read_verilog could not parse the file.
        print("{} - yvr - FAILED: --reader yosys-verilog could not read the golden "
              "(read-fail class; slang/yosys-slang read it)".format(name))
        print(comp.stdout.decode("utf-8", "ignore")[-1500:])
        return 1

    impl_top = top.rsplit(".", 1)[-1] if "." in top else top
    impl = os.path.join(work, "all_impl.v")
    with open(impl, "w") as output:
        for path in sorted(gen):
            with open(path) as source:
                output.write(source.read() + "\n")
    for label, side, selected_top in [("graph", "lg:" + os.path.join(work, "lg"), top),
                                      ("verilog", impl, impl_top)]:
        try:
            chk = subprocess.run(
                [lhd, "lec", "--ref", v, "--impl", side,
                 "--ref-top", top, "--impl-top", selected_top,
                 "--workdir", os.path.join(work, "check_" + label)],
                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=CHECK_TIMEOUT)
        except subprocess.TimeoutExpired:
            print("{} - yvr - FAILED: {} LEC timed out".format(name, label))
            return 1
        if chk.returncode != 0:
            print("{} - yvr - FAILED: Yosys {} differs from native Slang".format(name, label))
            print(chk.stdout.decode("utf-8", "ignore"))
            return 1
    print("{} - yvr - success: Yosys graph and Verilog agree with native Slang".format(name))
    return 0


if __name__ == "__main__":
    sys.exit(main())
