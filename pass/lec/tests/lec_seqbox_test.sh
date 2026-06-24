#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# Contract for the SEQUENTIAL black-box (stateful collapse). A combinational box
# models a leaf output as a pure function of inputs and forces it CONSTANT across
# BMC cycles, so it false-proves a timing difference through a stateful leaf. The
# state-aware box models outputs = UF(inputs, state) and next = UF2(inputs, state)
# with one shared state cut (matched-reset shared init, threaded by the unroll),
# so the collapsed leaf's output varies per cycle and a real timing difference is
# REFUTED — while a genuine self-equivalence still PROVES.

set -u
LHD=./bazel-bin/lhd/lhd
if [ ! -x "$LHD" ]; then
  if [ -x ./lhd/lhd ]; then LHD=./lhd/lhd; else
    echo "FAIL: could not find the lhd binary in $(pwd)"; exit 1; fi
fi
WORK="${TEST_TMPDIR:-/tmp/lecseqbox}"; mkdir -p "$WORK"; fail=0

# a STATEFUL leaf: a one-cycle register.
cat > "$WORK/sleaf.v" <<'EOF'
module sleaf(input clk, input [7:0] d, output reg [7:0] q);
  always @(posedge clk) q <= d;
endmodule
EOF
# impl: o = sleaf(x).q   (x delayed by 1)
cat > "$WORK/t_d1.v" <<'EOF'
module top(input clk, input [7:0] x, output [7:0] o);
  sleaf u(.clk(clk), .d(x), .q(o));
endmodule
EOF
# ref:  o = extra-flop(sleaf(x).q)  (x delayed by 2) -- NOT equivalent to impl
cat > "$WORK/t_d2.v" <<'EOF'
module top(input clk, input [7:0] x, output [7:0] o);
  wire [7:0] m; sleaf u(.clk(clk), .d(x), .q(m));
  reg [7:0] r; always @(posedge clk) r <= m; assign o = r;
endmodule
EOF
# a STALL feedback: the register output q feeds back to its own input d (d = en ?
# x : q). Collapsing sleaf must NOT deadlock — a Mealy box (q=UF(d,..)) would close
# a false combinational cycle (q -> d -> q); a Moore box (q=UF(state)) + emitting
# the output before the inputs breaks it.
cat > "$WORK/t_stall.v" <<'EOF'
module top(input clk, input en, input [7:0] x, output [7:0] o);
  wire [7:0] q; sleaf u(.clk(clk), .d(en ? x : q), .q(q)); assign o = q;
endmodule
EOF
C() { "$LHD" compile "$@" >/dev/null 2>&1; }
C "$WORK/t_d1.v" "$WORK/sleaf.v" --top top --emit-dir "lg:$WORK/td1" --workdir "$WORK/c1"
C "$WORK/t_d2.v" "$WORK/sleaf.v" --top top --emit-dir "lg:$WORK/td2" --workdir "$WORK/c2"
C "$WORK/t_stall.v" "$WORK/sleaf.v" --top top --emit-dir "lg:$WORK/tstall" --workdir "$WORK/c3"

run() {  # $1=label ; $2..=lhd lec args ; sets RC/OUT
  OUT=$("$LHD" lec "${@:2}" --top top --set lec.hierarchical=false --workdir "$WORK/w_$1" 2>&1); RC=$?
}

# 1) collapse the STATEFUL leaf, self-equivalent -> PROVEN (state-aware box sound)
run self --impl "lg:$WORK/td1" --ref "lg:$WORK/td1" --collapse sleaf --set lec.engine=bmc --set lec.bound=8
if [ "$RC" -ne 0 ] || ! echo "$OUT" | grep -q "PROVEN equivalent"; then
  echo "FAIL: stateful self-collapse not PROVEN (rc=$RC): $OUT"; fail=1
else echo "ok: stateful self-collapse -> PROVEN"; fi

# 2) collapse the leaf, delay-1 vs delay-2 -> REFUTED under BMC. The state-aware
#    box threads the leaf state so its output VARIES per cycle; a combinational
#    box would force it constant and FALSE-PROVE this.
run diff --impl "lg:$WORK/td1" --ref "lg:$WORK/td2" --collapse sleaf --set lec.engine=bmc --set lec.bound=8
if [ "$RC" -eq 0 ]; then echo "FAIL: stateful collapse FALSE-PROVED a timing diff (rc=0)"; fail=1
elif ! echo "$OUT" | grep -q "REFUTED"; then echo "FAIL: stateful collapse timing-diff: not REFUTED: $OUT"; fail=1
else echo "ok: stateful collapse refutes a timing diff (state threaded per cycle)"; fi

# 3) collapse a register whose output feeds back to its own input (stall). The box
#    must not deadlock the encoder on a false combinational cycle -> self PROVEN.
run stall --impl "lg:$WORK/tstall" --ref "lg:$WORK/tstall" --collapse sleaf --set lec.engine=ind
if [ "$RC" -ne 0 ] || ! echo "$OUT" | grep -q "PROVEN equivalent"; then
  echo "FAIL: stall-feedback stateful collapse not PROVEN (false cycle?) rc=$RC: $OUT"; fail=1
else echo "ok: stall-feedback stateful collapse -> PROVEN (no false combinational cycle)"; fi

if [ $fail -ne 0 ]; then echo "lec_seqbox_test: FAILED"; exit 1; fi
echo "lec_seqbox_test: PASSED"; exit 0
