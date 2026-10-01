#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
# Guarded mapped comparison: source state, native residual ablation, external LEC.
set -euo pipefail
W="${TEST_TMPDIR:-$(mktemp -d)}"
cat > "$W/pipe.v" <<'VERILOG'
module pipe(input clk,a,b,c,d, output reg q, output z);
wire s = (a & b) ^ (~a & c);
always @(posedge clk) q <= s;
assign z = (a ^ b) ^ (b ^ d);
endmodule
VERILOG
python3 pass/usyn/compare.py "$W/pipe.v" --top pipe \
  --liberty inou/prp/tests/abc/test.lib --workdir "$W/comparison" \
  --lhd lhd/lhd --monitor pass/usyn/measure_synth --residual-ablation \
  --usyn-set logical_inputs=2 --usyn-set cut_inputs=3 --usyn-set clock_phases=1 \
  --usyn-set static_xor=10
python3 - "$W/comparison" <<'PY'
import hashlib
import json
from pathlib import Path
import sys
root = Path(sys.argv[1])
p = json.loads((root / 'comparison.json').read_text())
assert p['schema_version'] == 2 and p['residual_ablation'], p
assert set(p['results']) == {'usyn-selection', 'usyn-residual', 'usyn', 'abc'}, p
assert p['source_sha256'] == hashlib.sha256(Path(p['source']).read_bytes()).hexdigest()
assert p['liberty_sha256'] == hashlib.sha256(Path(p['liberty']).read_bytes()).hexdigest()
settings = []
names = []
for label, row in p['results'].items():
    assert row['lec']['verdict'] == 'proven' and row['lec']['solver'] == 'cvc5', row
    assert row['mapping_measurement']['reason'] == 'completed', row
    assert row['mapping_measurement']['wall_ms'] > 0, row
    measurement = row['mapping_measurement']
    if measurement['sampled_peak_bytes'] is None:
        assert measurement['missing_readings'] > 0, measurement
    else:
        assert measurement['sampled_peak_bytes'] > 0, measurement
    assert Path(row['netlist']).is_file(), row
    timing = row['timing']['designs']
    assert timing and all(d['area'] > 0 and d['opaque_logic_nodes'] == 0 for d in timing), timing
    # Sequential timing completeness is reported, not inferred from area or LEC.
    assert all('constraints_complete' in d and 'timing_cells_complete' in d for d in timing), timing
    if row['mapper'] != 'usyn':
        continue
    native = json.loads(Path(row['usyn_report']).read_text())
    assert native['totals']['eligible_endpoints'] == native['totals']['register_bits'] == 1, native
    assert native['cache']['reused'] == 0, native
    settings.append((native['constraints'], native['endpoint_search']))
    names.append([e['name'] for r in native['regions'] for e in r['endpoints']])
    estimates = row['native']['estimated_cost']
    assert estimates['after']['total'] <= estimates['before']['total'], estimates
    flags = row['usyn_overrides']
    residual = row['native']['residual']
    work = row['native']['logical_work']
    assert work['total'] == sum(v for k, v in work.items() if k != 'total'), work
    assert work['admission'] > 0 and work['selection'] > 0, work
    assert work['pairs'] == row['native']['pair_work'], work
    if flags['residual'] == 'false':
        assert work['residual'] == work['feedback'] == work['cleanup'] == 0, work
        assert estimates['after_pairs'] == estimates['after_residual'] == estimates['after'], estimates
        assert residual['rewrite_wins'] == residual['resub_wins'] == 0, residual
    if flags['feedback'] == 'false':
        assert work['feedback'] == work['cleanup'] == 0, work
        assert residual['feedback_rounds'] == residual['feedback_attempts'] == residual['feedback_wins'] == 0, residual
assert all(s == settings[0] for s in settings), settings
assert all(n == names[0] for n in names) and names[0], names
for command in json.loads((root / 'commands.json').read_text()):
    result = json.loads((root / (command['label'] + '.result.json')).read_text())
    assert not any('satopt' in step for step in result['recipe']), result
print('PASS: shared-source mapped state and residual/feedback comparison')
PY
