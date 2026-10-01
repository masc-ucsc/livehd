#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# ONE mapped netlist gets ONE verdict, however it is read. A data latch enabled
# by a latch-based clock gate is synthesized by both mappers onto the Liberty
# cells of lib_cell_prep.lib, and each netlist is checked in three
# representations:
#   graph  lg: netlist            vs the source graph
#   vnet   its Verilog emission   vs the source graph
#   self   lg: netlist            vs its own Verilog emission
# All six must PROVE. They used to depend on the representation:
#   * ABC maps the gate onto DLCLKPx1, whose gensim graph numbers GCLK after the
#     internal IQ pin (port 4) while the Verilog re-read numbers it 3. The time
#     base spliced the Verilog instance from the --lib graph BY PORT ID, lost
#     GCLK, and every latch it enabled read as always-transparent: vnet and
#     self REFUTED (qg ref=15 impl=0). graph/inline_sub.cpp now binds a --lib
#     model by port NAME (graph:inline_sub_test pins the binding itself).
#   * USYN maps the gate onto INVx1 + DLXTPx1 + AND2x1. The phase schedule saw
#     `clk & en_l` as a clock-role latch in the source (and in the Verilog
#     re-read, whose comb inlining dissolves AND2x1) but an opaque AND2x1 as a
#     DATA latch reading the clock as a free input: graph and self REFUTED
#     (qg ref=15 impl=14, clk=0 at every phase). pass/lec/lib_cell_prep.cpp
#     exposes that cell (recipe line checked below).
# Broken twins (a complemented D, a complemented gate enable) must REFUTE
# against every representation. A --lib model that does not declare a port the
# netlist instance uses must REFUSE the run (class unsupported), never splice
# around it: once on a stateful cell (proof_prep) and once on the latch-enable
# AND2x1 (lib_cell_prep).
# An lg: netlist that carries its OWN cell bodies is compared as ITS design:
# the --lib model stands in only for a body-less instance, exactly as the
# encoder resolves a Sub. With the correct bodies it PROVES; with a broken one
# (ABC: DLCLKPx1 spelled GCLK = CLK, the gate bypassed; USYN: INVx1 spelled
# Y = A) it must REFUTE. The time base used to splice the --lib model over the
# design's own body and PROVED both broken netlists.

set -u

LHD="${LHD:-lhd/lhd}"
[ -x "$LHD" ] || LHD=./bazel-bin/lhd/lhd
[ -x "$LHD" ] || {
  echo "FAIL: could not find the lhd binary in $(pwd)"
  exit 1
}
LIB="$PWD/pass/lec/tests/lib_cell_prep.lib"
[ -f "$LIB" ] || {
  echo "FAIL: missing fixture $LIB"
  exit 1
}
W="${TEST_TMPDIR:-$(mktemp -d)}/lib_cell_prep"
mkdir -p "$W"
FAILS="$W/failures"
: >"$FAILS"
bad() {
  echo "FAIL: $*"
  echo "$*" >>"$FAILS"
}

TOP=lcp_gated
cat >"$W/src.v" <<'EOF'
module lcp_gated(input clk, input en, input [3:0] d, output [3:0] qg);
  logic en_l;
  always_latch if (!clk) en_l = en;
  wire gclk = clk & en_l;
  logic [3:0] g;
  always_latch if (gclk) g = d;
  assign qg = g;
endmodule
EOF
sed 's/g = d;/g = ~d;/' "$W/src.v" >"$W/notd.v"
sed 's/en_l = en;/en_l = ~en;/' "$W/src.v" >"$W/enlpol.v"
# Models that do not describe the netlist's cells: DLXTPx1's D renamed DIN, and
# AND2x1's B renamed BB (only within that cell's block).
awk '/cell\(DLXTPx1\)/{c=1} /cell\(DFFx1\)/{c=0} c{gsub(/"D"/,"\"DIN\""); gsub(/pin\(D\)/,"pin(DIN)")} {print}' \
  "$LIB" >"$W/ren_latch.lib"
