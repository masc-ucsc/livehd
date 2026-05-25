#!/usr/bin/env python3
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
"""Generate aligned Pyrope / Verilog / C++ synthetics for constprop scaling benchmarks.

All patterns encode the same dataflow in three languages so upass / yosys / clang
can each fold the chain to a single constant. The performance signal is parse +
IR-level fold work, not the lowering. Use --type to pick a pattern:

  constprop  flat `a += 1` chain (baseline).
  funcchain  N inlinable function calls, each doing mixed arithmetic.
  mixedops   straight-line block cycling through +, *, <<, ^, -, |, &, >>.
  condtuple  comptime-known if/else over a fresh 3-tuple per step
             (exercises branch elimination + tuple SROA).
"""

import argparse
import sys
from pathlib import Path

# 24-bit mask keeps the chain bounded across Python / Pyrope-unbounded /
# C++-unsigned / 32-bit Verilog wires so all four oracles agree exactly.
MASK = 0xFFFFFF


# ---------- value oracles (embedded as the expected constant per asset) ----------

def _value_constprop(n: int) -> int:
    return n + 1


def _value_funcchain(n: int) -> int:
    a = 1
    for _ in range(n):
        a = ((a * 3) + 1) & MASK
    return a


_MIXED_PY = (
    lambda x: (x + 3) & MASK,
    lambda x: (x * 5) & MASK,
    lambda x: (x << 1) & MASK,
    lambda x: x ^ 0x5A,
    lambda x: (x - 1) & MASK,
    lambda x: x | 0x01,
    lambda x: x & 0x0FFFFF,
    lambda x: x >> 1,
)


def _value_mixedops(n: int) -> int:
    a = 1
    for i in range(n):
        a = _MIXED_PY[i % 8](a)
    return a


def _value_condtuple(n: int) -> int:
    a = 1
    for _ in range(n):
        t0, t1, t2 = a, a + 1, a * 2
        a = t1 if t0 < 0x1000 else ((t2 ^ 0x5A) & MASK)
    return a


# ---------- Pyrope emitters ----------

def _prp_constprop(n: int) -> list[str]:
    out = ["mut a = 1\n"]
    out.extend(["a += 1\n"] * n)
    out.append(f"cassert a == {_value_constprop(n)}\n")
    return out


def _prp_funcchain(n: int) -> list[str]:
    out = [
        f"comb step2(x) -> (r) {{ r = ((x * 3) + 1) & 0x{MASK:X} }}\n",
        "mut a = 1\n",
    ]
    out.extend(["a = step2(a)\n"] * n)
    out.append(f"cassert a == {_value_funcchain(n)}\n")
    return out


_MIXED_PRP = (
    f"a = (a + 3) & 0x{MASK:X}\n",
    f"a = (a * 5) & 0x{MASK:X}\n",
    f"a = (a << 1) & 0x{MASK:X}\n",
    "a = a ^ 0x5A\n",
    f"a = (a - 1) & 0x{MASK:X}\n",
    "a = a | 0x01\n",
    "a = a & 0x0FFFFF\n",
    "a = a >> 1\n",
)


def _prp_mixedops(n: int) -> list[str]:
    out = ["mut a = 1\n"]
    out.extend(_MIXED_PRP[i % 8] for i in range(n))
    out.append(f"cassert a == {_value_mixedops(n)}\n")
    return out


def _prp_condtuple(n: int) -> list[str]:
    # Fresh `const t_i` per iter avoids Pyrope's no-shadowing rule while still
    # forcing the folder to track a tuple through a branch each step.
    out = ["mut a = 1\n"]
    for i in range(n):
        out.append(f"const t_{i} = (a, a + 1, a * 2)\n")
        out.append(
            f"if t_{i}.0 < 0x1000 {{ a = t_{i}.1 }}"
            f" else {{ a = (t_{i}.2 ^ 0x5A) & 0x{MASK:X} }}\n"
        )
    out.append(f"cassert a == {_value_condtuple(n)}\n")
    return out


# ---------- Verilog emitters ----------

_V_PROLOGUE = "module bench(output [31:0] out);\n  wire [31:0] a0 = 32'd1;\n"


def _v_epilogue(last: str) -> str:
    return f"  assign out = {last};\nendmodule\n"


def _verilog_constprop(n: int) -> list[str]:
    out = [_V_PROLOGUE]
    for i in range(1, n + 1):
        out.append(f"  wire [31:0] a{i} = a{i - 1} + 32'd1;\n")
    out.append(_v_epilogue(f"a{n}"))
    return out


def _verilog_funcchain(n: int) -> list[str]:
    out = [_V_PROLOGUE]
    for i in range(1, n + 1):
        out.append(
            f"  wire [31:0] a{i} = ((a{i - 1} * 32'd3) + 32'd1) & 32'h00FFFFFF;\n"
        )
    out.append(_v_epilogue(f"a{n}"))
    return out


