#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# Contract for `lec.partitions` / `lec.split` — parallel input-space case-split.
# On a PURELY COMBINATIONAL miter the engine auto-picks the small-width control
# input feeding the widest variable shift / mux selector, forks up to N workers
# (default 4), and proves the miter one selector-cofactor at a time. Pinning the
# selector to a constant folds the control-dependent datapath, so each cube is
# trivial. A decisive case-split verdict is used; otherwise it falls back to the
# monolithic single-ind query (never a regression). Only combinational pairs are
# case-split, so an ind-Refuted cube is a genuine reachable counterexample.

set -u
LHD=./bazel-bin/lhd/lhd
if [ ! -x "$LHD" ]; then
  if [ -x ./lhd/lhd ]; then LHD=./lhd/lhd; else
    echo "FAIL: could not find the lhd binary in $(pwd)"; exit 1; fi
fi
WORK="${TEST_TMPDIR:-/tmp/lecpart}"; mkdir -p "$WORK"; fail=0

# selector-based combinational ALU (sel picks the operation).
cat > "$WORK/good.sv" <<'EOF'
module m(input [1:0] sel, input [7:0] a, input [7:0] b, output reg [7:0] y);
  always @* case(sel) 0: y=a+b; 1: y=a-b; 2: y=a&b; default: y=a|b; endcase
endmodule
EOF
# buggy impl: differs ONLY in the sel==2 arm (a&b -> a^b).
cat > "$WORK/bug.sv" <<'EOF'
module m(input [1:0] sel, input [7:0] a, input [7:0] b, output reg [7:0] y);
  always @* case(sel) 0: y=a+b; 1: y=a-b; 2: y=a^b; default: y=a|b; endcase
endmodule
EOF
# pure datapath, NO control operand -> auto finds no split -> graceful fallback.
cat > "$WORK/plain.sv" <<'EOF'
module m(input [7:0] a, input [7:0] b, output [7:0] y);
  assign y = (a & b) | (a ^ b);
endmodule
EOF

run() { OUT=$("$LHD" lec "${@}" --top m --set lec.semdiff=none 2>&1); RC=$?; }

# 1) auto case-split PROVES the equal pair (solver path forced via semdiff=none).
run --ref "$WORK/good.sv" --impl "$WORK/good.sv"
if [ "$RC" -ne 0 ]; then echo "FAIL: equal rc=$RC (want 0)"; echo "$OUT"; fail=1
elif ! echo "$OUT" | grep -q "PROVEN equivalent"; then echo "FAIL: equal not PROVEN"; fail=1
elif ! echo "$OUT" | grep -q "case-split sel"; then echo "FAIL: equal did not case-split on sel"; fail=1
else echo "ok: equal -> case-split on sel PROVEN"; fi

# 2) auto case-split REFUTES the sel==2-arm bug, witness pinned to sel=2.
run --ref "$WORK/good.sv" --impl "$WORK/bug.sv"
if [ "$RC" -eq 0 ]; then echo "FAIL: bug rc=0 (want non-zero)"; fail=1
elif ! echo "$OUT" | grep -q "REFUTED"; then echo "FAIL: bug not REFUTED"; fail=1
elif ! echo "$OUT" | grep -q "sel=2"; then echo "FAIL: bug witness did not pin sel=2"; echo "$OUT" | grep -i witness; fail=1
else echo "ok: bug -> case-split REFUTED @ sel=2"; fi

# 3) lec.partitions=1 DISABLES the case-split -> single monolithic ind query.
run --ref "$WORK/good.sv" --impl "$WORK/good.sv" --set lec.partitions=1
if [ "$RC" -ne 0 ]; then echo "FAIL: partitions=1 rc=$RC (want 0)"; fail=1
elif ! echo "$OUT" | grep -q "PROVEN equivalent"; then echo "FAIL: partitions=1 not PROVEN"; fail=1
elif echo "$OUT" | grep -q "case-split"; then echo "FAIL: partitions=1 still case-split (should disable)"; fail=1
else echo "ok: partitions=1 -> case-split disabled (single ind)"; fi

# 4) explicit lec.split=sel forces the same signal.
run --ref "$WORK/good.sv" --impl "$WORK/good.sv" --set lec.split=sel
if [ "$RC" -ne 0 ] || ! echo "$OUT" | grep -q "case-split sel"; then echo "FAIL: explicit split=sel"; fail=1
else echo "ok: explicit split=sel -> case-split on sel"; fi

# 5) no control operand -> auto finds no split -> graceful fallback, still PROVEN.
run --ref "$WORK/plain.sv" --impl "$WORK/plain.sv"
if [ "$RC" -ne 0 ]; then echo "FAIL: plain rc=$RC (want 0)"; fail=1
elif ! echo "$OUT" | grep -q "PROVEN equivalent"; then echo "FAIL: plain not PROVEN"; fail=1
elif echo "$OUT" | grep -q "case-split"; then echo "FAIL: plain case-split (no selector to split on)"; fail=1
else echo "ok: no-selector -> fallback PROVEN (no case-split)"; fi

if [ $fail -ne 0 ]; then echo "lec_partitions_test: FAILED"; exit 1; fi
echo "lec_partitions_test: PASSED"; exit 0