awk '/cell\(AND2x1\)/{c=1} /cell\(DLCLKPx1\)/{c=0} c{gsub(/"B"/,"\"BB\""); gsub(/pin\(B\)/,"pin(BB)"); gsub(/\(A B\)/,"(A BB)")} {print}' \
  "$LIB" >"$W/ren_and.lib"

# lec <dir> <tag> <want: proven|refuted> <lec args...>
lec() {
  local d=$1 tag=$2 want=$3
  shift 3
  "$LHD" lec "$@" --top "$TOP" --set formal.simfail_run=false --workdir "$d/W_$tag" -q \
    --result-json "$d/$tag.json" >"$d/$tag.log" 2>&1
  grep -q "\"verdict\":\"$want\"" "$d/$tag.json" || bad "$d: $tag is not $want: $(head -c 600 "$d/$tag.json")"
}

# refused <dir> <tag> <message fragment> <lec args...>: the run must stop with
# an `unsupported` error naming the mismatched port, and never reach a verdict.
refused() {
  local d=$1 tag=$2 want=$3
  shift 3
  "$LHD" lec "$@" --top "$TOP" --set formal.simfail_run=false --workdir "$d/W_$tag" -q \
    --result-json "$d/$tag.json" >"$d/$tag.log" 2>&1
  grep -q '"class":"unsupported"' "$d/$tag.json" && grep -qF "$want" "$d/$tag.json" \
    && ! grep -q '"verdict"' "$d/$tag.json" || bad "$d: $tag must refuse ($want): $(head -c 600 "$d/$tag.json")"
}

