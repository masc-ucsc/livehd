#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# A Pyrope clock gate -- `Clock(clock_pin=, enable=)`, which lowers to a
# Clock_cell -- must not reach a synthesized netlist as a native Clock_cell
# (OpenTimer refuses it: minion's prim_clk_gate in lhdsuite). synth's
# prepare_design spells a plain Clock_cell as `clk & latch(en)`, which pass.abc
# maps onto the Liberty ICG cell (DLCLKPx1 in abc_icg_q.lib) and pass.usyn keeps
# as its native ICG barrier. For both fixtures (one gate, a chained pair) and
# both mappers: no Clock_cell survives, synthesis (incl. STA) succeeds, abc picks
# the ICG cell, and the graph LEC proves the netlist against the Pyrope source.
set -euo pipefail
LHD=lhd/lhd
W="${TEST_TMPDIR:-/tmp/lhd_clock_cell_icg_$$}"
mkdir -p "$W"
LIB=lhd/tests/abc_icg_q.lib
"$LHD" pass liberty gensim "$LIB" --emit-dir lg:"$W/models" --workdir "$W/gensim" -q
for fixture in equiv/mclk_derived:derived sim/chained_clock_gates:chain8; do
  src="inou/prp/tests/${fixture%%:*}.prp"
  top="${fixture#*:}"
  for mapper in abc usyn; do
    o="$W/$(basename "${fixture%%:*}")_$mapper"
    "$LHD" synth "$src" --top "$top" --set synth.mapper="$mapper" --set synth.liberty="$LIB" \
      --emit-dir lg:"$o/net" --emit verilog:"$o/net.v" --workdir "$o/W" -q > "$o.log" 2>&1 \
      || { cat "$o.log"; echo "FAIL: $fixture $mapper synthesis" >&2; exit 1; }
    if "$LHD" tool grep kind=clock_cell lg:"$o/net" 2>/dev/null | grep -q clock_cell; then
      echo "FAIL: $fixture $mapper left a native Clock_cell in the netlist" >&2
      exit 1
    fi
    if [ "$mapper" = abc ] && ! grep -q 'DLCLKPx1' "$o/net.v"; then
      echo "FAIL: $fixture abc did not map the clock gate onto the ICG cell" >&2
      exit 1
    fi
    "$LHD" lec --impl lg:"$o/net" --ref "$src" --lib lg:"$W/models" --top "$top" \
      --workdir "$o/lec" > "$o.lec.log" 2>&1 || { cat "$o.lec.log"; echo "FAIL: $fixture $mapper lec" >&2; exit 1; }
    grep -q 'PROVEN equivalent' "$o.lec.log" || { cat "$o.lec.log"; echo "FAIL: $fixture $mapper not proven" >&2; exit 1; }
  done
done
echo "PASS: Pyrope clock gates map (abc: ICG cell, usyn: native gate) and prove against the source"
