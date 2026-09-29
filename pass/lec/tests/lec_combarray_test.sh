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
  $LHD compile "$WORK/$1" --reader "$3" --top ca --emit-dir "verilog:$WORK/$2" --emit-dir "lg:$WORK/${2}_lg" --workdir "$WORK/w_$2" \
       -- --allow-use-before-declare >/dev/null 2>&1 || { echo "FAIL: compile $1 ($3)"; exit 1; }
  cat "$WORK/$2"/*.v > "$WORK/$2_all.v"
}
compile good.sv g_native slang
compile good.sv g_ys     yosys-slang
compile bad.sv  b_native slang

check() {
  $LHD lec --set formal.lec.hier=false --impl "verilog:$WORK/$1_all.v" --ref "verilog:$WORK/$2_all.v" --impl-top ca --ref-top ca \
       --workdir "$WORK/c_${1}_${2}_$$" 2>&1 | grep -o '"status":"[a-z]*"' | head -1
}
expect() { if [ "$2" != "$3" ]; then echo "FAIL: $1 -> got '$2', want '$3'"; fail=1; else echo "ok: $1 -> $2"; fi; }

expect "cross-reader comb array" "$(check g_native g_ys)" '"status":"pass"'
expect "corrupted chunk"         "$(check b_native g_native)" '"status":"fail"'

# A non-power-of-two ROM has undefined out-of-range reads. Those bits may
# mask the reference comparison, but valid addresses must still catch a bug.
cat > "$WORK/rom_ref.sv" <<'EOF'
module rom_diff(input logic [2:0] idx, output logic [7:0] val);
  logic [7:0] a [0:2];
  logic [7:0] b [2:0];
  initial begin
    a[0]=8'haa; a[1]=8'hbb; a[2]=8'hcc;
    b[0]=8'haa; b[1]=8'hbb; b[2]=8'hcc;
  end
  assign val = a[idx] - b[idx];
endmodule
EOF
for value in 0 1; do
  cat > "$WORK/rom_impl_$value.sv" <<EOF
module rom_diff(input logic [2:0] idx, output logic [7:0] val);
  assign val = 8'd$value;
endmodule
EOF
  $LHD lec --impl "verilog:$WORK/rom_impl_$value.sv" --ref "verilog:$WORK/rom_ref.sv" \
    --top rom_diff --workdir "$WORK/rom_check_$value" > "$WORK/rom_check_$value.log" 2>&1
  rc=$?
  if [ "$value" = 0 ]; then want_rc=0; want_verdict=proven; else want_rc=10; want_verdict=refuted; fi
  if [ "$rc" -ne "$want_rc" ] || ! grep -q "\"verdict\":\"$want_verdict\"" "$WORK/rom_check_$value.log"; then
    echo "FAIL: ROM value=$value expected $want_verdict (rc=$want_rc), got rc=$rc"
    cat "$WORK/rom_check_$value.log"
    fail=1
  fi
done

if [ $fail -ne 0 ]; then echo "lec_combarray_test: FAILED"; exit 1; fi
echo "lec_combarray_test: PASSED"
exit 0
