#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# End-to-end test for block-scoped synthesis attributes (2opt-freq B):
#   `{ ::[abc='<flow>', color=N] stmts }` makes the block its own synthesis
#   partition region with a per-region ABC flow override.
#
#   1. LEC invariance: the annotation must change ZERO semantics — the
#      annotated source is PROVEN equivalent to the same source with the
#      attribute stripped.
#   2. compile -> pass abc (NO pass.color): the block becomes region __c2
#      (color 2) in qor.json and its abc= flow override is applied from the
#      graph-embedded coloring_info "region_opts".
#   3. pass color synth -> pass abc: SEEDED precedence — the block region
#      survives the algorithm (still color 2, override still applied) and the
#      algorithm's ids allocate above it.
#   4. The tech-mapped netlist LECs against its pass.partition twin (lgyosys +
#      gensim cell models).
#   5. Negative controls: an unknown scope attribute and a double-quoted
#      abc= flow carrying `{` (string interpolation would corrupt `{D}`) must
#      FAIL the compile — a mistyped hint never silently no-ops.
#
# Hermetic: the small vendored Liberty (inou/prp/tests/abc/test.lib).

set -u

LHD=lhd/lhd
LIB=inou/prp/tests/abc/test.lib
PRP=inou/prp/tests/pyrope/abc_block_attr.prp
TOP=abc_block_attr.abc_block_attr
W="${TEST_TMPDIR:-/tmp/lhd_block_attr_$$}"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}
run() { "$LHD" "$@" -q --result-json "$W/r.json" || fail "$* -> $(cat "$W/r.json" 2>/dev/null)"; }

[ -f "$PRP" ] || fail "missing fixture $PRP"
[ -f "$LIB" ] || fail "missing liberty $LIB"

# 1. the annotation is semantics-free: annotated vs stripped source PROVEN
sed 's/{::\[.*\]/{/' "$PRP" > "$W/plain.prp"
run lec --impl "$PRP" --ref "$W/plain.prp" --top "$TOP" --workdir "$W/wl"

# 2. compile + abc WITHOUT pass.color: the block is its own region
run compile "$PRP" --top "$TOP" --recipe O1 --emit-dir lg:"$W/lg" --workdir "$W/w1"
run pass abc --top "$TOP" lg:"$W/lg" --emit-dir lg:"$W/net" --set abc.library="$LIB" --workdir "$W/w2"
grep -q "\"module\":\"${TOP}__c2\",\"color\":2" "$W/w2/qor.json" || fail "block region __c2 missing from qor.json"
grep -q "color 2 options override applied (coloring_info)" "$W/w2/logs/"*.log \
  || fail "block abc= flow override was not applied from coloring_info"

# 3. seeded precedence: pass.color must keep the block region + still override
run pass color synth --top "$TOP" lg:"$W/lg" --workdir "$W/w3"
run pass abc --top "$TOP" lg:"$W/lg" --emit-dir lg:"$W/net2" --set abc.library="$LIB" --workdir "$W/w4"
grep -q "\"module\":\"${TOP}__c2\",\"color\":2" "$W/w4/qor.json" || fail "block region lost after pass color synth"
grep -q "color 2 options override applied (coloring_info)" "$W/w4/logs/"*.log \
  || fail "block abc= flow override lost after pass color synth"

# 4. the mapped netlist (post-color run) LECs against its partition twin
run pass partition --top "$TOP" lg:"$W/lg" --emit-dir lg:"$W/re" --workdir "$W/w5"
run pass liberty gensim "$LIB" --emit-dir lg:"$W/models" --workdir "$W/w6"
run compile lg:"$W/net2" --top "$TOP" --recipe O0 --emit-dir verilog:"$W/netv" --workdir "$W/w7"
run compile lg:"$W/models" --recipe O0 --emit-dir verilog:"$W/modelsv" --workdir "$W/w8"
run compile lg:"$W/re" --top "$TOP" --recipe O0 --emit-dir verilog:"$W/rev" --workdir "$W/w9"
cat "$W/netv/"*.v "$W/modelsv/"*.v > "$W/impl.v"
cat "$W/rev/"*.v > "$W/ref.v"
run lec --set formal.solver=lgyosys --impl verilog:"$W/impl.v" --ref verilog:"$W/ref.v" --top "$TOP" --workdir "$W/wc"

# 5a. negative control: unknown scope attribute must fail the compile
sed "s/abc='[^']*'/colour=3/" "$PRP" > "$W/bad_key.prp"
if "$LHD" compile "$W/bad_key.prp" --top "$TOP" --recipe O1 --emit-dir lg:"$W/lgbad" --workdir "$W/wn1" \
    -q --result-json "$W/rn1.json" 2>/dev/null; then
  fail "unknown scope attribute compiled clean; expected a hard error"
fi

# 5b. negative control: double-quoted abc= flow with `{` (interpolation trap)
sed "s/abc='\([^']*\)'/abc=\"\1\"/" "$PRP" > "$W/bad_quote.prp"
if "$LHD" compile "$W/bad_quote.prp" --top "$TOP" --recipe O1 --emit-dir lg:"$W/lgbad2" --workdir "$W/wn2" \
    -q --result-json "$W/rn2.json" 2>/dev/null; then
  fail "double-quoted abc= flow with {D} compiled clean; expected a hard error"
fi

echo "PASS: block-scoped synthesis attributes (LEC-invariant, own region + flow override, seeded precedence, netlist LEC, negative controls)"
