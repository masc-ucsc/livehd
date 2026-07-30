#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# Contract for the `lhd lec` verdict -> exit-code policy:
#   PROVEN  -> exit 0 (pass)
#   REFUTED -> exit 10 / error class `equiv_fail`                         [hard fail]
#              (a real counterexample: the designs DIFFER)
#   UNKNOWN -> exit 7  / error class `unsupported`                        [hard fail]
#              (the solver could not complete the proof and found NO counterexample)
#   UNKNOWN + --set formal.strict=false -> exit 0 + a loud "INCONCLUSIVE" WARNING
#                                                                         [explicit opt-out]
#   UNKNOWN + a WITNESS -> hard fail regardless of formal.strict (still undecided, but a
#              concrete CEX is in hand — never report that as a pass, and never let the
#              opt-out forgive it).
# RULING (2026-07-29): `formal.strict` now DEFAULTS TO TRUE — "an inconclusive should be
# a fail; the user can ignore it, but that must not be the default". An undecided run that
# exits 0 is indistinguishable from a real proof to any gate built on it, so could-not-prove
# now FAILS by default and is downgraded to a warning only when a run explicitly asks for
# that with `--set formal.strict=false`. This REPLACES the old deferred-warning default
# (could-not-prove => warning) that cases 1-2 used to pin.
# The two failures stay DISTINCT and must never be conflated: an UNKNOWN disproves nothing.
# rc 7 means "could not decide — raise the budget"; rc 10 means "here is a counterexample".
# That is why the strict opt-out moves rc 7 to 0 but leaves rc 10 untouched (case 5).
# The ind/bmc trust asymmetry: an ind Refute is NOT a disproof (its step case starts from
# an ARBITRARY, possibly unreachable state), so `auto` must let bmc clear it — case 7 pins
# that a bmc bounded-proof still WINS over an ind refute.
#
# Case 1/2/3's UNKNOWN is a 16-bit multiply-associativity miter ((a*b)*c vs a*(b*c)):
# equivalent, but equivalence-checking two structurally different multiplier trees blows
# up exponentially. Verified genuinely hard, not budget-shaped: it is still UNKNOWN at
# --set formal.timeout=300, while the same miter at 3/5/8 bits is PROVEN in ms-to-seconds.
# Do NOT "fix" a future failure here by raising the timeout — if this ever starts proving,
# the fixture is too easy and needs a wider/harder miter to keep pinning the UNKNOWN path.

set -u
LHD=./bazel-bin/lhd/lhd
if [ ! -x "$LHD" ]; then
  if [ -x ./lhd/lhd ]; then LHD=./lhd/lhd; else
    echo "FAIL: could not find the lhd binary in $(pwd)"; exit 1; fi
fi
WORK="${TEST_TMPDIR:-/tmp/lecpolicy}"; mkdir -p "$WORK"; fail=0

# equivalent but cvc5-can't-decide-quickly (nonlinear multiply) -> UNKNOWN
cat > "$WORK/hard_ref.v"  <<'EOF'
module foo(input [15:0] a, input [15:0] b, input [15:0] c, output [15:0] z); assign z = (a*b)*c; endmodule
EOF
cat > "$WORK/hard_impl.v" <<'EOF'
module foo(input [15:0] a, input [15:0] b, input [15:0] c, output [15:0] z); assign z = a*(b*c); endmodule
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

run() {  # $1=label $2..=lhd args ; sets RC/OUT
  # The default timeout is only supplied when the CALLER did not pass one.
  # It used to be appended unconditionally, so a caller's explicit
  # `--set formal.timeout=20` was silently overridden by this 2 (last-wins) —
  # the override never took effect. lhd now rejects the duplicate outright.
  local to=(--set formal.timeout=2)
  case " ${*:2} " in
    *" --set formal.timeout="*) to=() ;;
  esac
  OUT=$("$LHD" lec "${@:2}" --top foo --set formal.lec.hier=false ${to[@]+"${to[@]}"} --workdir "$WORK/w_$1" 2>&1); RC=$?
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
elif ! echo "$OUT" | grep -q "formal.strict=false"; then
  echo "FAIL: UNKNOWN default: failure does not name the opt-out"; fail=1
else echo "ok: UNKNOWN default -> exit $RC + an actionable could-not-decide message"; fi

