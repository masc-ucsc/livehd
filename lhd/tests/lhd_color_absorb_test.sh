#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# End-to-end test for `pass.color synth`'s ABSORB (2c-color-size subtask D):
# every def below the size window's `min` is structurally inlined into its
# parents, so its logic can cluster with its neighbours under one ABC region.
#
#   prp -> lg (O1, hierarchy intact)
#   reference Verilog from the untouched library
#   lhd pass color synth        (absorb: default min=1000 GE)
#   Verilog from the absorbed library
#   lhd lec (absorbed vs original): must be LEC-equivalent
#
# Absorb REWRITES the netlist -- it is the one thing pass.color does that is not
# an annotation -- so the property that matters is the one a unit test cannot
# give: the design still computes exactly what it did. Every def in these
# fixtures is a few dozen gate-equivalents, so `min=1000` absorbs all of them and
# the top ends up holding the whole design.
#
# Fixtures (inou/prp/tests/pyrope), same two the hierarchical partition test uses:
#   hier_comb - combinational, top instances `adder` x2 + `bitmix`
#   hier_seq  - sequential 3-level, top -> stage_unit x2 -> delayer (flops)
set -u
LHD=lhd/lhd
W="${TEST_TMPDIR:-/tmp/lhd_color_absorb_$$}"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}
run() { "$LHD" "$@" -q --result-json "$W/r.json" || fail "$* -> $(cat "$W/r.json" 2>/dev/null)"; }

# fixture  top             a-child-def-that-must-be-absorbed
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

  run compile "$PRP" --top "$TOP" --recipe O1 --emit-dir lg:"$D/lg" --workdir "$D/w1"
  run compile lg:"$D/lg" --top "$TOP" --recipe O0 --emit verilog:"$D/ref.v" --workdir "$D/w2"

  # The child instance must exist BEFORE absorb, or the test proves nothing.
  grep -q "^ *${CHILD} " "$D/ref.v" || fail "$FIX: fixture has no '$CHILD' instance to absorb"

  # Absorb + color. `min` defaults to 1000 GE, far above every def here.
  run pass color synth --top "$TOP" --stats lg:"$D/lg" --workdir "$D/w3"
  run compile lg:"$D/lg" --top "$TOP" --recipe O0 --emit verilog:"$D/post.v" --workdir "$D/w4"

  # The instance is gone: its logic is now the parent's own.
  grep -q "^ *${CHILD} " "$D/post.v" && fail "$FIX: '$CHILD' still instantiated after absorb"

  # ... and the design still computes the same function. This is the whole point.
  run lec --set formal.solver=lgyosys --impl verilog:"$D/post.v" --ref verilog:"$D/ref.v" --top "$TOP" --workdir "$D/c"
  echo "PASS: $FIX absorbed '$CHILD' and stayed LEC-equivalent"
done

# absorb=false is the escape hatch: pass.color stays a pure annotation pass.
D="$W/off"
mkdir -p "$D"
run compile "inou/prp/tests/pyrope/hier_comb.prp" --top hier_comb.top --recipe O1 --emit-dir lg:"$D/lg" --workdir "$D/w1"
run pass color synth --top hier_comb.top --set color.absorb=false lg:"$D/lg" --workdir "$D/w2"
run compile lg:"$D/lg" --top hier_comb.top --recipe O0 --emit verilog:"$D/off.v" --workdir "$D/w3"
grep -q "^ *adder " "$D/off.v" || fail "absorb=false still inlined 'adder'"
echo "PASS: absorb=false leaves the hierarchy alone"

# min=0 disables the window entirely, and with it absorb.
D="$W/min0"
mkdir -p "$D"
run compile "inou/prp/tests/pyrope/hier_comb.prp" --top hier_comb.top --recipe O1 --emit-dir lg:"$D/lg" --workdir "$D/w1"
run pass color synth --top hier_comb.top --set color.min_ge=0 lg:"$D/lg" --workdir "$D/w2"
run compile lg:"$D/lg" --top hier_comb.top --recipe O0 --emit verilog:"$D/min0.v" --workdir "$D/w3"
grep -q "^ *adder " "$D/min0.v" || fail "min=0 still inlined 'adder'"
echo "PASS: min=0 disables absorb"

echo "PASS: all pass.color absorb flows"
