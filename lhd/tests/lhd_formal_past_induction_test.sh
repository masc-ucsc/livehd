#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
# The induction step must read `past(x, n)` from n frames back, like the BMC
# unroll does. It once read the current frame, so `x == past(x, 1)` became
# `x == x` and a toggling register was "proven" frozen for every cycle.

set -u
LHD="${LHD:-lhd/lhd}"
case "$LHD" in /*) ;; *) LHD="$PWD/$LHD" ;; esac
W="${TEST_TMPDIR:-/tmp/lhd_formal_past_induction_$$}"
mkdir -p "$W"

fail() { echo "FAIL: $*" >&2; exit 1; }

cat >"$W/tgl.prp" <<'PRP'
pub mod leaf(en:Bool) -> (q:Bool@[0]) {
  reg t:Bool = false
  if en {
    t = not t
  }
  q = t
}
PRP
cat >"$W/bad.verify.prp" <<'PRP'
const sub = import("tgl.leaf")

formal leaf.frozen {
  mut acc = sub
  assert(acc.t == past(acc.t, 1), "WRONG: t never changes")
}
PRP
cat >"$W/good.verify.prp" <<'PRP'
const sub = import("tgl.leaf")

formal leaf.rises {
  mut acc = sub
  assert(rose(acc.t) implies past(acc.en, 1), "t rises only after an enabled cycle")
}
PRP

# One checked step: the base case cannot see the bug, so only the induction
# step decides. It must not upgrade the false claim to every cycle.
OUT="$W/bad.out"
(cd "$W" && "$LHD" formal verify tgl.prp bad.verify.prp --top leaf --set formal.engine=bmc --set formal.bound=1 \
  --set formal.simfail_run=false --diag-fmt pretty) >"$OUT" 2>&1
grep -q 'PROVEN-full (inductive' "$OUT" && fail "induction proved a toggling register frozen: $(grep 'assert at' "$OUT")"

# The same claim with room to fail is refuted.
OUT="$W/bad2.out"
(cd "$W" && "$LHD" formal verify tgl.prp bad.verify.prp --top leaf --set formal.bound=4 \
  --set formal.simfail_run=false --diag-fmt pretty) >"$OUT" 2>&1
grep -q 'REFUTED' "$OUT" || fail "a toggling register claimed frozen was not refuted: $(grep 'assert at' "$OUT")"

# A true history property still earns the every-cycle verdict.
OUT="$W/good.out"
(cd "$W" && "$LHD" formal verify tgl.prp good.verify.prp --top leaf --set formal.engine=bmc --set formal.bound=1 \
  --set formal.simfail_run=false --diag-fmt pretty) >"$OUT" 2>&1 \
  || fail "a true past() property failed: $(cat "$OUT")"
grep -q 'PROVEN-full (inductive' "$OUT" || fail "a true past() property lost its unbounded proof: $(grep 'assert at' "$OUT")"

echo "PASS: induction reads past() from the earlier frame"
