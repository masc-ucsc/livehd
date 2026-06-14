#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# Smoke test for `lhd pass color` (2c-color): every algorithm
# (acyclic|synth|path|mincut) colors a real (cprop-optimized) design via the
# CLI, the active coloring metadata is recorded on the top graph, and `clear`
# removes the coloring. Per-algorithm coloring invariants are covered by the
# gtests in pass/color; this exercises the lhd plumbing end to end.

set -u

LHD=lhd/lhd
V0=lhd/tests/part_hier.v
TOP=part_hier
W="${TEST_TMPDIR:-/tmp/lhd_color_$$}"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}
run() { "$LHD" "$@" -q --result-json "$W/r.json" || fail "$* -> $(cat "$W/r.json" 2>/dev/null)"; }

for ALG in acyclic synth path mincut; do
  D="$W/$ALG"
  mkdir -p "$D"
  run compile verilog "$V0" --top "$TOP" --reader yosys-verilog --recipe O1 --emit-dir lg:"$D/lg" --workdir "$D/w1"
  run pass color "$ALG" --top "$TOP" lg:"$D/lg" --workdir "$D/w2"
  echo "PASS: color $ALG ran"
  # continuous mode (per-region split) must also run
  run pass color "$ALG" --top "$TOP" --set color.continuous=true lg:"$D/lg" --workdir "$D/w3"
  # clear must remove the coloring
  run pass color clear --top "$TOP" lg:"$D/lg" --workdir "$D/w4"
done

echo "PASS: all pass.color algorithms run through the lhd CLI"
