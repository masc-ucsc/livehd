#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# Coverage regression for Cprop::try_find_single_driver_pin (pass/cprop). A
# Pyrope bit-range write chain (r#[0]=a .. r#[3]=d) from runtime inputs lowers
# to a chain of LiveHD Set_mask nodes that upass cannot comptime-fold. Reading
# a single bit back (y0=r#[0], y2=r#[2]) makes cprop's scalar_get_mask walk the
# chain via try_find_single_driver_pin (the recursive single-driver resolver)
# to land on the writing value pin. At --recipe O1 (cprop on) the reads must
# fold: y0 -> a, y2 -> c. If the resolver regresses the reads stay as get_mask
# expressions and these greps fail.

set -u

LHD=lhd/lhd
PRP=lhd/tests/setmask_bitread.prp
W="${TEST_TMPDIR:-/tmp/lhd_setmask_$$}"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

"$LHD" compile "$PRP" --recipe O1 \
  --emit verilog:"$W/bitread.gen.v" --workdir "$W/w" --result-json "$W/r.json" -q 2>/dev/null \
  || fail "O1 compile of setmask_bitread.prp failed"
[ -s "$W/bitread.gen.v" ] || fail "O1 compile produced empty netlist"

# The recipe must actually have run pass.cprop (not silently a cprop-less one).
grep -q 'pass.cprop' "$W/r.json" || fail "recipe did not run pass.cprop: $(cat "$W/r.json")"

# try_find_single_driver_pin folded each single-bit read onto its Set_mask
# writer's value pin: y0 <= a (bit 0), y2 <= c (bit 2).
grep -Eq 'y0[[:space:]]*=[[:space:]]*a;' "$W/bitread.gen.v" \
  || fail "bit-0 read did not fold to a (Set_mask chain resolver regressed): $(cat "$W/bitread.gen.v")"
grep -Eq 'y2[[:space:]]*=[[:space:]]*c;' "$W/bitread.gen.v" \
  || fail "bit-2 read did not fold to c (Set_mask chain resolver regressed): $(cat "$W/bitread.gen.v")"

echo "PASS: setmask bit reads folded through try_find_single_driver_pin"
