#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# Gate for the FORMAL PHASE SCHEDULE (todo/livehd/2f-lec "phase-schedule",
# 2f-latch M10): one source period is encoded as an ordered sequence of
# microsteps, so a latch closes and a negedge endpoint commits INSIDE the period
# that the old one-step-is-one-period encoding collapsed.
#
# EVERY proof here is paired with a REFUTATION that must still fire. That
# pairing is the whole point: an encoder that has stopped distinguishing the two
# machines proves both, and a symmetric "before and after" check cannot see it
# (the cross-model gating rule, 2f-latch "Rules"). The negative controls are
# built to be indistinguishable from the positive case UNLESS the schedule is
# real — a wrong microstep assignment, or a clock guard sampled on the wrong
# phase, flips exactly one of each pair.

set -u

LHD=./bazel-bin/lhd/lhd
if [ ! -x "$LHD" ]; then
  if [ -x ./lhd/lhd ]; then LHD=./lhd/lhd; else
    echo "FAIL: could not find the lhd binary in $(pwd)"; exit 1; fi
fi

WORK="${TEST_TMPDIR:-/tmp/phasesched}"
mkdir -p "$WORK"
fail=0

# $1=label $2=ref.v $3=impl.prp $4=top $5=PROVEN|REFUTED  [$6.. extra lhd args]
check() {
  local label=$1 ref=$2 impl=$3 top=$4 want=$5; shift 5
  local out rc
  out=$("$LHD" lec --impl "$WORK/$impl" --ref "verilog:$WORK/$ref" --top "$top" --reader slang \
        --workdir "$WORK/w_$label" "$@" 2>&1); rc=$?
  case "$want" in
    PROVEN)
      if [ "$rc" -ne 0 ] || ! echo "$out" | grep -q "PROVEN equivalent"; then
        echo "FAIL: $label expected PROVEN (rc=$rc)"; echo "$out" | grep -aE "^lec: " | tail -2; fail=1
      else echo "ok: $label PROVEN"; fi ;;
    REFUTED)
      # A REFUTATION must be a real counterexample, never an UNKNOWN: an encoder
      # that merely degraded to "could not decide" has not caught the bug.
      if ! echo "$out" | grep -q "REFUTED"; then
        echo "FAIL: $label expected REFUTED (rc=$rc)"; echo "$out" | grep -aE "^lec: " | tail -2; fail=1
      else echo "ok: $label REFUTED (the control still fires)"; fi ;;
  esac
}

# ── 1. BOTH clock-role latch polarities, each feeding a coincident-edge flop ──
# `low_latch` is transparent while clk==0 and closes at the RISE, which is the
# same edge `rise` samples on — so the flop takes the value the latch closed on,
# not the one it held through the previous period. `high_latch` is the dual.
# A schedule that puts either latch in the wrong microstep lands a full period
# off, which is what the mutant below detects.
cat > "$WORK/edges.v" <<'EOF'
module edges (input wire clk, input wire [2:0] d_lo, input wire [2:0] d_hi,
              output wire [2:0] rise_o, output wire [2:0] fall_o);
  reg [2:0] low_latch, high_latch, rise, fall;
  always_latch if (!clk) low_latch  <= d_lo;
  always_latch if ( clk) high_latch <= d_hi;
  always @(posedge clk) rise <= low_latch;
  always @(negedge clk) fall <= high_latch;
  assign rise_o = rise;
  assign fall_o = fall;
endmodule
EOF
cat > "$WORK/edges.prp" <<'EOF'
pub mod edges::[timecheck=false](clk:u1, d_lo:u3, d_hi:u3) -> (rise_o:u3@[], fall_o:u3@[]) {
  reg rise:u3:[clock_pin=ref clk]
  reg fall:u3:[clock_pin=ref clk, posclk=false]
  rise = d_lo
  fall = d_hi
  rise_o = rise
  fall_o = fall
}
EOF
# CONTROL: the two latch windows swapped. Identical structure, identical cell
# count, only the polarity differs — so this refutes iff the schedule really
# distinguishes close-before-rise from close-before-fall.
sed 's/rise = d_lo/rise = d_hi/; s/fall = d_hi/fall = d_lo/' "$WORK/edges.prp" > "$WORK/edges_bad.prp"

