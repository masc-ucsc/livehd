#!/usr/bin/env python3
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
"""Generate aligned Pyrope and Verilog synthetics for the constprop contract bench.

Both forms encode the same dataflow: start at 1, add 1 N times, assert the
result. Designed so upass / yosys-opt can fold the chain to a single constant
and the perf signal is the parse + IR-level fold work, not the lowering.
"""

import argparse
import sys
from pathlib import Path


def emit_prp(n: int, path: Path) -> None:
    lines = ["mut a = 1\n"]
    lines.extend(["a += 1\n"] * n)
    lines.append(f"cassert a == {n + 1}\n")
    path.write_text("".join(lines))


def emit_verilog(n: int, path: Path) -> None:
    lines = [
        "module bench(output [31:0] out);\n",
        "  wire [31:0] a0 = 32'd1;\n",
    ]
    for i in range(1, n + 1):
        lines.append(f"  wire [31:0] a{i} = a{i - 1} + 32'd1;\n")
    lines.append(f"  assign out = a{n};\n")
    lines.append("endmodule\n")
    path.write_text("".join(lines))


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--n", type=int, required=True, help="chain length")
    p.add_argument("--prp", type=Path, required=True)
    p.add_argument("--verilog", type=Path, required=True)
    args = p.parse_args()

    args.prp.parent.mkdir(parents=True, exist_ok=True)
    args.verilog.parent.mkdir(parents=True, exist_ok=True)
    emit_prp(args.n, args.prp)
    emit_verilog(args.n, args.verilog)
    return 0


if __name__ == "__main__":
    sys.exit(main())
