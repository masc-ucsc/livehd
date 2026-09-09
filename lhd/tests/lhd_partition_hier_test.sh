#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# Hierarchical end-to-end test for `lhd pass color` + `lhd pass partition`
# (2c-color / 2p hierarchical-input; prerequisite for 2a-abc). Unlike
# lhd_partition_test.sh (a single flat Verilog module), this drives two
# multi-module Pyrope fixtures so the passes must descend the hierarchy:
#
#   prp -> lg
#   lhd pass color <alg>                  (colors EVERY def: top + sub-defs)
#   lhd pass partition --emit-dir lg:dir2 (partitions every def + re-links Subs)
#   lg:dir2 -> verilog
#   lhd lec --set formal.solver=lgyosys (partitioned vs original): must be LEC-equivalent
#
# Fixtures (inou/prp/tests/pyrope):
#   hier_comb  - combinational, top instances `adder` x2 + `bitmix`
#   hier_seq   - sequential 3-level, top -> stage_unit x2 -> delayer (flops)
#
# `acyclic` is the coloring here, because it is the one that still colours PER
# DEF and therefore exercises the re-link. `synth` deliberately no longer does:
# it colours the flat view of the hierarchy (virtual flattening) and records
# "hier_flat":true, so pass.partition inlines the hierarchy instead of re-linking
# it -- that path has its own end-to-end test in lhd_color_hier_flat_test.sh.

set -u

LHD=lhd/lhd
W="${TEST_TMPDIR:-/tmp/lhd_partition_hier_$$}"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}
run() { "$LHD" "$@" -q --result-json "$W/r.json" || fail "$* -> $(cat "$W/r.json" 2>/dev/null)"; }

# fixture  top                child-def-kept
DESIGNS=(
  "hier_comb hier_comb.top adder"
  "hier_seq  hier_seq.top   delayer"
)

for entry in "${DESIGNS[@]}"; do
  set -- $entry
  FIX="$1"; TOP="$2"; CHILD="$3"
  PRP="inou/prp/tests/pyrope/$FIX.prp"
  [ -f "$PRP" ] || fail "missing fixture $PRP"
  BASE="${TOP%.*}"  # e.g. hier_comb
  for ALG in acyclic; do
    D="$W/$FIX/$ALG"
    mkdir -p "$D"
    # 1. compile the hierarchical design to an lg library (all defs)
    run compile "$PRP" --top "$TOP" --emit-dir lg:"$D/lg" --workdir "$D/w1"
    # 2. reference Verilog (pre-color; coloring only adds attrs, but keep it clean)
    run compile lg:"$D/lg" --top "$TOP" --emit verilog:"$D/ref.v" --workdir "$D/w2"
    # 3. color every def in the hierarchy.
    run pass color "$ALG" --top "$TOP" lg:"$D/lg" --workdir "$D/w3"
    # 4. partition every def + re-link Sub instances into a fresh library
    run pass partition --top "$TOP" lg:"$D/lg" --emit-dir lg:"$D/lg2" --workdir "$D/w4"
    # 5. emit Verilog from the partitioned library (verbatim, no re-opt)
    run compile lg:"$D/lg2" --top "$TOP" --emit verilog:"$D/part.v" --workdir "$D/w5"
    # hierarchy preserved: the child def survives as its own module (partition
    # re-links the Sub instances to it).
    grep -q "^module ${CHILD}" "$D/part.v" || fail "$FIX/$ALG: child def '$CHILD' dropped (hierarchy lost)"
    # The coloring splits at least one def into several colors -> per-(def,
    # color) region submodules under a wrapper. The single-region optimization --
    # no pointless `<def>__c<id>` wrapper whose only body is one region instance
    # -- is pinned below.
    grep -q "__c" "$D/part.v" || fail "$FIX/$ALG: multi-region partition has no per-color submodules"
    # 6. LEC: the partitioned hierarchical design must equal the original
    run lec --set formal.solver=lgyosys --impl verilog:"$D/part.v" --ref verilog:"$D/ref.v" --top "$TOP" --workdir "$D/c"
    echo "PASS: $FIX [$ALG] hierarchical partition is LEC-equivalent to the original"
  done
done

# The single-region optimization: a def that IS one region is emitted directly
# under its own name -- no `<def>__c<id>` wrapper whose only body is one region
# instance. `synth_alg=pipe` on the purely combinational fixture is the coloring
# that gives it (pipe cuts at state only, and hier_comb has none), so every def
# there is exactly one region. `hier=false` keeps it a PER-DEF coloring: it
# colours the top body alone (one region) and leaves the children uncolored (one
# color-0 region each), which is the shape this optimization is about. With the
# hierarchical default the colours would span defs and partition would flatten.
# The hierarchy and the LEC must survive it too.
FD="$W/onecolor"
mkdir -p "$FD"
run compile "inou/prp/tests/pyrope/hier_comb.prp" --top hier_comb.top --emit-dir lg:"$FD/lg" --workdir "$FD/w1"
run compile lg:"$FD/lg" --top hier_comb.top --emit verilog:"$FD/ref.v" --workdir "$FD/w2"
run pass color synth --top hier_comb.top --set color.hier=false --set color.synth_alg=pipe lg:"$FD/lg" --workdir "$FD/w3"
run pass partition --top hier_comb.top lg:"$FD/lg" --emit-dir lg:"$FD/lg2" --workdir "$FD/w4"
run compile lg:"$FD/lg2" --top hier_comb.top --emit verilog:"$FD/part.v" --workdir "$FD/w5"
grep -q "^module adder" "$FD/part.v" || fail "pipe: child def 'adder' dropped (hierarchy lost)"
grep -q "__c" "$FD/part.v" && fail "pipe: single-region defs must not get a __c wrapper"
run lec --set formal.solver=lgyosys --impl verilog:"$FD/part.v" --ref verilog:"$FD/ref.v" --top hier_comb.top --workdir "$FD/c"
echo "PASS: single-region-per-def partition needs no __c wrapper and is LEC-equivalent"

# stats-only mode on a hierarchical input must succeed (per-def region stats).
SD="$W/stats"
mkdir -p "$SD"
run compile "inou/prp/tests/pyrope/hier_comb.prp" --top hier_comb.top --emit-dir lg:"$SD/lg" --workdir "$SD/w1"
run pass color synth --top hier_comb.top lg:"$SD/lg" --workdir "$SD/w2"
run pass partition --top hier_comb.top lg:"$SD/lg" --workdir "$SD/w3"
echo "PASS: hierarchical partition stats-only mode"

echo "PASS: all hierarchical pass.color/pass.partition flows"
