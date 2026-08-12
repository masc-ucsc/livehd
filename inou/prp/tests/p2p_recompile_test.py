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

    # One .prp per SOURCE FILE: both lambdas of sibling_recompile.prp land in
    # the one emitted sibling_recompile.prp, so the sibling call needs NO
    # import — it resolves lexically, spelled with the bare lambda name.
    top = out / "sibling_recompile.prp"
    text = top.read_text()
    if "mod inc(" not in text or "mod top(" not in text:
        print("emitted file lost one of the sibling lambdas:\n" + text)
        return 1
    if "inc(" not in text or "import(" in text:
        print("emitted top lacks the bare sibling callee, or imported a same-file one:\n" + text)
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
