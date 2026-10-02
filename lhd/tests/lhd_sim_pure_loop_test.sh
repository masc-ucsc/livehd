#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
set -eu
LHD="${LHD:-lhd/lhd}"
W="${TEST_TMPDIR:-/tmp/lhd_pure_loop_$$}"
mkdir -p "$W"
mode="${1:-fast}"
extra=()
case "$mode" in
  fast) ;;
  observed) extra=(--set sim.vcd=true) ;;
  legacy) extra=(--set sim.slop_u=false) ;;
  *) echo "unknown pure-loop mode: $mode" >&2; exit 1 ;;
esac
"$LHD" sim lhd/tests/lhd_sim_pure_loop.prp --workdir "$W/$mode" --set compile.unroll=false \
  --set sim.tune.profile=off "${extra[@]}" --result-json "$W/$mode.json" --diag-fmt pretty
python3 - "$W" "$mode" <<'PY'
import json, pathlib, sys
w = pathlib.Path(sys.argv[1])
mode = sys.argv[2]
doc = json.loads((w / f'{mode}.json').read_text())
assert doc['status'] == 'pass', doc
headers = '\n'.join(p.read_text() for p in (w / mode / 'sim').glob('*.hpp'))
if mode == 'observed':
    assert 'static Out __pure_eval(' not in headers
else:
    assert 'static Out __pure_eval(' in headers
    assert 'LHD_SIM_PRESERVE_LOOP for (std::size_t ordinal' in headers
    assert '__li3' not in headers
PY
echo "PASS ($mode): rolled loops, multihot OR, nested bit writes, signed arithmetic, early exit"
