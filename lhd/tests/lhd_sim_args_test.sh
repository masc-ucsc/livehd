#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# `lhd sim` runtime test parameters: each `test name(params)` parameter becomes
# a `--<name>` flag on the generated driver (bound at run time, not baked in),
# alongside `--seed` and `--help`. This test drives only `lhd sim --setup-only`
# (no nested bazel / host compiler needed) and asserts on the generated driver
# source + the diagnostics, covering:
#   * a valid parameter generates a `--<name>` flag, `--seed`/`--help`, and the
#     validating numeric parsers (`_to_i64`/`_to_u64`);
#   * a parameter name that is unsafe as a C++ identifier / collides with a
#     reserved driver flag is rejected at setup with a clear message
#     (C++ keyword, `argc`, leading-underscore, backtick — review #1-#5);
#   * `lhd sim --seed <non-numeric>` is a CLI usage error (review #8).

set -u

LHD="${LHD:-lhd/lhd}"
W="${TEST_TMPDIR:-/tmp/lhd_sim_args_$$}"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

# ---- a valid parameterized test sets up + generates the expected driver ------
cat > "$W/good.prp" <<'EOF'
/*
:name: good
:type: simulation
*/
mod nn(a:u8) -> (s:u8@[0]) { s = a }
test nn.t(cycles:u20 = 20) {
  mut acc = nn
  mut v = 0
  tick cycles { acc.a = 1; step; v = acc.s }
  assert(v == 1, "v must be 1")
}
EOF
"$LHD" sim "$W/good.prp" --setup-only --workdir "$W/good" -q >/dev/null 2>&1 \
  || fail "valid parameterized test failed to set up"
# One driver per design (drv.cpp) holds every `test` block; each `test`-parameter
# is bound from the central `--<name>` arg map inside its run function.
DRV="$W/good/sim/drv.cpp"
[ -f "$DRV" ] || fail "expected driver not generated: $DRV"
grep -q '_args.find("cycles")' "$DRV" || fail "driver does not bind the cycles parameter"
grep -q -- '"--seed"'  "$DRV" || fail "driver missing the --seed flag"
grep -q -- '"--list-tests"' "$DRV" || fail "driver missing the --list-tests flag"
grep -q -- '"--test"'  "$DRV" || fail "driver missing the --test selector flag"
grep -q    '_to_i64'   "$DRV" || fail "driver missing the validating integer parser"
grep -q    '_to_u64'   "$DRV" || fail "driver missing the validating seed parser"
grep -q    'hlop_set_random_seed' "$DRV" || fail "driver does not seed the hlop PRNG"
grep -q    '_tests_json' "$DRV" || fail "driver missing the embedded --list-tests JSON"

# ---- reserved / unsafe parameter names are rejected at setup -----------------
# $1 = parameter declaration text, $2 = a label for messages
reject_param() {
  local decl="$1" label="$2"
  cat > "$W/bad.prp" <<EOF
/*
:name: bad
:type: simulation
*/
mod nn(a:u8) -> (s:u8@[0]) { s = a }
test nn.t($decl) { const v = nn(a=1); assert(v == 1, "x") }
EOF
  local out
  out="$("$LHD" sim "$W/bad.prp" --setup-only --workdir "$W/bad" -q 2>&1)" \
    && fail "reserved parameter name ($label) was NOT rejected"
  echo "$out" | grep -q 'not a usable simulation parameter name' \
    || fail "rejection of $label lacked the explanatory message: $out"
}
reject_param 'default:u8 = 2' 'C++ keyword'
reject_param 'argc:u8 = 2'    'main argument argc'
reject_param '_seed:u8 = 2'   'leading-underscore driver-local collision'
reject_param 'test:u8 = 2'    'reserved --test selector flag'
reject_param '`my param`:u8 = 2' 'backtick-escaped identifier'

# ---- `--list-tests` emits the dotted name + params as JSON (no build) ---------
LT="$("$LHD" sim "$W/good.prp" --list-tests --diag-fmt jsonl 2>/dev/null | head -1)" \
  || fail "--list-tests failed"
echo "$LT" | grep -q '"name":"nn.t"' || fail "--list-tests JSON missing the dotted test name: $LT"
echo "$LT" | grep -q '"name":"cycles"' || fail "--list-tests JSON missing the cycles parameter: $LT"
echo "$LT" | grep -q '"default":"20"' || fail "--list-tests JSON missing the cycles default: $LT"

# ---- `--seed` must be numeric (CLI usage error otherwise) ---------------------
SEED_OUT="$("$LHD" sim "$W/good.prp" --seed nope --setup-only --workdir "$W/s" -q 2>&1)" \
  && fail "non-numeric --seed was accepted"
echo "$SEED_OUT" | grep -q 'seed expects a non-negative integer' \
  || fail "non-numeric --seed lacked the usage message: $SEED_OUT"

# a numeric --seed is fine
"$LHD" sim "$W/good.prp" --seed 42 --setup-only --workdir "$W/s2" -q >/dev/null 2>&1 \
  || fail "numeric --seed 42 was rejected"

echo "PASS: lhd sim runtime test-parameter flags + validation"
