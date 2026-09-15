#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# Contract for the `lhd lec` verdict -> exit-code policy:
#   PROVEN  -> exit 0 (pass)
#   REFUTED -> exit 10 / error class `equiv_fail`                         [hard fail]
#              (a real counterexample: the designs DIFFER)
#   UNKNOWN -> exit 7  / error class `unsupported`                        [hard fail]
#              (the solver could not complete the proof and found NO counterexample)
# UNKNOWN always fails; the removed formal.strict opt-out is rejected.
# An UNKNOWN is not a disproof: its exit class remains distinct from REFUTED.
# The ind/bmc trust asymmetry: an ind Refute is NOT a disproof (its step case starts from
# an ARBITRARY, possibly unreachable state), so `auto` must let bmc clear it — case 7 pins
# that a bmc bounded-proof still WINS over an ind refute.
#
# Case 1/2/3's UNKNOWN is a MASKED 16-bit multiply-associativity miter
# (((a*b)*c) & 16'hF0F0 vs (a*(b*c)) & 16'hF0F0): equivalent, but equivalence-checking two
# structurally different multiplier trees blows up exponentially. Verified genuinely hard,
# not budget-shaped: it is still UNKNOWN at --set formal.timeout=300, while the same miter
# at 3/5/8 bits is PROVEN in ms-to-seconds.
# Do NOT "fix" a future failure here by raising the timeout — if this ever starts proving,
# the fixture is too easy and needs a wider/harder miter to keep pinning the UNKNOWN path.
# That is exactly what happened on 2026-08-03: the BARE product ((a*b)*c vs a*(b*c)) is the
# shape `formal.lec.int_blast=auto` — now the default — re-solves as unbounded integers in
# ~0.1s on its second leg, so cases 1-3 started passing PROVEN. Following the rule above,
# the fixture was HARDENED rather than the knob switched off: the `& 16'hF0F0` puts the
# product under an `iand` lazy refinement, where int-blasting lands in undecidable
# nonlinear integer arithmetic. Measured UNKNOWN on BOTH legs (int_blast=off and
# int_blast=iand) at formal.timeout=30.
# `formal.min_timeout` is pinned small in run() for the same reason the timeout is: the
# int-blast retry's whole budget IS the min_timeout floor, so the 20s default would add
# ~20s to each of the three UNKNOWN cases.

set -u
LHD=./bazel-bin/lhd/lhd

if [ ! -x "$LHD" ]; then
  if [ -x ./lhd/lhd ]; then LHD=./lhd/lhd; else
    echo "FAIL: could not find the lhd binary in $(pwd)"; exit 1; fi
fi
WORK="${TEST_TMPDIR:-/tmp/lecpolicy}"; mkdir -p "$WORK"; fail=0

# equivalent but cvc5-can't-decide-quickly (masked nonlinear multiply) -> UNKNOWN
cat > "$WORK/hard_ref.v"  <<'EOF'
module foo(input [15:0] a, input [15:0] b, input [15:0] c, output [15:0] z); assign z = ((a*b)*c) & 16'hF0F0; endmodule
EOF
cat > "$WORK/hard_impl.v" <<'EOF'
module foo(input [15:0] a, input [15:0] b, input [15:0] c, output [15:0] z); assign z = (a*(b*c)) & 16'hF0F0; endmodule
EOF
# genuinely different -> REFUTED
cat > "$WORK/diff_ref.v"  <<'EOF'
module foo(input a, input b, output o); assign o = a & b; endmodule
EOF
cat > "$WORK/diff_impl.v" <<'EOF'
module foo(input a, input b, output o); assign o = a | b; endmodule
EOF
# identical -> PROVEN
cat > "$WORK/eq_impl.v" <<'EOF'
module foo(input a, input b, output o); assign o = ~((~a)|(~b)); endmodule
EOF
# Sequential pair that is EQUIVALENT from reset but whose INDUCTIVE step case refutes:
# `s` is 0 in every reachable state, so z==b in both — but induction starts from an
# ARBITRARY equal state, picks s=1, and reports a (spurious) a!=b divergence. bmc from
# reset must overrule it. This is the false-negative that makes ind-Refuted untrusted.
cat > "$WORK/unreach_ref.v"  <<'EOF'
module foo(input clk, input rst, input [7:0] a, input [7:0] b, output reg [7:0] z);
  reg s;
  always @(posedge clk)
    if (rst) begin s <= 1'b0; z <= 8'h0; end
    else     begin s <= 1'b0; z <= s ? a : b; end
