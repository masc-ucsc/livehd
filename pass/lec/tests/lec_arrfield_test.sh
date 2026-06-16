#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# Nested combinational struct-array write `arr[idx].field = v` (lhs-nesting). The
# native reader FLATTENS a combinational packed-struct array to one packed bus,
# so sequential field writes compose via set_mask (a memory-element RMW would
# clobber siblings). Native-emit must LEC-match the yosys-slang flattening, and a
# corrupted field write must REFUTE.

set -u

LHD=./bazel-bin/lhd/lhd
if [ ! -x "$LHD" ]; then
  if [ -x ./lhd/lhd ]; then LHD=./lhd/lhd; else
    echo "FAIL: could not find the lhd binary in $(pwd)"; exit 1; fi
fi

WORK="${TEST_TMPDIR:-/tmp/lecarrfield}"
mkdir -p "$WORK"
fail=0

cat > "$WORK/good.sv" <<'EOF'
module m(input [1:0] sel, input [7:0] d, output [15:0] o);
  typedef struct packed { logic [3:0] a; logic [3:0] b; } pair_t;
  pair_t arr [2];
  always_comb begin
    arr[0] = '0;
    arr[1] = '0;
    arr[0].a = d[3:0];
    arr[1].b = d[7:4];
    if (sel[0]) arr[0].b = 4'hf;
  end
  assign o = {arr[1], arr[0]};
endmodule
EOF
# Corrupted: writes the wrong field (a instead of b).
cat > "$WORK/bad.sv" <<'EOF'
module m(input [1:0] sel, input [7:0] d, output [15:0] o);
  typedef struct packed { logic [3:0] a; logic [3:0] b; } pair_t;
  pair_t arr [2];
  always_comb begin
    arr[0] = '0;
    arr[1] = '0;
    arr[0].a = d[3:0];
    arr[1].a = d[7:4];   // bug: should be .b
    if (sel[0]) arr[0].b = 4'hf;
  end
  assign o = {arr[1], arr[0]};
endmodule
EOF

compile() {  # $1=src $2=dir $3=reader
  rm -rf "$WORK/$2"; mkdir -p "$WORK/$2"
  $LHD compile "$WORK/$1" --reader "$3" --top m --emit-dir "verilog:$WORK/$2" --workdir "$WORK/w_$2" \
       -- --allow-use-before-declare >/dev/null 2>&1 || { echo "FAIL: compile $1 ($3)"; exit 1; }
  cat "$WORK/$2"/*.v > "$WORK/$2_all.v"
}
compile good.sv g_native slang
compile good.sv g_ys     yosys-slang
compile bad.sv  b_native slang

check() {  # $1 $2 (impl ref dirs) -> pass|fail
  $LHD lec --set lec.solver=lgyosys --impl "verilog:$WORK/$1_all.v" --ref "verilog:$WORK/$2_all.v" --impl-top m --ref-top m \
       --workdir "$WORK/c_${1}_${2}_$$" 2>&1 | grep -o '"status":"[a-z]*"' | head -1
}
expect() { if [ "$2" != "$3" ]; then echo "FAIL: $1 -> got '$2', want '$3'"; fail=1; else echo "ok: $1 -> $2"; fi; }

# native flattening LEC-matches yosys-slang's flattening.
expect "cross-reader struct-array" "$(check g_native g_ys)" '"status":"pass"'
# corrupted field write must be caught.
expect "corrupted field"           "$(check b_native g_native)" '"status":"fail"'

if [ $fail -ne 0 ]; then echo "lec_arrfield_test: FAILED"; exit 1; fi
echo "lec_arrfield_test: PASSED"
exit 0
