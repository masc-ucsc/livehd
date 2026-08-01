#!/usr/bin/env python3
"""Fast pass.prp_writer multi-module recompile regression (no LEC)."""

import argparse
import pathlib
import shutil
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("-i", "--input", required=True)
    args = parser.parse_args()

    lhd = pathlib.Path("bazel-bin/lhd/lhd")
    if not lhd.exists():
        lhd = pathlib.Path("lhd/lhd")
    work = pathlib.Path("tmp_p2p_recompile")
    shutil.rmtree(work, ignore_errors=True)
    out = work / "prp"
    out.mkdir(parents=True)

    emit = subprocess.run(
        [
            str(lhd),
            "compile",
            args.input,
            "--set",
            "compile.upass.inline=true",
            "--emit-dir",
            "pyrope:" + str(out) + "/",
            "--workdir",
            str(work / "emit"),
        ],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    if emit.returncode:
        sys.stdout.buffer.write(emit.stdout)
        return emit.returncode

    top = out / "sibling_recompile.top.prp"
    text = top.read_text()
    expected = 'const inc = import("sibling_recompile.inc.inc")'
    if expected not in text or "inc(" not in text:
        print("emitted top lacks sibling import or bare callee:\n" + text)
        return 1

    recomp = subprocess.run(
        [str(lhd), "compile", str(top), "--workdir", str(work / "recompile")],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    if recomp.returncode:
        sys.stdout.buffer.write(recomp.stdout)
        return recomp.returncode
    print("sibling_recompile - p2p recompile - success")
    return 0


if __name__ == "__main__":
    sys.exit(main())
