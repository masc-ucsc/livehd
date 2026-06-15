#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# M4 memory cut (SMT array theory). A register file read through BOTH front-ends
# must PROVE equivalent (the `ind` single-step miter collapses corresponding
# memories to one shared array symbol), and a register file with a corrupted
# write address must REFUTE. Exercises select/store + the wensize normalization
# that bridges the native (word-enable) and yosys-slang (per-bit-enable) readers.

set -u

LHD=./bazel-bin/lhd/lhd
if [ ! -x "$LHD" ]; then
  if [ -x ./lhd/lhd ]; then LHD=./lhd/lhd; else
    echo "FAIL: could not find the lhd binary in $(pwd)"; exit 1; fi
fi

WORK="${TEST_TMPDIR:-/tmp/lecmem}"
mkdir -p "$WORK"
fail=0

cat > "$WORK/good.sv" <<'EOF'
module rf(input clk, input we, input [2:0] waddr, input [7:0] wdata, input [2:0] raddr, output [7:0] rdata);
  logic [7:0] mem [8];
  always_ff @(posedge clk) if (we) mem[waddr] <= wdata;
  assign rdata = mem[raddr];
endmodule
EOF
# Corrupted: writes to the wrong address.
cat > "$WORK/bad.sv" <<'EOF'
module rf(input clk, input we, input [2:0] waddr, input [7:0] wdata, input [2:0] raddr, output [7:0] rdata);
  logic [7:0] mem [8];
  always_ff @(posedge clk) if (we) mem[waddr ^ 3'd1] <= wdata;
  assign rdata = mem[raddr];
endmodule
EOF

compile() {  # $1=src $2=lgdir $3=reader
  $LHD compile "$WORK/$1" --reader "$3" --top rf --emit-dir "lg:$WORK/$2" --workdir "$WORK/w_$2" \
       -- --allow-use-before-declare >/dev/null 2>&1 || { echo "FAIL: compile $1 ($3)"; exit 1; }
}
# native + yosys-slang read of the golden RF, and native read of the broken RF.
compile good.sv g_native slang
compile good.sv g_ys     yosys-slang
compile bad.sv  b_native slang

verdict() {  # $1=impl $2=ref -> PROVEN | REFUTED | UNKNOWN
  $LHD lec --impl "lg:$WORK/$1" --ref "lg:$WORK/$2" --top rf --set lec.engine=ind \
       --workdir "$WORK/q_${1}_${2}_$$" 2>&1 \
    | grep -o "PROVEN equivalent\|REFUTED (not equivalent)\|UNKNOWN" | head -1
}

expect() { if [ "$2" != "$3" ]; then echo "FAIL: $1 -> got '$2', want '$3'"; fail=1; else echo "ok: $1 -> $2"; fi; }

# Cross-front-end: native RF == yosys-slang RF (the collapse + wensize bridge).
expect "cross-reader RF" "$(verdict g_native g_ys)" "PROVEN equivalent"
# Corrupted write address must be caught.
expect "corrupted RF"    "$(verdict b_native g_native)" "REFUTED (not equivalent)"

if [ $fail -ne 0 ]; then echo "lec_mem_test: FAILED"; exit 1; fi
echo "lec_mem_test: PASSED"
exit 0
