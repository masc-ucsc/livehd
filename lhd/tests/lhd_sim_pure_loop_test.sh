#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
set -eu
LHD="${LHD:-lhd/lhd}"
W="${TEST_TMPDIR:-/tmp/lhd_pure_loop_$$}"
mkdir -p "$W"
mode="${1:-fast}"
extra=()
fixture=lhd/tests/lhd_sim_pure_loop.prp
# `fast` and `legacy` assert the reference C++ (Slop) pure evaluator, so they
# pin that backend; `llvm`, `embedded` and `nested` cover the native loop.
case "$mode" in
  fast) extra=(--set sim.tune.backend=slop) ;;
  observed) extra=(--set sim.vcd=true) ;;
  legacy) extra=(--set sim.tune.backend=slop --set sim.slop_u=false) ;;
  llvm) extra=(--set sim.tune.backend=llvm) ;;
  embedded) extra=(--set sim.tune.backend=llvm); fixture=lhd/tests/lhd_sim_embedded_loop.prp ;;
  nested) extra=(--set sim.tune.backend=llvm); fixture=lhd/tests/lhd_sim_nested_loop.prp ;;
  *) echo "unknown pure-loop mode: $mode" >&2; exit 1 ;;
esac
"$LHD" sim "$fixture" --workdir "$W/$mode" --set compile.unroll=false \
  --set sim.tune.profile=off ${extra[@]+"${extra[@]}"} --result-json "$W/$mode.json" --diag-fmt pretty
python3 - "$W" "$mode" <<'PY'
import json, pathlib, sys
w = pathlib.Path(sys.argv[1])
mode = sys.argv[2]
doc = json.loads((w / f'{mode}.json').read_text())
assert doc['status'] == 'pass', doc
headers = '\n'.join(p.read_text() for p in (w / mode / 'sim').glob('*.hpp'))
if mode == 'observed':
    assert 'static Out __pure_eval(' not in headers
elif mode == 'llvm':
    # The wrapper can take a calling color's operands directly (a loop whose
    # color is not an LLVM kernel), instead of binding each into its inputs.
    assert 'const Out& __native_direct(' in headers
elif mode == 'embedded':
    # The loop lives inside the caller's LLVM kernel: nothing calls it.
    bodies = '\n'.join(p.read_text() for p in (w / mode / 'sim').glob('*.cpp'))
    for call in ('__compact_advance();  // compact-loop control', '__native_direct(', '::__native_loop('):
        assert call not in bodies.replace('const Out& __native_direct(', ''), call
    assert list((w / mode / 'sim').glob('*sel.color-kernel-*.llvm.o')), 'caller color is not an LLVM kernel'
elif mode == 'nested':
    # Two innermost levels are embedded; the outermost level keeps its one call.
    bodies = '\n'.join(p.read_text() for p in (w / mode / 'sim').glob('*.cpp'))
    assert bodies.count('__compact_advance();  // compact-loop control') == 1, bodies.count('__compact_advance();  // compact-loop control')
else:
    assert 'static Out __pure_eval(' in headers
    assert 'LHD_SIM_PRESERVE_LOOP for (std::size_t ordinal' in headers
    assert '__li3' not in headers
PY
echo "PASS ($mode): rolled loops, multihot OR, nested bit writes, signed arithmetic, early exit"
