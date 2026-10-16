#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# A reference memory written through a reset-less address register: the first
# write lands at an UNKNOWN address, so BMC's reference knowledge state must
# cloud every entry. That state used to put an all-unknown constant array next
# to the all-known one, and cvc5 aborted the run with "Array theory solver does
# not yet support write-chains connecting two different constant arrays"
# (exit 1). Equivalent designs must now prove and a wrong write must refute.
set -euo pipefail
LHD=./bazel-bin/lhd/lhd
if [ ! -x "$LHD" ]; then LHD=./lhd/lhd; fi
WORK="${TEST_TMPDIR:-$(mktemp -d)}"
cat > "$WORK/ref.v" <<'V'
module dut(input clk, input we, input [1:0] wa_in, input [7:0] d, input [1:0] ra, output [7:0] q);
  reg [1:0] wa;  // no reset: unknown at power-on
  reg [7:0] mem [0:3];
  always @(posedge clk) begin
    wa <= wa_in;
    if (we) mem[wa] <= d;
  end
  assign q = mem[ra];
endmodule
V
sed 's/\bwa\b/wa_q/g; s/\bmem\b/store/g; s/wa_q_in/wa_in/g' "$WORK/ref.v" > "$WORK/good.v"
sed 's/store\[wa_q\] <= d;/store[wa_q] <= d ^ 8'"'"'h01;/' "$WORK/good.v" > "$WORK/bad.v"
grep -q "d ^ 8'h01" "$WORK/bad.v"
check() {
  local tag=$1 impl=$2 expected=$3 code=0
  shift 3
  "$LHD" lec --set formal.effort=proof --ref "verilog:$WORK/ref.v" --impl "verilog:$WORK/$impl.v" --top dut \
    --set formal.timeout=20 --set formal.simfail=false "$@" --workdir "$WORK/$tag" \
    --result-json "$WORK/$tag.json" > "$WORK/$tag.log" 2>&1 || code=$?
  if grep -q "write-chains" "$WORK/$tag.log"; then
    echo "FAIL: $tag: cvc5 array constant-chain crash"; exit 1
  fi
  case "$expected" in proven-*) test "$code" = 0 ;; refuted-cex) test "$code" = 10 ;; *) false ;; esac || {
    echo "FAIL: $tag: exit $code"; tail -5 "$WORK/$tag.log"; exit 1; }
  python3 - "$WORK/$tag.json" "$expected" <<'PYRESULT'
import json, sys
result = json.load(open(sys.argv[1]))
assert result["lec"]["verdict"] == sys.argv[2], result["lec"]
PYRESULT
  echo "ok: $tag -> $expected"
}
check bmc_good good proven-bounded --set formal.engine=bmc --set formal.bound=4 --set formal.lec.hier=false
check bmc_bad bad refuted-cex --set formal.engine=bmc --set formal.bound=4 --set formal.lec.hier=false
check auto_good good proven-full
check auto_bad bad refuted-cex
echo "lec_mem_unknown_addr_smoke: PASSED"