_MIXED_V_EXPRS = (
    "(a{p} + 32'd3) & 32'h00FFFFFF",
    "(a{p} * 32'd5) & 32'h00FFFFFF",
    "(a{p} << 1) & 32'h00FFFFFF",
    "a{p} ^ 32'h5A",
    "(a{p} - 32'd1) & 32'h00FFFFFF",
    "a{p} | 32'h01",
    "a{p} & 32'h0FFFFF",
    "a{p} >> 1",
)


def _verilog_mixedops(n: int) -> list[str]:
    out = [_V_PROLOGUE]
    for i in range(1, n + 1):
        expr = _MIXED_V_EXPRS[(i - 1) % 8].format(p=i - 1)
        out.append(f"  wire [31:0] a{i} = {expr};\n")
    out.append(_v_epilogue(f"a{n}"))
    return out


def _verilog_condtuple(n: int) -> list[str]:
    out = [_V_PROLOGUE]
    for i in range(1, n + 1):
        p = i - 1
        out.append(f"  wire [31:0] t0_{i} = a{p};\n")
        out.append(f"  wire [31:0] t1_{i} = a{p} + 32'd1;\n")
        out.append(f"  wire [31:0] t2_{i} = a{p} * 32'd2;\n")
        out.append(
            f"  wire [31:0] a{i} = (t0_{i} < 32'h1000)"
            f" ? t1_{i} : ((t2_{i} ^ 32'h5A) & 32'h00FFFFFF);\n"
        )
    out.append(_v_epilogue(f"a{n}"))
    return out


# ---------- C++ emitters ----------
# Self-contained, no headers — keeps the -O0 signal as parse cost, not include
# processing. -O2 still folds each chain to a constant returned from main.

def _cpp_constprop(n: int) -> list[str]:
    out = ["int main() {\n", "  int a = 1;\n"]
    out.extend(["  a += 1;\n"] * n)
    out.append(f"  return a == {_value_constprop(n)} ? 0 : 1;\n")
    out.append("}\n")
    return out


def _cpp_funcchain(n: int) -> list[str]:
    out = [
        "static inline unsigned step2(unsigned x) {\n",
        f"  return ((x * 3u) + 1u) & 0x{MASK:X}u;\n",
        "}\n",
        "int main() {\n",
        "  unsigned a = 1u;\n",
    ]
    out.extend(["  a = step2(a);\n"] * n)
    out.append(f"  return a == {_value_funcchain(n)}u ? 0 : 1;\n")
    out.append("}\n")
    return out


_MIXED_CPP = (
    f"  a = (a + 3u) & 0x{MASK:X}u;\n",
    f"  a = (a * 5u) & 0x{MASK:X}u;\n",
    f"  a = (a << 1) & 0x{MASK:X}u;\n",
    "  a = a ^ 0x5Au;\n",
    f"  a = (a - 1u) & 0x{MASK:X}u;\n",
    "  a = a | 0x01u;\n",
    "  a = a & 0x0FFFFFu;\n",
    "  a = a >> 1;\n",
)


def _cpp_mixedops(n: int) -> list[str]:
    out = ["int main() {\n", "  unsigned a = 1u;\n"]
    out.extend(_MIXED_CPP[i % 8] for i in range(n))
    out.append(f"  return a == {_value_mixedops(n)}u ? 0 : 1;\n")
    out.append("}\n")
    return out


def _cpp_condtuple(n: int) -> list[str]:
    out = ["int main() {\n", "  unsigned a = 1u;\n"]
    for _ in range(n):
        out.append("  { unsigned t0 = a, t1 = a + 1u, t2 = a * 2u;\n")
        out.append(
            f"    if (t0 < 0x1000u) a = t1;"
            f" else a = (t2 ^ 0x5Au) & 0x{MASK:X}u;\n"
        )
        out.append("  }\n")
    out.append(f"  return a == {_value_condtuple(n)}u ? 0 : 1;\n")
    out.append("}\n")
    return out


# ---------- dispatch ----------

_EMITTERS = {
    "constprop": (_prp_constprop, _verilog_constprop, _cpp_constprop),
    "funcchain": (_prp_funcchain, _verilog_funcchain, _cpp_funcchain),
    "mixedops":  (_prp_mixedops,  _verilog_mixedops,  _cpp_mixedops),
    "condtuple": (_prp_condtuple, _verilog_condtuple, _cpp_condtuple),
}


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--n", type=int, required=True,
                   help="chain length (~ ops per test)")
    p.add_argument("--type", dest="kind", choices=sorted(_EMITTERS.keys()),
                   default="constprop",
                   help="pattern to generate (default: constprop)")
    p.add_argument("--prp", type=Path, required=True)
    p.add_argument("--verilog", type=Path, required=True)
    p.add_argument("--cpp", type=Path, required=True)
    args = p.parse_args()

    prp_fn, v_fn, cpp_fn = _EMITTERS[args.kind]

    for out in (args.prp, args.verilog, args.cpp):
        out.parent.mkdir(parents=True, exist_ok=True)
    args.prp.write_text("".join(prp_fn(args.n)))
    args.verilog.write_text("".join(v_fn(args.n)))
    args.cpp.write_text("".join(cpp_fn(args.n)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