# 2) UNKNOWN + the EXPLICIT opt-out -> exit 0 + a loud inconclusive WARNING (the old
#    default, now reachable only on request). The warning must not read as a proof.
run unkl --set formal.strict=false --ref "$WORK/hard_ref.v" --impl "$WORK/hard_impl.v"
if [ "$RC" -ne 0 ]; then echo "FAIL: UNKNOWN strict=false rc=$RC (want 0)"; fail=1
elif ! echo "$OUT" | grep -q "UNKNOWN"; then echo "FAIL: UNKNOWN strict=false: verdict not UNKNOWN"; fail=1
elif ! echo "$OUT" | grep -qi "INCONCLUSIVE"; then echo "FAIL: UNKNOWN strict=false: no inconclusive warning"; fail=1
elif ! echo "$OUT" | grep -q '"severity":"warning"'; then
  echo "FAIL: UNKNOWN strict=false: inconclusive was not raised as a warning diagnostic"; fail=1
elif ! echo "$OUT" | grep -q "NOT a proof of equivalence"; then
  echo "FAIL: UNKNOWN strict=false: warning does not disclaim being a proof"; fail=1
else echo "ok: UNKNOWN + --set formal.strict=false -> exit 0 + inconclusive warning"; fi

# 3) UNKNOWN + an EXPLICIT formal.strict=true -> same hard fail as the default. Pins that
#    the flag still means what it says now that true is also the default value.
run unks --set formal.strict=true --ref "$WORK/hard_ref.v" --impl "$WORK/hard_impl.v"
if [ "$RC" -eq 0 ]; then echo "FAIL: UNKNOWN strict=true rc=0 (want non-zero)"; fail=1
elif [ "$RC" -ne "$RC_UNK" ]; then
  echo "FAIL: UNKNOWN explicit strict=true rc=$RC != default rc=$RC_UNK (strict must BE the default)"; fail=1
else echo "ok: UNKNOWN explicit strict=true -> exit $RC (same as the default)"; fi

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

# 5) REFUTED + --set formal.strict=false -> STILL a hard fail. The opt-out forgives an
#    UNDECIDED run, never a disproof; nothing downgrades a counterexample.
run refl --set formal.strict=false --ref "$WORK/diff_ref.v" --impl "$WORK/diff_impl.v"
if [ "$RC" -eq 0 ]; then echo "FAIL: REFUTED strict=false rc=0 — the opt-out must NOT forgive a counterexample"; fail=1
elif [ "$RC" -ne "$RC_REF" ]; then echo "FAIL: REFUTED strict=false rc=$RC != strict rc=$RC_REF"; fail=1
elif ! echo "$OUT" | grep -q "REFUTED"; then echo "FAIL: REFUTED strict=false: verdict not REFUTED"; fail=1
else echo "ok: REFUTED + --set formal.strict=false -> still exit $RC"; fi

# 6) PROVEN -> exit 0
run prv --ref "$WORK/diff_ref.v" --impl "$WORK/eq_impl.v"
if [ "$RC" -ne 0 ]; then echo "FAIL: PROVEN rc=$RC (want 0)"; fail=1
elif ! echo "$OUT" | grep -q "PROVEN equivalent"; then echo "FAIL: PROVEN: verdict not PROVEN"; fail=1
else echo "ok: PROVEN -> exit 0"; fi

# 7) ind-Refuted but bmc clears it (unreachable step-case) -> PROVEN, exit 0.
#    Guards the soundness rationale AND the exit-code policy: an ind Refute must never
#    on its own fail a design bmc can prove — the `auto` race only escalates an ind CEX
#    to a failure when bmc could NOT settle the query.
run unreach --ref "$WORK/unreach_ref.v" --impl "$WORK/unreach_impl.v" --set formal.timeout=20
if [ "$RC" -ne 0 ]; then
  echo "FAIL: unreachable-state ind-refute rc=$RC (want 0: bmc clears a spurious ind CEX)"; fail=1
elif echo "$OUT" | grep -q "REFUTED"; then
  echo "FAIL: a spurious single-step ind CEX was reported as REFUTED"; fail=1
else echo "ok: ind-Refuted from an unreachable state -> bmc overrules -> exit 0"; fi

if [ $fail -ne 0 ]; then echo "lec_verdict_policy_test: FAILED"; exit 1; fi
echo "lec_verdict_policy_test: PASSED"; exit 0
