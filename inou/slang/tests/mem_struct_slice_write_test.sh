#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# A bit-slice write into one entry of a STRUCT-ELEMENT (tuple) memory must be
# decomposed across the per-field memories upass.detuple splits the array into.
# It used to take the chunked write-enable path, whose store names the aggregate
# — a net that does not survive the split — so the write was silently DROPPED.
#
# Checked on the emitted Pyrope, not by LEC: a split tuple memory has one memory
# cell per field where the Verilog reference has a single array, and the lgyosys
# miter cannot pair their free initial contents. It refutes even for a
# whole-element write that has always lowered correctly, so a LEC verdict here
# would say nothing about this decomposition. See tests/sv for the fixture.

set -u

LHD=lhd/lhd
SRC=inou/slang/tests/sv/mem_struct_slice_write.v
TOP=mem_struct_slice_write
W="${TEST_TMPDIR:-/tmp/${TOP}_$$}"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  [ -f "$W/prp/$TOP.prp" ] && cat "$W/prp/$TOP.prp" >&2
  exit 1
}

"$LHD" compile "$SRC" --reader slang --top "$TOP" --recipe O1 \
  --emit-dir pyrope:"$W/prp" --emit verilog:"$W/out.v" --workdir "$W/work" -q 2>/dev/null \
  || fail "compile failed"

prp="$W/prp/$TOP.prp"
[ -s "$prp" ] || fail "Pyrope unit was not emitted"

# ── the write reaches the per-field memories, never the aggregate ───────────
grep -qE '^\s*(onef|span)\[' "$prp" \
  && fail "a store still targets the aggregate array name (the write is dropped after detuple)"

# ── slice covering exactly one field: a plain field store, no read-back ─────
grep -qE '`onef\.lo`\[adr\] = din#\[0\.\.=3\]' "$prp" \
  || fail "the one-field slice did not become a plain store of onef.lo"
grep -qE '`onef\.hi`\[[^]]*\] *=' "$prp" \
  && fail "the one-field slice wrote onef.hi, which the slice does not cover"

# ── slice crossing the field boundary: one field-local splice per field ─────
# `span[..][5:2] <= din[11:8]` puts din[1:0] in lo[3:2] and din[3:2] in hi[1:0].
grep -qE '#\[0\.\.=1\] = din#\[8\.\.=11\]#\[2\.\.=3\]' "$prp" \
  || fail "the hi half of the boundary-crossing slice is missing or misplaced"
grep -qE '#\[2\.\.=3\] = din#\[8\.\.=11\]#\[0\.\.=1\]' "$prp" \
  || fail "the lo half of the boundary-crossing slice is missing or misplaced"
grep -qE '`span\.hi`\[[^]]*\] *=' "$prp" || fail "span.hi was never written back"
grep -qE '`span\.lo`\[[^]]*\] *=' "$prp" || fail "span.lo was never written back"

echo "PASS: $TOP"
exit 0
