#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
# A preview latch remains transparent while its ICG is off. Its ungated
# falling-edge reader must sample the second half-period's input in that case.
set -euo pipefail

LHD=./bazel-bin/lhd/lhd
if [ ! -x "$LHD" ]; then LHD=./lhd/lhd; fi
test -x "$LHD"
LHD=$(realpath "$LHD")
WORK="${TEST_TMPDIR:-$(mktemp -d /tmp/lec_gated_preview.XXXXXX)}"
mkdir -p "$WORK"

cat >"$WORK/common.v" <<'SV'
module phase_anchor(input clk, d, output reg q);
  always_latch if (clk) q = d;
endmodule
module preview_gate(input clk, en, output gclk);
  reg held_en;
  always_latch if (!clk) held_en = en;
  assign gclk = clk & held_en;
endmodule
SV
cat "$WORK/common.v" >"$WORK/reference.v"
cat >>"$WORK/reference.v" <<'SV'
module preview(input clk, en, d, output reg q, output anchor_q);
  wire gclk;
  preview_gate cg(clk, en, gclk);
  reg value;
  always_latch if (!gclk) value = d;
  always @(negedge clk) q <= value;
  phase_anchor anchor(clk, d, anchor_q);
endmodule
SV
sed 's/always @(negedge clk)/always @(negedge gclk)/' "$WORK/reference.v" >"$WORK/reference_gated.v"

model() {
  local name=$1 expression=$2
  cat "$WORK/common.v" >"$WORK/$name.v"
  cat >>"$WORK/$name.v" <<SV
module preview(input clk, en, d, output reg q, output anchor_q);
  reg sampled_data;
  reg sampled_en;
  always @(posedge clk) begin
    sampled_data <= d;
    sampled_en <= en;
  end
  always @(negedge clk) q <= $expression;
  phase_anchor anchor(clk, d, anchor_q);
endmodule
SV
}
model good 'sampled_en ? sampled_data : d'
model gated 'sampled_en ? sampled_data : q'
model hold 'sampled_data'
model live 'd'
model live_enable 'en ? sampled_data : d'

check() {
  local name=$1 reference=$2 expected=$3 rc=0
  LHD_LEC_PHASE_PLAN=1 "$LHD" lec --impl "verilog:$WORK/$name.v" \
    --ref "verilog:$WORK/$reference.v" --top preview --workdir "$WORK/work_$name" \
    --set formal.engine=bmc --set formal.effort=proof --set formal.semdiff=off \
    --set formal.bound=3 --set formal.timeout=15 >"$WORK/$name.log" 2>&1 || rc=$?
  if ! python3 - "$WORK/$name.log" "$rc" "$expected" <<'PY'
import json
from pathlib import Path
import sys

path, code, expected = sys.argv[1:]
text = Path(path).read_text()
records = [json.loads(line) for line in text.splitlines() if line.startswith('{"schema_version"')]
assert records, 'No structured LEC verdict'
verdict = records[-1].get('lec', {}).get('verdict', '')
if expected == 'proven':
    assert int(code) == 0 and verdict.startswith('proven'), (code, verdict)
else:
    assert int(code) == 10 and verdict == 'refuted-cex', (code, verdict)
PY
  then
    cat "$WORK/$name.log" >&2
    echo "FAIL: $name did not produce $expected" >&2
    exit 1
  fi
}

# A high-window producer settles at Close_high. Sampling preview D in the
# same batch would see its old Q; forwarding at Fall must see its new Q.
sed -e 's/reg value;/reg value; reg produced; always_latch if (clk) produced = d;/' \
    -e 's/value = d;/value = produced;/' "$WORK/reference.v" >"$WORK/reference_high.v"
# The second substitution also touches the inserted producer: keep its D live.
sed -i 's/if (clk) produced = produced;/if (clk) produced = d;/' "$WORK/reference_high.v"
sed -e 's/reg sampled_data;/reg sampled_data; reg produced; always @(negedge clk) produced <= d;/' \
    -e 's/sampled_data <= d;/sampled_data <= produced;/' "$WORK/good.v" >"$WORK/good_high.v"

check good reference proven
grep -q 'gated-off fall forwards=1' "$WORK/good.log"
check good_high reference_high proven
check gated reference_gated proven
check hold reference refuted
check live reference refuted
check live_enable reference refuted
echo 'PASS: gated-off transparency and sampled guard timing prove; hold/live/live-enable mutants refute'
