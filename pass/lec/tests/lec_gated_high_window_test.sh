#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
# A latch's live data qualifier and its clock cell's sampled guard are separate.
set -euo pipefail
LHD=./bazel-bin/lhd/lhd
if [ ! -x "$LHD" ]; then LHD=./lhd/lhd; fi
test -x "$LHD"
LHD=$(realpath "$LHD")
WORK="${TEST_TMPDIR:-$(mktemp -d /tmp/lec_gated_high_window.XXXXXX)}"
mkdir -p "$WORK"
cat >"$WORK/common.v" <<'SV'
module phase_anchor(input clk, d, output reg q);
  always_latch if (clk) q = d;
endmodule
module window_gate(input clk, en, output gclk);
  reg held_en;
  always_latch if (!clk) held_en = en;
  assign gclk = clk & held_en;
endmodule
SV
cat "$WORK/common.v" >"$WORK/reference.v"
cat >>"$WORK/reference.v" <<'SV'
module qualified(input clk, gate_en, write_en, d, output reg q, output anchor_q);
  wire gclk;
  window_gate cg(clk, gate_en, gclk);
  reg held_write_en, value;
  always_latch if (!clk) held_write_en = write_en;
  always_latch if (gclk && held_write_en) value = d;
  always @(negedge clk) q <= value;
  phase_anchor anchor(clk, d, anchor_q);
endmodule
SV
model() {
  local name=$1 clock=$2 enable=$3
  cat "$WORK/common.v" >"$WORK/$name.v"
  cat >>"$WORK/$name.v" <<SV
module qualified(input clk, gate_en, write_en, d, output reg q, output anchor_q);
  wire gclk;
  window_gate cg(clk, gate_en, gclk);
  reg held_write_en, sampled_gate_en;
  always @(posedge clk) begin
    held_write_en <= write_en;
    sampled_gate_en <= gate_en;
  end
  always @(negedge clk) if ($enable) q <= d;
  phase_anchor anchor(clk, d, anchor_q);
endmodule
SV
}
model good clk 'sampled_gate_en && held_write_en'
model live_en clk 'sampled_gate_en && write_en'
model ignore_en clk sampled_gate_en
model ungated clk held_write_en
model live_gate clk 'gate_en && held_write_en'
# Keep the inline form, then add an observed gated flop to force the equivalent
# Clock_cell representation in the second set of fixtures.
python3 - "$WORK" <<'PY_FIXTURE'
from pathlib import Path
import sys
root = Path(sys.argv[1])
for name in ['reference', 'good', 'live_en', 'ignore_en', 'ungated', 'live_gate']:
    path = root / (name + '.v')
    text = path.read_text()
    (root / (name + '_inline.v')).write_text(text)
    text = text.replace('output anchor_q);', 'output anchor_q, output reg gate_tag);')
    text = text.replace('  phase_anchor anchor(clk, d, anchor_q);',
                        '  always @(posedge gclk) gate_tag <= d;\n  phase_anchor anchor(clk, d, anchor_q);')
    path.write_text(text)
PY_FIXTURE

check() {
  local name=$1 expected=$2 reference=${3:-reference} rc=0
  LHD_LEC_PHASE_PLAN=1 "$LHD" lec --impl "verilog:$WORK/$name.v" \
    --ref "verilog:$WORK/$reference.v" --top qualified --workdir "$WORK/work_$name" \
    --set formal.engine=bmc --set formal.effort=proof --set formal.semdiff=off \
    --set formal.bound=3 --set formal.timeout=15 >"$WORK/$name.log" 2>&1 || rc=$?
  if ! python3 - "$WORK/$name.log" "$rc" "$expected" <<'PY'
import json
from pathlib import Path
import sys
path, code, expected = sys.argv[1:]
records = [json.loads(line) for line in Path(path).read_text().splitlines()
           if line.startswith('{"schema_version')]
verdict = next(record['lec']['verdict'] for record in reversed(records) if 'lec' in record)
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
check good proven
check live_en refuted
check ignore_en refuted
check ungated refuted
check live_gate refuted
check good_inline proven reference_inline
check live_en_inline refuted reference_inline
check ignore_en_inline refuted reference_inline
check ungated_inline refuted reference_inline
check live_gate_inline refuted reference_inline
echo 'PASS: high-window qualifiers retain the clock guard in explicit and inline gates; all eight mutants refute'
