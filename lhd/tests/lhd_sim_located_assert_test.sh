#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# `lhd sim` STRUCTURED + LOCATED assert failures:
#   * a failed `assert` prints the source location (prp:line), the cycle (the
#     `clock` value, which survives the tick loop so a post-loop assert still
#     reports it), and BOTH operand VALUES of a top-level comparison — a literal
#     operand prints only its source (`v=10 == 30`, not `30=30`);
#   * `lhd sim --result-json PATH` enriches the result envelope with a per-test
#     `tests` array: {test,status,cycle,failing_assert,prp_file,line,msg}.
# The structural checks run hermetically (`--setup-only`, no compiler). When the
# sibling ../hlop + ../iassert headers are present (a dev / repo-root run), it ALSO
# host-compiles + runs `lhd sim --result-json` and checks the located stdout + the
# envelope's tests array.

set -u

LHD="${LHD:-lhd/lhd}"
W="${TEST_TMPDIR:-/tmp/lhd_sim_la_$$}"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

# Three tests: a failing compare with a literal RHS (+ message), a failing compare
# with two named operands (no message), and a passing one. The failing asserts are
# AFTER the tick loop, so they exercise the post-loop `clock` (_clk) survival.
cat > "$W/la.prp" <<'EOF'
/*
:name: la
:type: simulation
*/
mod cnt(enable:bool) -> (value:u8@[0]) {
  reg count:u8 = 0
  value = count
  if enable { wrap count += 1 }
}
test cnt.bad_literal {
  mut acc = cnt
  mut v = 0
  tick 12 { acc.enable = true; acc.reset = clock < 2; step; v = acc.value }
  assert(v == 30, "must reach 30")
}
test cnt.bad_two {
  mut acc = cnt
  mut v = 0
  mut expect = 99
  tick 12 { acc.enable = true; acc.reset = clock < 2; step; v = acc.value }
  assert(v == expect)
}
test cnt.good {
  mut acc = cnt
  mut v = 0
  tick 12 { acc.enable = true; acc.reset = clock < 2; step; v = acc.value }
  assert(v == 10)
}
EOF

# ---- structural: the generated driver carries the located-assert machinery ----
"$LHD" sim "$W/la.prp" --setup-only --workdir "$W/s" -q >/dev/null 2>&1 || fail "setup-only failed"
DRV="$W/s/sim/drv.cpp"
[ -f "$DRV" ] || fail "driver not generated"

grep -q 'struct _Fail'                  "$DRV" || fail "no _Fail (first-failing-assert) record"
grep -q '\[\[maybe_unused\]\] long _clk' "$DRV" || fail "no _clk cycle tracker"
grep -q '_clk = clock;'                 "$DRV" || fail "tick loop does not update _clk"
grep -q 'ASSERT FAILED ('               "$DRV" || fail "no located ASSERT FAILED message"
grep -q 'clock=%ld'                     "$DRV" || fail "located message does not print the cycle"
grep -q '_ff.has = true'                "$DRV" || fail "first-failing-assert is not captured"
grep -q '_ff.assertion ='               "$DRV" || fail "failing_assert source is not captured"
grep -q '_ff.line ='                    "$DRV" || fail "assert line is not captured"
# the result-json array is a BARE array (the kernel embeds it as the envelope's tests member)
grep -q 'std::string _rj = "\[";'       "$DRV" || fail "result array is not a bare JSON array"
grep -q '"--result-json"'               "$DRV" || fail "driver does not accept --result-json"
grep -q 'failing_assert'                "$DRV" || fail "driver does not emit failing_assert"
# a literal RHS prints only its source (one %ld for the LHS): `v=%ld == 30`
grep -q 'v=%ld == 30'                   "$DRV" || fail "literal operand should not print '=value' ($(grep 'ASSERT FAILED .la.prp:.*== 30' "$DRV"))"
# two named operands each print their value: `v=%ld == expect=%ld`
grep -q 'v=%ld == expect=%ld'           "$DRV" || fail "both named operands should print their value"

# ---- a `tick` nested in another `tick` is rejected at setup (no shared _clk) ---
cat > "$W/nest.prp" <<'EOF'
/*
:name: nest
:type: simulation
*/
mod cnt(enable:bool) -> (value:u8@[0]) { reg count:u8 = 0; value = count; if enable { wrap count += 1 } }
test cnt.t {
  mut acc = cnt
  tick 4 { acc.enable = true; step; tick 2 { acc.enable = false; step } }
  assert(true)
}
EOF
NOUT="$("$LHD" sim "$W/nest.prp" --setup-only --workdir "$W/nest" -q 2>&1)" && fail "nested tick was NOT rejected"
echo "$NOUT" | grep -q 'cannot be nested' || fail "nested-tick rejection message missing: $NOUT"

# ---- opportunistic real build + run (needs the sibling runtime headers) -------
HLOP_INC=""
IASSERT_INC=""
for d in ../hlop/hlop ../hlop; do [ -f "$d/slop.hpp" ] && HLOP_INC="$d" && break; done
for d in ../iassert/src ../iassert; do [ -f "$d/iassert.hpp" ] && IASSERT_INC="$d" && break; done
if [ -z "$HLOP_INC" ] || [ -z "$IASSERT_INC" ]; then
  echo "SKIP run checks: sibling hlop/iassert headers not found (structural checks passed)"
  echo "PASS: lhd sim located asserts + --result-json (structural)"
  exit 0
