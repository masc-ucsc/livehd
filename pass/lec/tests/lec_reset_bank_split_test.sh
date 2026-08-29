#!/bin/bash
# A differently named mapped register split must not be mistaken for a
# reset-less register-file bank and held through only one side's reset prologue.

set -u
LHD=./bazel-bin/lhd/lhd
if [ ! -x "$LHD" ]; then
  if [ -x ./lhd/lhd ]; then LHD=./lhd/lhd; else
    echo "FAIL: could not find the lhd binary in $(pwd)"; exit 1; fi
fi
WORK="${TEST_TMPDIR:-/tmp/lec_reset_bank_split}"
mkdir -p "$WORK"

cat > "$WORK/ref.v" <<'EOF'
module split(input clk, input rst, input [3:0] in, output [3:0] out);
  reg [3:0] stage;
  always @(posedge clk) begin
    if (rst) stage <= 4'b0;
    else stage <= in;
  end
  assign out = stage;
endmodule
EOF

cat > "$WORK/impl.v" <<'EOF'
module split(input clk, input rst, input [3:0] in, output [3:0] out);
  reg stages___q_0, stages___q_1, stages___q_2, stages___q_3;
  always @(posedge clk) begin
    if (rst) begin
      stages___q_0 <= 1'b0;
      stages___q_1 <= 1'b0;
      stages___q_2 <= 1'b0;
      stages___q_3 <= 1'b0;
    end else begin
      stages___q_0 <= in[0];
      stages___q_1 <= in[1];
      stages___q_2 <= in[2];
      stages___q_3 <= in[3];
    end
  end
  assign out = {stages___q_3, stages___q_2, stages___q_1, stages___q_0};
endmodule
EOF

OUT=$(
  "$LHD" lec --ref "$WORK/ref.v" --impl "$WORK/impl.v" --top split \
    --set formal.engine=bmc --set formal.bound=2 \
    --set formal.reset_cycles=2 --set formal.lec.hier=false \
    --set formal.timeout=20 --workdir "$WORK/w" 2>&1
)
RC=$?
if [ "$RC" -ne 0 ]; then
  echo "$OUT"
  echo "FAIL: equivalent packed/scalar reset registers refuted (rc=$RC)"
  exit 1
fi
if ! echo "$OUT" | grep -q "PASS(2).*equivalent"; then
  echo "$OUT"
  echo "FAIL: expected a bounded equivalence proof"
  exit 1
fi
echo "lec_reset_bank_split_test: PASSED"
