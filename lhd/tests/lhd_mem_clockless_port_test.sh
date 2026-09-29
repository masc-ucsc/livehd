#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# A Memory whose write PORT 0 has no clock_pin while later ports carry the
# array clock (lhd/tests/mem_clockless_port.sv: `assign rf_q[0] = '0` next to
# an always_ff writing the other entries -- minion's prim_rf_2r1w_preview).
# inou.cgen.verilog took the wrapper's shared clock from port 0 only and died
# with "memory ... should have a clock pin" (inou.cgen mem-malformed), both on
# a plain `lhd compile --emit verilog` and inside pass.abc's memory lowering
# (which re-emits the memory alone through cgen). The shared clock is the
# first clock ANY port carries; a clockless port commits on it.
#
#   1. compile -> Verilog: the wrapper instance is clocked by clk;
#   2. lhd synth (abc, the memory bit-blasted) and lhd synth (usyn) succeed;
#   3. the abc netlist is PROVEN against the compiled design through gensim models.
set -u

LHD="${LHD:-lhd/lhd}"
LIB=inou/prp/tests/abc/test.lib
SRC=lhd/tests/mem_clockless_port.sv
TOP=mem_clockless_port
W="${TEST_TMPDIR:-/tmp/lhd_mem_clockless_port_$$}"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}
[ -f "$LIB" ] || fail "missing liberty $LIB"
[ -f "$SRC" ] || fail "missing fixture $SRC"

# 1. plain Verilog emission
"$LHD" compile "$SRC" --reader slang --top "$TOP" --emit-dir verilog:"$W/v" --workdir "$W/w1" \
    -q --result-json "$W/c.json" || fail "compile -> $(cat "$W/c.json" 2>/dev/null)"
grep -q '^\.clk(clk)' "$W/v/$TOP.v" || fail "memory wrapper is not clocked by clk: $(cat "$W/v/$TOP.v")"
grep -q "wr_addr_0(2'h0)" "$W/v/$TOP.v" || fail "the clockless entry-0 write port is missing: $(cat "$W/v/$TOP.v")"
echo "PASS: compile emits the memory with the shared clock on its clockless port"

# 2. synthesis, both mappers (independent: run side by side)
synth() {  # synth <dir> [--set ...]
  local d="$1"
  shift
  "$LHD" synth "$SRC" --reader slang --top "$TOP" --workdir "$d/w" --emit-dir lg:"$d/net" \
      --set synth.liberty="$LIB" --set synth.opentimer=false --set pass.abc.memory=true "$@" \
      -q --result-json "$d/r.json" || fail "synth $* -> $(cat "$d/r.json" 2>/dev/null)"
}
synth "$W/abc" --set synth.mapper=abc &
abc_pid=$!
synth "$W/usyn" --set synth.mapper=usyn &
usyn_pid=$!
"$LHD" pass liberty gensim "$LIB" --emit-dir lg:"$W/models" --workdir "$W/wg" -q --result-json "$W/g.json" \
    || fail "gensim -> $(cat "$W/g.json" 2>/dev/null)"
wait "$abc_pid" || exit 1
wait "$usyn_pid" || exit 1
echo "PASS: lhd synth (abc and usyn) maps the memory"

# 3. LEC: mapped netlist vs the compiled reference graph
"$LHD" lec --impl lg:"$W/abc/net" --ref lg:"$W/abc/w/synth/lg" --lib lg:"$W/models" --top "$TOP.$TOP" \
    --workdir "$W/wl" -q --result-json "$W/lec.json" || fail "lec -> $(cat "$W/lec.json" 2>/dev/null)"
grep -q '"verdict":"proven"' "$W/lec.json" || fail "netlist not PROVEN: $(cat "$W/lec.json")"
echo "PASS: abc netlist PROVEN equivalent to the compiled design"
