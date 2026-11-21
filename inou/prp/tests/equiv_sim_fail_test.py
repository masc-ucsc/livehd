#!/usr/bin/env python3
"""Run the instance-output and combinational feedback regressions on native simulators."""
import argparse
import os
import subprocess
import tempfile
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--prp", required=True, type=Path)
parser.add_argument("--mode", required=True, choices=("lg-emit", "v2prp-sim"))
args = parser.parse_args()
rtl = args.prp.with_suffix(".v").resolve()
top = rtl.stem + "_top"
lhd = Path(os.environ.get("LHD", "lhd/lhd")).resolve()


def run(*argv):
    subprocess.run([lhd, *argv], check=True)


with tempfile.TemporaryDirectory(dir=os.environ.get("TEST_TMPDIR")) as tmp:
    work = Path(tmp)
    lg = work / "lg"
    pyrope = work / "pyrope"
    emit = ["--emit-dir", f"pyrope:{pyrope}"] if args.mode == "v2prp-sim" else []
    run("compile", rtl, "--reader", "slang", "--top", top, "--workdir", work / "compile",
        "--emit-dir", f"lg:{lg}", *emit)
    if args.mode == "lg-emit":
        run("pass", "lnast-llvm", f"lg:{lg}", "--top", top, "--emit-dir", f"sim:{work}/kernels")
    else:
        # Check every byte on both paths against the RTL specification. Each side
        # must independently preserve the packed isZero field and complemented count.
        for mode, entry, inputs in [("rtl", f"lg:{top}", [f"lg:{lg}"]),
                                    ("emitted", f"pyrope/{top}.{top}", [])]:
            tb = work / f"{mode}_tb.prp"
            tb.write_text(f'''const design = import("{entry}")
test check {{
  mut dut = design
  tick 256 {{
    dut.a = clock
    step
    const want = if clock == 0 {{ 8 }} elif clock >= 128 {{ 7 }} elif clock >= 64 {{ 6 }} elif clock >= 32 {{ 5 }} else {{ 0 }}
    assert(dut.r == want, "packed instance output")
  }}
}}
''')
            run("sim", *inputs, tb, "--workdir", work / mode)
