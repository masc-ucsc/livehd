#!/usr/bin/env python3
"""The guard on the guard: prove `prp-equiv-*`'s yosys oracle can still FAIL.

`run_yosys_lec` (prplib.py) is a cross-check that TOLERATES every verdict but a
counterexample — which is exactly the shape of thing that silently becomes a
no-op. A refactor that loses the lgcheck binary, mangles a top name, or swaps
the exit-code mapping would leave all 326 pairs printing a tolerated line and
passing forever, reproducing at the harness level the very failure (passing
while checking nothing) the oracle was added to catch.

So: one pair the oracle must PROVE, one it must REFUTE, and the opt-out tag.
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
        # ...and the documented opt-out must still opt out.
        rc |= check(":yosys_lec: false on a broken pair (skipped)",
                    runner.run_yosys_lec(make_test(tmp, ":yosys_lec: false\n"), str(impl), "oracle_dut",
                                         str(diff), "oracle_dut", os.path.join(tmp, "off")),
                    0)

    print("yosys_lec_oracle_test: " + ("PASSED" if rc == 0 else "FAILED"))
    return rc


if __name__ == "__main__":
    sys.exit(main())
