#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# End-to-end test for `lhd pass color` + `lhd pass partition` (2c-color / 2p):
#
#   verilog -> lg (O1)
#   lhd pass color <alg> --set color.continuous=true   (in-place coloring)
#   lhd pass partition --emit-dir lg:dir2              (one module per region + new top)
#   lg:dir2 -> verilog
#   lhd check  (partitioned verilog vs the original): must be LEC-equivalent
#
# Runs for the self-contained coloring algorithms (acyclic, synth); also checks
# the stats-only mode and that an incomplete coloring is a clean error (not an
# abort).

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
  # the new top must be a real hierarchy (instantiates the per-region modules)
  grep -q "part_flat__c" "$D/part.v" || fail "$ALG: partitioned verilog has no per-color submodules"
  # 5. LEC: the partitioned design must equal the original
  run check --impl verilog:"$D/part.v" --ref verilog:"$V0" --top "$TOP" --workdir "$D/c"
  echo "PASS: $ALG partition is LEC-equivalent to the original"
done

# stats-only mode (no --emit-dir): must succeed and print region stats
SD="$W/stats"
mkdir -p "$SD"
run compile verilog "$V0" --top "$TOP" --reader yosys-verilog --recipe O1 --emit-dir lg:"$SD/lg" --workdir "$SD/w1"
run pass color acyclic --top "$TOP" --set color.continuous=true lg:"$SD/lg" --workdir "$SD/w2"
run pass partition --top "$TOP" lg:"$SD/lg" --workdir "$SD/w3"
echo "PASS: partition stats-only mode"

# clear: coloring removal must succeed, and partitioning afterwards is a clean
# error (incomplete coloring), NOT a crash/abort.
CD="$W/clear"
mkdir -p "$CD"
run compile verilog "$V0" --top "$TOP" --reader yosys-verilog --recipe O1 --emit-dir lg:"$CD/lg" --workdir "$CD/w1"
run pass color acyclic --top "$TOP" lg:"$CD/lg" --workdir "$CD/w2"
run pass color clear --top "$TOP" lg:"$CD/lg" --workdir "$CD/w3"
"$LHD" pass partition --top "$TOP" lg:"$CD/lg" --emit-dir lg:"$CD/lg2" -q --result-json "$CD/r.json" --workdir "$CD/w4"
rc=$?
[ "$rc" -ge 128 ] && fail "partition on a cleared (uncolored) design crashed (signal $((rc - 128))) instead of erroring cleanly"
[ "$rc" -eq 0 ] && fail "partition on a cleared (uncolored) design unexpectedly succeeded"
echo "PASS: cleared coloring -> partition errors cleanly (rc=$rc)"

echo "PASS: all pass.color/pass.partition flows"
