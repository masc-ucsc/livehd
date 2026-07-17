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
#   lhd lec --set formal.solver=lgyosys (impl vs ref): must be LEC-equivalent
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
run lec --set formal.solver=lgyosys --impl verilog:"$W/impl.v" --ref verilog:"$W/ref.v" --top "$TOP" --workdir "$W/wc"

# 8. negative control: a corrupted reference MUST fail the equivalence check
sed 's/\^/\&/g' "$W/ref.v" > "$W/ref_bad.v"
if "$LHD" lec --set formal.solver=lgyosys --impl verilog:"$W/impl.v" --ref verilog:"$W/ref_bad.v" --top "$TOP" \
    --workdir "$W/wcn" -q --result-json "$W/rn.json" 2>/dev/null; then
  fail "negative control passed LEC against a corrupted reference (the check is not sound)"
fi

echo "PASS: pass.abc tech-map LEC-equivalent to original logic (+ negative control)"

# ---------------------------------------------------------------------------
# No prior coloring: `pass abc` must run WITHOUT `pass color` first. Color 0 (an
# uncolored design) is treated as just another color — the whole design folds
# into one color-0 region — with a single non-fatal warning, and the tech-mapped
# netlist stays LEC-equivalent to the original logic.
# ---------------------------------------------------------------------------
N="$W/nocolor"
mkdir -p "$N"
run compile "$PRP" --top "$TOP" --recipe O1 --emit-dir lg:"$N/lg" --workdir "$N/w1"
# abc directly on the uncolored design (NO pass color) — must succeed + warn once
"$LHD" pass abc --top "$TOP" lg:"$N/lg" --emit-dir lg:"$N/net" --set abc.library="$LIB" \
    -q --result-json "$N/r.json" --workdir "$N/w2" || fail "pass abc without color failed -> $(cat "$N/r.json" 2>/dev/null)"
grep -q '"diagnostics_count":{"errors":0,"warnings":1}' "$N/r.json" \
  || fail "expected one uncolored-node warning, got $(grep -o '"diagnostics_count":{[^}]*}' "$N/r.json")"
# behavioral cell models + original-design reference, then emit + LEC
run pass liberty gensim "$LIB" --emit-dir lg:"$N/models" --workdir "$N/w3"
run compile lg:"$N/net" --top "$TOP" --recipe O0 --emit-dir verilog:"$N/netv" --workdir "$N/w4"
run compile lg:"$N/models" --recipe O0 --emit-dir verilog:"$N/modelsv" --workdir "$N/w5"
run compile lg:"$N/lg" --top "$TOP" --recipe O0 --emit-dir verilog:"$N/origv" --workdir "$N/w6"
grep -q "NAND2x1\|NOR2x1\|INVx1\|XOR2x1" "$N/netv/${TOP}__c0"*.v || fail "no standard cells in the uncolored ABC netlist"
cat "$N/netv/"*.v "$N/modelsv/"*.v > "$N/impl.v"
cat "$N/origv/"*.v > "$N/orig.v"
run lec --set formal.solver=lgyosys --impl verilog:"$N/impl.v" --ref verilog:"$N/orig.v" --top "$TOP" --workdir "$N/c"
echo "PASS: pass.abc runs WITHOUT a prior color pass (color-0 region, LEC-equivalent)"

# ---------------------------------------------------------------------------
# abc.rc script alias in `flow`: the library entry never sources abc.rc, so the
# pass installs the standard scripts (resyn2, compress2rs, ...) as aliases. A
# `flow="...resyn2..."` must resolve and still produce a LEC-equivalent netlist.
# Reuses the colored lg + cell models + reference from the top of this test.
# ---------------------------------------------------------------------------
A="$W/alias"
mkdir -p "$A"
run pass abc --top "$TOP" lg:"$W/lg" --emit-dir lg:"$A/net" --set abc.library="$LIB" \
    --set abc.flow="strash; resyn2; &get -n; &dch -f; &nf {D}; &put" --workdir "$A/w1"
run compile lg:"$A/net" --top "$TOP" --recipe O0 --emit-dir verilog:"$A/netv" --workdir "$A/w2"
grep -q "NAND2x1\|NOR2x1\|INVx1\|XOR2x1" "$A/netv/${TOP}__c"*.v || fail "no standard cells in the resyn2-mapped netlist (alias did not resolve?)"
cat "$A/netv/"*.v "$W/modelsv/"*.v > "$A/impl.v"
run lec --set formal.solver=lgyosys --impl verilog:"$A/impl.v" --ref verilog:"$W/ref.v" --top "$TOP" --workdir "$A/c"
echo "PASS: pass.abc resolves abc.rc script aliases in flow (resyn2, LEC-equivalent)"
