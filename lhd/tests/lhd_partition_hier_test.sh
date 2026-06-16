#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# Hierarchical end-to-end test for `lhd pass color` + `lhd pass partition`
# (2c-color / 2p hierarchical-input; prerequisite for 2a-abc). Unlike
# lhd_partition_test.sh (a single flat Verilog module), this drives two
# multi-module Pyrope fixtures so the passes must descend the hierarchy:
#
#   prp -> lg (O1)
#   lhd pass color <alg>                  (colors EVERY def: top + sub-defs)
#   lhd pass partition --emit-dir lg:dir2 (partitions every def + re-links Subs)
#   lg:dir2 -> verilog
#   lhd check (partitioned vs original): must be LEC-equivalent
#
# Fixtures (inou/prp/tests/pyrope):
#   hier_comb  - combinational, top instances `adder` x2 + `bitmix`
#   hier_seq   - sequential 3-level, top -> stage_unit x2 -> delayer (flops)
#
# Covers both the `synth` (the abc driver) and `acyclic` colorings.

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
  for ALG in synth acyclic; do
    D="$W/$FIX/$ALG"
    mkdir -p "$D"
    # 1. compile the hierarchical design to an lg library (all defs)
    run compile "$PRP" --top "$TOP" --recipe O1 --emit-dir lg:"$D/lg" --workdir "$D/w1"
    # 2. reference Verilog (pre-color; coloring only adds attrs, but keep it clean)
    run compile lg:"$D/lg" --top "$TOP" --recipe O0 --emit verilog:"$D/ref.v" --workdir "$D/w2"
    # 3. color every def in the hierarchy
    run pass color "$ALG" --top "$TOP" lg:"$D/lg" --workdir "$D/w3"
    # 4. partition every def + re-link Sub instances into a fresh library
    run pass partition --top "$TOP" lg:"$D/lg" --emit-dir lg:"$D/lg2" --workdir "$D/w4"
    # 5. emit Verilog from the partitioned library (verbatim, no re-opt)
    run compile lg:"$D/lg2" --top "$TOP" --recipe O0 --emit verilog:"$D/part.v" --workdir "$D/w5"
    # hierarchy preserved: per-color submodules exist AND the child def survived
    grep -q "${BASE}.*__c" "$D/part.v" || fail "$FIX/$ALG: no per-color submodules in partitioned Verilog"
    grep -q "${BASE}.${CHILD}" "$D/part.v" || fail "$FIX/$ALG: child def '$CHILD' dropped (hierarchy lost)"
    # 6. LEC: the partitioned hierarchical design must equal the original
    run check --impl verilog:"$D/part.v" --ref verilog:"$D/ref.v" --top "$TOP" --workdir "$D/c"
    echo "PASS: $FIX [$ALG] hierarchical partition is LEC-equivalent to the original"
  done
done

# stats-only mode on a hierarchical input must succeed (per-def region stats).
SD="$W/stats"
mkdir -p "$SD"
run compile "inou/prp/tests/pyrope/hier_comb.prp" --top hier_comb.top --recipe O1 --emit-dir lg:"$SD/lg" --workdir "$SD/w1"
run pass color synth --top hier_comb.top lg:"$SD/lg" --workdir "$SD/w2"
run pass partition --top hier_comb.top lg:"$SD/lg" --workdir "$SD/w3"
echo "PASS: hierarchical partition stats-only mode"

echo "PASS: all hierarchical pass.color/pass.partition flows"
