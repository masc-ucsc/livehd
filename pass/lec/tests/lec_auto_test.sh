#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# Contract for the `lec.engine=auto` parallel portfolio: race the inductive
# flop-cut miter and BMC-from-reset as two forked workers, take the first
# TRUSTWORTHY verdict, and encode the verdict-trust asymmetry:
#   inductive Proven (UNSAT + complete correspondence) -> PASS (kill bmc)
#   BMC Refuted (reachable CEX from reset)             -> FAIL (kill ind) + witness
#   inductive Refuted (single-step, maybe-unreachable) -> HINT only, never a FAIL
#   BMC bounded-Proven (no CEX <= bound)               -> not a full PASS
#   neither trustworthy                                -> inconclusive (exit 0)
# The headline property: on a design that ind FALSE-refutes (an unreachable
# step-case) auto must NOT hard-fail, even though `engine=ind` alone exits 1.

set -u
LHD=./bazel-bin/lhd/lhd
if [ ! -x "$LHD" ]; then
  if [ -x ./lhd/lhd ]; then LHD=./lhd/lhd; else
    echo "FAIL: could not find the lhd binary in $(pwd)"; exit 1; fi
fi
WORK="${TEST_TMPDIR:-/tmp/lecauto}"; mkdir -p "$WORK"; fail=0

# --- equal combinational -> auto PROVEN (inductive Proven trusted) ---
cat > "$WORK/eq_ref.v"  <<'EOF'
module foo(input [7:0] a, input [7:0] b, output [7:0] z); assign z = a + b; endmodule
EOF
cat > "$WORK/eq_impl.v" <<'EOF'
module foo(input [7:0] a, input [7:0] b, output [7:0] z); assign z = b + a; endmodule
EOF
# --- real combinational difference -> auto REFUTED (BMC Refuted trusted) ---
cat > "$WORK/bug_impl.v" <<'EOF'
module foo(input [7:0] a, input [7:0] b, output [7:0] z); assign z = a - b; endmodule
EOF
# --- unreachable-state design: s only ever holds {0,1,2} from reset, so the
#     special-cased s==3 is UNREACHABLE. ind assumes an arbitrary equal state
#     (incl. s==3) and FALSE-refutes; bmc from reset never reaches s==3. ---
cat > "$WORK/ur_ref.v"  <<'EOF'
module foo(input clk, input rst, output [3:0] z);
  reg [1:0] s;
  always @(posedge clk) if (rst) s <= 2'd0; else s <= (s==2'd2) ? 2'd0 : s + 2'd1;
  assign z = (s == 2'd3) ? 4'hF : {2'b00, s};
endmodule
EOF
cat > "$WORK/ur_impl.v" <<'EOF'
module foo(input clk, input rst, output [3:0] z);
  reg [1:0] s;
  always @(posedge clk) if (rst) s <= 2'd0; else s <= (s==2'd2) ? 2'd0 : s + 2'd1;
  assign z = {2'b00, s};
endmodule
EOF

run() {  # $1=label ; $2..=lhd args ; sets RC/OUT
  OUT=$("$LHD" lec "${@:2}" --top foo --set lec.timeout=20 --workdir "$WORK/w_$1" 2>&1); RC=$?
}

# 1) auto on the equal pair -> PROVEN via the inductive engine, exit 0
run eq --ref "$WORK/eq_ref.v" --impl "$WORK/eq_impl.v" --set lec.engine=auto
if [ "$RC" -ne 0 ]; then echo "FAIL: auto equal rc=$RC (want 0)"; fail=1
elif ! echo "$OUT" | grep -q "PROVEN equivalent"; then echo "FAIL: auto equal: not PROVEN"; fail=1
elif ! echo "$OUT" | grep -q "ind reached Proven"; then echo "FAIL: auto equal: ind did not win"; fail=1
else echo "ok: auto equal -> ind Proven (PASS)"; fi

# 2) auto on a real bug -> REFUTED via BMC, exit non-zero, with a witness
run bug --ref "$WORK/eq_ref.v" --impl "$WORK/bug_impl.v" --set lec.engine=auto
if [ "$RC" -eq 0 ]; then echo "FAIL: auto bug rc=0 (want non-zero)"; fail=1
elif ! echo "$OUT" | grep -q "REFUTED"; then echo "FAIL: auto bug: not REFUTED"; fail=1
elif ! echo "$OUT" | grep -q "bmc reached Refuted"; then echo "FAIL: auto bug: bmc did not win"; fail=1
else echo "ok: auto bug -> bmc Refuted (FAIL)"; fi

# 3a) ind ALONE false-refutes the unreachable-state design -> exit non-zero
run ur_ind --ref "$WORK/ur_ref.v" --impl "$WORK/ur_impl.v" --set lec.engine=ind
if [ "$RC" -eq 0 ]; then echo "FAIL: ind-only unreachable rc=0 (expected a FALSE refute, non-zero)"; fail=1
else echo "ok: ind-only unreachable -> REFUTED (the false refute auto must not trust)"; fi

# 3b) auto on the SAME design must NOT hard-fail: ind-Refuted is a HINT, bmc
#     only bounded-Proves -> inconclusive, exit 0 (the headline property).
run ur_auto --ref "$WORK/ur_ref.v" --impl "$WORK/ur_impl.v" --set lec.engine=auto
if [ "$RC" -ne 0 ]; then echo "FAIL: auto unreachable rc=$RC (want 0 — ind-Refuted is only a hint)"; fail=1
elif echo "$OUT" | grep -q "REFUTED"; then echo "FAIL: auto unreachable reported REFUTED (must not trust the ind CEX)"; fail=1
elif ! echo "$OUT" | grep -qi "HINT"; then echo "FAIL: auto unreachable: ind CEX not labelled a hint"; fail=1
else echo "ok: auto unreachable -> inconclusive, NOT a hard fail"; fi

if [ $fail -ne 0 ]; then echo "lec_auto_test: FAILED"; exit 1; fi
echo "lec_auto_test: PASSED"; exit 0
