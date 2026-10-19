#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
# Per-property cone cache: with one --workdir, a formal block whose cone did not
# change keeps its proof across edits; a block whose cone changed is re-proved.

set -u
LHD="${LHD:-lhd/lhd}"
case "$LHD" in /*) ;; *) LHD="$PWD/$LHD" ;; esac
W="${TEST_TMPDIR:-/tmp/lhd_formal_cone_cache_$$}"
rm -rf "$W"
mkdir -p "$W"

fail() { echo "FAIL: $*" >&2; exit 1; }

# Two independent counters; each formal block reads only one of them.
design() {  # $1 = a's wrap value, $2 = b's wrap compare, $3 = name of b, $4 = extra comment
  cat >"$W/pair.prp" <<EOF
// two independent counters$4
pub mod pair(en_a:Bool, en_b:Bool) -> (ca:U4@[0], cb:U4@[0]) {
  reg a:U4  = 0
  reg $3:U4 = 0
  ca = a
  cb = $3
  if en_a {
    if a == $1 {
      a = 0
    } else {
      wrap a = a + 1
    }
  }
  if en_b {
    if $3 $2 5 {
      $3 = 0
    } else {
      wrap $3 = $3 + 1
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

# run STEP -> verifies into the shared workdir, keeps the report as $W/STEP.json
run() {
  (cd "$W" && "$LHD" formal verify pair.prp pair.verify.prp --top pair --workdir wd \
    --set formal.bound=12 --set formal.simfail_run=false --diag-fmt pretty) >"$W/$1.out" 2>&1
  local rc=$?
  cp "$W/wd/formal_report.json" "$W/$1.json" 2>/dev/null || fail "$1: no formal_report.json: $(cat "$W/$1.out")"
  return $rc
}

# check STEP BLOCK VERDICT REUSED  (REUSED is true|false)
check() {
  python3 - "$W/$1.json" "$2" "$3" "$4" <<'PYEOF' || fail "$1: $2 expected $3 reused=$4: $(grep -E 'assert at|^formal' "$W/$1.out")"
import json, sys
f, block, verdict, reused = sys.argv[1:]
d = json.load(open(f))
hit = [o for o in d["obligations"] if o["kind"] == "assert" and o["block"].split("@")[0] == block]
assert hit, f"no assert for {block}"
o = hit[0]
assert o["verdict"].split("-")[0] == verdict, f"verdict {o['verdict']}"
assert o["cone_reused"] == (reused == "true"), f"cone_reused {o['cone_reused']}"
assert o["cone_digest"], "empty cone_digest"
PYEOF
}

design 9 "==" b ""
run base || fail "base run failed: $(cat "$W/base.out")"
check base pair.a_range proven false
check base pair.b_range proven false

design 9 "==" b " (comment only)"
run comment || fail "comment run failed: $(cat "$W/comment.out")"
check comment pair.a_range proven true
check comment pair.b_range proven true
grep -q 'reused from cone cache' "$W/comment.out" || fail "comment run printed no cone reuse"

design 9 "==" cnt_b ""
run rename || fail "rename run failed: $(cat "$W/rename.out")"
check rename pair.a_range proven true
check rename pair.b_range proven true

design 9 ">=" cnt_b ""
run edit_b || fail "edit_b run failed: $(cat "$W/edit_b.out")"
check edit_b pair.a_range proven true
check edit_b pair.b_range proven false

design 12 ">=" cnt_b ""
run bug_a && fail "bug_a run passed but a can reach 12"
check bug_a pair.a_range refuted false
check bug_a pair.b_range proven true

echo "PASS: per-property cone cache (comment, rename, one-cone edit, bug)"
