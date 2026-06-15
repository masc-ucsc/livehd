#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# Reset-phase separation (lec.phase) for the BMC engine. Two sequential pairs
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
       --set lec.engine=bmc --set lec.bound=8 --set "lec.phase=$4" \
       --workdir "$WORK/q_${3}_$4_$$_$RANDOM" 2>&1 \
    | grep -o "PROVEN equivalent\|REFUTED (not equivalent)\|UNKNOWN" | head -1
}

expect() {  # $1=label $2=got $3=want
  if [ "$2" != "$3" ]; then echo "FAIL: $1 -> got '$2', want '$3'"; fail=1
  else echo "ok: $1 -> $2"; fi
}

# pair A: equivalent only under reset
expect "A reset" "$(verdict a2_lg a1_lg cnt reset)" "PROVEN equivalent"
expect "A run"   "$(verdict a2_lg a1_lg cnt run)"   "REFUTED (not equivalent)"

# pair B: equivalent only while running
expect "B reset" "$(verdict b2_lg b1_lg dut reset)" "REFUTED (not equivalent)"
expect "B run"   "$(verdict b2_lg b1_lg dut run)"   "PROVEN equivalent"

if [ $fail -ne 0 ]; then echo "lec_phase_test: FAILED"; exit 1; fi
echo "lec_phase_test: PASSED"
exit 0
