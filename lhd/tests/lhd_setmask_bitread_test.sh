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
# writer's value pin: y0 <= a (bit 0), y2 <= c (bit 2). A 1-bit UNSIGNED input
# port carries a to-positive Get_mask wrapper (tolg wraps unsigned ports so the
# signed-declared port reads with its unsigned value; the wrapper is required
# for arithmetic and is NOT soundly removable in general — it differs from the
# bare port under sign-extension). So a folded single-bit read of `a` renders as
# `a & 1` (bare, inlined, or via a one-use wire), semantically `a`. Accept any
# of those forms; the point is each read resolved to its writer (a / c) instead
# of staying an unfolded Set_mask-chain expression.
folds_to() {  # <out> <src> : true when `out` is driven by `src` (bare or 1-bit to-positive mask)
  local out=$1 src=$2 v="$W/bitread.gen.v" w
  grep -Eq "${out}[[:space:]]*=[[:space:]]*${src};" "$v" && return 0                       # y0 = a;
  grep -Eq "${out}[[:space:]]*=[[:space:]]*\(?${src}[[:space:]]*&[[:space:]]*1" "$v" && return 0  # y0 = (a & 1…);
  w=$(grep -oE "${out}[[:space:]]*=[[:space:]]*[A-Za-z0-9_]+;" "$v" | head -1 | sed -E 's/.*=[[:space:]]*//; s/;.*//')
  [ -n "$w" ] && grep -Eq "${w}[[:space:]]*=[[:space:]]*\(?${src}[[:space:]]*&[[:space:]]*1" "$v"  # y0 = W; W = (a & 1…);
}
folds_to y0 a || fail "bit-0 read did not fold to a (Set_mask chain resolver regressed): $(cat "$W/bitread.gen.v")"
folds_to y2 c || fail "bit-2 read did not fold to c (Set_mask chain resolver regressed): $(cat "$W/bitread.gen.v")"

echo "PASS: setmask bit reads folded through try_find_single_driver_pin"
