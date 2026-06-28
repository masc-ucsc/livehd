#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# `lhd sim` VCD clock/reset waveforms + the `tick clocks=(name=ratio)
# resets=(name=ticks) { ... }` clause. The clock/reset of a sequential design are
# real input ports that the testbench would otherwise force to 0 (so the clock
# never toggled and the reset never asserted). This test drives only `lhd sim
# --setup-only` (no nested bazel / host compiler) and asserts on the generated
# sim body + driver source + diagnostics:
#   * the generated DUT body traces a dedicated clock waveform (rise/fall) and a
#     reset waveform, drives the reset port from the configured window (functional
#     reset), and advances the period counter every cycle (independent of VCD);
#   * the driver configures the clock ratio + reset window on the instance;
#   * more than one clock or reset entry is rejected at setup with a clear message.

set -u

LHD=lhd/lhd
W="${TEST_TMPDIR:-/tmp/lhd_sim_cr_$$}"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

# ---- a clocks=/resets= clause sets up + emits the waveform machinery ----------
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
  mut v = 0
  tick cycles clocks=(clock=1) resets=(reset=2) {
    const r = cnt(enable=true)
    v = r
  }
  assert(v == cycles - 2, "reset holds count at 0 for 2 ticks")
}
EOF
"$LHD" sim "$W/cr.prp" --setup-only --set sim.vcd=true --workdir "$W/cr" -q >/dev/null 2>&1 \
  || fail "clocks=/resets= clause failed to set up"

DRV="$W/cr/sim/drv_cnt_t.cpp"
[ -f "$DRV" ] || fail "expected driver not generated: $DRV"
# the DUT body is the single non-driver .cpp in the sim dir
BODY="$(ls "$W"/cr/sim/*.cpp | grep -v '/drv_' | head -1)"
[ -n "$BODY" ] && [ -f "$BODY" ] || fail "DUT sim body not generated"

# clock/reset are traced as the dedicated waveform, not ordinary io signals
grep -q '__vv_clk' "$BODY" || fail "DUT body missing the clock waveform var"
grep -q '__vv_rst' "$BODY" || fail "DUT body missing the reset waveform var"
grep -q 'change(__vv_clk, "1")' "$BODY" || fail "clock never rises in the trace"
grep -q 'change(__vv_clk, "0")' "$BODY" || fail "clock never falls in the trace"
# functional reset: the reset port is driven from the window (not left at 0)
grep -q 'decltype(in.reset)::create_integer' "$BODY" || fail "reset port is not driven from the window"
grep -q '__rst_active_low' "$BODY" || fail "DUT body missing the reset polarity"
# the period counter advances every cycle (kept outside the VCD guard)
grep -q '__vcd_tick += ' "$BODY" || fail "period counter is not advanced"

# the driver configures the clock ratio + reset window on the instance
grep -q '__clk_ratio = (unsigned)(1)' "$DRV" || fail "driver did not set the clock ratio"
grep -q '__rst_ticks = (unsigned)(2)' "$DRV" || fail "driver did not set the reset window"
grep -q '__rst_base = ' "$DRV"             || fail "driver did not anchor the reset window"

# ---- more than one clock / reset entry is rejected at setup -------------------
# $1 = the tick clause text, $2 = expected message fragment, $3 = label
reject_clause() {
  local clause="$1" want="$2" label="$3"
  cat > "$W/bad.prp" <<EOF
/*
:name: bad
:type: simulation
*/
mod cnt(enable:bool) -> (value:u8@[0]) { reg count:u8 = 0; value = count; if enable { wrap count += 1 } }
test cnt.t {
  mut v = 0
  tick 4 $clause { const r = cnt(enable=true); v = r }
  assert(v == v)
}
EOF
  local out
  out="$("$LHD" sim "$W/bad.prp" --setup-only --workdir "$W/bad" -q 2>&1)" \
    && fail "multiple $label entries were NOT rejected"
  echo "$out" | grep -q "$want" \
    || fail "rejection of multiple $label lacked '$want': $out"
}
reject_clause 'clocks=(clk_a=1, clk_b=2)' 'only a single clock' 'clock'
reject_clause 'resets=(rst_a=1, rst_b=2)' 'only a single reset' 'reset'

echo "PASS: lhd sim VCD clock/reset waveforms + tick clocks=()/resets=() clause"
