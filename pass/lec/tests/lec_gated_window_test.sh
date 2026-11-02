#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
# A latch's live data qualifier and its clock cell's sampled guard are separate.
set -euo pipefail
LHD=./bazel-bin/lhd/lhd
if [ ! -x "$LHD" ]; then LHD=./lhd/lhd; fi
test -x "$LHD"
LHD=$(realpath "$LHD")
WORK="${TEST_TMPDIR:-$(mktemp -d /tmp/lec_gated_window.XXXXXX)}"
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
  reg held_write_en, value, high_value;
  always_latch if (gclk) held_write_en = write_en;
  always_latch if (!gclk && held_write_en) value = d;
  // Minion's qualified low latch feeds a high-window latch on the same
  // gated clock. A same-edge flop reader remains a deliberate refusal.
  always_latch if (gclk) high_value = value;
  always @(negedge clk) q <= high_value;
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
  reg held_write_en, sampled_value;
  always @(negedge gclk) held_write_en <= write_en;
  always @(posedge $clock) if ($enable) sampled_value <= d;
  always @(negedge clk) q <= sampled_value;
  phase_anchor anchor(clk, d, anchor_q);
endmodule
SV
}
model good gclk held_write_en
model live_en gclk write_en
model ignore_en gclk "1'b1"
model ungated clk held_write_en
check() {
  local name=$1 expected=$2 rc=0
  LHD_LEC_PHASE_PLAN=1 "$LHD" lec --impl "verilog:$WORK/$name.v" \
    --ref "verilog:$WORK/reference.v" --top qualified --workdir "$WORK/work_$name" \
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
echo 'PASS: separate live data qualifiers and sampled clock guards prove; all three mutants refute'