fi

# the run exits non-zero (asserts fired) but still produces the located stdout + the
# enriched result envelope.
"$LHD" sim "$W/la.prp" --result-json "$W/r.json" --workdir "$W/run" --diag-fmt pretty > "$W/run.out" 2>&1
RC=$?
[ "$RC" = "1" ] || fail "expected exit 1 (asserts fired), got $RC: $(cat "$W/run.out")"

# located stdout: prp:line, cycle (post-loop clock = 11), both operands, the message
grep -Eq 'ASSERT FAILED \(la\.prp:[0-9]+\) clock=11: v=10 == 30  \[must reach 30\]' "$W/run.out" \
  || fail "literal-operand located message wrong: $(grep 'ASSERT FAILED' "$W/run.out")"
grep -Eq 'ASSERT FAILED \(la\.prp:[0-9]+\) clock=11: v=10 == expect=99' "$W/run.out" \
  || fail "two-operand located message wrong: $(grep 'ASSERT FAILED' "$W/run.out")"
grep -q 'first at la.prp:' "$W/run.out" || fail "FAIL summary lacks the first-assert location"
grep -q 'PASS cnt.good'    "$W/run.out" || fail "cnt.good did not pass: $(cat "$W/run.out")"

# the result envelope carries a per-test tests array
[ -s "$W/r.json" ] || fail "--result-json file not written"
python3 - "$W/r.json" <<'PY' || fail "result-json tests array is wrong (see message above)"
import json, sys
d = json.load(open(sys.argv[1]))
t = {x["test"]: x for x in d.get("tests", [])}
def need(c, m):
    if not c:
        print("  result-json check failed:", m); sys.exit(1)
need("tests" in d, "envelope has no tests array")
need(set(t) == {"cnt.bad_literal","cnt.bad_two","cnt.good"}, "unexpected test set: %s" % list(t))
bl = t["cnt.bad_literal"]
need(bl["status"]=="fail" and bl["cycle"]==11 and bl["failing_assert"]=="v == 30"
     and bl["prp_file"].endswith("la.prp") and isinstance(bl["line"],int) and bl["msg"]=="must reach 30",
     "bad_literal record wrong: %s" % bl)
bt = t["cnt.bad_two"]
need(bt["status"]=="fail" and bt["failing_assert"]=="v == expect" and "msg" not in bt,
     "bad_two record wrong: %s" % bt)
need(t["cnt.good"]["status"]=="pass", "good record wrong: %s" % t["cnt.good"])
print("  result-json tests array OK")
PY

# ---- unary operators must NOT be dropped (a silent false-PASS in a verifier) ---
# and `_` digit separators must compile. `cnt.neg` has v==10 after the loop, so
# `-v == 10` is FALSE and MUST fail; the located message must show the operator.
# `cnt.sep` uses `_` separators in a tick count and a literal and must PASS.
cat > "$W/un.prp" <<'EOF'
/*
:name: un
:type: simulation
*/
mod cnt(enable:bool) -> (value:u8@[0]) { reg count:u8 = 0; value = count; if enable { wrap count += 1 } }
test cnt.neg {
  mut acc = cnt
  mut v = 0
  tick 1_2 { acc.enable = true; acc.reset = clock < 2; step; v = acc.value }
  assert(-v == 10, "negation must be respected")
}
test cnt.sep {
  mut acc = cnt
  mut v = 0
  tick 12 { acc.enable = true; acc.reset = clock < 2; step; v = acc.value }
  assert(v == 1_0)
}
EOF
"$LHD" sim "$W/un.prp" --result-json "$W/un.json" --workdir "$W/unrun" --diag-fmt pretty > "$W/un.out" 2>&1
URC=$?
[ "$URC" = "1" ] || fail "unary test: expected exit 1 (cnt.neg must fail), got $URC: $(cat "$W/un.out")"
grep -q 'FAIL cnt.neg'                "$W/un.out" || fail "cnt.neg did NOT fail -> unary '-' was dropped (silent false-PASS!): $(cat "$W/un.out")"
grep -Eq 'clock=11: -v=-10 == 10'     "$W/un.out" || fail "negation operator/value missing from located message: $(grep 'ASSERT' "$W/un.out")"
grep -q 'PASS cnt.sep'                "$W/un.out" || fail "cnt.sep (with '_' separators) failed to compile/run: $(cat "$W/un.out")"
python3 - "$W/un.json" <<'PY' || fail "unary result-json wrong"
import json, sys
t = {x["test"]: x for x in json.load(open(sys.argv[1])).get("tests", [])}
assert t["cnt.neg"]["status"]=="fail" and t["cnt.neg"]["failing_assert"]=="-v == 10", t["cnt.neg"]
assert t["cnt.sep"]["status"]=="pass", t["cnt.sep"]
PY

echo "PASS: lhd sim located asserts + --result-json (host-compile, located stdout + tests array)"
