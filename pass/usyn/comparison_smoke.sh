#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
set -euo pipefail
W="${TEST_TMPDIR:-$(mktemp -d)}"
cat >"$W/shared.v" <<'EOF'
module shared(input a,b,c,d, output s,y,z);
assign s = a & b;
assign y = s & c;
assign z = s & d;
endmodule
EOF
python3 pass/usyn/compare.py "$W/shared.v" --top shared \
  --liberty inou/prp/tests/abc/timing.lib --workdir "$W/comparison" \
  --lhd lhd/lhd --monitor pass/usyn/measure_synth --delay 120 \
  --usyn-set support=2 --usyn-set literals=8 --usyn-set series=2
# The ordinary fused command differs only in its mapper and output directory.
for mapper in usyn abc; do
  pass/usyn/measure_synth --archive "$W/fused-$mapper-measurement" --seconds 30 --memory-mb 4096 -- \
    lhd/lhd synth "$W/shared.v" --top shared --set "synth.mapper=$mapper" \
    --set synth.liberty=inou/prp/tests/abc/timing.lib --set abc.delay=120 \
    --workdir "$W/fused-$mapper" --emit "verilog:$W/fused-$mapper.v" \
    --result-json "$W/fused-$mapper.json" -q || { cat "$W/fused-$mapper-measurement/command.log" "$W/fused-$mapper.json"; exit 1; }
done
pass/usyn/measure_synth --archive "$W/override-measurement" --seconds 30 --memory-mb 4096 -- \
  lhd/lhd synth "$W/shared.v" --top shared --set synth.mapper=usyn \
  --set synth.liberty=inou/prp/tests/abc/timing.lib --set abc.delay=120 --set pass.usyn.delay=160 \
  --workdir "$W/override" --result-json "$W/override.json" -q \
  || { cat "$W/override-measurement/command.log" "$W/override.json"; exit 1; }
python3 - "$W" <<'PY'
import json
from pathlib import Path
import sys
work = Path(sys.argv[1])
root = work / 'comparison'
report = json.loads((root / 'comparison.json').read_text())
for mapper in ('usyn', 'abc'):
    result = report['results'][mapper]
    assert result['lec']['verdict'] == 'proven', result
    assert result['lec']['solver'] == 'cvc5', result
    timing = result['timing']['designs']
    assert len(timing) == 1 and timing[0]['area'] > 0, timing
    assert timing[0]['opaque_logic_nodes'] == 0, timing
    assert timing[0]['native_state_nodes'] == 0, timing
    assert timing[0]['timing_cells_complete'], timing
    assert timing[0]['constraints_complete'], timing
    assert result['mapping_measurement']['reason'] == 'completed', result
    assert Path(result['netlist']).is_file(), result
    fused = work / f'fused-{mapper}/synth'
    assert (fused / 'net/library.txt').is_file(), fused
    assert (fused / 'timing.json').is_file(), fused
    assert (work / f'fused-{mapper}.v').is_file(), fused
    assert (fused / 'qor.json.usyn.json').exists() == (mapper == 'usyn'), fused
    qor = json.loads((fused / 'qor.json').read_text())
    assert qor['regions'] and all(row['budget'] == 120 for row in qor['regions']), qor
usyn = json.loads((root / 'usyn/qor.json.usyn.json').read_text())
# Equivalence is the separate `lhd lec` step above; synthesis itself proves nothing.
rows = usyn['regions_searched']
assert rows and all(r['status'] == 'abc_tmap' for r in rows), rows
assert usyn['totals']['abc_tmap'] == len(rows) and usyn['totals']['domino'] > 0, usyn['totals']
assert usyn['recipe']['support'] == 2 and all(r['domino_in3'] == 0 for r in rows), rows
override = json.loads((work / 'override/synth/qor.json').read_text())
assert override['regions'] and all(row['budget'] == 160 for row in override['regions']), override
print('PASS: standalone comparison and fused synth.mapper switch')
PY
