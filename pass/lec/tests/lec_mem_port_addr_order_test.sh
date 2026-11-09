#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# Memory write-port pairing by constant address. A reg array written one entry
# per port (what a Verilog `for` or a Pyrope loop unrolls to) can be emitted in
# opposite entry order by two front ends (Verilog 3..0, Pyrope 0..<4). Pairing
# ports by POSITION then compares entry 3 against entry 0 on every port and the
# per-port cones all DIFF, sending the whole array to the monolithic solve.
# With every write address a distinct constant the writes commute, so the
# per-cut sweep must pair them by address and prove the cones alone.
#
# A design this small proves by another route even when the per-port cones
# DIFF, so the verdict alone cannot tell the fix from the old positional
# pairing. The assertion is on LHD_LEC_CONE_LOG: no per-port `wrN.*` cone may
# come back DIFF. Soundness: a design whose entry-1 data really differs must
# still show a DIFF cone and must never PROVE.

set -u

LHD=${LHD:-./bazel-bin/lhd/lhd}
if [ ! -x "$LHD" ]; then
  if [ -x ./lhd/lhd ]; then LHD=./lhd/lhd; else
    echo "FAIL: could not find the lhd binary in $(pwd)"; exit 1; fi
fi

WORK="${TEST_TMPDIR:-/tmp/lecmemportaddr}"
mkdir -p "$WORK"
fail=0

gen() {  # $1=file, $2=entry order, $3=xor mask on entry 1's data
  {
    echo "module foo(input clk, input [3:0] we, input [31:0] wd, input [1:0] ra, output [7:0] z);"
    echo "  reg [7:0] m [0:3];"
    echo "  always @(posedge clk) begin"
    for e in $2; do
      mask="8'h00"; [ "$e" = 1 ] && mask="$3"
      echo "    if (we[$e]) m[$e] <= wd[$((e * 8)) +: 8] ^ $mask;"
    done
    echo "  end"
    echo "  assign z = m[ra];"
    echo "endmodule"
  } > "$1"
}
gen "$WORK/down.v" "3 2 1 0" "8'h00"
gen "$WORK/up.v" "0 1 2 3" "8'h00"
gen "$WORK/bad.v" "0 1 2 3" "8'h01"

final_word() {
  grep -aE "^lec: '[^']+' " | grep -oE " (PROVEN-full|PROVEN-bounded\([0-9]+\)|TESTED-sampled\([0-9]+\)|TIMEOUT-(time|memory)|REFUTED-(cex|interface|unsupported|unchecked|vacuous|unexplained))( |$)" | tail -1 | tr -d ' '
}
port_diffs() {  # $1=ref $2=impl -> number of per-port cones that came back DIFF
  LHD_LEC_CONE_LOG=1 $LHD lec --set formal.effort=proof --ref "$WORK/$1" --impl "$WORK/$2" --top foo \
       --set formal.lec.hier=false --set formal.engine=ind --set formal.lec.decompose=true \
       --workdir "$WORK/c_${1}_${2}_$$" 2>&1 | grep -acE ':wr[0-9]+\.[a-z]+ +DIFF'
}
verdict() {  # $1=ref $2=impl
  $LHD lec --set formal.effort=proof --ref "$WORK/$1" --impl "$WORK/$2" --top foo \
       --set formal.lec.hier=false --set formal.engine=ind --set formal.lec.decompose=true \
       --workdir "$WORK/q_${1}_${2}_$$" 2>&1 | final_word
}

for pair in "down.v up.v" "up.v down.v"; do
  set -- $pair
  v=$(verdict "$1" "$2")
  if [ "$v" = "PROVEN-full" ]; then
    echo "ok: $1 vs $2 (opposite write order) -> PROVEN-full"
  else
    echo "FAIL: $1 vs $2 (opposite write order) -> '$v' (want PROVEN-full: ports must pair by address)"; fail=1
  fi
  n=$(port_diffs "$1" "$2")
  if [ "$n" = 0 ]; then
    echo "ok: $1 vs $2 -> no per-port cone DIFF"
  else
    echo "FAIL: $1 vs $2 -> $n per-port cone(s) DIFF (ports paired by position, not address)"; fail=1
  fi
done

v=$(verdict down.v bad.v)
if [ "${v%%-*}" = "PROVEN" ] || [ "${v%%-*}" = "TESTED" ]; then
  echo "FAIL: entry-1 data differs -> FALSE PROVEN ($v)"; fail=1
elif [ -n "$v" ]; then
  echo "ok: entry-1 data differs -> $v (not PROVEN)"
else
  echo "FAIL: entry-1 data differs -> no verdict parsed"; fail=1
fi

n=$(port_diffs down.v bad.v)
if [ "$n" -gt 0 ]; then
  echo "ok: entry-1 data differs -> $n per-port cone(s) DIFF"
else
  echo "FAIL: entry-1 data differs but no per-port cone DIFF (address pairing hid a real difference)"; fail=1
fi

if [ $fail -ne 0 ]; then echo "lec_mem_port_addr_order_test: FAILED"; exit 1; fi
echo "lec_mem_port_addr_order_test: PASSED"
exit 0
