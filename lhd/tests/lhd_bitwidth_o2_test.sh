#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# --recipe O2 (pass.cprop + pass.bitwidth) over the yosys-verilog flow:
# bw_mix.v carries one node of each bitwidth-relevant cell class (sum, mult,
# and/or/xor/not, comparators, shl/sra/logic-shift, mux, flop) and must stay
# logically equivalent after bitwidth inference; bw_mem.v drives the memory
# sizing path (no LEC: combinational-memory miters are inconclusive, the
# observable is the generated memory instance).

set -u

LHD=lhd/lhd
MIX=lhd/tests/bw_mix.v
MEM=lhd/tests/bw_mem.v
W="${TEST_TMPDIR:-/tmp/lhd_bw_o2_$$}"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

# 1. O2 compile of the cell mix + LEC against the source.
"$LHD" compile "$MIX" --reader yosys-verilog --top bw_mix --recipe O2 \
  --emit verilog:"$W/bw_mix.gen.v" --workdir "$W/w_mix" -q 2>/dev/null \
  || fail "O2 compile of bw_mix.v failed"
[ -s "$W/bw_mix.gen.v" ] || fail "O2 compile produced empty netlist"
"$LHD" lec --set lec.solver=lgyosys --impl verilog:"$W/bw_mix.gen.v" --ref verilog:"$MIX" --top bw_mix \
  --workdir "$W/w_chk" -q 2>/dev/null \
  || fail "bw_mix O2 netlist is not equivalent to the source"

# 2. The recipe must actually have run pass.bitwidth (not silently O1).
"$LHD" compile "$MIX" --reader yosys-verilog --top bw_mix --recipe O2 \
  --emit verilog:"$W/bw_mix2.gen.v" --workdir "$W/w_mix2" --result-json "$W/r.json" -q 2>/dev/null \
  || fail "O2 recompile for recipe check failed"
grep -q 'pass.bitwidth' "$W/r.json" || fail "result recipe does not list pass.bitwidth: $(cat "$W/r.json")"

# 3. O2 over a synchronous RAM: bitwidth memory sizing must keep the
#    memory instance in the generated Verilog.
"$LHD" compile "$MEM" --reader yosys-verilog --top bw_mem --recipe O2 \
  --emit verilog:"$W/bw_mem.gen.v" --workdir "$W/w_mem" -q 2>/dev/null \
  || fail "O2 compile of bw_mem.v failed"
grep -qi 'memory' "$W/bw_mem.gen.v" || fail "O2 memory netlist lost the memory instance"

echo "PASS lhd_bitwidth_o2_test"
