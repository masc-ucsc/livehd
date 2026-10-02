#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
# BMC must respect initialized memory refining unspecified reference startup,
# while still refuting an incorrect write after that startup becomes defined.
set -euo pipefail
W="${TEST_TMPDIR:-/tmp/lgcheck_memory_poweron_$$}"
mkdir -p "$W"
CHECK="$PWD/inou/yosys/lgcheck"
YOSYS="$PWD/inou/yosys/yosys2"
cat > "$W/gold.v" <<'RTL'
module mem(input clk, we, addr, input [7:0] d, output [7:0] q);
  reg [7:0] data[0:1];
  always @(posedge clk) if (we) data[addr] <= d;
  assign q = data[addr];
endmodule
RTL
sed '/always/i\  initial begin data[0] = 1; data[1] = 2; end' "$W/gold.v" > "$W/gate.v"
sed "s/<= d;/<= d ^ 8'd1;/" "$W/gate.v" > "$W/broken.v"
for variant in gate broken; do
  mkdir -p "$W/$variant"
  rc=0
  (cd "$W/$variant" && LGCHECK_BMC_ONLY=1 LGCHECK_EQUIV_TIMEOUT=3 LGCHECK_BMC_STEPS=4 \
    "$CHECK" --yosys "$YOSYS" --gold_reader slang --gate_reader slang \
    --reference "$W/gold.v" --implementation "$W/$variant.v" --top mem > check.log 2>&1) || rc=$?
  expected=2
  [ "$variant" != broken ] || expected=1
  if [ "$rc" != "$expected" ]; then
    cat "$W/$variant/check.log" >&2
    echo "FAIL: $variant returned $rc; expected $expected" >&2
    exit 1
  fi
done
echo 'PASS: unspecified memory startup is refined; incorrect subsequent writes refute'
