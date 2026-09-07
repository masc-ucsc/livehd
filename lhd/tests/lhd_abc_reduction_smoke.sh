#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
set -euo pipefail
export PATH="/opt/homebrew/bin:/usr/local/bin:$PATH"
LHD="${LHD:-lhd/lhd}"
LIB=inou/prp/tests/abc/test.lib
W="${TEST_TMPDIR:-$(mktemp -d)}/reduction"
mkdir -p "$W"
run() { "$LHD" "$@" -q --result-json "$W/result.json"; }
cat > "$W/source.v" <<'SV'
module reductions(input [8:0] a, input signed [3:0] b, output [6:0] y);
  assign y[0] = &a;
  assign y[1] = ~(&a);
  assign y[2] = &b;
  assign y[3] = &(a[3:0]);
  nand gate_nand(y[4], a[0], a[1], a[2], a[3]);
  assign y[5] = &(a[0]);
  assign y[6] = &{a[3:0], b};
endmodule
SV
run pass liberty gensim "$LIB" --emit-dir lg:"$W/models" --emit verilog:"$W/models.v" --workdir "$W/models-work"
for reader in yosys-slang yosys-verilog; do
  run synth "$W/source.v" --reader "$reader" --top reductions --set synth.liberty="$LIB" \
    --set synth.opentimer=false --emit verilog:"$W/$reader.v" --workdir "$W/$reader-synth"
  cat "$W/$reader.v" "$W/models.v" > "$W/impl.v"
  run lec --impl verilog:"$W/impl.v" --ref verilog:"$W/source.v" --top reductions \
    --set formal.solver=lgyosys --workdir "$W/$reader-lec"
  python3 - "$W/result.json" <<'PY'
import json,sys
r=json.load(open(sys.argv[1]))['lec']; assert r['verdict']=='proven',r
PY
done
echo 'PASS: mapped signed/unsigned reductions and NAND primitives preserve finite width'
