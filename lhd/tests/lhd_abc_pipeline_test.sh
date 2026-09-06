#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
# A pipeline Flop's pipe_min must survive technology mapping, including QN cells.
set -euo pipefail
LHD=lhd/lhd
W="${TEST_TMPDIR:-/tmp/lhd_abc_pipeline_$$}"
mkdir -p "$W"
cat > "$W/pipeline.prp" <<'PRP'
pipe mul(a:u4, b:u4) -> (c:u8) { c = a * b }
pipe add(a:u8, b:u8) -> (c:u9) { c = a + b }
pub mod pipeline(in1:u4, in2:u4) -> (out:u9@[4]) {
  stage[3] tmp = mul(a=in1, b=in2)
  stage[3] in1_d = in1
  stage[1] out@[4] = add(a=tmp@[3], b=in1_d@[3])
}
PRP
run() { "$LHD" "$@" -q; }
run compile "$W/pipeline.prp" --emit-dir lg:"$W/ref" --emit verilog:"$W/ref.v" --workdir "$W/compile"
for kind in test test_qn; do
  LIB="inou/prp/tests/abc/$kind.lib"
  D="$W/$kind"
  mkdir -p "$D"
  run synth lg:"$W/ref" --top pipeline --set synth.liberty="$LIB" --set synth.opentimer=false \
    --emit-dir lg:"$D/net" --emit verilog:"$D/net.v" --workdir "$D/synth" --result-json "$D/synth.json"
  python3 - "$D/synth.json" <<'PYCOUNT'
import json, sys
abc = json.load(open(sys.argv[1]))["qor"]["abc"]
assert sum(abc["dff"]["cells"].values()) == 45, abc["dff"]
PYCOUNT
  run pass liberty gensim "$LIB" --emit-dir lg:"$D/models" --emit verilog:"$D/models.v" --workdir "$D/gensim"
  cat "$D/net.v" "$D/models.v" > "$D/impl.v"
  run lec --impl lg:"$D/net" --ref lg:"$W/ref" --lib lg:"$D/models" --top pipeline \
    --set formal.timeout=60 --workdir "$D/native" --result-json "$D/native.json"
  run lec --impl verilog:"$D/impl.v" --ref verilog:"$W/ref.v" --top pipeline \
    --set formal.solver=lgyosys --workdir "$D/yosys" --result-json "$D/yosys.json"
done
