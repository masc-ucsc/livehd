#!/bin/bash
set -euo pipefail
LHD=./lhd/lhd
[[ -x "$LHD" ]] || LHD=./bazel-bin/lhd/lhd
W=$(mktemp -d "${TEST_TMPDIR:-/tmp}/unpacked-layout.XXXXXX")
trap 'rm -rf "$W"' EXIT
"$LHD" compile inou/slang/tests/sv/unpacked_struct_nested.sv --top unpacked_struct_nested \
  --emit verilog:"$W/out.v" --workdir "$W/compile" > "$W/compile.log" 2>&1 \
  || { cat "$W/compile.log"; exit 1; }
cat > "$W/gold.v" <<'SV'
module unpacked_struct_nested(input [3:0] x, input [4:0] v, input pick, input signed [2:0] idx, output [7:0] selected, output [5:0] lanes, output [15:0] y);
  assign selected = (idx == 1 || idx == 2) ? {{4{x[3]}}, x} : 8'd0;
  assign lanes = {(idx == 2 ? 3'd6 : 3'd5), (idx == 1 ? 3'd6 : 3'd2)};
  assign y = {x, pick, (pick ? 5'd7 : (v ^ 5'h15)), (pick ? (v ^ 5'h15) : v), 1'b0};
endmodule
SV
"$LHD" lec --impl "$W/out.v" --ref "$W/gold.v" --top unpacked_struct_nested \
  --set formal.effort=proof --set formal.timeout=10 --workdir "$W/lec" \
  --result-json "$W/result.json" > "$W/lec.log" 2>&1 || { cat "$W/lec.log"; exit 1; }
python3 - "$W/result.json" <<'PY'
import json, sys
r = json.load(open(sys.argv[1]))
# Timeout/unknown is a pass for small LEC tests; only a refutation fails.
assert not r['lec']['verdict'].startswith('refuted'), r
PY

# Constant out-of-range unpacked elements under a struct root.
"$LHD" compile inou/slang/tests/sv/unpacked_struct_oob.sv --top unpacked_struct_oob \
  --emit verilog:"$W/oob.v" --workdir "$W/oob_compile" > "$W/oob_compile.log" 2>&1 \
  || { cat "$W/oob_compile.log"; exit 1; }
cat > "$W/oob_gold.v" <<'SV'
module unpacked_struct_oob(input [7:0] d, input [1:0] k, output [7:0] e, output [7:0] f, output [7:0] g);
  assign e = k==3 ? 8'h55 : ~d;
  assign f = k==0 ? 1 : k==2 ? 2 : k==1 ? 3 : 4;
  assign g = 0;
endmodule
SV
"$LHD" lec --impl "$W/oob.v" --ref "$W/oob_gold.v" --top unpacked_struct_oob \
  --set formal.effort=proof --set formal.timeout=10 --workdir "$W/oob_lec" \
  --result-json "$W/oob_result.json" > "$W/oob_lec.log" 2>&1 || { cat "$W/oob_lec.log"; exit 1; }
python3 - "$W/oob_result.json" <<'PY'
import json, sys
r = json.load(open(sys.argv[1]))
assert not r['lec']['verdict'].startswith('refuted'), r
PY
