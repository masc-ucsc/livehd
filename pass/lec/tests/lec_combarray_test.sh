#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# A constant-indexed COMBINATIONAL plain-vector array written with chunked
# bit-slice element writes inside a generate block (the dcache data/metadata
# idiom). The native reader flattens such an array to one packed bus so the
# bit-slice writes compose via set_mask; a memory-element chunked store instead
# mis-keyed the din temp (upass bundle abort) and mis-composed the chunks.
# Native-emit must LEC-match yosys-slang; a corrupted slice must REFUTE.

set -u

LHD=./bazel-bin/lhd/lhd
if [ ! -x "$LHD" ]; then
  if [ -x ./lhd/lhd ]; then LHD=./lhd/lhd; else
    echo "FAIL: could not find the lhd binary in $(pwd)"; exit 1; fi
fi

WORK="${TEST_TMPDIR:-/tmp/leccombarr}"
mkdir -p "$WORK"
fail=0

cat > "$WORK/good.sv" <<'EOF'
module ca #(parameter int N = 2)
  ( input  logic [63:0] din  [N]
  , output logic [63:0] dout [N]
  );
  logic [63:0] mem [N];
  for (genvar b = 0; b < N; b++) begin : gen_b
    always_comb begin
      mem[b] = '0;
      mem[b][31:0]    = din[b][31:0];
      mem[b][63:32]   = din[b][63:32];
      dout[b] = mem[b];
    end
  end
endmodule
EOF
# Corrupted: the high chunk takes the wrong source bits.
cat > "$WORK/bad.sv" <<'EOF'
module ca #(parameter int N = 2)
  ( input  logic [63:0] din  [N]
  , output logic [63:0] dout [N]
  );
  logic [63:0] mem [N];
  for (genvar b = 0; b < N; b++) begin : gen_b
    always_comb begin
      mem[b] = '0;
      mem[b][31:0]    = din[b][31:0];
      mem[b][63:32]   = din[b][31:0];   // bug: should be [63:32]
      dout[b] = mem[b];
    end
  end
endmodule
EOF

compile() {  # $1=src $2=dir $3=reader
  rm -rf "$WORK/$2"; mkdir -p "$WORK/$2"
  $LHD compile "$WORK/$1" --reader "$3" --top ca --emit-dir "verilog:$WORK/$2" --workdir "$WORK/w_$2" \
       -- --allow-use-before-declare >/dev/null 2>&1 || { echo "FAIL: compile $1 ($3)"; exit 1; }
  cat "$WORK/$2"/*.v > "$WORK/$2_all.v"
}
compile good.sv g_native slang
compile good.sv g_ys     yosys-slang
compile bad.sv  b_native slang

check() {
  $LHD check --impl "verilog:$WORK/$1_all.v" --ref "verilog:$WORK/$2_all.v" --impl-top ca --ref-top ca \
       --workdir "$WORK/c_${1}_${2}_$$" 2>&1 | grep -o '"status":"[a-z]*"' | head -1
}
expect() { if [ "$2" != "$3" ]; then echo "FAIL: $1 -> got '$2', want '$3'"; fail=1; else echo "ok: $1 -> $2"; fi; }

expect "cross-reader comb array" "$(check g_native g_ys)" '"status":"pass"'
expect "corrupted chunk"         "$(check b_native g_native)" '"status":"fail"'

if [ $fail -ne 0 ]; then echo "lec_combarray_test: FAILED"; exit 1; fi
echo "lec_combarray_test: PASSED"
exit 0
