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

# COI: a DESIGN-body assert reports its cone, including state.
cat >"$W/body.prp" <<'EOF'
pub mod cnt2(en:Bool) -> (q:U8@[0]) {
  reg c:U8 = 0
  q = c
  if en and (c < 10) { wrap c = c + 1 }
  assert(c <= 10, "bounded")
}
EOF
OUT="$W/bout"
(cd "$W" && "$LHD" formal verify body.prp --top cnt2 --set formal.bound=20) >"$OUT" 2>&1
grep -qE '\[COI [0-9]+ node\(s\), [1-9][0-9]* state\]' "$OUT" || fail "design assert must report a cone with state: $(grep -i coi "$OUT")"
grep -qE '^  COI +[0-9]+ +largest cone' "$OUT" || fail "no COI summary row: $(grep -i coi "$OUT")"

# A sidecar formal block is its own monitor graph. Its cone must be continued
# through the binds into the DESIGN, so it reports design state, not the
# monitor's expression size (which has none).
OUT="$W/sout"
(cd "$W" && "$LHD" formal verify d.prp d.verify.prp --top cnt --set formal.bound=20) >"$OUT" 2>&1
grep -qE '\[COI [0-9]+ node\(s\), [1-9][0-9]* state\]' "$OUT" \
  || fail "sidecar COI must reach design state through the binds: $(grep -i coi "$OUT")"
grep -q 'COI not measured' "$OUT" && fail "sidecar COI should now be measured: $(grep -i coi "$OUT")"

# A property over a constant has a cone of its own, not the whole design.
cat >"$W/k.verify.prp" <<'EOF'
const dut = import("d.cnt")

formal cnt.konst {
  mut acc = dut
  assert(acc.q >= 0, "trivial")
}
EOF
OUT="$W/kout"
(cd "$W" && "$LHD" formal verify d.prp k.verify.prp --top cnt --set formal.bound=20) >"$OUT" 2>&1
grep -qE '\[COI [0-9]+ node' "$OUT" || fail "a sidecar property must carry a cone: $(grep -i coi "$OUT")"

# A window no deeper than the history a property reads never exercised it.
# Under the default engine, ind-first can settle after ONE checked step, so a
# `past(x, 1)` property reports PROVEN having checked nothing. Say so.
cat >"$W/t.verify.prp" <<'EOF'
const dut = import("d.cnt")

formal cnt.temporal {
  mut acc = dut
  assert(past(acc.q, 1) <= acc.q, "q never decreases")
}
EOF
OUT="$W/tout"
(cd "$W" && "$LHD" formal verify d.prp t.verify.prp --top cnt --set formal.bound=20) >"$OUT" 2>&1
grep -q 'SHALLOW:' "$OUT" || fail "a one-step window over a past() property must be disclosed: $(cat "$OUT")"

# bmc walks the full bound, so the same property is genuinely checked.
OUT="$W/tout2"
(cd "$W" && "$LHD" formal verify d.prp t.verify.prp --top cnt --set formal.bound=20 \
   --set formal.engine=bmc) >"$OUT" 2>&1
grep -q 'SHALLOW:' "$OUT" && fail "bmc walks the full bound; no shallow warning expected: $(grep -i -A1 shallow "$OUT")"

# A design with no temporal property proves inductively in one step, which is
# unbounded and complete -- that must NOT be flagged.
OUT="$W/tout3"
(cd "$W" && "$LHD" formal verify d.prp d.verify.prp --top cnt --set formal.bound=20) >"$OUT" 2>&1
grep -q 'SHALLOW:' "$OUT" && fail "a non-temporal one-step inductive proof is not shallow: $(grep -i -A1 shallow "$OUT")"

echo "PASS: formal verify prints a verdict and kind tally"
