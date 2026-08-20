#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# Three shapes of the packed-2-D-array REG declaration gate, pinned together
# because they trade against each other (see tests/sv for the fixture):
#
#  * `tbl` — a plain packed register bank, every access at a literal index. It
#    must still be declared as a Pyrope ARRAY (`reg tbl:[4]u12`). It used to
#    flatten to one `reg tbl:u48` that each access bit-sliced back out — the
#    array-ness the Pyrope emission was losing on real designs (xiangshan's
#    DataModule__16entry: `reg data:u912` plus sixteen `data#[57k..=57k+56]`).
#    A constant selector is NOT a reason to flatten: constant- and
#    runtime-indexed arrays are the same LGraph memory.
#
#  * `rot` — pattern async reset ({3,2,1,0}) AND a whole-array datapath write.
#    Stays an array, with the reset SPLIT PER ENTRY into the array's own
#    initializer (`= (0, 1, 2, 3)`). One scalar `initial` attribute would
#    BROADCAST, collapsing the pattern to its bottom lane.
#
#  * `ptr` — the same pattern reset with NO whole-array datapath write. That
#    write is what becomes the memory's `update` bus, and only the update-bus
#    lowering has anywhere to hang a reset, so as an array `ptr` would lose its
#    reset outright. It stays a flat bus, which does reset correctly.
#
# All three must LEC against the source Verilog. The verdict is a BOUNDED pass,
# not PROVEN: `rot`'s memory state has no counterpart to pair with in the ref's
# flat flop, so the inductive miter declines and BMC decides it. That still
# covers what can break here — every regression this test guards against
# (dropped reset, broadcast reset, dropped element write) diverges within the
# first cycles, and each one did REFUTE at exactly this bound while being
# developed.

set -u

LHD=lhd/lhd
SRC=inou/slang/tests/sv/packed_array_reg_array_emit.v
TOP=packed_array_reg_array_emit
W="${TEST_TMPDIR:-/tmp/packed_array_reg_array_emit_$$}"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  [ -f "$W/prp/$TOP.prp" ] && grep -E '^\s+reg ' "$W/prp/$TOP.prp" >&2
  exit 1
}

"$LHD" compile "$SRC" --reader slang --top "$TOP" --recipe O1 \
  --emit-dir pyrope:"$W/prp" --emit verilog:"$W/out.v" --workdir "$W/work" -q 2>/dev/null \
  || fail "compile failed"

prp="$W/prp/$TOP.prp"
[ -s "$prp" ] || fail "Pyrope unit was not emitted"

# ── the const-indexed register bank IS an array ─────────────────────────────
grep -q 'reg tbl:\[4\]u12' "$prp" \
  || fail "const-indexed packed register bank did not emit a Pyrope array"
grep -q 'reg tbl:u48' "$prp" && fail "packed register bank still flattens to one wide flop"
grep -q 'tbl\[3\]' "$prp" || fail "element access did not use an array index"
grep -q 'tbl#\[' "$prp" && fail "element access still lowers through a bit-range slice"
# An element read is already elem_bits wide; re-masking it to its own width is
# a no-op that used to appear on 14% of all element accesses.
grep -qE 'tbl\[[0-9]+\]#\[0\.\.=11\]' "$prp" && fail "element read re-masked to its own width"

# ── the pattern reset survives PER ENTRY on the array that keeps one ────────
grep -q 'reg rot:\[4\]u10' "$prp" \
  || fail "array with a whole-array datapath write did not stay an array"
grep -qF 'reg rot:[4]u10:[ordering="old", reset_pin=ref rst_i, async=true] = (0, 1, 2, 3)' "$prp" \
  || fail "per-entry reset pattern was not split into the array initializer"

# ── and the one whose reset could not survive stays flat ────────────────────
grep -q 'reg ptr:u40' "$prp" \
  || fail "array whose reset would be dropped did not stay a flat bus"
grep -q 'init=0xc0200400' "$prp" || fail "flat reset pattern was not preserved"

# ── and the whole module is still the same circuit ──────────────────────────
[ -s "$W/out.v" ] || fail "Verilog was not emitted"
lec=$("$LHD" lec --impl verilog:"$W/out.v" --ref verilog:"$SRC" --top "$TOP" \
      --workdir "$W/lec" 2>&1) || fail "lec exited nonzero: $lec"
grep -qa "REFUTED" <<<"$lec" && fail "packed-array reg lowering was REFUTED: $lec"
grep -qaE "PROVEN equivalent|PASS\([0-9]+\)" <<<"$lec" \
  || fail "lec reported no positive verdict: $lec"

echo "PASS: const-indexed bank is an array, pattern reset splits per entry, unsurvivable reset stays flat"
