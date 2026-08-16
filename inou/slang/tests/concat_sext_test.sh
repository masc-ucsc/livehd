#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# `{{N{v[msb]}}, v…}` — firtool's (and hand-written RTL's) spelling of a sign
# extension — must lower to LNAST's `sext`, not to a bit-extract + N-bit
# broadcast + shift + or. Equally important: the recognizer must stay off every
# look-alike, because a replicated bit that is NOT the MSB of the lanes below it
# is a mask idiom, and folding one to a sext is a silent miscompile.

set -u

LHD=lhd/lhd
SRC=inou/slang/tests/sv/concat_sext.v
W="${TEST_TMPDIR:-/tmp/concat_sext_$$}"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

"$LHD" compile "$SRC" --reader slang --top concat_sext --recipe O1 \
  --emit-dir pyrope:"$W/prp" --emit verilog:"$W/out.v" --workdir "$W/work" -q 2>/dev/null \
  || fail "concat_sext compile failed"

prp="$W/prp/concat_sext.prp"
[ -s "$prp" ] || fail "Pyrope unit was not emitted"

# ── recognized: one sext, no broadcast ──────────────────────────────────────
# `{{32{a[31]}}, a}` — the replicated bit is the MSB of the whole lane below.
grep -q 'o_whole = a#sext\[0..=31\]#\[0..=63\]' "$prp" \
  || fail "whole-lane sign extension did not fold to a sext"
# `{{32{b[31]}}, b[31:0]}` — the lane is a range select of the same base.
grep -q 'o_slice = b#\[0..=31\]#sext\[0..=31\]#\[0..=63\]' "$prp" \
  || fail "range-select sign extension did not fold to a sext"
# `{{51{c[11]}}, c, 1'b0}` — three lanes: the sign bit is the MSB of the TOPMOST
# lane, which is the MSB of all the lanes below the replication taken together,
# so the sext index is the COMBINED width minus one (12+1-1 = 12), not c's.
grep -q 'o_multi = (c << 1)#sext\[0..=12\]#\[0..=63\]' "$prp" \
  || fail "multi-lane sign extension did not fold to a sext at the combined width"

# ── refused: every look-alike keeps the broadcast ───────────────────────────
# An unrelated 1-bit net: `{{32{rdy}}, a}` is a mask, not a sign extension.
grep -q 'o_mask = (_rep_[0-9]* << 32) | a' "$prp" \
  || fail "unrelated-bit replication was folded (or its lowering changed shape)"
# Bit 30 of a 32-bit lane is not its MSB.
grep -q 'o_notmsb = (_rep_[0-9]* << 32) | a' "$prp" \
  || fail "non-MSB replication was folded"
# The MSB of a DIFFERENT variable.
grep -q 'o_other = (_rep_[0-9]* << 32) | a' "$prp" \
  || fail "cross-variable replication was folded"

grep -q '#sext\[0..=30\]' "$prp" && fail "a sext appeared for the non-MSB shape"

# ── and the whole thing is still the same circuit ───────────────────────────
[ -s "$W/out.v" ] || fail "Verilog was not emitted"
lec=$("$LHD" lec --impl verilog:"$W/out.v" --ref verilog:"$SRC" --top concat_sext \
      --workdir "$W/lec" 2>&1)
grep -qa "PROVEN equivalent" <<<"$lec" || fail "sign-extension lowering was not PROVEN equivalent: $lec"

echo "PASS: sign-extension concats fold to sext, look-alikes do not, LEC PROVEN"