# One job per mapper: synthesis, the gensim models, the representation matrix,
# the broken twins and the mechanism checks.
mapper() {
  local m=$1 d="$W/$1" p=()
  mkdir -p "$d"
  "$LHD" synth --reader slang --top "$TOP" --workdir "$d/W" --emit-dir lg:"$d/net" --emit-dir verilog:"$d/netv" \
    --set synth.mapper="$m" --set synth.liberty="$LIB" -q -- "$W/src.v" >"$d/synth.log" 2>&1 || {
    bad "$m: synth: $(tail -3 "$d/synth.log")"
    return
  }
  cat "$d/netv/"*.v >"$d/net.v"
  local v l
  for v in models ren_latch ren_and; do
    l="$W/$v.lib"
    [ "$v" = models ] && l="$LIB"
    "$LHD" pass liberty gensim "$l" --emit-dir lg:"$d/$v" --workdir "$d/Wm_$v" -q >"$d/gensim_$v.log" 2>&1 || {
      bad "$m: gensim $v: $(tail -3 "$d/gensim_$v.log")"
      return
    }
  done
  # the shapes this test is about
  if [ "$m" = abc ]; then
    grep -q '^DLCLKPx1 ' "$d/net.v" || bad "abc: the clock gate must map onto DLCLKPx1"
  else
    grep -q '^AND2x1 ' "$d/net.v" || bad "usyn: the gate must map onto an AND2x1 cell"
  fi
  [ "$(grep -c '^DLXTPx1 ' "$d/net.v")" -ge 4 ] || bad "$m: g must map onto DLXTPx1 cells"
  for t in notd enlpol; do
    "$LHD" compile -q --reader slang --top "$TOP" --emit-dir lg:"$d/$t" --workdir "$d/Wc_$t" -- "$W/$t.v" \
      >"$d/c_$t.log" 2>&1 || bad "$m: compile $t"
  done
  # The netlist compiled together with its own copy of every cell (the models'
  # Verilog), then with one cell broken.
  "$LHD" compile -q lg:"$d/models" --emit-dir verilog:"$d/cells" --workdir "$d/Wcells" >"$d/cells.log" 2>&1 \
    || bad "$m: emit the cell models as Verilog"
  local broken=DLCLKPx1 fix='module DLCLKPx1(input CLK, input GATE, output GCLK); assign GCLK = CLK; endmodule'
  if [ "$m" = usyn ]; then
    broken=INVx1 fix='module INVx1(input A, output Y); assign Y = A; endmodule'
  fi
  [ -f "$d/cells/$broken.v" ] || bad "$m: no $broken model to break"
  cat "$d/net.v" "$d/cells/"*.v >"$d/own_ok.v"
  { cat "$d/net.v"; for v in "$d/cells/"*.v; do [ "$v" = "$d/cells/$broken.v" ] || cat "$v"; done; echo "$fix"; } \
    >"$d/own_bad.v"
  for t in own_ok own_bad; do
    "$LHD" compile -q --reader slang --top "$TOP" --emit-dir lg:"$d/$t" --workdir "$d/Wc_$t" -- "$d/$t.v" \
      >"$d/c_$t.log" 2>&1 || bad "$m: compile $t: $(tail -3 "$d/c_$t.log")"
  done
  local lib=(--lib lg:"$d/models")
  lec "$d" graph proven --impl lg:"$d/net" --ref lg:"$d/W/synth/lg" "${lib[@]}" &
  p+=($!)
  lec "$d" vnet proven --impl "$d/net.v" --ref lg:"$d/W/synth/lg" "${lib[@]}" &
  p+=($!)
  lec "$d" self proven --impl lg:"$d/net" --ref "$d/net.v" "${lib[@]}" &
  p+=($!)
  for t in notd enlpol; do
    lec "$d" "${t}_lg" refuted --impl lg:"$d/net" --ref lg:"$d/$t" "${lib[@]}" &
    p+=($!)
    lec "$d" "${t}_v" refuted --impl "$d/net.v" --ref lg:"$d/$t" "${lib[@]}" &
    p+=($!)
  done
  lec "$d" own_ok proven --impl lg:"$d/own_ok" --ref lg:"$d/W/synth/lg" "${lib[@]}" &
  p+=($!)
  lec "$d" own_bad refuted --impl lg:"$d/own_bad" --ref lg:"$d/W/synth/lg" "${lib[@]}" &
  p+=($!)
  refused "$d" ren_latch "input 'D' of the instance is not an input of 'DLXTPx1'" \
    --impl lg:"$d/net" --ref lg:"$d/W/synth/lg" --lib lg:"$d/ren_latch" &
  p+=($!)
  if [ "$m" = usyn ]; then
    refused "$d" ren_and "latch-enable --lib cell instance" \
      --impl lg:"$d/net" --ref lg:"$d/W/synth/lg" --lib lg:"$d/ren_and" &
    p+=($!)
  fi
  wait "${p[@]}"
  if [ "$m" = usyn ]; then
    grep -q 'clock-dependent --lib cell(s) on latch enables' "$d/graph.json" \
      || bad "usyn graph: the AND2x1 on the latch enable was not exposed to the phase schedule"
    grep -qF "input 'B' of the instance is not an input of 'AND2x1'" "$d/ren_and.json" \
      || bad "usyn ren_and: the refusal must name AND2x1's port B"
  fi
}

mapper abc &
pa=$!
mapper usyn &
pu=$!
wait "$pa" "$pu"

if [ -s "$FAILS" ]; then
  echo "FAIL: $(wc -l <"$FAILS" | tr -d ' ') check(s) failed"
  exit 1
fi
echo "PASS: ABC and USYN netlists of a clock-gated data latch PROVE as lg:, as Verilog and against their own Verilog;"
echo "PASS: a complemented D and a complemented gate enable REFUTE against every representation;"
echo "PASS: a --lib model missing a port the netlist uses REFUSES the run;"
echo "PASS: an lg: netlist with its own cell bodies PROVES with correct cells and REFUTES with a broken one"
