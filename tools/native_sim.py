#!/usr/bin/env python3
"""Run directed, self-checking vectors through LiveHD's native simulator.

Usage: native_sim.py SOURCE TOP VECTORS.json WORKDIR (lhd is $LHD, else lhd/lhd).

Each vector has `inputs` and `outputs` dictionaries. An output may be a value
or [value, mask] when only selected bits are defined; booleans become 1/0.
Inputs retain their values between vectors.

The check is cycle-level only:
- Each vector is one cycle of the design's reference clock: its inputs are
  applied, `step` advances the reference clock once, and only then are the
  outputs sampled. Pre-edge values (e.g. a read-during-write bypass) are never
  observed.
- `step` drives the reference clock. Assigning that pin in `inputs` is accepted
  but ignored, so its edge polarity, async-vs-sync reset timing and sub-cycle
  toggles are not observable here.
- Any other clock input is an ordinary input: it sees an edge when its value
  changes between consecutive vectors.
- sim.unknown_zero does not zero register power-on state: mask such outputs or
  drive a reset before checking them.
"""
import argparse
import json
import os
from pathlib import Path
import subprocess


def _lit(value):
    """A vector value as a Pyrope literal (Python's str(True) is not one)."""
    return int(value) if isinstance(value, bool) else value


def simulate(lhd, source, top, vectors, work):
    work = Path(work)
    work.mkdir(parents=True, exist_ok=True)
    graph = work / "lg"
    subprocess.run([str(lhd), "compile", str(source), "--top", top,
                    "--set", "compile.formal.mode=none", "--emit-dir", "lg:" + str(graph),
                    "--workdir", str(work / "compile"), "-q"], check=True)
    lines = [f'const dut = import("lg:{top}")', 'test directed.values {',
             '  mut acc = dut', '  mut cycle:u32 = 0',
             f'  tick {len(vectors)} {{']
    inputs = {}
    for index, vector in enumerate(vectors):
        inputs.update(vector.get("inputs", {}))
        lines.append(f'    if cycle == {index} {{')
        lines.extend(f'      acc.{pin} = {_lit(value)}' for pin, value in inputs.items())
        lines.append('    }')
    lines.append('    step')
    for index, vector in enumerate(vectors):
        for pin, expected in vector.get("outputs", {}).items():
            actual = f'acc.{pin}'
            if isinstance(expected, list):
                expected, mask = expected
                actual = f'({actual} & {_lit(mask)})'
            lines.append(f'    assert((cycle == {index}) implies ({actual} == {_lit(expected)}), '
                         f'"vector {index}: {pin}")')
    lines.extend(['    cycle += 1', '  }', '}'])
    bench = work / "tb.prp"
    bench.write_text("\n".join(lines) + "\n")
    report = work / "native-result.json"
    result = subprocess.run([str(lhd), "sim", "lg:" + str(graph), str(bench),
                    "--set", "sim.ninja=false", "--set", "sim.tune.profile=off",
                    "--set", "compile.upass.inline=false",
                    "--set", "sim.unknown_zero=true", "--workdir", str(work / "sim"),
                    "--result-json", str(report), "-q"])
    if result.returncode and report.exists():
        print(report.read_text(), flush=True)
    result.check_returncode()
    # A zero exit alone is not a pass: the one bench test must have run and passed.
    tests = json.loads(report.read_text()).get("tests") or []
    if [(t.get("test"), t.get("status")) for t in tests] != [("directed.values", "pass")]:
        raise RuntimeError(f"{report}: expected one passing directed.values test, got {tests}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("source")
    parser.add_argument("top")
    parser.add_argument("vectors")
    parser.add_argument("work")
    args = parser.parse_args()
    simulate(os.environ.get("LHD", "lhd/lhd"), args.source, args.top,
             json.loads(Path(args.vectors).read_text()), args.work)
