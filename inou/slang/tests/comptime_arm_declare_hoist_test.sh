#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# A plain packed local is declared LAZILY at its first lowered touch. When that
# touch sits inside an `if` arm whose guard is comptime-decidable, the declare
# lands INSIDE the arm -- and upass then deletes the dead arm together with the
# declaration. The emitted Pyrope keeps the writes (they are in the surviving
# sibling arm) but declares nothing, so it does not recompile:
#
#   error: assignment to undeclared variable 'gen_..._local_operands'
#
# Reduced from cvfpu's fpnew_opgroup_multifmt_slice. lower_members' module-top
# `mut` pre-declare is what closes it (the reg/array/struct/wire hoists next to
# it already dodge the same hazard for their own classes).
#
# The bug is only visible on the ROUND TRIP -- the first leg emits fine -- so
# the recompile below is the load-bearing half of this gate.

set -u

LHD=lhd/lhd
SRC=inou/slang/tests/sv/comptime_arm_declare_hoist.v
TOP=comptime_arm_declare_hoist
W="${TEST_TMPDIR:-/tmp/comptime_arm_declare_hoist_$$}"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

"$LHD" compile "$SRC" --reader slang --top "$TOP" \
  --emit-dir pyrope:"$W/prp" --workdir "$W/work" -q 2>/dev/null \
  || fail "verilog -> pyrope failed"

prp="$W/prp/$TOP.prp"
[ -s "$prp" ] || fail "Pyrope unit was not emitted"

# ── both locals are declared, at module top, with their width ───────────────
grep -q '^  mut flat_operands:u96 = 0' "$prp" \
  || fail "module-scope local lost its declaration: $(grep -c flat_operands "$prp") mentions, none declared"
grep -q '^  mut gen_lanes_0_active_lane_local_operands:u96 = 0' "$prp" \
  || fail "generate-block local lost its declaration"

# The x-fill poison rides the declare, at module top -- not stranded inside the
# arm that happened to lower first.
grep -q '^  flat_operands = 0sb?' "$prp" || fail "module-scope local lost its x-fill"
grep -q '^  gen_lanes_0_active_lane_local_operands = 0sb?' "$prp" \
  || fail "generate-block local lost its x-fill"

# The hoist must NOT rename the symbol: lname_of memoizes off genblk_prefix_,
# so a hoist that names it under an empty prefix silently drops the hierarchy
# path that LEC state matching and VCD names ride on.
grep -q '^  local_operands' "$prp" \
  && fail "generate-block local lost its genblk_prefix_ path (renamed to 'local_operands')"

# ── and the emitted Pyrope actually recompiles ──────────────────────────────
out=$("$LHD" compile "$prp" --emit-dir lg:"$W/lg" --workdir "$W/rt" 2>&1) \
  || fail "emitted Pyrope does not recompile: $out"

echo "PASS: comptime-decided arm no longer swallows the local's declaration"
