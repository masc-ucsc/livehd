#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# Exercises the "Linking libraries (Pyrope + a Verilog black box)" example in
# ../docs/docs/livehd/02-usage.md — keep the command sequence here in sync with
# that section. It mixes a yosys-elaborated Verilog leaf (`inv`, a black box) and
# a Pyrope leaf (`adder.adder`), imported as compiled LGraphs into a Pyrope top,
# then links them into one new lg: library and emits Verilog.

set -u

LHD=lhd/lhd
D=lhd/tests/merge_demo
W="${TEST_TMPDIR:-/tmp/lhd_usage_merge_$$}"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

# 1. Verilog leaf -> lg: (through yosys)
"$LHD" compile "$D/inv.v" --top inv --emit-dir lg:"$W/inv_lg/" --workdir "$W/w1" -q --result-json "$W/r1.json" \
  || fail "inv.v compile→lg failed: $(cat "$W/r1.json" 2>/dev/null)"

# 2. Pyrope leaf -> lg:
"$LHD" compile "$D/adder.prp" --emit-dir lg:"$W/adder_lg/" --workdir "$W/w2" -q --result-json "$W/r2.json" \
  || fail "adder.prp compile→lg failed: $(cat "$W/r2.json" 2>/dev/null)"

# 3. Top Pyrope importing BOTH -> ln: (imports stay unresolved until link)
"$LHD" compile "$D/top.prp" --emit-dir ln:"$W/top_ln/" --workdir "$W/w3" -q --result-json "$W/r3.json" \
  || fail "top.prp compile→ln failed: $(cat "$W/r3.json" 2>/dev/null)"

# 4. LINK: merge the two lg: libraries + lower the top against them -> new lg:
"$LHD" compile --top top lg:"$W/inv_lg/" lg:"$W/adder_lg/" ln:"$W/top_ln/" \
  --emit-dir lg:"$W/merged_lg/" --workdir "$W/w4" -q --result-json "$W/r4.json" \
  || fail "link/merge failed: $(cat "$W/r4.json" 2>/dev/null)"
grep -q '"status":"pass"' "$W/r4.json" || fail "merge not pass: $(cat "$W/r4.json")"
# All three units present in the assembled library, with bodies.
for n in inv adder.adder top.top; do
  grep -q "graph_io .* ${n}\$" "$W/merged_lg/library.txt" || fail "merged lib misses ${n}: $(cat "$W/merged_lg/library.txt")"
done

# 5. compile the assembled library -> Verilog
"$LHD" compile lg:"$W/merged_lg/" --emit verilog:"$W/top.v" --workdir "$W/w5" -q --result-json "$W/r5.json" \
  || fail "compile of merged library failed: $(cat "$W/r5.json" 2>/dev/null)"
grep -q '"status":"pass"' "$W/r5.json" || fail "compile not pass"
grep -q 'module inv' "$W/top.v" || fail "verilog misses module inv: $(cat "$W/top.v")"
grep -q 'module \\adder.adder' "$W/top.v" || fail "verilog misses module adder.adder"
grep -q 'module \\top.top' "$W/top.v" || fail "verilog misses module top.top"
grep -q 'inv u_inv' "$W/top.v" || fail "top does not instantiate the inv black box"
grep -q '\\adder.adder .*\\u_adder' "$W/top.v" || fail "top does not instantiate adder.adder"

echo "lhd_usage_merge_test passed"
