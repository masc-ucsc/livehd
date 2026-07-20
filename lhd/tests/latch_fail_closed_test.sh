#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# todo/livehd/2f-latch M0 — LIVE fail-closed test.
#
# M0's whole thesis is that a tool which cannot handle a construct must SAY SO
# and exit nonzero. Every shape below used to report SUCCESS (exit 0, or a
# plausible-looking artifact) while silently doing the wrong thing, which makes
# every downstream gate built on it VACUOUS. Each case here pins both halves:
# the nonzero exit AND the specific diagnostic code, because "exits nonzero" is
# also what a crash does.
#
#   1. `lhd formal verify` on a latch design — the shared encoder REFUSES the
#      Latch cell, which used to surface as status:pass / exit 0 under a
#      warning. An encoder refusal is NOT a solver timeout: nothing was
#      encoded, so no budget can help and `formal.strict` must not be needed to
#      see it. Also asserts the report artifact still exists on the fail path
#      (the agent loop requires it on EVERY run).
#   2. the same design at the DEFAULT settings must not become clean merely
#      because `formal.strict` is off (the regression this guards).
#   3. `lhd sim` on a latch — used to die with a misleading `combinational-loop`
#      error naming an internal node, sending the reader after a loop that does
#      not exist.
#   4. `lhd sim` on a GATED-clock flop — sim commits every flop once per step()
#      regardless of clock_pin, so the gate is dead code and the flop LOADS
#      every tick. Silently wrong waveform, exit 0. (Lifted by M5.)
#   5. `lhd sim` on a TWO-CLOCK design — all state advances as if it shared one
#      clock and only the first net reaches the VCD. Silently wrong, exit 0.
#      (Lifted by M6.)
#
# Cases 4 and 5 are about FLOPS, not latches: they are here because an ICG is
# `latch + gated clock`, so latch support that ignores the gate ships the most
# dangerous bug in the area (see the todo's "Clock blindness" section).

set -u

LHD="${LHD:-lhd/lhd}"
if [ ! -x "$LHD" ]; then
  if [ -x ./bazel-bin/lhd/lhd ]; then
    LHD=./bazel-bin/lhd/lhd
  else
    echo "FAIL: could not find the lhd binary in $(pwd)"
    exit 1
  fi
fi

W="$(mktemp -d)"
trap 'rm -rf "$W"' EXIT

fail() {
  echo "FAIL: $*"
  exit 1
}

# Run a command; require a NONZERO exit and that the combined output contains
# the given diagnostic code. Both halves matter: a crash also exits nonzero, and
# a diagnostic that is only a warning still exits 0.
expect_fail_with() { # <label> <code> <cmd...>
  local label=$1 code=$2
  shift 2
  local out rc
  out="$("$@" 2>&1)"
  rc=$?
  if [ $rc -eq 0 ]; then
    echo "$out" | tail -5
    fail "$label: expected a NONZERO exit, got 0 (this is the silent-success bug M0 closes)"
  fi
  if ! grep -q "$code" <<<"$out"; then
    echo "$out" | tail -5
    fail "$label: exited $rc but without the '$code' diagnostic (wrong failure mode — a crash is not a refusal)"
  fi
  echo "ok: $label -> exit $rc, '$code'"
}

# ---- 1 + 2: encoder refusal on a latch design is a HARD failure --------------
# A closed testbench (no free inputs): stimulus is a pure function of an
# explicitly-initialized cycle counter, per the 2f-latch fixture conventions.
cat > "$W/lverify.prp" <<'EOF'
pub mod lhold_tb() -> (ok:bool@[0]) {
  reg cyc:u3 = 0
  reg l:u8:[latch=true]
  const c  = cyc
  const lq = l
  const en = (c == 0) or (c == 4)
  mut dv:u8 = 42
  if c <= 1 {
    dv = 5
  }
  if en {
    l = dv
  }
  wrap cyc += 1
  ok = lq#[0] == 0
  assert(((c >= 1) and (c <= 3)) implies (lq == 5), "latch holds its captured value")
}
EOF

