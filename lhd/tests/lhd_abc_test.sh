#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# End-to-end test for `lhd pass abc` (task 2a-abc): technology-map a colored
# combinational design to a standard-cell netlist of blackbox Sub cells, and
# prove it LEC-equivalent to the original logic. The companion
# `pass liberty gensim` supplies a behavioral model per cell so the LEC stays
# self-contained (no PDK Verilog).
#
#   prp -> lg (O1)
#   pass color synth          (the abc driver coloring)
#   pass abc   --emit-dir lg:net   (partition + ABC tech-map per region)
#   pass partition --emit-dir lg:re  (same module structure, original logic)
#   pass liberty gensim test.lib --emit-dir lg:models
#   cgen net + models -> impl.v ; cgen re -> ref.v
#   lhd lec --set lec.solver=lgyosys (impl vs ref): must be LEC-equivalent
#   negative control: a corrupted reference must FAIL the check
#
# Hermetic: uses a small vendored Liberty (inou/prp/tests/abc/test.lib), not the
# sky130 PDK.

set -u

LHD=lhd/lhd
LIB=inou/prp/tests/abc/test.lib
PRP=inou/prp/tests/pyrope/abc_comb.prp
TOP=abc_comb.abc_comb
W="${TEST_TMPDIR:-/tmp/lhd_abc_$$}"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}
run() { "$LHD" "$@" -q --result-json "$W/r.json" || fail "$* -> $(cat "$W/r.json" 2>/dev/null)"; }

[ -f "$PRP" ] || fail "missing fixture $PRP"
[ -f "$LIB" ] || fail "missing liberty $LIB"

# 1. compile the flat combinational design to an lg library
run compile "$PRP" --top "$TOP" --recipe O1 --emit-dir lg:"$W/lg" --workdir "$W/w1"
# 2. color every node (synth boundaries = the abc driver)
run pass color synth --top "$TOP" lg:"$W/lg" --workdir "$W/w2"
# 3. ABC technology-map each colored region -> standard-cell netlist
run pass abc --top "$TOP" lg:"$W/lg" --emit-dir lg:"$W/net" --set abc.library="$LIB" --workdir "$W/w3"
# 4. partition the SAME regions, keeping the original logic (the LEC twin)
run pass partition --top "$TOP" lg:"$W/lg" --emit-dir lg:"$W/re" --workdir "$W/w4"
# 5. behavioral model per combinational cell so the netlist Subs resolve for LEC
run pass liberty gensim "$LIB" --emit-dir lg:"$W/models" --workdir "$W/w5"

# 6. emit Verilog: impl = netlist modules + cell models ; ref = original logic
run compile lg:"$W/net" --top "$TOP" --recipe O0 --emit-dir verilog:"$W/netv" --workdir "$W/w6"
run compile lg:"$W/models" --recipe O0 --emit-dir verilog:"$W/modelsv" --workdir "$W/w7"
run compile lg:"$W/re" --top "$TOP" --recipe O0 --emit-dir verilog:"$W/rev" --workdir "$W/w8"

# the netlist really is a standard-cell netlist (Sub instances of Liberty cells)
grep -q "NAND2x1\|NOR2x1\|INVx1\|XOR2x1" "$W/netv/${TOP}__c"*.v || fail "no standard cells in the ABC netlist"

cat "$W/netv/"*.v "$W/modelsv/"*.v > "$W/impl.v"
cat "$W/rev/"*.v > "$W/ref.v"

# 7. LEC: the tech-mapped netlist must equal the original logic
run lec --set lec.solver=lgyosys --impl verilog:"$W/impl.v" --ref verilog:"$W/ref.v" --top "$TOP" --workdir "$W/wc"

# 8. negative control: a corrupted reference MUST fail the equivalence check
sed 's/\^/\&/g' "$W/ref.v" > "$W/ref_bad.v"
if "$LHD" lec --set lec.solver=lgyosys --impl verilog:"$W/impl.v" --ref verilog:"$W/ref_bad.v" --top "$TOP" \
    --workdir "$W/wcn" -q --result-json "$W/rn.json" 2>/dev/null; then
  fail "negative control passed LEC against a corrupted reference (the check is not sound)"
fi

echo "PASS: pass.abc tech-map LEC-equivalent to original logic (+ negative control)"
