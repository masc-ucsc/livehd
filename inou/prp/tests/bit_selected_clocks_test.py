#!/usr/bin/env python3
"""Check independent clock edges against explicit values, beyond LEC's state cuts."""
import os
import json
from pathlib import Path
import subprocess
import tempfile


def run(*args):
    subprocess.run(args, check=True, timeout=30, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)


def main():
    lhd = str(Path("bazel-bin/lhd/lhd") if Path("bazel-bin/lhd/lhd").exists() else Path("lhd/lhd"))
    tests = Path("inou/prp/tests")
    with tempfile.TemporaryDirectory(dir=os.environ.get("TEST_TMPDIR")) as scratch:
        work = Path(scratch)

        def simulate(source, label):
            exe = str(work / label)
            run("iverilog", "-g2012", "-s", "tb", "-o", exe, str(source), str(tests / "bit_selected_clocks_tb.v"))
            run("vvp", exe)

        for relative in ("equiv/bit_selected_clocks.v", "equiv/bit_selected_clocks_1.v", "bit_selected_clocks_vector.v"):
            source = tests / relative
            stem = source.stem
            simulate(source, stem + "_source")
            out = work / (stem + ".v")
            run(lhd, "compile", str(source), "--emit", "verilog:" + str(out), "--workdir", str(work / stem))
            simulate(out, stem + "_native")

        out = work / "reference.v"
        run(lhd, "compile", str(tests / "equiv/bit_selected_clocks.prp"),
            "--emit", "verilog:" + str(out), "--workdir", str(work / "reference"))
        simulate(out, "pyrope_reference")

        emitted = work / "emitted"
        run(lhd, "compile", str(tests / "equiv/bit_selected_clocks_1.v"),
            "--emit-dir", "pyrope:" + str(emitted), "--workdir", str(work / "writer"))
        run(lhd, "compile", str(emitted / "bit_selected_clocks.prp"),
            "--emit", "verilog:" + str(out), "--workdir", str(work / "roundtrip"))
        simulate(out, "pyrope_roundtrip")

        # Unsupported combinations must fail cleanly, never merge clocks or
        # silently turn a dynamically selected destination into one flop.
        for label, statements, code in (
            ("overlap", "always @(posedge clocks[0]) q[0] <= d; always @(posedge clocks[1]) q[0] <= d;", "overlapping-bit-clocks"),
            ("blocking", "always @(posedge clocks[0]) q[0] = d; always @(posedge clocks[1]) q[1] = d;", "blocking-bit-clock-register"),
            ("dynamic", "always @(posedge clocks[0]) q[index] <= d;", "unsupported-bit-clock-write"),
        ):
            source = work / (label + ".v")
            source.write_text("module reject(input [1:0] clocks, input d, index, output reg [1:0] q);\n"
                              + statements + "\nendmodule\n")
            diag = work / (label + ".jsonl")
            result = subprocess.run([lhd, "compile", str(source), "--emit", "diagnostics:" + str(diag),
                                     "--workdir", str(work / label)], timeout=30, capture_output=True)
            diagnostics = [json.loads(line) for line in diag.read_text().splitlines()]
            if result.returncode != 7 or not any(d.get("severity") == "error" and d.get("code") == code for d in diagnostics):
                raise AssertionError((label, result.returncode, result.stdout.decode(), diagnostics))
    print("PASS: source, native lowering, Pyrope reference, and round trip preserve independent clock edges")


if __name__ == "__main__":
    try:
        main()
    except subprocess.CalledProcessError as error:
        print(error.stdout.decode(errors="replace"))
        raise