expect_fail_with "formal verify on a latch (default settings)" "unsupported" \
  "$LHD" formal verify "$W/lverify.prp" --top lhold_tb --workdir "$W/vwd" \
  --set formal.bound=8 --set formal.prpfail_run=false

# The refusal must NOT depend on formal.strict — that flag exists to escalate a
# solver GIVE-UP, and conflating the two is exactly what made this exit 0.
out="$("$LHD" formal verify "$W/lverify.prp" --top lhold_tb --workdir "$W/vwd2" \
  --set formal.bound=8 --set formal.strict=false --set formal.prpfail_run=false 2>&1)"
[ $? -ne 0 ] || fail "formal verify with formal.strict=false went back to exit 0 (a REFUSAL is not a strictness setting)"
grep -q "REFUSAL, not a timeout" <<<"$out" \
  || fail "the refusal diagnostic must say it is not a timeout (so nobody 'fixes' it by raising formal.timeout)"
echo "ok: refusal is independent of formal.strict"

# The agent-loop report must exist even on the refusal path.
[ -s "$W/vwd/formal_report.json" ] \
  || fail "formal_report.json missing on the refusal path (the report must exist on EVERY run)"
echo "ok: formal_report.json still written on the refusal path"

# ---- 3: sim on a latch names the LATCH, not a phantom comb loop --------------
cat > "$W/lsim.prp" <<'EOF'
pub mod lhold8(en:bool, d:u8) -> (q:u8@[0]) {
  reg l:u8:[latch=true]
  q = l
  if en {
    l = d
  }
}
EOF

expect_fail_with "sim on a latch" "latch-unsupported" \
  "$LHD" sim "$W/lsim.prp" --setup-only --workdir "$W/swd"

out="$("$LHD" sim "$W/lsim.prp" --setup-only --workdir "$W/swd2" 2>&1)"
grep -q "combinational-loop" <<<"$out" \
  && fail "sim still reports the MISLEADING combinational-loop error for a latch"
echo "ok: the phantom combinational-loop diagnostic is gone"

# ---- 4: sim on a gated-clock flop -------------------------------------------
cat > "$W/gated.prp" <<'EOF'
pub mod gate8(clk:bool, en:bool, d:u8) -> (q:u8@[1]) {
  wire gclk:bool = nil
  gclk = clk and en
  reg f:u8:[clock_pin=ref gclk] = 0
  q = f
  f = d
}
EOF

expect_fail_with "sim on a gated-clock flop" "gated-clock-unsupported" \
  "$LHD" sim "$W/gated.prp" --setup-only --workdir "$W/gwd"

# ---- 5: sim on a two-clock design -------------------------------------------
cat > "$W/twoclk.prp" <<'EOF'
pub mod twoclk(clka:bool, clkb:bool, d:u8) -> (q:u8@[1]) {
  reg ra:u8:[clock_pin=clka] = 0
  reg rb:u8:[clock_pin=clkb] = 0
  ra = d
  rb = ra
  q = rb
}
EOF

expect_fail_with "sim on a two-clock design" "multi-clock-unsupported" \
  "$LHD" sim "$W/twoclk.prp" --setup-only --workdir "$W/twd"

# ---- guard: a plain SINGLE-clock design must still simulate ------------------
# The two refusals above are narrow. If they over-trigger, every ordinary design
# stops simulating — so pin the negative side too.
cat > "$W/plain.prp" <<'EOF'
pub mod plain8(d:u8) -> (q:u8@[1]) {
  reg f:u8 = 0
  q = f
  f = d
}

test plain8.one_cycle_delay {
  mut acc = plain8
  tick 3 {
    acc.d = 7
    step
    assert((clock >= 1) implies (acc.q == 7), "a plain flop still delays by one cycle")
  }
}
EOF

"$LHD" sim "$W/plain.prp" --setup-only --workdir "$W/pwd" >"$W/plain.log" 2>&1 \
  || { tail -5 "$W/plain.log"; fail "an ordinary single-clock design no longer simulates — the M0 clock checks over-trigger"; }
echo "ok: an ordinary single-clock design still simulates"

echo "PASS: latch_fail_closed_test"
