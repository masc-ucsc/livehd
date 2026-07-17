#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# End-to-end test for `lhd pass color` + `lhd pass partition` (2c-color / 2p):
#
#   verilog -> lg (O1)
#   lhd pass color <alg> --set color.continuous=true   (in-place coloring)
#   lhd pass partition --emit-dir lg:dir2              (one module per region + new top)
#   lg:dir2 -> verilog
#   lhd lec --set formal.solver=lgyosys  (partitioned verilog vs the original): LEC-equivalent
#
# Runs for the self-contained coloring algorithms (acyclic, synth); also checks
# the stats-only mode and that an UNCOLORED design partitions cleanly: color 0
# (no `pass.color` run) is treated as just another color — the whole design
# folds into one color-0 region — with a single warning, and the result is still
# LEC-equivalent to the original.

set -u

LHD=lhd/lhd
V0=lhd/tests/part_flat.v
TOP=part_flat
W="${TEST_TMPDIR:-/tmp/lhd_partition_$$}"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}
run() { "$LHD" "$@" -q --result-json "$W/r.json" || fail "$* -> $(cat "$W/r.json" 2>/dev/null)"; }

for ALG in acyclic synth; do
  D="$W/$ALG"
  mkdir -p "$D"
  # 1. compile to an optimized lg
  run compile verilog "$V0" --top "$TOP" --reader yosys-verilog --recipe O1 --emit-dir lg:"$D/lg" --workdir "$D/w1"
  # 2. color in place (continuous => one region per color, the partition contract)
  run pass color "$ALG" --top "$TOP" --set color.continuous=true lg:"$D/lg" --workdir "$D/w2"
  # 3. partition into a fresh library
  run pass partition --top "$TOP" lg:"$D/lg" --emit-dir lg:"$D/lg2" --workdir "$D/w3"
  # 4. emit verilog from the partitioned library (verbatim, no re-opt)
  run compile lg:"$D/lg2" --top "$TOP" --recipe O0 --emit verilog:"$D/part.v" --workdir "$D/w4"
  # top module is always emitted; whether it splits depends on the coloring.
  grep -q "^module part_flat" "$D/part.v" || fail "$ALG: top module not emitted"
  if [ "$ALG" = synth ]; then
    # synth colors this design as ONE region -> emitted directly under its own
    # name, no pointless part_flat__c wrapper (the single-region optimization).
    grep -q "part_flat__c" "$D/part.v" && fail "$ALG: single-region design must not be wrapped in __c submodules"
  else
    # acyclic splits into several colors -> a real hierarchy of per-region modules.
    grep -q "part_flat__c" "$D/part.v" || fail "$ALG: multi-region partition has no per-color submodules"
  fi
  # 5. LEC: the partitioned design must equal the original
  run lec --set formal.solver=lgyosys --impl verilog:"$D/part.v" --ref verilog:"$V0" --top "$TOP" --workdir "$D/c"
  echo "PASS: $ALG partition is LEC-equivalent to the original"
done

# stats-only mode (no --emit-dir): must succeed and print region stats
SD="$W/stats"
mkdir -p "$SD"
run compile verilog "$V0" --top "$TOP" --reader yosys-verilog --recipe O1 --emit-dir lg:"$SD/lg" --workdir "$SD/w1"
run pass color acyclic --top "$TOP" --set color.continuous=true lg:"$SD/lg" --workdir "$SD/w2"
run pass partition --top "$TOP" lg:"$SD/lg" --workdir "$SD/w3"
echo "PASS: partition stats-only mode"

# clear: coloring removal must succeed, and partitioning an UNCOLORED design must
# then succeed too — color 0 is treated as just another color (one color-0
# region for the whole design), with a single non-fatal warning, and the
# rebuild stays LEC-equivalent to the original.
CD="$W/clear"
mkdir -p "$CD"
run compile verilog "$V0" --top "$TOP" --reader yosys-verilog --recipe O1 --emit-dir lg:"$CD/lg" --workdir "$CD/w1"
run pass color acyclic --top "$TOP" lg:"$CD/lg" --workdir "$CD/w2"
run pass color clear --top "$TOP" lg:"$CD/lg" --workdir "$CD/w3"
"$LHD" pass partition --top "$TOP" lg:"$CD/lg" --emit-dir lg:"$CD/lg2" -q --result-json "$CD/r.json" --workdir "$CD/w4"
rc=$?
[ "$rc" -ge 128 ] && fail "partition on an uncolored design crashed (signal $((rc - 128)))"
[ "$rc" -eq 0 ] || fail "partition on an uncolored design failed (rc=$rc) -> $(cat "$CD/r.json" 2>/dev/null)"
# exactly one warning: the uncolored-node advisory (color 0 treated as a color)
grep -q '"diagnostics_count":{"errors":0,"warnings":1}' "$CD/r.json" \
  || fail "expected one uncolored-node warning, got $(grep -o '"diagnostics_count":{[^}]*}' "$CD/r.json")"
# the whole design is one color-0 region: a single region, so it is emitted
# directly under the top's own name (no pointless __c0 wrapper).
run compile lg:"$CD/lg2" --top "$TOP" --recipe O0 --emit verilog:"$CD/part.v" --workdir "$CD/w5"
grep -q "^module part_flat" "$CD/part.v" || fail "uncolored partition did not emit the top module"
grep -q "part_flat__c" "$CD/part.v" && fail "uncolored single-region design must not get a __c wrapper"
# and it is still LEC-equivalent to the original
run lec --set formal.solver=lgyosys --impl verilog:"$CD/part.v" --ref verilog:"$V0" --top "$TOP" --workdir "$CD/c"
echo "PASS: uncolored design -> partition warns once + color-0 region, LEC-equivalent"

echo "PASS: all pass.color/pass.partition flows"
