#!/usr/bin/env python3
"""Fast R5 check: a per-port-clock Memory keeps all three clock bindings."""

import argparse
import glob
import os
import subprocess
import sys
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("-i", "--input", required=True)
    args = parser.parse_args()

    lhd = (
        "./bazel-bin/lhd/lhd" if os.path.exists("./bazel-bin/lhd/lhd") else "./lhd/lhd"
    )
    with tempfile.TemporaryDirectory(prefix="lhd_multiclock_mem_") as scratch:
        out_dir = os.path.join(scratch, "verilog")
        work_dir = os.path.join(scratch, "work")
        run = subprocess.run(
            [
                lhd,
                "compile",
                args.input,
                "--recipe",
                "O0",
                "--emit-dir",
                "verilog:" + out_dir + "/",
                "--workdir",
                work_dir,
            ],
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
        )
        if run.returncode != 0:
            print(run.stdout)
            return 1

        files = glob.glob(os.path.join(out_dir, "*.v"))
        if len(files) != 1:
            print("expected one emitted Verilog unit, got {}".format(files))
            return 1
        with open(files[0], encoding="utf-8") as stream:
            text = stream.read()

    required = (
        "module cgen_memory_multiclock_2rd_1wr",
        ".wr_clock_0(wclk)",
        ".rd_clock_0(r0clk)",
        ".rd_clock_1(r1clk)",
    )
    missing = [item for item in required if item not in text]
    if missing:
        print("missing multiclock memory bindings: {}".format(", ".join(missing)))
        return 1
    print("memory_multiclock_ports - success")
    return 0


if __name__ == "__main__":
    sys.exit(main())
