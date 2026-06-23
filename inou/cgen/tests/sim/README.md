# inou.cgen.sim differential tests (TODO 3d)

`diff_test.py` cross-checks `inou.cgen.sim` (executable Slop C++) against Icarus
Verilog, **X-masked**: where `iverilog` shows a known `0`/`1` the two must match;
where it shows `x`/`z`, hlop is free (Slop has no unknowns).

For one design it: `lhd compile <prp> --top <top> --emit verilog:<top>.v
--emit-dir sim:sim/` (both backends from one LGraph), generates a random
stimulus, drives both an `iverilog` testbench and a Slop C++ driver (built via
the emitted standalone Bazel module), and compares per-bit.

    python3 inou/cgen/tests/sim/diff_test.py inou/prp/tests/pyrope/abc_arith.prp abc_arith 64
    python3 inou/cgen/tests/sim/diff_test.py inou/prp/tests/pyrope/abc_comb.prp  abc_comb  64

Requires `iverilog`/`vvp` on PATH. **v1 is combinational only** (no clock);
sequential (clocked) designs need a clocked testbench — Phase 2. Run from the
repo root; the generated `difftest_<top>/` work dir is disposable.

Status: `abc_arith` and `abc_comb` pass 64/64 (Sum/Mult/And/Or/Xor/Not/LT/GT/EQ/
SHL/Mux/Get_mask + the hlop cross-width converter).
