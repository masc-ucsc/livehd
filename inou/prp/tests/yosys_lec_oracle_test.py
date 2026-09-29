#!/usr/bin/env python3
"""The guard on the guard: prove `prp-equiv-*`'s yosys oracle can still FAIL.

`run_yosys_lec` (prplib.py) is a cross-check that TOLERATES every verdict but a
counterexample — which is exactly the shape of thing that silently becomes a
no-op. A refactor that loses the lgcheck binary, mangles a top name, or swaps
the exit-code mapping would leave all 326 pairs printing a tolerated line and
passing forever, reproducing at the harness level the very failure (passing
while checking nothing) the oracle was added to catch.

So: one pair the oracle must PROVE, two it must REFUTE (the second only after
a mid-run reset), and the opt-out tag.
Hand-written Verilog only — no lhd, no Pyrope — so a compile regression cannot
turn this into a false green.
"""

import os
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from prplib import PrpRunner, PrpTest  # noqa: E402

# One design, and a version of it that is off by one. `equiv_make` needs the
# port names to correspond, as it does for a real cgen/golden pair.
IMPL = """module oracle_dut(input clk, input [3:0] a, output reg [3:0] y);
  always @(posedge clk) y <= a + 4'd1;
endmodule
"""
GOLD_DIFF = IMPL.replace("a + 4'd1", "a + 4'd2")

# A 1-entry memory with a SYNCHRONOUS reset arm, and a model that clears it only
# at power-on (the old slang lowering of a packed memory's reset arm). The two
# differ only once `rst` is re-asserted AFTER a write, so a miter that explored
# just the power-on prologue would pass them. The slang fixture
# inou/slang/tests/sv/memory_sync_reset.v (`// :lec_solver: lgyosys`) relies on
# lgcheck telling them apart.
MEM_RESET_GOLD = """module memory_reset(input clk, rst, we, input [4:0] d, output [4:0] q);
  reg [4:0] mem [0:0];
  always @(posedge clk) if (rst) mem[0] <= 0; else if (we) mem[0] <= d;
  assign q = mem[0];
endmodule
"""
MEM_RESET_POWER_ON_ONLY = """module memory_reset(input clk, rst, we, input [4:0] d, output reg [4:0] q = 0);
  always @(posedge clk) if (!rst && we) q <= d;
endmodule
"""

HEADER = """/*
:name: yosys_lec_oracle
:type: equiv
{extra}*/
"""


def make_test(tmp, extra=""):
    prp = Path(tmp) / "yosys_lec_oracle.prp"
    prp.write_text(HEADER.format(extra=extra))
    return PrpTest(str(prp))


def check(label, got, want):
    print("{}: {} (expected {})".format(label, got, want))
    return 0 if got == want else 1


def main():
    runner = PrpRunner()
    if runner._lgcheck_tools()[0] is None:
        # Never pass quietly on a missing oracle: that is the state this test
        # exists to make visible.
        print("FAILED: inou/yosys/lgcheck is not reachable, so no prp-equiv-* pair "
              "is cross-checked at all (run from the repo root, or set LHD_LGCHECK)")
        return 1

    rc = 0
    with tempfile.TemporaryDirectory() as tmp:
        impl = Path(tmp) / "impl.v"
        impl.write_text(IMPL)
        same = Path(tmp) / "gold_same.v"
        same.write_text(IMPL)
        diff = Path(tmp) / "gold_diff.v"
        diff.write_text(GOLD_DIFF)

        test = make_test(tmp)
        rc |= check("equivalent pair (oracle must not fail it)",
                    runner.run_yosys_lec(test, str(impl), "oracle_dut", str(same), "oracle_dut",
                                         os.path.join(tmp, "same")),
                    0)
        # The one verdict that must fail a pair. If this returns 0 the oracle has
        # stopped looking at the designs.
        rc |= check("off-by-one pair (oracle MUST refute it)",
                    runner.run_yosys_lec(test, str(impl), "oracle_dut", str(diff), "oracle_dut",
                                         os.path.join(tmp, "diff")),
                    1)
        mem_gold = Path(tmp) / "mem_reset_gold.v"
        mem_gold.write_text(MEM_RESET_GOLD)
        mem_bad = Path(tmp) / "mem_reset_power_on_only.v"
        mem_bad.write_text(MEM_RESET_POWER_ON_ONLY)
        rc |= check("power-on-only memory reset (oracle MUST refute a reset re-asserted after a write)",
                    runner.run_yosys_lec(test, str(mem_bad), "memory_reset", str(mem_gold), "memory_reset",
                                         os.path.join(tmp, "mem_reset")),
                    1)
        # ...and the documented opt-out must still opt out.
        rc |= check(":yosys_lec: false on a broken pair (skipped)",
                    runner.run_yosys_lec(make_test(tmp, ":yosys_lec: false\n"), str(impl), "oracle_dut",
                                         str(diff), "oracle_dut", os.path.join(tmp, "off")),
                    0)

    print("yosys_lec_oracle_test: " + ("PASSED" if rc == 0 else "FAILED"))
    return rc


if __name__ == "__main__":
    sys.exit(main())
