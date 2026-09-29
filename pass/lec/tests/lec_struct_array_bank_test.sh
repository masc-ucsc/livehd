#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
# Array-of-struct scalar banks bridge to Pyrope memories at the whole top.
set -eu
LHD=lhd/lhd
W="${TEST_TMPDIR:-/tmp/lec_struct_array_bank_$$}"
mkdir -p "$W"
cat > "$W/bank.prp" <<'PRP'
pub mod array_field::[timecheck=false](clock:u1, reset:u1, a:u8, b:u8, sel:u1) -> (y:u8@[0]) {
  reg bank_bits_data:[2]u8:[ordering="old"]
  y = bank_bits_data[sel]
  bank_bits_data[0] = a
  bank_bits_data[1] = b
}
PRP
cat > "$W/bank.v" <<'RTL'
module array_field(input clock, reset, input [7:0] a,b, input sel, output [7:0] y);
reg [7:0] bank_0_bits_data, bank_1_bits_data;
always @(posedge clock) begin bank_0_bits_data<=a; bank_1_bits_data<=b; end
assign y=sel ? bank_1_bits_data : bank_0_bits_data;
endmodule
RTL
"$LHD" lec --impl "$W/bank.prp" --ref "$W/bank.v" --top array_field \
  --set formal.engine=ind --set formal.lec.hier=false --workdir "$W/good" --result-json "$W/good.json"
grep -q '"verdict":"proven".*"bounded":false' "$W/good.json"
# The bridge must prove the next-state relation, not assume it.
sed 's/bank_bits_data\[1\] = b/bank_bits_data[1] = a/' "$W/bank.prp" > "$W/bad.prp"
if "$LHD" lec --impl "$W/bad.prp" --ref "$W/bank.v" --top array_field \
  --set formal.lec.hier=false --workdir "$W/bad" --result-json "$W/bad.json"; then
  echo 'FAIL: incorrect array write passed' >&2
  exit 1
fi
grep -q '"verdict":"refuted"' "$W/bad.json"
echo 'PASS: array-of-struct flop bank and mutation'
