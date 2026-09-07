#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# Partition-boundary environment for pass.abc (pass/abc/abc_boundary.cpp):
# a region is sized against what lies BEYOND its ports. The fixture maps one
# NAND into region 1 and its eight consumers into region 2. On its own, region
# 1 sees a load-free output and keeps NAND2x1; against the eight sink pins and
# region 2's departure it must take NAND2x2 (the hermetic timing.lib carries
# exactly those two drive strengths).
#
#   boundary=true  : qor.json reports crossing bits + a re-sized cell, the
#                    driver region holds NAND2x2, and the refined netlist is
#                    LEC-equivalent to the partition twin (cell swaps only)
#   boundary=false : no boundary scoreboard, NAND2x1 stays
#   incremental    : a second run is all hits, starts no ABC, and emits the
#                    byte-identical (refined) netlist -- the cache holds the
#                    refined bodies and the refinement is skipped
set -u

LHD=lhd/lhd
LIB=inou/prp/tests/abc/timing.lib
PRP=inou/prp/tests/pyrope/abc_boundary.prp
TOP=abc_boundary.abc_boundary
W="${TEST_TMPDIR:-/tmp/lhd_abc_boundary_$$}"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}
run() { "$LHD" "$@" -q --result-json "$W/r.json" || fail "$* -> $(cat "$W/r.json" 2>/dev/null)"; }

[ -f "$PRP" ] || fail "missing fixture $PRP"
[ -f "$LIB" ] || fail "missing liberty $LIB"

run compile "$PRP" --top "$TOP" --emit-dir lg:"$W/lg" --workdir "$W/w1"

# 1. boundary on (the default): the driver region takes the stronger NAND
run pass abc --top "$TOP" lg:"$W/lg" --emit-dir lg:"$W/net_on" --set synth.liberty="$LIB" \
    --set abc.delay=25 --workdir "$W/w_on"
python3 - "$W/w_on/qor.json" "$TOP" <<'PY' || fail "boundary=true qor.json has no boundary scoreboard"
import json, sys
q = json.load(open(sys.argv[1]))
b = q["total"]["boundary"]
assert b["bits"] > 0 and b["resized"] >= 1, b
c1 = [r for r in q["regions"] if r["module"] == sys.argv[2] + "__c1"][0]
assert c1["boundary"]["resized"] >= 1 and c1["boundary"]["delay_pre"] > 0, c1
PY
run compile lg:"$W/net_on" --top "$TOP" --emit-dir verilog:"$W/v_on" --workdir "$W/wv_on"
grep -q "NAND2x2" "$W/v_on/${TOP}__c1.v" || fail "boundary=true left the crossing driver on NAND2x1"
grep -q "pass.abc boundary: .* cell(s) re-sized" "$W/w_on/logs/"*_lhd_pass_abc.log \
  || fail "no boundary refinement summary in the pass log"

# 2. boundary off: the old behaviour, load-free ports
run pass abc --top "$TOP" lg:"$W/lg" --emit-dir lg:"$W/net_off" --set synth.liberty="$LIB" \
    --set abc.delay=25 --set abc.boundary=false --workdir "$W/w_off"
grep -q '"boundary"' "$W/w_off/qor.json" && fail "boundary=false still reports a boundary scoreboard"
run compile lg:"$W/net_off" --top "$TOP" --emit-dir verilog:"$W/v_off" --workdir "$W/wv_off"
grep -q "NAND2x2" "$W/v_off/${TOP}__c1.v" && fail "boundary=false sized the crossing driver up"
grep -q "buffer -N 16 -p" "$W/w_off/logs/"*_lhd_pass_abc.log && fail "boundary=false still buffers primary inputs"

# 3. the refined netlist is LEC-equivalent to the partition twin
run pass partition --top "$TOP" lg:"$W/lg" --emit-dir lg:"$W/re" --workdir "$W/w_re"
run pass liberty gensim "$LIB" --emit-dir lg:"$W/models" --workdir "$W/w_models"
run compile lg:"$W/models" --emit-dir verilog:"$W/modelsv" --workdir "$W/w_modelsv"
run compile lg:"$W/re" --top "$TOP" --emit-dir verilog:"$W/rev" --workdir "$W/w_rev"
cat "$W/v_on/"*.v "$W/modelsv/"*.v > "$W/impl.v"
cat "$W/rev/"*.v > "$W/ref.v"
run lec --set formal.solver=lgyosys --impl verilog:"$W/impl.v" --ref verilog:"$W/ref.v" --top "$TOP" --workdir "$W/w_lec"
echo "PASS: boundary-sized netlist is LEC-equivalent to the partition twin"

# 4. incremental: the cache holds the refined bodies; an all-hit run neither
# starts ABC nor re-refines, and emits the same netlist
I="$W/incr"
mkdir -p "$I"
run pass abc --top "$TOP" lg:"$W/lg" --emit-dir lg:"$I/net1" --set synth.liberty="$LIB" \
    --set abc.delay=25 --workdir "$I/w"
run compile lg:"$I/net1" --top "$TOP" --emit-dir verilog:"$I/v1" --workdir "$I/wv1"
run pass abc --top "$TOP" lg:"$W/lg" --emit-dir lg:"$I/net2" --set synth.liberty="$LIB" \
    --set abc.delay=25 --workdir "$I/w"
grep -q '"incremental":{"hits":[1-9][0-9]*,"misses":0' "$I/w/qor.json" || fail "second run was not all hits: $(grep -o '"incremental":{[^}]*}' "$I/w/qor.json")"
grep -q '"abc_started":0' "$I/w/qor.json" || fail "all-hit run started ABC"
grep -q "pass.abc boundary:" "$I/w/logs/"*_lhd_pass_abc.log && {
  n=$(grep -c "pass.abc boundary:" "$I/w/logs/"*_lhd_pass_abc.log | awk -F: '{s+=$NF} END {print s}')
  [ "$n" = "1" ] || fail "the all-hit run re-ran the boundary refinement"
}
run compile lg:"$I/net2" --top "$TOP" --emit-dir verilog:"$I/v2" --workdir "$I/wv2"
for f in "$I/v1/"*.v; do
  cmp -s "$f" "$I/v2/$(basename "$f")" || fail "all-hit run emitted a different netlist: $(basename "$f")"
done
grep -q "NAND2x2" "$I/v2/${TOP}__c1.v" || fail "the cached body lost the boundary re-size"
echo "PASS: all-hit incremental run reuses the refined bodies without ABC"

echo "PASS: pass.abc partition-boundary environment"
