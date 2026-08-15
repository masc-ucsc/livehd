#!/bin/bash
set -euo pipefail

# Bazel's local-test PATH omits Homebrew on macOS even for no-sandbox tests.
export PATH="/opt/homebrew/bin:${PATH}"

LHD=./bazel-bin/lhd/lhd
if [[ ! -x "$LHD" ]]; then
  LHD=./lhd/lhd
fi

SRC=${1:?missing Pyrope source}
WD="${TEST_TMPDIR:-tmp_prp_array_oob_runtime}/array_oob_runtime"
mkdir -p "$WD/v" "$WD/w"

"$LHD" compile "$SRC" \
  --emit-dir verilog:"$WD/v/" --workdir "$WD/w" -q >/dev/null

VFILES=("$WD"/v/*.v)
VFILE=${VFILES[0]}
grep -q 'array index out of range' "$VFILE" || {
  echo "missing runtime bounds diagnostic in generated Verilog"
  exit 1
}

cat >"$WD/tb.sv" <<'EOF'
module tb;
  logic [2:0] idx;
  logic [7:0] o;
  array_oob_runtime dut(.idx(idx), .o(o));
  initial begin
    idx = 3'd0;
    #1;
    idx = 3'd4;
    #1;
    $finish;
  end
endmodule
EOF

iverilog -g2012 -s tb -o "$WD/sim" "$VFILE" "$WD/tb.sv"
vvp "$WD/sim" >"$WD/runtime.log" 2>&1 || true
grep -q 'array index out of range' "$WD/runtime.log" || {
  echo "out-of-range access did not diagnose at runtime"
  cat "$WD/runtime.log"
  exit 1
}