endmodule
EOF
cat > "$WORK/unreach_impl.v" <<'EOF'
module foo(input clk, input rst, input [7:0] a, input [7:0] b, output reg [7:0] z);
  reg s;
  always @(posedge clk)
    if (rst) begin s <= 1'b0; z <= 8'h0; end
    else     begin s <= 1'b0; z <= b; end
endmodule
EOF

# The reference has one dead state bit that the implementation legitimately
# removes. Equivalence is about observable traces: once every common output and
# next-state obligation proves for ARBITRARY `dead`, the one-sided register is
# proven unobservable and must not turn a real proof into incomplete
# correspondence. A missing primary output remains incomplete; this exception
# applies only to nxt:/mem: internal-state cuts.
cat > "$WORK/dead_ref.v" <<'EOF'
module foo(input clk, input rst, input a, output reg y);
  reg dead;
  always @(posedge clk) begin
    if (rst) begin dead <= 1'b0; y <= 1'b0; end
    else begin dead <= a; y <= a; end
  end
endmodule
EOF
cat > "$WORK/dead_impl.v" <<'EOF'
module foo(input clk, input rst, input a, output reg y);
  always @(posedge clk) begin
    if (rst) y <= 1'b0;
    else y <= a;
  end
endmodule
EOF

run() {  # $1=label $2..=lhd args ; sets RC/OUT
  # The default timeout is only supplied when the CALLER did not pass one.
  # It used to be appended unconditionally, so a caller's explicit
  # `--set formal.timeout=20` was silently overridden by this 2 (last-wins) —
  # the override never took effect. lhd now rejects the duplicate outright.
  local to=(--set formal.timeout=2)
  case " ${*:2} " in
    *" --set formal.timeout="*) to=() ;;
  esac
  # Same last-wins guard for the int-blast retry's budget (see the header): its
  # whole grant is the min_timeout floor, so the 20s default would add ~20s to
  # every UNKNOWN case here. min_timeout is a FLOOR, never a cap, so pinning it
  # low cannot take a verdict away from the cases that do settle (7).
  local mt=(--set formal.min_timeout=1)
  case " ${*:2} " in
    *" --set formal.min_timeout="*) mt=() ;;
  esac
  OUT=$("$LHD" lec "${@:2}" --top foo --set formal.lec.hier=false ${to[@]+"${to[@]}"} \
        ${mt[@]+"${mt[@]}"} --workdir "$WORK/w_$1" 2>&1); RC=$?
}

# 1) UNKNOWN, DEFAULT policy (strict is on by default) -> hard fail, and the message has
#    to say WHY it failed and how to proceed (raise the budget, or opt out explicitly).
#    An UNKNOWN must still never be dressed up as a REFUTED.
run unk --ref "$WORK/hard_ref.v" --impl "$WORK/hard_impl.v"
RC_UNK=$RC
if [ "$RC" -eq 0 ]; then echo "FAIL: UNKNOWN default rc=0 (want non-zero: strict is the default)"; fail=1
elif ! echo "$OUT" | grep -q "UNKNOWN"; then echo "FAIL: UNKNOWN default: verdict not UNKNOWN"; fail=1
elif echo "$OUT" | grep -q "REFUTED"; then echo "FAIL: UNKNOWN default: reported as REFUTED"; fail=1
elif ! echo "$OUT" | grep -q "could not decide equivalence"; then
  echo "FAIL: UNKNOWN default: failure does not explain that nothing was decided"; fail=1
elif ! echo "$OUT" | grep -q "NOT a disproof"; then
  echo "FAIL: UNKNOWN default: failure does not distinguish itself from a disproof"; fail=1
elif ! echo "$OUT" | grep -q "formal.timeout"; then
  echo "FAIL: UNKNOWN default: failure does not point at the budget knob"; fail=1
else echo "ok: UNKNOWN default -> exit $RC + an actionable could-not-decide message"; fi

# The old opt-out must be rejected before any proof runs.
run retired --set formal.strict=false --ref "$WORK/hard_ref.v" --impl "$WORK/hard_impl.v"
if [ "$RC" -eq 0 ] || ! echo "$OUT" | grep -qi 'unknown'; then
  echo "FAIL: removed formal.strict option was accepted"; fail=1
fi

