#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# `lhd formal verify` prints a one-screen tally: counts by kind and by verdict.

set -u

LHD="${LHD:-lhd/lhd}"
case "$LHD" in /*) ;; *) LHD="$PWD/$LHD" ;; esac  # the test cds into $W
W="${TEST_TMPDIR:-/tmp/lhd_formal_sum_$$}"
mkdir -p "$W"

fail() { echo "FAIL: $*" >&2; exit 1; }

# Two asserts that hold, one that does not.
cat >"$W/d.prp" <<'EOF'
pub mod cnt(en:Bool) -> (q:U8@[0]) {
  reg c:U8 = 0
  q = c
  if en and (c < 10) { wrap c = c + 1 }
}
EOF
cat >"$W/d.verify.prp" <<'EOF'
const dut = import("d.cnt")

formal cnt.ok {
  mut acc = dut
  assert(acc.q <= 10, "bounded")
  assert(acc.q >= 0, "non negative")
}

formal cnt.bad {
  mut acc = dut
  assert(acc.q != 3, "must refute")
}
EOF
OUT="$W/out"
(cd "$W" && "$LHD" formal verify d.prp d.verify.prp --top cnt --set formal.bound=20) >"$OUT" 2>&1

grep -q 'formal summary' "$OUT" || fail "no summary block: $(cat "$OUT")"
grep -qE 'properties +3 +\(3 assert, 0 assume, 0 cover\)' "$OUT" || fail "bad property tally: $(grep properties "$OUT")"
grep -qE 'proven +2' "$OUT" || fail "expected 2 proven: $(grep proven "$OUT")"
grep -qE 'failed \(cex\) +1' "$OUT" || fail "expected 1 cex: $(grep cex "$OUT")"
grep -qE 'undetermined +0' "$OUT" || fail "expected 0 undetermined: $(grep undetermined "$OUT")"

# An unchecked assume is counted apart from the asserts, not folded in.
cat >"$W/a.verify.prp" <<'EOF'
const dut = import("d.cnt")

formal cnt.env {
  mut acc = dut
  assume_nocheck(acc.en)
  assert(acc.q <= 10, "bounded")
}
EOF
OUT="$W/aout"
(cd "$W" && "$LHD" formal verify d.prp a.verify.prp --top cnt --set formal.bound=20) >"$OUT" 2>&1
grep -qE 'properties +2 +\(1 assert, 1 assume, 0 cover\)' "$OUT" || fail "assume not split out: $(grep properties "$OUT")"
grep -qE 'assumes +1 +\(1 unchecked' "$OUT" || fail "unchecked assume not counted: $(grep assumes "$OUT")"

echo "PASS: formal verify prints a verdict and kind tally"
