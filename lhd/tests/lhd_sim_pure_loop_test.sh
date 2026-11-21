#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
set -eu
LHD="${LHD:-lhd/lhd}"
W="${TEST_TMPDIR:-/tmp/lhd_pure_loop_$$}"
mkdir -p "$W"
mode="${1:-fast}"
extra=()
fixture=lhd/tests/lhd_sim_pure_loop.prp
case "$mode" in
  fast) ;;
  observed) extra=(--set sim.vcd=true) ;;
  embedded) fixture=lhd/tests/lhd_sim_embedded_loop.prp ;;
  nested) fixture=lhd/tests/lhd_sim_nested_loop.prp ;;
  *) echo "unknown pure-loop mode: $mode" >&2; exit 1 ;;
esac
"$LHD" sim "$fixture" --workdir "$W/$mode" --set compile.unroll=false \
  ${extra[@]+"${extra[@]}"} --result-json "$W/$mode.json" --diag-fmt pretty
python3 - "$W" "$mode" <<'PY'
import json, pathlib, sys
w = pathlib.Path(sys.argv[1])
mode = sys.argv[2]
doc = json.loads((w / f'{mode}.json').read_text())
assert doc['status'] == 'pass', doc
assert all(t['status'] == 'pass' for t in doc['tests']) and doc['tests'], doc
# no generated C++: the loops run as LLVM kernels
assert not list((w / mode / 'sim').glob('*.cpp')) and not list((w / mode / 'sim').glob('*.hpp'))
if mode == 'observed':
    assert list((w / mode).glob('*.vcd')), 'the replayed waveform is missing'
PY
echo "PASS ($mode): rolled loops, multihot OR, nested bit writes, signed arithmetic, early exit"