check latch_edges     edges.v edges.prp     edges PROVEN
check latch_edges_mut edges.v edges_bad.prp edges REFUTED

# ── 2. The ACTIVE-LOW clock gate: the guard's SAMPLE POINT is observable ──────
# `clk | ~held_en` latches its enable while the clock is HIGH, so the value that
# gates the fall is the POST-RISE one. The enable is `early[0]`, and `early`
# commits at the rise — so a scheduler that samples every guard before the RISE
# reads the PREVIOUS period's value. That is a wrong verdict, not an UNKNOWN,
# which is why it needs a gate of its own.
cat > "$WORK/inv.v" <<'EOF'
module inv_cell (input wire clk, input wire en, output wire gclk_n);
  reg held_en;
  always_latch if (clk) held_en <= en;
  assign gclk_n = clk | ~held_en;
endmodule
module inv (input wire clk, input wire [2:0] d, output wire [2:0] q);
  wire gn;
  reg [2:0] early, late;
  inv_cell u_gate(.clk(clk), .en(early[0]), .gclk_n(gn));
  always @(posedge clk) early <= d;
  always @(negedge gn)  late  <= d ^ 3'b111;
  assign q = late ^ early;
endmodule
EOF
cat > "$WORK/inv.prp" <<'EOF'
pub mod inv::[timecheck=false](clk:u1, d:u3) -> (q:u3@[]) {
  reg early:u3:[clock_pin=ref clk]
  reg late:u3:[clock_pin=ref clk, posclk=false]
  early = d
  if (early & 1) != 0 {
    late = d ^ 0ub111
  }
  q = late ^ early
}
EOF
# CONTROL: gate `late` on the enable's value BEFORE the rise (`late`'s own held
# copy of it) instead of after. This is precisely the machine a pre-rise sample
# would model, so it MUST refute — if it proves, the sample point is not being
# honoured and the two flavours have collapsed into one.
cat > "$WORK/inv_bad.prp" <<'EOF'
pub mod inv::[timecheck=false](clk:u1, d:u3) -> (q:u3@[]) {
  reg early:u3:[clock_pin=ref clk]
  reg prev:u3:[clock_pin=ref clk]
  reg late:u3:[clock_pin=ref clk, posclk=false]
  prev = early
  early = d
  if (prev & 1) != 0 {
    late = d ^ 0ub111
  }
  q = late ^ early
}
EOF
check gate_invert     inv.v inv.prp     inv PROVEN
check gate_invert_mut inv.v inv_bad.prp inv REFUTED

# ── 3. The escape hatch still refuses ────────────────────────────────────────
# `formal.phase_sched=false` puts the run back on the M8 rewrite, which DECLINES
# a coincident latch/flop edge by name. Pinning it keeps the fallback honest:
# turning the schedule off must fail closed, never silently answer.
out=$("$LHD" lec --impl "$WORK/edges.prp" --ref "verilog:$WORK/edges.v" --top edges --reader slang \
      --workdir "$WORK/w_off" --set formal.phase_sched=false 2>&1); rc=$?
if [ "$rc" -eq 0 ]; then
  echo "FAIL: phase_sched=false did not refuse the coincident-edge design (rc=0)"; fail=1
elif ! echo "$out" | grep -qi "coincident"; then
  echo "FAIL: phase_sched=false refused, but not by name"; echo "$out" | tail -2; fail=1
else
  echo "ok: phase_sched=false -> the M8 rewrite declines by name (fail-closed fallback)"
fi

if [ $fail -ne 0 ]; then echo "phase_schedule_test: FAILED"; exit 1; fi
echo "phase_schedule_test: PASS"
