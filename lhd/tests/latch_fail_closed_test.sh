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
#   1. `lhd formal verify` on a latch design — the shared encoder REFUSED the
#      Latch cell, which used to surface as status:pass / exit 0 under a
#      warning. LIFTED BY M8 (edge normalization rewrites the latch into a
#      flop-with-enable before the encoder sees it), so asserting the refusal
#      here would now be asserting a BUG. What remains is the property that
#      survives every milestone: the design gets a REAL, NON-VACUOUS verdict,
#      identical across every invocation shape, and a MUTATED design refutes.
#   2. the verdict must not depend on the invocation shape (the fork-race wire
#      codec dropping a Verify_result field — M8 step 0a).
#   3. `lhd sim` on a latch — used to die with a misleading `combinational-loop`
#      error naming an internal node. LIFTED BY M5 (latches now simulate); what
#      remains here is that the misleading diagnostic never returns.
#   4. `lhd sim` on a GATED-clock flop — the gate was dead code and the flop
#      LOADED every tick, a silently wrong waveform at exit 0. LIFTED BY M5 for
#      the ICG shape `<clock> & <enable>`, which folds into a commit guard. A
#      derived clock that does NOT fold must still fail closed, and that is now
#      the case this file pins.
#   5. `lhd sim` on a TWO-CLOCK design — all state advanced as if it shared one
#      clock and only the first net reached the VCD, silently wrong at exit 0.
#      LIFTED BY M6 (non-reference clock nets get real edge detection); what
#      remains here is that the refusal does not come back.
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

# INVERTED AT M8 (was: expect_fail_with "unsupported"). Edge normalization
# retypes the Latch into a flop-with-enable — tolg already baked `din = en ? d
# : q`, so the update `q_next = en ? din : q` IS transparency observed at the
# end of the cycle — and the ordinary Flop encoding proves the obligation.
out="$("$LHD" formal verify "$W/lverify.prp" --top lhold_tb --workdir "$W/vwd" \
  --set formal.bound=8 --set formal.simfail_run=false 2>&1)"
rc=$?
if [ $rc -ne 0 ]; then
  echo "$out" | tail -5
  fail "formal verify on a latch exited $rc — M8 edge normalization regressed (the encoder is refusing the cell again)"
fi
grep -q "pass.single_edge" <<<"$out" \
  || fail "the latch was proven WITHOUT edge normalization in the recipe — the verdict is not coming from where we think"
echo "ok: formal verify PROVES a latch design (M8 lifted the encoder refusal)"

# NON-VACUITY. "PROVEN" means nothing unless the same machinery REFUTES a wrong
# claim: an encoder that quietly checks nothing also reports success. The mutant
# asserts the latch captured 6 where it captures 5, and must come back REFUTED
# verbatim (a degradation to UNKNOWN is NOT a fix -- verdict discipline).
sed 's/(lq == 5)/(lq == 6)/' "$W/lverify.prp" > "$W/lverify_mutant.prp"
out="$("$LHD" formal verify "$W/lverify_mutant.prp" --top lhold_tb --workdir "$W/vwdm" \
  --set formal.bound=8 --set formal.simfail_run=false 2>&1)"
[ $? -ne 0 ] || fail "the MUTATED latch property still passed: the obligation is not actually being checked (vacuous)"
grep -q "REFUTED" <<<"$out" \
  || { echo "$out" | tail -5; fail "the mutated latch property must be REFUTED verbatim, not merely inconclusive"; }
echo "ok: a wrong latch claim is REFUTED (the proof above is not vacuous)"

# The agent-loop report must exist on EVERY run.
[ -s "$W/vwd/formal_report.json" ] \
  || fail "formal_report.json missing (the report must exist on EVERY run)"
echo "ok: formal_report.json written"

