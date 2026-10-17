#!/usr/bin/env bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# Several Pyrope units that each define `pub comb top`. The sim driver keyed
# every DUT by its entity tail, so `imp_p1.top`, `imp_p2.top` and the
# testbench's own `top` collapsed onto ONE map slot: `import("imp_p1.top")` and
# `import("imp_p2.top")` both instantiated the same class, with no warning, and
# a test comparing them compared a module with itself. Each import (and the
# testbench file's own bare `top`) must bind its own class.
set -euo pipefail

LHD="${LHD_BIN:-lhd/lhd}"
case "$LHD" in
  /*) ;;
  *) LHD="$PWD/$LHD" ;;  # the sim runs from the work dir below
esac
W="${TEST_TMPDIR:-/tmp/lhd_sim_import_same_entity_$$}"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

cat >"$W/imp_p1.prp" <<'EOF'
pub comb top(x:U4) -> (o:U4) {
  o = x
}
EOF

cat >"$W/imp_p2.prp" <<'EOF'
pub comb top(x:U4) -> (o:U4) {
  o = x ^ 1
}
EOF

cat >"$W/imp_tb.prp" <<'EOF'
const r = import("imp_p1.top")
const t = import("imp_p2.top")

pub comb top(x:U4) -> (o:U4) {
  o = x ^ 2
}

test imp.differ {
  mut ur = r
  mut ut = t
  mut uo = top
  tick 2 {
    ur.x = 4
    ut.x = 4
    uo.x = 4
    step
    assert(ur.o == 4, "imp_p1.top is the identity")
    assert(ut.o == 5, "imp_p2.top flips bit 0")
    assert(uo.o == 6, "the testbench's own top flips bit 1")
  }
}

test imp.same {
  mut ur = r
  mut ut = t
  tick 2 {
    ur.x = 4
    ut.x = 4
    step
    assert(ur.o == ut.o, "two distinct modules cannot agree here")
  }
}
EOF

rc=0
(cd "$W" && "$LHD" sim imp_p1.prp imp_p2.prp imp_tb.prp --workdir sw --set lhd.incremental=false) >"$W/out.json" 2>&1 || rc=$?
[ "$rc" -ne 0 ] || fail "imp.same compared a module with itself (it must fail); output: $(cat "$W/out.json")"

python3 - "$W/out.json" <<'PY' || fail "per-test verdicts are wrong; output: $(cat "$W/out.json")"
import json, sys
res = None
for line in open(sys.argv[1]):
    line = line.strip()
    if line.startswith('{') and '"tests"' in line:
        res = json.loads(line)
assert res is not None, "no result JSON"
tests = {t["test"]: t for t in res["tests"]}
assert tests["imp.differ"]["status"] == "pass", tests["imp.differ"]
assert tests["imp.same"]["status"] == "fail", tests["imp.same"]
assert tests["imp.same"]["failing_assert"] == "ur.o == ut.o", tests["imp.same"]
PY

drv="$W/sw/sim/drv.cpp"
for cls in imp_p1_top imp_p2_top imp_tb_top; do
  grep -q "make_unique<$cls>" "$drv" || fail "drv.cpp never instantiates $cls"
done

echo "PASS: each import of a shared entity name binds its own unit"
