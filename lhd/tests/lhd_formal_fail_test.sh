#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# 2f-formal FAIL policy: a refuted (definitive counterexample) formal property —
# assert / assume / Hotmux one-hotness — is RECORDED as a build error and the
# build fails (exit != 0), but the compile CONTINUES so cgen still emits the
# design with the failing check kept as a runtime check (never elided, never
# used to optimize). The diagnostic carries the counterexample and a hint that a
# different top-level instantiation may change the result. pass.formal runs in
# the O2 graph recipe.

set -u

LHD=lhd/lhd
W="${TEST_TMPDIR:-/tmp/lhd_formal_fail_$$}"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

# compile_o2 <name> : compile $W/<name>.prp through O2 + emit verilog/diag.
# Sets globals: RC, DIAG (jsonl path), VOUT (verilog path).
compile_o2() {
  local n="$1"
  DIAG="$W/$n.jsonl"
  VOUT="$W/$n.v"
  "$LHD" compile "$W/$n.prp" --recipe O2 --workdir "$W/$n" \
    --emit "verilog:$VOUT" --emit "diagnostics:$DIAG" >/dev/null 2>&1
  RC=$?
}

# ---------------------------------------------------------------------------
# 1. A refuted `assert` over free inputs: FAIL recorded, build fails, but the
#    netlist is still emitted with the runtime assert KEPT.
# ---------------------------------------------------------------------------
cat >"$W/assert_fail.prp" <<'EOF'
comb chk(a:u8, b:u8) -> (x:u8) {
  assert(a != b, "a and b must differ")
  x = a + b
}
EOF
compile_o2 assert_fail
[ "$RC" -ne 0 ] || fail "refuted assert must fail the build (got rc=0)"
grep -q '"code":"assert-refuted"' "$DIAG" || fail "missing assert-refuted diagnostic: $(cat "$DIAG")"
grep -q 'counterexample:' "$DIAG" || fail "assert-refuted must include a counterexample: $(cat "$DIAG")"
grep -q 'a and b must differ' "$DIAG" || fail "assert-refuted must carry the user message: $(cat "$DIAG")"
grep -q 'different top-level instantiation' "$DIAG" || fail "assert-refuted must hint at a different top: $(cat "$DIAG")"
[ -s "$VOUT" ] || fail "compile must CONTINUE and still emit the netlist on a refuted assert"
grep -q 'assert (' "$VOUT" || fail "the failing assert must be KEPT as a runtime check in the netlist: $(cat "$VOUT")"
grep -q 'a and b must differ' "$VOUT" || fail "runtime assert must keep its \$error message: $(cat "$VOUT")"

# ---------------------------------------------------------------------------
# 2. A refuted `assume`: same FAIL policy. A refuted assume must NOT be turned
#    into a synthesis hypothesis; it is kept as a runtime contract check.
# ---------------------------------------------------------------------------
cat >"$W/assume_fail.prp" <<'EOF'
comb chk(a:u8, b:u8) -> (x:u8) {
  assume(a != b)
  x = a + b
}
EOF
compile_o2 assume_fail
[ "$RC" -ne 0 ] || fail "refuted assume must fail the build (got rc=0)"
grep -q '"code":"assume-refuted"' "$DIAG" || fail "missing assume-refuted diagnostic: $(cat "$DIAG")"
[ -s "$VOUT" ] || fail "compile must CONTINUE and still emit the netlist on a refuted assume"
grep -q 'assume (' "$VOUT" || fail "the failing assume must be KEPT as a runtime check in the netlist: $(cat "$VOUT")"

# ---------------------------------------------------------------------------
# 3. An overlapping `unique if` (Hotmux one-hotness refuted): the old hard
#    .fatal() path is gone — same FAIL policy (record + continue + emit).
# ---------------------------------------------------------------------------
cat >"$W/hotmux_fail.prp" <<'EOF'
comb chk(p:bool, q:bool) -> (y:u8) {
  mut y = 0
  unique if p { y = 1 } elif q { y = 2 }
}
EOF
compile_o2 hotmux_fail
[ "$RC" -ne 0 ] || fail "refuted Hotmux one-hotness must fail the build (got rc=0)"
grep -q '"code":"onehot-violated"' "$DIAG" || fail "missing onehot-violated diagnostic: $(cat "$DIAG")"
[ -s "$VOUT" ] || fail "compile must CONTINUE (not fatal-abort) and still emit on a refuted Hotmux"

# ---------------------------------------------------------------------------
# 4. A provably one-hot `unique if` (distinct constant arms): PROVEN, so it
#    compiles clean (no error) and the netlist is emitted. Proven is the only
#    path that may elide/optimize.
# ---------------------------------------------------------------------------
cat >"$W/onehot_ok.prp" <<'EOF'
comb chk(x:u2, a:u8, b:u8) -> (y:u8) {
  mut y = 0
  unique if x == 0 { y = a } elif x == 1 { y = b }
}
EOF
compile_o2 onehot_ok
[ "$RC" -eq 0 ] || fail "a provably one-hot unique-if must compile clean (got rc=$RC): $(cat "$DIAG")"
grep -q '"severity":"error"' "$DIAG" && fail "proven one-hot must emit no error: $(cat "$DIAG")"
[ -s "$VOUT" ] || fail "proven one-hot must still emit the netlist"

echo "PASS: 2f-formal FAIL policy (assert/assume/hotmux record+continue+emit; proven clean)"
