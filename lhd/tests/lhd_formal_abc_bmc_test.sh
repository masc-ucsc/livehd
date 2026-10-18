#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
# ABC bmc3 pre-pass: the same verdicts, proof depths, failing cycles and
# witnesses as the cvc5-only run (LHD_VERIFY_ABC=0), on a proof and on a bug.

set -u
LHD="${LHD:-lhd/lhd}"
case "$LHD" in /*) ;; *) LHD="$PWD/$LHD" ;; esac
W="${TEST_TMPDIR:-/tmp/lhd_formal_abc_bmc_$$}"
rm -rf "$W"
mkdir -p "$W"

fail() { echo "FAIL: $*" >&2; exit 1; }

# Two counters; `a` wraps at $1 (a bug when $1 > 9), `b` only counts when the
# environment allows it.
design() {
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
    if b == 5 {
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

# run TAG [env...] -> report copied to $W/TAG.json
run() {
  local tag=$1
  shift
  rm -rf "$W/wd_$tag"
  (cd "$W" && env "$@" "$LHD" formal verify pair.prp pair.verify.prp --top pair --workdir "wd_$tag" \
    --set formal.abc=true --set formal.bound=16 --set formal.engine=bmc --set formal.simfail_run=false --diag-fmt pretty) >"$W/$tag.out" 2>&1
  cp "$W/wd_$tag/formal_report.json" "$W/$tag.json" 2>/dev/null || fail "$tag: no formal_report.json: $(cat "$W/$tag.out")"
}

# same A B -> the per-assert verdicts, depths and failing cycles agree
same() {
  python3 - "$W/$1.json" "$W/$2.json" <<'PYEOF' || fail "$1 vs $2 differ"
import json, sys
def rows(f):
    d = json.load(open(f))
    return {o["block"].split("@")[0]: (o["verdict"], o["proven_to"], o["refuted_at"], bool(o.get("witness")))
            for o in d["obligations"] if o["kind"] == "assert"}
a, b = rows(sys.argv[1]), rows(sys.argv[2])
assert a and a == b, f"{a} != {b}"
PYEOF
}

for wrap in 9 12; do
  design $wrap
  run abc_$wrap
  run cvc5_$wrap LHD_VERIFY_ABC=0
  grep -q 'abc bmc3 pre-pass' "$W/abc_$wrap.out" || fail "wrap $wrap: the ABC pre-pass did not run"
  grep -q 'abc bmc3 pre-pass' "$W/cvc5_$wrap.out" && fail "wrap $wrap: LHD_VERIFY_ABC=0 still ran ABC"
  same abc_$wrap cvc5_$wrap
done

grep -q 'abc bmc3 pre-pass: 2 of 2 proven' "$W/abc_9.out" || fail "ABC did not prove the good design: $(grep -o 'abc bmc3[^;]*' "$W/abc_9.out")"
grep -q '1 refuted with a trace' "$W/abc_12.out" || fail "ABC gave no trace for the bug: $(grep -o 'abc bmc3[^;]*' "$W/abc_12.out")"

echo "PASS: ABC bmc3 pre-pass matches cvc5 (proof and bug, with witness)"