# 4) REFUTED -> hard fail for a DIFFERENT reason than an UNKNOWN: a real counterexample.
#    Both fail now, so the exit codes are what keeps them apart — a gate must be able to
#    tell "the designs DIFFER" from "I could not decide".
run ref --ref "$WORK/diff_ref.v" --impl "$WORK/diff_impl.v"
RC_REF=$RC
if [ "$RC" -eq 0 ]; then echo "FAIL: REFUTED rc=0 (want non-zero)"; fail=1
elif ! echo "$OUT" | grep -q "REFUTED"; then echo "FAIL: REFUTED: verdict not REFUTED"; fail=1
elif [ "$RC" -eq "$RC_UNK" ]; then
  echo "FAIL: REFUTED rc=$RC is the SAME as UNKNOWN rc=$RC_UNK (a disproof must not be conflated with an undecided run)"; fail=1
elif ! echo "$OUT" | grep -q "is not equivalent"; then echo "FAIL: REFUTED: no not-equivalent error"; fail=1
elif ! echo "$OUT" | grep -q "counterexample"; then echo "FAIL: REFUTED: no counterexample reported"; fail=1
elif echo "$OUT" | grep -q "could not decide equivalence"; then
  echo "FAIL: REFUTED: reported as an undecided run"; fail=1
else echo "ok: REFUTED -> exit $RC (counterexample), distinct from UNKNOWN exit $RC_UNK"; fi


# 6) PROVEN -> exit 0
run prv --ref "$WORK/diff_ref.v" --impl "$WORK/eq_impl.v"
if [ "$RC" -ne 0 ]; then echo "FAIL: PROVEN rc=$RC (want 0)"; fail=1
elif ! echo "$OUT" | grep -q "PROVEN equivalent"; then echo "FAIL: PROVEN: verdict not PROVEN"; fail=1
else echo "ok: PROVEN -> exit 0"; fi

# 7) ind-Refuted but bmc clears it (unreachable step-case) -> PROVEN, exit 0.
#    Guards the soundness rationale AND the exit-code policy: an ind Refute must never
#    on its own fail a design bmc can prove — the `auto` race only escalates an ind CEX
#    to a failure when bmc could NOT settle the query.
# The rescue is a BOUNDED claim, disclosed in the result.
run unreach --ref "$WORK/unreach_ref.v" --impl "$WORK/unreach_impl.v" --set formal.timeout=20
if [ "$RC" -ne 0 ]; then
  echo "FAIL: unreachable-state ind-refute rc=$RC (want 0: bmc clears a spurious ind CEX)"; fail=1
elif echo "$OUT" | grep -q "REFUTED"; then
  echo "FAIL: a spurious single-step ind CEX was reported as REFUTED"; fail=1
else echo "ok: ind-Refuted from an unreachable state -> bmc overrules -> exit 0"; fi

# 8) Dead one-sided internal state is harmless after all common obligations
# prove for arbitrary values of it. This is an unbounded inductive proof.
run dead --ref "$WORK/dead_ref.v" --impl "$WORK/dead_impl.v" --set formal.engine=ind \
  --set formal.lec.cones=true --set formal.lec.decompose=true
if [ "$RC" -ne 0 ]; then
  echo "FAIL: dead one-sided state rc=$RC (want 0): $OUT"; fail=1
elif ! echo "$OUT" | grep -q "PROVEN equivalent"; then
  echo "FAIL: dead one-sided state did not report PROVEN: $OUT"; fail=1
elif ! echo "$OUT" | grep -q "one-sided internal state is unobservable"; then
  echo "FAIL: dead one-sided state proof did not disclose why the unmatched cut is safe: $OUT"; fail=1
else echo "ok: dead one-sided internal state -> unbounded PROVEN"; fi

# 9) The hard worker backstop is a real registered option, and invalid negative
# multipliers fail during option validation instead of wrapping into a huge
# alarm duration.
run bad_hard_mult --ref "$WORK/eq_impl.v" --impl "$WORK/eq_impl.v" \
  --set formal.hard_timeout_mult=-1
if [ "$RC" -eq 0 ]; then
  echo "FAIL: negative formal.hard_timeout_mult unexpectedly succeeded: $OUT"; fail=1
elif ! echo "$OUT" | grep -q "formal.hard_timeout_mult must be >= 0"; then
  echo "FAIL: negative hard-timeout multiplier lacked the validation diagnostic: $OUT"; fail=1
else echo "ok: formal.hard_timeout_mult is registered and range-checked"; fi

if [ $fail -ne 0 ]; then echo "lec_verdict_policy_test: FAILED"; exit 1; fi
echo "lec_verdict_policy_test: PASSED"; exit 0
