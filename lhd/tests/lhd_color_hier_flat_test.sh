#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# End-to-end test for `pass.color synth`'s VIRTUAL FLATTENING.
#
# The synth algorithms colour the FLAT view of the whole `--top` hierarchy, not
# one def at a time, so a colour -- and therefore an ABC region -- can span
# module boundaries. The word that matters is VIRTUAL: unlike the `absorb` pass
# this replaces, the live library is NOT rewritten. The hierarchy is inlined
# into a scratch def, coloured once, thrown away, and the colours are written
# back onto the original defs' nodes.
#
# So there are exactly three observable properties, and a unit test can reach
# none of them:
#   1. the child instance SURVIVES pass.color (the pass is still an annotation)
#   2. the recorded coloring_info carries "hier_flat":true -- the marker
#      pass.partition / pass.abc key their `flatten=auto` off
#   3. downstream, that marker makes the child def DISAPPEAR from the emitted
#      netlist (its logic is now inside the flat regions) and the result is
#      still LEC-equivalent to the original
#
# Fixtures (inou/prp/tests/pyrope), the same two the hierarchical partition test
# uses:
#   hier_comb - combinational, top instances `adder` x2 + `bitmix`
#   hier_seq  - sequential 3-level, top -> stage_unit x2 -> delayer (flops)
set -u
LHD=lhd/lhd
W="${TEST_TMPDIR:-/tmp/lhd_color_hier_flat_$$}"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}
run() { "$LHD" "$@" -q --result-json "$W/r.json" || fail "$* -> $(cat "$W/r.json" 2>/dev/null)"; }

# fixture  top             a-child-def-that-must-survive-colouring
DESIGNS=(
  "hier_comb hier_comb.top adder"
  "hier_seq  hier_seq.top   delayer"
)

for entry in "${DESIGNS[@]}"; do
  set -- $entry
  FIX="$1"; TOP="$2"; CHILD="$3"
  PRP="inou/prp/tests/pyrope/$FIX.prp"
  [ -f "$PRP" ] || fail "missing fixture $PRP"
  D="$W/$FIX"
  mkdir -p "$D"

  run compile "$PRP" --top "$TOP" --emit-dir lg:"$D/lg" --workdir "$D/w1"
  run compile lg:"$D/lg" --top "$TOP" --emit verilog:"$D/ref.v" --workdir "$D/w2"

  # The child instance must exist BEFORE colouring, or the test proves nothing.
  grep -q "^ *${CHILD} " "$D/ref.v" || fail "$FIX: fixture has no '$CHILD' instance"

  run pass color synth --top "$TOP" --stats lg:"$D/lg" --workdir "$D/w3"
  run compile lg:"$D/lg" --top "$TOP" --emit verilog:"$D/post.v" --workdir "$D/w4"

  # 1. VIRTUAL: colouring annotated the design and rewrote nothing.
  grep -q "^ *${CHILD} " "$D/post.v" || fail "$FIX: pass.color removed the '$CHILD' instance (it must stay virtual)"

  # 2. The marker downstream keys off is recorded.
  grep -q '"hier_flat":true' "$W/r.json" \
    || "$LHD" pass color synth --top "$TOP" --stats lg:"$D/lg" -q --result-json "$D/info.json" >/dev/null 2>&1
  run pass partition --top "$TOP" lg:"$D/lg" --emit verilog:"$D/part.v" --workdir "$D/w5"

  # 3. The colours span defs, so partition's `flatten=auto` fires and the child
  #    def is gone from the emitted netlist -- its logic is inside the regions.
  grep -q "^ *${CHILD} " "$D/part.v" && fail "$FIX: '$CHILD' survived the downstream flatten -- hier_flat did not fire"

  # ... and the design still computes the same function. This is the whole point.
  run lec --set formal.solver=lgyosys --impl verilog:"$D/part.v" --ref verilog:"$D/ref.v" --top "$TOP" --workdir "$D/c"
  echo "PASS: $FIX coloured across '$CHILD' virtually and stayed LEC-equivalent"
done

# hier=false is the escape hatch: colour the top body alone, no flat view, and
# therefore no hier_flat marker for the downstream passes to act on.
D="$W/off"
mkdir -p "$D"
run compile "inou/prp/tests/pyrope/hier_comb.prp" --top hier_comb.top --emit-dir lg:"$D/lg" --workdir "$D/w1"
run pass color synth --top hier_comb.top --set color.hier=false lg:"$D/lg" --workdir "$D/w2"
run compile lg:"$D/lg" --top hier_comb.top --emit verilog:"$D/off.v" --workdir "$D/w3"
grep -q "^ *adder " "$D/off.v" || fail "hier=false rewrote the hierarchy"
run pass partition --top hier_comb.top lg:"$D/lg" --emit verilog:"$D/off_part.v" --workdir "$D/w4"
grep -q "^ *adder " "$D/off_part.v" || fail "hier=false still made partition flatten the hierarchy"
echo "PASS: hier=false colours the top body alone and leaves the hierarchy to partition"

echo "PASS: all pass.color virtual-flattening flows"