# ---- 2a: the VERDICT MUST NOT DEPEND ON THE INVOCATION SHAPE ----------------
# 2f-latch M8 step 0a. `lhd formal verify` runs its bmc-first|ind-first
# portfolio either FORKED (results shipped back through serialize_verify) or
# SEQUENTIALLY IN-PROCESS (when a verdict cache is active, i.e. --workdir and
# formal.cache on). Verify_result::unsupported was missing from that wire codec,
# so the FORKED shapes silently dropped it and the parent reported a clean
# exit-0 for a design the encoder never encoded. Measured before the fix:
#   no --workdir                       -> rc 0   (forked, flag lost)
#   --workdir                          -> rc 7   (in-process, flag kept)
#   no --workdir + formal.cache=false  -> rc 0   (forked, flag lost)
# The invariant pinned here is the one that survives every later milestone: all
# three shapes AGREE. (When M8 teaches the encoder this design, all three flip
# to 0 together; a divergence is always the codec bug coming back.)
verdict_rc() { # <extra args...>  -> prints the exit code
  local wd
  wd="$(mktemp -d "$W/shape.XXXXXX")"
  "$LHD" formal verify "$W/lverify.prp" --top lhold_tb --set formal.bound=8 \
    --set formal.simfail_run=false "$@" >"$wd/log" 2>&1
  echo $?
}
rc_nowd=$(verdict_rc)
rc_wd=$(verdict_rc --workdir "$W/vwd3")
rc_nocache=$(verdict_rc --set formal.cache=false)
if [ "$rc_nowd" != "$rc_wd" ] || [ "$rc_nowd" != "$rc_nocache" ]; then
  fail "the verify verdict depends on the INVOCATION SHAPE: no-workdir=$rc_nowd workdir=$rc_wd cache=false=$rc_nocache (a Verify_result field is being dropped by the fork-race wire codec — see serialize_verify)"
fi
echo "ok: all three verify invocation shapes agree (rc=$rc_nowd) — no field lost in the fork race"

# ---- 2b: `lhd lec` on a latch pair -----------------------------------------
# ALSO INVERTED AT M8. This case was born as a regression guard for a hole found
# while landing M2: `lhd lec` runs its ind|bmc portfolio in a FORKED race and
# ships each engine's result through a hand-rolled wire codec, and the refusal
# flag was missing from it, so `lhd lec` on a latch design kept exiting 0 with
# status:pass long after `lhd formal verify` had been fixed. The refusal itself
# is gone (edge normalization runs MITER-WIDE, so both sides land in one time
# base); what this now pins is the pair of verdicts that proves the oracle is
# real — an equivalent latch pair PROVES, a polarity-flipped one REFUTES.
cat > "$W/lecref.v" <<'EOF'
module dut(input g, input [7:0] d, output reg [7:0] q);
  always_latch begin
    if (g)
      q <= d;
  end
