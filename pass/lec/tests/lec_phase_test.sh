#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# Reset-phase separation (formal.phase) for the BMC engine. Two sequential pairs
# whose verdict DIFFERS by phase prove the gating is real:
#   pair A (+1 vs +2 counter, same reset value): equivalent ONLY under reset.
#   pair B (y = rst ? <diff const> : data):      equivalent ONLY while running.
# We assert the exact verdict per phase.

set -u

LHD=./bazel-bin/lhd/lhd

if [ ! -x "$LHD" ]; then
  if [ -x ./lhd/lhd ]; then LHD=./lhd/lhd; else
    echo "FAIL: could not find the lhd binary in $(pwd)"; exit 1; fi
fi

WORK="${TEST_TMPDIR:-/tmp/lecphase}"
mkdir -p "$WORK"
fail=0

# ---- pair A: +1 vs +2 counter, both reset to 0 -----------------------------
cat > "$WORK/a1.v" <<'EOF'
module cnt(input clk, input rst, output reg [3:0] q);
  always @(posedge clk) if (rst) q <= 4'd0; else q <= q + 4'd1;
endmodule
EOF
cat > "$WORK/a2.v" <<'EOF'
module cnt(input clk, input rst, output reg [3:0] q);
  always @(posedge clk) if (rst) q <= 4'd0; else q <= q + 4'd2;
endmodule
EOF

# ---- pair B: combinational reset-forced output, identical when running -----
cat > "$WORK/b1.v" <<'EOF'
module dut(input clk, input rst, input [7:0] data, output [7:0] y);
  assign y = rst ? 8'hAA : data;
endmodule
EOF
cat > "$WORK/b2.v" <<'EOF'
module dut(input clk, input rst, input [7:0] data, output [7:0] y);
  assign y = rst ? 8'h00 : data;
endmodule
EOF

compile() {  # $1=src $2=lgdir $3=top
  $LHD compile "$WORK/$1" --reader yosys-verilog --top "$3" --emit-dir "lg:$WORK/$2" --workdir "$WORK/w_$2" >/dev/null 2>&1 \
    || { echo "FAIL: compile $1"; exit 1; }
}
compile a1.v a1_lg cnt
compile a2.v a2_lg cnt
compile b1.v b1_lg dut
compile b2.v b2_lg dut

# verdict $impl $ref $top $phase  -> echoes PROVEN | REFUTED | UNKNOWN
verdict() {
  $LHD lec --impl "lg:$WORK/$1" --ref "lg:$WORK/$2" --top "$3" \
       --set formal.lec.hier=false --set formal.engine=bmc --set formal.bound=8 --set "formal.phase=$4" \
       --workdir "$WORK/q_${3}_$4_$$_$RANDOM" 2>&1 \
    | grep -oE "PASS\\([0-9]+\\)|PROVEN equivalent|REFUTED \\(not equivalent\\)|UNKNOWN" | head -1 \
    | sed -E "s/PASS\\([0-9]+\\)/PROVEN equivalent/"   # PASS(n) is a pass; this test is about phase mechanics, not depth
}

expect() {  # $1=label $2=got $3=want
  if [ "$2" != "$3" ]; then echo "FAIL: $1 -> got '$2', want '$3'"; fail=1
  else echo "ok: $1 -> $2"; fi
}

# pair A: equivalent only during reset
expect "A just_reset"  "$(verdict a2_lg a1_lg cnt just_reset)"  "PROVEN equivalent"
expect "A after_reset" "$(verdict a2_lg a1_lg cnt after_reset)" "REFUTED (not equivalent)"

# pair B: equivalent only while running (after reset)
expect "B just_reset"  "$(verdict b2_lg b1_lg dut just_reset)"  "REFUTED (not equivalent)"
expect "B after_reset" "$(verdict b2_lg b1_lg dut after_reset)" "PROVEN equivalent"

# `full` = just_reset AND after_reset: each pair fails one stage, so full REFUTES both;
# a design compared against itself is equivalent in BOTH stages, so full PROVES it.
expect "A full"      "$(verdict a2_lg a1_lg cnt full)" "REFUTED (not equivalent)"
expect "B full"      "$(verdict b2_lg b1_lg dut full)" "REFUTED (not equivalent)"
expect "self full"   "$(verdict a1_lg a1_lg cnt full)" "PROVEN equivalent"

# ---- pair C: slang -> Pyrope latch round trip ------------------------------
# The high-phase latch captures an enable used LIVE by the low-phase latch.
# The data arm is a genuine mux/concat value, not Q feedback. The phase encoder
# used to peel that mux as though it were an ICG hold arm and then sample the
# live enable one half-period late, refuting the writer's equivalent Pyrope.
cat > "$WORK/c.v" <<'EOF'
module latch_child(input logic clock, input logic d, output logic q);
  always_latch if (!clock) q = d;
