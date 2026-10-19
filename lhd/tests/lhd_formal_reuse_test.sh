#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
# Cross-version reuse in the ABC pre-pass (formal.reuse=equiv|cex|inv, formal.pdr):
# a sequentially-equal rewrite keeps its answer (equiv), a still-failing bug is
# refuted by replaying the old trace (cex), and an old PDR invariant that is
# still inductive proves the edited property (inv).

set -u
LHD="${LHD:-lhd/lhd}"
case "$LHD" in /*) ;; *) LHD="$PWD/$LHD" ;; esac
W="${TEST_TMPDIR:-/tmp/lhd_formal_reuse_$$}"
rm -rf "$W"
mkdir -p "$W"

fail() { echo "FAIL: $*" >&2; exit 1; }

design() {  # $1 = a's wrap value, $2 = b's wrap compare
  cat >"$W/pair.prp" <<EOF
pub mod pair(en_a:Bool, en_b:Bool) -> (ca:U4@[0], cb:U4@[0]) {
  reg a:U4 = 0
  reg b:U4 = 0
  ca = a
  cb = b
  if en_a {
    if a == $1 {
      a = 0
    } else {
      wrap a = a + 1
    }
  }
  if en_b {
    if b $2 5 {
      b = 0
    } else {
      wrap b = b + 1
    }
  }
}
EOF
}

cat >"$W/pair.verify.prp" <<'EOF'
const dut = import("pair.pair")

formal pair.a_range {
  mut acc = dut
  assert(acc.ca <= 9, "a stays in range")
}

formal pair.b_range {
  mut acc = dut
  assert(acc.cb <= 5, "b stays in range")
}
EOF

# run STEP WORKDIR REUSE
run() {
  (cd "$W" && "$LHD" formal verify pair.prp pair.verify.prp --top pair --workdir "$2" \
    --set formal.bound=12 --set formal.simfail_run=false --set formal.reuse="$3" --set formal.pdr=true \
    --diag-fmt pretty) >"$W/$1.out" 2>&1
  cp "$W/$2/formal_report.json" "$W/$1.json" 2>/dev/null || fail "$1: no formal_report.json: $(cat "$W/$1.out")"
}

# check STEP BLOCK VERDICT REUSE_BY UNBOUNDED
check() {
  python3 - "$W/$1.json" "$2" "$3" "$4" "$5" <<'PYEOF' || fail "$1: $2 expected $3 by '$4' unbounded=$5: $(grep -E 'assert at|^formal|abc' "$W/$1.out")"
import json, sys
f, block, verdict, by, ub = sys.argv[1:]
d = json.load(open(f))
o = [o for o in d["obligations"] if o["kind"] == "assert" and o["block"].split("@")[0] == block][0]
assert o["verdict"].split("-")[0] == verdict, f"verdict {o['verdict']}"
assert o["reuse_by"] == by or (by == "equiv" and o["reuse_by"].startswith("equiv:")), f"reuse_by '{o['reuse_by']}'"
if verdict == "proven":
    assert o["unbounded"] == (ub == "true"), f"unbounded {o['unbounded']}"
PYEOF
}

# equiv: b's `== 5` -> `>= 5` changes the cone (a cone-hash miss) but not the
# behavior from reset (b never passes 5): sequential equivalence keeps it.
design 9 "=="
run e0 we equiv
check e0 pair.a_range proven "" true
check e0 pair.b_range proven "" true
design 9 ">="
run e1 we equiv
check e1 pair.a_range proven equiv true
check e1 pair.b_range proven equiv true
# the hash cache re-proves the same rewrite
design 9 "=="
run h0 wh hash
design 9 ">="
run h1 wh hash
check h1 pair.a_range proven hash true
check h1 pair.b_range proven "" true

# cex: a wraps at 12 (bug); at 13 the old trace still fails at the same cycle.
design 12 "=="
run c0 wc cex
check c0 pair.a_range refuted "" false
design 13 "=="
run c1 wc cex
check c1 pair.a_range refuted cex false

# inv: an old PDR invariant that is still inductive proves the property at
# once (b's `>=` rewrite); one that only partly holds (a now wraps at 8, so
# the old cubes miss state 9) is Houdini-filtered and seeds PDR.
design 9 "=="
run i0 wi inv
check i0 pair.a_range proven "" true
check i0 pair.b_range proven "" true
design 9 ">="
run i1 wi inv
check i1 pair.a_range proven inv true
check i1 pair.b_range proven inv true
design 8 ">="
run i2 wi inv
check i2 pair.a_range proven pdr+inv true
check i2 pair.b_range proven inv true

echo "PASS: cross-version reuse (equiv, cex, inv)"
