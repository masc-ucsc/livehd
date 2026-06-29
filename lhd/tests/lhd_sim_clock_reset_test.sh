#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# `lhd sim` instance/`step` model: a DUT is a persistent instance you poke
# (`acc.x = v`), advance with `step`, and peek (`acc.y`); the clock is a synthetic
# VCD waveform driven by `step`, and reset is an ordinary poked input. `clock` is
# the cycle index. This test drives only `lhd sim --setup-only` (no nested bazel /
# host compiler) and asserts on the generated body + driver source + diagnostics:
#   * the body traces a toggling clock, advances a period counter, and exposes the
#     instance API (step()/__in/__out); reset is NOT a synthetic waveform;
#   * the driver pokes inputs, steps the instance, and peeks the output;
#   * a `tick` body with no `step`, >1 clock, an output poke, or an unknown field
#     is rejected at setup with a clear message.

set -u

LHD=lhd/lhd
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
mod cnt(enable:bool) -> (value:u8@[0]) {
  reg count:u8 = 0
  value = count
  if enable { wrap count += 1 }
}
test cnt.t(cycles:u20 = 20) {
  mut acc = cnt
  mut v = 0
  tick cycles clocks=(clock=1) {
    acc.enable = true
    acc.reset  = clock < 2     // reset is just a poked input
    step
    v = acc.value
  }
  assert(v == cycles - 2, "reset holds count at 0 for 2 cycles")
}
EOF
"$LHD" sim "$W/cr.prp" --setup-only --set sim.vcd=true --workdir "$W/cr" -q >/dev/null 2>&1 \
  || fail "instance/step test failed to set up"

DRV="$W/cr/sim/drv_cnt_t.cpp"
[ -f "$DRV" ] || fail "expected driver not generated: $DRV"
# the DUT body is the single non-driver .cpp in the sim dir; its interface the .hpp
BODY="$(ls "$W"/cr/sim/*.cpp | grep -v '/drv_' | head -1)"
HDR="$(ls "$W"/cr/sim/*.hpp | head -1)"
[ -n "$BODY" ] && [ -f "$BODY" ] || fail "DUT sim body not generated"
[ -n "$HDR" ]  && [ -f "$HDR" ]  || fail "DUT sim header not generated"

# clock is a synthetic toggling waveform; the period counter advances each cycle (.cpp body)
grep -q 'change(__vv_clk, "1")' "$BODY" || fail "clock never rises in the trace"
grep -q 'change(__vv_clk, "0")' "$BODY" || fail "clock never falls in the trace"
grep -q '__vcd_tick += '        "$BODY" || fail "period counter is not advanced"
# the instance API exists (.hpp struct: inline step() + the __in/__out members)
grep -q 'void step()'  "$HDR" || fail "no step() method on the instance"
grep -q 'In  __in{};'  "$HDR" || fail "no input latch __in on the instance"
grep -q 'Out __out{};' "$HDR" || fail "no output snapshot __out on the instance"
# reset is an ordinary poked input -- NOT a synthetic waveform / window
grep -q '__vv_rst'    "$HDR" "$BODY" && fail "reset must not be a synthetic waveform (it is a poked input now)"
grep -q '__rst_ticks' "$HDR" "$BODY" && fail "the reset-window machinery must be gone"

# the driver pokes inputs, steps the instance, and peeks the output
grep -q 'acc.step()'                  "$DRV" || fail "driver does not step the instance"
grep -q 'acc.__in.enable'             "$DRV" || fail "driver does not poke the enable input"
grep -q 'acc.__in.reset'              "$DRV" || fail "driver does not poke the reset input"
grep -q 'acc.__out.value'             "$DRV" || fail "driver does not peek the output"
grep -q '__clk_ratio = (unsigned)(1)' "$DRV" || fail "driver did not set the clock ratio"

# ---- error cases rejected at setup -------------------------------------------
# $1 = statements inside the test, $2 = expected message fragment, $3 = label
expect_err() {
  cat > "$W/bad.prp" <<EOF
/*
:name: bad
:type: simulation
*/
mod cnt(enable:bool) -> (value:u8@[0]) { reg count:u8 = 0; value = count; if enable { wrap count += 1 } }
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
expect_err '  tick 4 clocks=(a=1, b=2) { acc.enable = true; step }'  'only a single clock'         'two clocks'
expect_err '  tick 4 { acc.value = 1; step }'                        'cannot poke output'          'poke an output'
expect_err '  tick 4 { acc.nope = 1; step }'                         'unknown field'               'poke an unknown field'

echo "PASS: lhd sim instance/step model (clock waveform, reset-as-input, poke/step/peek)"
