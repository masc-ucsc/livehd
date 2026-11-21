#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# `lhd sim` instance/`step` model: a DUT is a persistent instance you poke
# (`acc.x = v`), advance with `step`, and read (`acc.y`) through hoisted refs; the clock is a synthetic
# VCD waveform driven by `step`, and reset is an ordinary poked input. `clock` is
# the cycle index. The test checks:
#   * the instance/`step` model simulates (reset is an ordinary poked input, `clock` is the cycle index, `step N`
#     advances N cycles) and a VCD run has a toggling clock;
#   * a `tick` body with no `step`, a removed `clocks=`/`resets=` clause, an
#     output poke, or an unknown field is rejected at setup with a clear message.

set -u
LHD="${LHD:-lhd/lhd}"
W="${TEST_TMPDIR:-/tmp/lhd_sim_cr_$$}"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

# ---- instance/step generates the expected body + driver ----------------------
cat > "$W/cr.prp" <<'EOF'
/*
:name: cr
:type: simulation
*/
mod cnt(enable:Bool) -> (value:U8@[0]) {
  reg count:U8 = 0
  value = count
  if enable { wrap count += 1 }
}
test cnt.t(cycles:U20 = 20) {
  mut acc = cnt
  mut v = 0
  tick cycles {
    acc.enable = true
    acc.reset = clock < 2    // reset is just a poked input
    step
    v = acc.value
  }
  assert(v == cycles - 2, "reset holds count at 0 for 2 cycles")
}
EOF
"$LHD" sim "$W/cr.prp" --set sim.vcd=true --workdir "$W/cr" --diag-fmt pretty >"$W/cr.out" 2>&1 \
  || fail "instance/step test failed: $(cat "$W/cr.out")"
grep -q 'PASS cnt.t' "$W/cr.out" || fail "cnt.t did not pass: $(cat "$W/cr.out")"
[ -z "$(ls "$W"/cr/sim/*.cpp "$W"/cr/sim/*.hpp 2>/dev/null)" ] || fail "C++ was generated: $(ls "$W/cr/sim")"
# the VCD replay shows a toggling clock and reset as an ordinary signal, not a synthetic window
VCD="$W/cr/cnt.t.vcd"
[ -s "$VCD" ] || fail "no VCD replayed: $(ls "$W/cr")"
grep -q 'reset' "$VCD" || fail "reset is not an ordinary signal in the waveform"
grep -Eq '^[01][^ ]*$' "$VCD" || fail "no value changes in the VCD"

# ---- error cases rejected at setup -------------------------------------------
# $1 = statements inside the test, $2 = expected message fragment, $3 = label
expect_err() {
  cat > "$W/bad.prp" <<EOF
/*
:name: bad
:type: simulation
*/
mod cnt(enable:Bool) -> (value:U8@[0]) { reg count:U8 = 0; value = count; if enable { wrap count += 1 } }
test cnt.t {
  mut acc = cnt
  mut v = 0
$1
  assert(v == v)
}
EOF
  local out
  out="$("$LHD" sim "$W/bad.prp" --setup-only --workdir "$W/bad" -q 2>&1)" \
    && fail "$3 was NOT rejected"
  echo "$out" | grep -q "$2" || fail "$3 lacked '$2': $out"
}
expect_err '  tick 4 { acc.enable = true; v = acc.value }'           'must advance the clock with' 'tick body with no step'
# A tick takes only a cycle count (owner ruling 75): the `clocks=`/`resets=`
# clauses are gone, and the parser says so.
expect_err '  tick 4 clocks=(a=1, b=2) { acc.enable = true; step }'  'clauses were removed'        'tick clocks clause'
expect_err '  tick 4 resets=(r=2) { acc.enable = true; step }'       'clauses were removed'        'tick resets clause'
expect_err '  tick 4 { acc.value = 1; step }'                        "cannot drive 'acc.value'"    'drive an output'
expect_err '  tick 4 { acc.nope = 1; step }'                         "cannot drive 'acc.nope'"     'drive an unknown field'

# the clock loop-var name must not collide with a param/local/instance (it would
# silently shadow it inside the tick body)
cat > "$W/coll.prp" <<'EOF'
/*
:name: coll
:type: simulation
*/
mod cnt(enable:Bool) -> (value:U8@[0]) { reg count:U8 = 0; value = count; if enable { wrap count += 1 } }
test cnt.t(clock:U8 = 3) {
  mut acc = cnt
  tick 4 { acc.enable = true; step }
  assert(clock == clock)
}
EOF
COUT="$("$LHD" sim "$W/coll.prp" --setup-only --workdir "$W/coll" -q 2>&1)" \
  && fail "a param named like the clock loop var was NOT rejected"
echo "$COUT" | grep -q 'collides with a test parameter' || fail "clock-collision message missing: $COUT"

# `step N` advances N cycles: the value read after `step 3` is 3 (a run, not a text match)
cat > "$W/stepn.prp" <<'EOF'
/*
:name: stepn
:type: simulation
*/
mod cnt(enable:Bool) -> (value:U8@[0]) { reg count:U8 = 0; value = count; if enable { wrap count += 1 } }
test cnt.t {
  mut acc = cnt
  mut v = 0
  tick 1 { acc.enable = true; step 3; v = acc.value }
  assert(v == 3)
}
EOF
"$LHD" sim "$W/stepn.prp" --workdir "$W/stepn" --diag-fmt pretty >"$W/stepn.out" 2>&1 \
  || fail "step N failed: $(cat "$W/stepn.out")"
grep -q 'PASS cnt.t' "$W/stepn.out" || fail "step 3 did not advance 3 cycles: $(cat "$W/stepn.out")"

echo "PASS: lhd sim instance/step model (clock waveform, reset-as-input, step N)"