endmodule

module latch_pair(
  input logic clock,
  input logic en,
  input logic [31:0] d,
  output logic [64:0] q,
  output logic child_q
);
  logic en_l;
  logic [64:0] q_l;
  latch_child u_child(.clock(clock), .d(d[0]), .q(child_q));
  always_latch if (clock) en_l = en;
  always_latch if (!clock && en_l) q_l = {{33{d[31]}}, d};
  assign q = q_l;
endmodule
EOF

if ! $LHD compile "$WORK/c.v" --reader slang --top latch_pair \
       --emit-dir "lg:$WORK/c_ref_lg" --emit-dir "pyrope:$WORK/c_prp" --workdir "$WORK/w_c_emit" >/dev/null 2>&1; then
  echo "FAIL: compile/emit latch_pair"; fail=1
elif ! $LHD compile "$WORK/c_prp/latch_pair.prp" --top latch_pair \
       --emit-dir "lg:$WORK/c_impl_lg" --workdir "$WORK/w_c_impl" >/dev/null 2>&1; then
  echo "FAIL: recompile emitted latch_pair Pyrope"; fail=1
else
  c_out=$($LHD lec --impl "lg:$WORK/c_impl_lg" --ref "lg:$WORK/c_ref_lg" --top latch_pair \
          --set formal.engine=ind --set formal.timeout=10 --workdir "$WORK/q_c" 2>&1)
  if ! grep -qa "PROVEN equivalent" <<<"$c_out"; then
    echo "FAIL: latch round trip was not PROVEN"; fail=1
  # Pin the EXACT proof arm. BOTH phase arms spell the word "microstep", so
  # matching that alone lets the proof drift between arms with no test turning
  # red. Expected arm here: the COMPOSED-PERIOD one, query.cpp's
  #   "phase-inductive: one composed source period, <n> microsteps"
  # The emitted Pyrope now re-reads to terms syntactically IDENTICAL to the
  # source graph's, so the per-step arm's `at == bt` skip leaves it with no
  # compare point (`step_diffs.empty()` -> step_proven=false) and it hands the
  # period to the composed arm. The per-STEP arm is pinned by pair D below.
  # If this ever lands on a different arm, pin THAT arm's exact spelling --
  # never relax this back to a bare "microstep" match.
  elif ! grep -qa "phase-inductive: one composed source period" <<<"$c_out"; then
    echo "FAIL: latch round trip was not proven on the composed-period phase arm"; fail=1
  else
    echo "ok: latch round trip -> PROVEN equivalent (composed period)"
  fi
fi

# ---- pair D: two SV spellings of the same 65-bit sign-extended latch --------
# Same phase structure as pair C, but the two sides are built from DIFFERENT
# source expressions ({{33{d[31]}},d} vs a signed cast), so their state terms
# are not syntactically equal and the proof must compare each scheduled
# transition: the phase-STEP induction arm.
cat > "$WORK/d.v" <<'EOF'
module latch_child(input logic clock, input logic d, output logic q);
  always_latch if (!clock) q = d;
endmodule

module latch_pair(
  input logic clock,
  input logic en,
  input logic [31:0] d,
  output logic [64:0] q,
  output logic child_q
);
  logic en_l;
  logic [64:0] q_l;
  latch_child u_child(.clock(clock), .d(d[0]), .q(child_q));
  always_latch if (clock) en_l = en;
  always_latch if (!clock && en_l) q_l = 65'(signed'(d));
  assign q = q_l;
endmodule
EOF

if ! $LHD compile "$WORK/d.v" --reader slang --top latch_pair \
       --emit-dir "lg:$WORK/d_lg" --workdir "$WORK/w_d" >/dev/null 2>&1; then
  echo "FAIL: compile latch_pair (signed-cast spelling)"; fail=1
else
  d_out=$($LHD lec --impl "lg:$WORK/d_lg" --ref "lg:$WORK/c_ref_lg" --top latch_pair \
          --set formal.engine=ind --set formal.timeout=10 --workdir "$WORK/q_d" 2>&1)
  if ! grep -qa "PROVEN equivalent" <<<"$d_out"; then
    echo "FAIL: latch spelling pair was not PROVEN"; fail=1
  elif ! grep -qa "phase-step induction" <<<"$d_out"; then
    echo "FAIL: latch spelling pair did not exercise phase-step induction"; fail=1
  else
    echo "ok: latch spelling pair -> PROVEN equivalent (phase-step)"
  fi
fi

if [ $fail -ne 0 ]; then echo "lec_phase_test: FAILED"; exit 1; fi
echo "lec_phase_test: PASSED"
exit 0
