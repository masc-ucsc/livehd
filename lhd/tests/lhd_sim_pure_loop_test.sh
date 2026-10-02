#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
set -eu
LHD="${LHD:-lhd/lhd}"
W="${TEST_TMPDIR:-/tmp/lhd_pure_loop_$$}"
mkdir -p "$W"
"$LHD" sim lhd/tests/lhd_sim_pure_loop.prp --workdir "$W/fast" --set compile.unroll=false --result-json "$W/fast.json" --diag-fmt pretty
"$LHD" sim lhd/tests/lhd_sim_pure_loop.prp --workdir "$W/observed" --set compile.unroll=false --set sim.vcd=true --result-json "$W/observed.json" --diag-fmt pretty
"$LHD" sim lhd/tests/lhd_sim_pure_loop.prp --workdir "$W/legacy" --set compile.unroll=false --set sim.slop_u=false --result-json "$W/legacy.json" --diag-fmt pretty
python3 - "$W" <<'PY'
import json, pathlib, sys
w = pathlib.Path(sys.argv[1])
for mode in ('fast', 'observed', 'legacy'):
    doc = json.loads((w / f'{mode}.json').read_text())
    assert doc['status'] == 'pass', doc
headers = '\n'.join(p.read_text() for p in (w / 'fast/sim').glob('*.hpp'))
assert 'static Out __pure_eval(' in headers
assert 'LHD_SIM_PRESERVE_LOOP for (std::size_t ordinal' in headers
assert '__li3' not in headers
observed = '\n'.join(p.read_text() for p in (w / 'observed/sim').glob('*.hpp'))
assert 'static Out __pure_eval(' not in observed
PY
echo 'PASS: rolled pure loops, multihot OR, nested bit writes, signed arithmetic, early exit, VCD'
