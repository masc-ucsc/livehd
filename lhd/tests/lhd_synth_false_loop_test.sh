#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# `lhd synth` over a FALSE word-level loop through pure-comb instances (an
# arbiter request/can_grant handshake, see synth_false_loop_arb.sv). The flow
# must bound every width, map, and stay LEC-equivalent to both the synthesis
# reference graph and the source Verilog.

set -u

LHD=lhd/lhd
LIB=inou/prp/tests/abc/test.lib
FIX=lhd/tests/synth_false_loop_arb.sv
TOP=synth_false_loop_arb
W="${TEST_TMPDIR:-/tmp/lhd_synth_false_loop_$$}"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

run() {
  "$LHD" "$@" --result-json "$W/result.json" >"$W/latest.log" 2>&1 \
    || fail "$* -> $(cat "$W/latest.log")"
}

run compile "$FIX" --reader slang --top "$TOP" --emit-dir lg:"$W/lg" --workdir "$W/w_compile"
if grep -q 'bitwidth-unbounded' "$W/latest.log"; then
  fail "bitwidth left the per-element can_grant reassembly unbounded: $(grep bitwidth-unbounded "$W/latest.log")"
fi

for mapper in abc usyn; do
  run synth "$FIX" --reader slang --top "$TOP" --workdir "$W/w_$mapper" --emit-dir lg:"$W/net_$mapper" \
    --set synth.liberty="$LIB" --set synth.mapper="$mapper" --set synth.opentimer=false
done

run pass liberty gensim "$LIB" --emit-dir lg:"$W/models" --workdir "$W/w_models"
for mapper in abc usyn; do
  run lec --impl lg:"$W/net_$mapper" --ref lg:"$W/w_$mapper/synth/lg" --lib lg:"$W/models" --top "$TOP" \
    --workdir "$W/w_lec_$mapper"
  run lec --impl lg:"$W/net_$mapper" --ref verilog:"$FIX" --lib lg:"$W/models" --top "$TOP" \
    --workdir "$W/w_lec_src_$mapper"
done

echo "PASS: false word-level instance loop is width-bounded, mapped (abc+usyn) and LEC-proven"
