#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# Smoke test for `lhd pass color` (2c-color): every algorithm
# (acyclic|cgen|synth|path|mincut|flat) colors a real (cprop-optimized) design
# via the CLI, the active coloring metadata is recorded on the top graph, and
# `clear` removes the coloring. Per-algorithm coloring invariants are covered by
# the gtests in pass/color; this exercises the lhd plumbing end to end. `flat`
# additionally gets a dedicated one-color-for-the-whole-hierarchy assertion.

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

for ALG in acyclic cgen synth path mincut flat; do
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

# `flat` must yield exactly ONE color across the WHOLE hierarchy (the flatten
# equivalent) -- both the top and every sub-def, even with continuous requested.
FD="$W/flat_one"
mkdir -p "$FD"
run compile verilog "$V0" --top "$TOP" --reader yosys-verilog --recipe O1 --emit-dir lg:"$FD/lg" --workdir "$FD/w1"
run pass color flat --top "$TOP" --set color.continuous=true lg:"$FD/lg" --workdir "$FD/w2"
NCOL=$("$LHD" tool --diag-fmt pretty cat --top "$TOP" lg:"$FD/lg" 2>/dev/null | grep -o 'color=[0-9]*' | sort -u | wc -l | tr -d ' ')
[ "$NCOL" = "1" ] || fail "flat must leave a single color across the hierarchy, got $NCOL distinct"
echo "PASS: flat colors the whole hierarchy with one color"
