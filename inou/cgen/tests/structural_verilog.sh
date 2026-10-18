#!/usr/bin/env bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
set -euo pipefail
emitter="$PWD/$1"
lhd="$PWD/$2"
cd "$TEST_TMPDIR"
"$emitter" --gtest_filter=CgenVerilog.StructuralWiringUsesPlainVerilogAndExplicitWidths
cat > reference.v <<'V'
module structural_wiring(input signed [2:0] s, input [2:0] u,
 output [7:0] sx, ux, slice, joined, negative);
 assign sx = {{5{s[2]}},s};
 assign ux = {5'b0,u};
 assign slice = {8{s[2]}};
 assign joined = {s[2],s,1'b0,u};
 assign negative = 8'hfa;
endmodule
V
"$lhd" lec --impl verilog:structural_wiring/structural_wiring.v --ref verilog:reference.v \
 --top structural_wiring --workdir work --set formal.effort=proof --set formal.timeout=5 \
 --set formal.simfail=false --set pass.satopt=false \
 --result-json result.json -q
python3 - <<'PY'
import json
from pathlib import Path
r=json.loads(Path('result.json').read_text())
assert r['status']=='pass' and r.get('exit_code',0)==0 and r['lec']['verdict']!='refuted',r
PY
cp result.json "$TEST_UNDECLARED_OUTPUTS_DIR/lec.json"
cp structural_wiring/structural_wiring.v "$TEST_UNDECLARED_OUTPUTS_DIR/emitted.v"
