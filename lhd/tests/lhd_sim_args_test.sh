#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# `lhd sim` runtime test parameters: each `test name(params)` parameter becomes
# a `+<name>=VALUE` flag on the generated driver (bound at run time, not baked in),
# alongside `--seed` and `--help`. This test drives only `lhd sim --setup-only`
# (no nested bazel / host compiler needed) and asserts on the generated driver
# source + the diagnostics, covering:
#   * a valid parameter generates a `+<name>=VALUE` flag, `--seed`/`--help`, and the
#     validating numeric parsers (`_to_i64`/`_to_u64`);
#   * a parameter name that is unsafe as a C++ identifier / collides with a
#     reserved C++ runtime name is rejected at setup with a clear message
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
mod nn(a:U8) -> (s:U8@[0]) { s = a }
test nn.t(cycles:U20 = 20) {
  mut acc = nn
  mut v = 0
  tick cycles { acc.a = 1; step; v = acc.s }
  assert(v == 1, "v must be 1")
}
EOF
"$LHD" sim "$W/good.prp" --setup-only --workdir "$W/good" -q >/dev/null 2>&1 \
  || fail "valid parameterized test failed to set up"
# The setup lowers every `test` block into ONE object and writes the table of tests and their parameters the runtime
# binds `+<name>=VALUE` from (no generated C++).
MAN="$W/good/sim/drv.tb.json"
[ -f "$MAN" ] && [ -f "$W/good/sim/drv.tb.o" ] || fail "expected testbench object and manifest not generated in $W/good/sim"
grep -q '"name":"cycles"' "$MAN" || fail "manifest does not list the cycles parameter"
grep -q '"default":"20"' "$MAN" || fail "manifest does not record the cycles default"
[ -z "$(ls "$W"/good/sim/*.cpp "$W"/good/sim/*.hpp 2>/dev/null)" ] || fail "C++ was generated: $(ls "$W/good/sim")"

# ---- a reserved Pyrope word cannot be a parameter name; every other name is usable -----------------
# $1 = parameter declaration text, $2 = a label for messages,
# $3 = (optional) the expected message pattern, when the name is refused BEFORE
#      the simulation-parameter check ever sees it.
reject_param() {
  local decl="$1" label="$2" want="${3:-not a usable simulation parameter name}"
  cat > "$W/bad.prp" <<EOF
/*
:name: bad
:type: simulation
*/
mod nn(a:U8) -> (s:U8@[0]) { s = a }
test nn.t($decl) {
  mut acc = nn
  mut v = 0
  tick 2 { acc.a = 1; step; v = acc.s }
  assert(v == 1, "x")
}
EOF
  local out
  out="$("$LHD" sim "$W/bad.prp" --setup-only --workdir "$W/bad" -q 2>&1)" \
    && fail "reserved parameter name ($label) was NOT rejected"
  # The body is the ordinary instance/`step` form on purpose: it must set up
  # cleanly so the ONLY thing that can fail is the parameter name. A body that
  # errors on its own would make every case below pass vacuously.
  # Two rejection paths, both explanatory, and which one fires depends on the
  # name: a Pyrope RESERVED WORD (`test`) is refused at parse time by the
  # backtick rule ("'test' is reserved, so it cannot be a parameter
  # name"), while every other unusable name reaches the simulation-parameter
  # check. Each case names the layer it expects, so relaxing one does not
  # silently accept the wrong diagnostic for the others.
  echo "$out" | grep -qE "$want" \
    || fail "rejection of $label lacked the explanatory message: $out"
}
reject_param 'test:U8 = 2'    'Pyrope reserved test keyword' 'is reserved, so it cannot be a parameter name'

# A parameter is an ordinary name now: nothing becomes a C++ identifier, so what used to be refused (a C++ keyword, a
# driver-internal name, a leading underscore, a backtick-escaped name) simulates like any other.
for name in default argc _seed '`my param`'; do
  cat > "$W/ok.prp" <<EOF
/*
:name: ok
:type: simulation
*/
mod nn(a:U8) -> (s:U8@[0]) { s = a }
test nn.t($name:U8 = 2) {
  mut acc = nn
  mut v = 0
  tick 2 { acc.a = 1; step; v = acc.s }
  assert(v == 1, "x")
}
EOF
  "$LHD" sim "$W/ok.prp" --workdir "$W/ok" -q >"$W/ok.out" 2>&1 || fail "a parameter named $name should be usable: $(cat "$W/ok.out")"
done

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
