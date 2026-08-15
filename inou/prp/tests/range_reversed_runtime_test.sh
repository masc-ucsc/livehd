#!/bin/bash
set -euo pipefail

# Bazel's local-test PATH omits Homebrew on macOS even for no-sandbox tests.
export PATH="/opt/homebrew/bin:${PATH}"

LHD=./bazel-bin/lhd/lhd
if [[ ! -x "$LHD" ]]; then
  LHD=./lhd/lhd
fi

SRC=${1:?missing Pyrope source}
WD="${TEST_TMPDIR:-tmp_prp_range_reversed_runtime}/range_reversed_runtime"
mkdir -p "$WD/v" "$WD/w"

"$LHD" compile "$SRC" \
  --emit-dir verilog:"$WD/v/" --workdir "$WD/w" -q >/dev/null

VFILES=("$WD"/v/*.v)
VFILE=${VFILES[0]}
grep -q 'descending bit-range select' "$VFILE" || {
  echo "missing runtime descending-range diagnostic in generated Verilog"
  exit 1
}

cat >"$WD/tb.sv" <<'EOF'
module tb;
  logic [3:0] i, j;
  logic [31:0] v, b, o;
  range_reversed_runtime dut(.i(i), .j(j), .v(v), .b(b), .o(o));
  initial begin
    b = 32'hdead_beef;
    v = 32'hffff_ffff;

    // In range: bits [11:4] of the destination take `v`, the rest hold.
    i = 4'd12; j = 4'd4;
    #1;
    if (o !== 32'hdead_bfff) begin
      $display("FAIL in-range: o=%h expected dead_bfff", o);
      $fatal(1);
    end

    // Reversed with a NEGATIVE hi (i == 0 makes `i-1` == -1): the range selects
    // no bits, so the destination must come back untouched.
    i = 4'd0; j = 4'd3;
    #1;
    if (o !== 32'hdead_beef) begin
      $display("FAIL reversed/negative hi: o=%h expected dead_beef (untouched)", o);
      $fatal(1);
    end

    // Reversed with both endpoints non-negative: same obligation.
    i = 4'd3; j = 4'd9;
    #1;
    if (o !== 32'hdead_beef) begin
      $display("FAIL reversed: o=%h expected dead_beef (untouched)", o);
      $fatal(1);
    end
    $display("DATAPATH_OK");
    $finish;
  end
endmodule
EOF

iverilog -g2012 -s tb -o "$WD/sim" "$VFILE" "$WD/tb.sv"
vvp "$WD/sim" >"$WD/runtime.log" 2>&1 || true

grep -q 'DATAPATH_OK' "$WD/runtime.log" || {
  echo "reversed range did not leave the destination untouched"
  cat "$WD/runtime.log"
  exit 1
}
grep -q 'descending bit-range select' "$WD/runtime.log" || {
  echo "reversed range did not diagnose at runtime"
  cat "$WD/runtime.log"
  exit 1
}

# Tripwire on the SPELLING, checked after the behavioral obligations above so a
# real regression reports as a wrong value rather than as a failed grep. `hi` is
# `i - 1`, so it is a SIGNED net: the guard has to test the sign of the WIDTH
# (`width < 0`, both operands signed) and not compare the endpoints (`hi < lo`).
# Verilog makes a relational UNSIGNED as soon as one operand is unsigned, so the
# endpoint spelling read a negative `hi` as a huge value and never fired.
grep -qE "< *\(?'s" "$VFILE" || {
  echo "guard is not a signed comparison -- a negative hi would read as huge"
  grep -n '<' "$VFILE"
  exit 1
}