endmodule
EOF
cat > "$W/lecimpl.v" <<'EOF'
module dut(input g, input [7:0] d, output reg [7:0] q);
  always_latch begin
    if (g)
      q <= (d | 8'h0);
  end
endmodule
EOF
cat > "$W/lecflip.v" <<'EOF'
module dut(input g, input [7:0] d, output reg [7:0] q);
  always_latch begin
    if (!g)
      q <= d;
  end
endmodule
EOF

out="$("$LHD" lec --impl verilog:"$W/lecimpl.v" --ref verilog:"$W/lecref.v" --top dut --workdir "$W/lwd" 2>&1)"
[ $? -eq 0 ] || { echo "$out" | tail -5; fail "lec on an EQUIVALENT latch pair no longer passes (M8 normalization regressed)"; }
echo "ok: lec PROVES an equivalent latch pair"

out="$("$LHD" lec --impl verilog:"$W/lecflip.v" --ref verilog:"$W/lecref.v" --top dut --workdir "$W/lwdf" 2>&1)"
[ $? -eq 0 ] && { echo "$out" | tail -5; fail "lec did NOT see a transparent-high vs transparent-low latch difference (the polarity fold is blind — the double-negation hazard)"; }
grep -q "REFUTED" <<<"$out" \
  || { echo "$out" | tail -5; fail "the polarity flip must be REFUTED verbatim, not merely inconclusive"; }
echo "ok: lec REFUTES an enable-polarity flip (the proof above is not vacuous)"

# ---- 3 + 4: LIFTED BY M5 — these two refusals are now real support ----------
# M0 made `lhd sim` REFUSE a latch (it used to die with a misleading
# `combinational-loop` error naming an internal node) and REFUSE a gated-clock
# flop (the gate was dead code, so the flop loaded every tick — a silently wrong
# waveform). M5 replaced both refusals with actual simulation, so asserting the
# refusals here would now be asserting a BUG. The positive behavior is pinned by
# the promoted trackers in inou/prp/tests/sim/ (latch_sim_hold, latch_sim_low,
# latch_sim_master_slave, flop_sim_gated_clock); all this file still owes is
# that the two misleading OLD failure modes never come back.
cat > "$W/lsim.prp" <<'EOF'
pub mod lhold8(en:bool, d:u8) -> (q:u8@[0]) {
  reg l:u8:[latch=true]
  q = l
  if en {
    l = d
  }
}

test lhold8.opens_and_holds {
  mut acc = lhold8
  tick 3 {
    acc.en = clock == 0
    acc.d  = 5
    step
    // Observation-visibility rule: read q only at CLOSED ticks, after at least
    // one closing edge. c1 and c2 are both closed and must show the captured 5.
    assert((clock >= 1) implies (acc.q == 5), "latch captured 5 and holds it")
  }
}
EOF

out="$("$LHD" sim "$W/lsim.prp" --setup-only --workdir "$W/swd" 2>&1)" || {
  echo "$out" | tail -3
  fail "sim now REFUSES a latch again — M5 support regressed"
}
grep -q "combinational-loop" <<<"$out" \
  && fail "sim reports the MISLEADING combinational-loop error for a latch (the pre-M0 failure mode is back)"
echo "ok: a latch simulates, and the phantom combinational-loop diagnostic stays gone"

cat > "$W/gated.prp" <<'EOF'
pub mod gate8(clk:bool, en:bool, d:u8) -> (q:u8@[1]) {
  wire gclk:bool = nil
  gclk = clk and en
  reg f:u8:[clock_pin=ref gclk] = 0
  q = f
  f = d
}

test gate8.holds_while_gated {
  mut acc = gate8
  tick 3 {
    acc.en = clock == 0
    acc.d  = if clock == 0 { 5 } else { 99 }
    step
    assert((clock >= 1) implies (acc.q == 5), "en=0: no edge on the gated clock, flop HOLDS")
  }
}
EOF

"$LHD" sim "$W/gated.prp" --setup-only --workdir "$W/gwd" >"$W/gated.log" 2>&1 \
  || { tail -3 "$W/gated.log"; fail "sim now REFUSES an ICG-shaped gated clock again — the M5 fold regressed"; }
echo "ok: an ICG-shaped gated clock folds into a commit guard"

# A derived clock that is NOT the foldable ICG shape must STILL fail closed:
# simulating it would commit every tick with the gate as dead code.
cat > "$W/derived.prp" <<'EOF'
pub mod der8(clk:bool, sel:bool, d:u8) -> (q:u8@[1]) {
  wire dclk:bool = nil
  dclk = clk != sel
  reg f:u8:[clock_pin=ref dclk] = 0
  q = f
  f = d
}

test der8.smoke {
  mut acc = der8
  tick 2 {
    acc.sel = false
    acc.d   = 1
    step
  }
}
EOF

expect_fail_with "sim on a NON-ICG derived clock" "gated-clock-unsupported" \
  "$LHD" sim "$W/derived.prp" --setup-only --workdir "$W/dwd"

# ---- 5: LIFTED BY M6 — a second clock domain now simulates -------------------
# M0 REFUSED a design with state on two clock nets, because every clock was
# advanced as if it were the one clock (and only the first reached the VCD). M6
# gives non-reference clock nets real edge detection, so this is now supported
# and asserting the refusal would be asserting a bug. The VALUE semantics —
# including pre-edge sampling on a coincident tick — are pinned by
# inou/prp/tests/sim/multiclock_two_domain.prp against an iverilog golden; all
# this file owes is that the refusal is gone.
cat > "$W/twoclk.prp" <<'EOF'
pub mod twoclk(clkb:bool, d:u8) -> (qa:u8@[1], qb:u8@[2]) {
  reg ra:u8 = 0
  reg rb:u8:[clock_pin=ref clkb] = 0
  qa = ra
  qb = rb
  ra = d
  rb = ra
}

test twoclk.second_domain_holds {
  mut acc = twoclk
  tick 3 {
    acc.d    = 7
    acc.clkb = false          // never toggles: the second domain must HOLD
    step
    assert(acc.qb == 0, "no clkb edge: the second domain never commits")
  }
}
EOF

"$LHD" sim "$W/twoclk.prp" --setup-only --workdir "$W/twd" >"$W/twd.log" 2>&1 \
  || { tail -3 "$W/twd.log"; fail "sim now REFUSES a two-clock design again — the M6 support regressed"; }
echo "ok: a second clock domain simulates (M0 refusal lifted by M6)"

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
