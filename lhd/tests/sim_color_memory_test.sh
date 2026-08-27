#!/usr/bin/env bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

set -euo pipefail

LHD="${LHD:-lhd/lhd}"
PRP="inou/prp/tests/sim/mem_color_direct.prp"
work="${TEST_TMPDIR:-/tmp/lhd_sim_color_memory_$$}"
mkdir -p "$work"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

"$LHD" sim "$PRP" --setup-only --workdir "$work/setup" -q >/dev/null
header="$(ls "$work"/setup/sim/*memcolor.hpp | head -1)"
body="$(ls "$work"/setup/sim/*memcolor.cpp | head -1)"

grep -q 'color-direct eligible=true' "$header" || fail "deterministic registered memories fell back to the retired scheduler"
grep -q 'Memory_old<Slop_u<8>' "$header" || fail "proven-unsigned old-order memory did not retain Slop_u entries"
grep -q 'Memory_fwd<Slop_u<8>' "$header" || fail "proven-unsigned forwarding memory did not retain Slop_u entries"
grep -q '::__color_eval(std::size_t' "$body" || fail "memory design has no occurrence-wide color dispatcher"
grep -q '\.stage_write<' "$body" || fail "memory write protocol was not lowered into a color"
grep -q '__changed |= .*\.tick() != 0;' "$body" || fail "memory commit does not participate in deterministic dirty reduction"
grep -q '__color_commit(1)' "$body" || fail "memory tick is not owned by the rise commit barrier"
grep -q '\.read<' "$body" || fail "memory read ports were not materialized as versioned data values"
! grep -Eq '\.read<[^;]*\.zext_to<8>|\.read_all\(\)\.zext_to<32>' "$body" \
  || fail "same-width unsigned memory read was converted back through Slop"

"$LHD" sim "$PRP" --workdir "$work/run" -q >/dev/null

# A whole-memory value that crosses a color boundary must reuse the value read
# by its producer. Calling read_all() once for its side effect and again for the
# boundary used to double every whole-array read in memory-heavy designs.
whole_prp="inou/prp/tests/sim/mem_whole.prp"
"$LHD" sim "$whole_prp" --setup-only --workdir "$work/whole" -q >/dev/null
whole_body="$(ls "$work"/whole/sim/*whole_sim.cpp | head -1)"
! grep -q 'read_all();  // value unused' "$whole_body" \
  || fail "whole-memory boundary emitted a duplicate discarded read_all"
grep -Eq 'auto __color_tmp_[0-9]+ = .*\.read_all\(\);' "$whole_body" \
  || fail "whole-memory boundary did not materialize its producer value"
# ...and hands that already-canonical value straight to the Slop_u boundary
# storage. A `Slop<80>{...}` landing here is the same wasted round-trip the
# per-entry assertion above guards against, one spelling further out.
! grep -q 'allout = Slop<' "$whole_body" \
  || fail "canonical whole-memory value was converted back through signed Slop"

echo "PASS: deterministic registered memories execute in the occurrence-wide scheduler"
