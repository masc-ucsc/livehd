#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
set -euo pipefail
LHD=lhd/lhd
W="${TEST_TMPDIR:?}"
LIB=inou/prp/tests/abc/test.lib
python3 - "$W/parallel.prp" <<'PY'
import sys
ports = ', '.join(f'y{i}:u32@[0]' for i in range(8))
with open(sys.argv[1], 'w') as f:
    f.write(f'mod parallel(a:u32, b:u32) -> ({ports}) {{\n')
    for i in range(8):
        f.write(f'  {{::[color={i+1}]\n    y{i} = (a + {2*i+1}) * (b ^ {2*i+3})\n  }}\n')
    f.write('}\n')
PY
run_synth() {
  "$LHD" synth "$W/parallel.prp" --top parallel --workdir "$W/$1" \
    --set synth.liberty="$LIB" --set synth.opentimer=false --set synth.threads="$2" \
    --set abc.ware=false --set abc.boundary=false --emit verilog:"$W/$1.v" --result-json "$W/$1-result.json" -q
}
run_synth serial 1
run_synth parallel 2
python3 - "$W/serial/synth/qor.json" "$W/parallel/synth/qor.json" <<'PY'
import json, os, sys
s, p = [json.load(open(path)) for path in sys.argv[1:]]
print('parallel scheduling:', p['parallel'])
assert s['parallel']['limit'] == 1, s['parallel']
assert p['parallel']['requested'] == 2, p['parallel']
assert 1 <= p['parallel']['limit'] <= 2, p['parallel']
assert p['parallel']['peak_workers'] <= 2, p['parallel']
assert len(p['regions']) >= 8, len(p['regions'])
assert [r['module'] for r in s['regions']] == [r['module'] for r in p['regions']]
if (os.cpu_count() or 1) >= 2 and p['parallel']['memory_limit_bytes'] >= 2**31:
    # The scheduler admits the second lane before the first thread can even
    # start, so two concurrent WORKERS is deterministic. Two concurrent ABC
    # phases is not: these colors are tiny, and a worker only leaves the shared
    # graph lock for the duration of its own (sub-millisecond) ABC flow.
    assert p['parallel']['peak_workers'] == 2, p['parallel']
    assert p['parallel']['peak_abc'] >= 1, p['parallel']
PY
# Warm reuse must preserve the netlist and avoid creating any ABC session.
cp "$W/parallel.v" "$W/cold.v"
run_synth parallel 2
cmp "$W/cold.v" "$W/parallel.v"
python3 - "$W/parallel/synth/qor.json" <<'PY'
import json, sys
q = json.load(open(sys.argv[1]))
assert q['incremental']['misses'] == 0, q['incremental']
assert q['incremental']['abc_started'] == 0, q['incremental']
PY
# Automatic CPU selection is accepted and does not change cache identity.
run_synth parallel 0
python3 - "$W/parallel/synth/qor.json" <<'CHECK_AUTO'
import json, os, sys
q = json.load(open(sys.argv[1]))
assert q['parallel']['requested'] == 0, q['parallel']
assert q['parallel']['limit'] == (os.cpu_count() or 1), q['parallel']
assert q['incremental']['misses'] == 0, q['incremental']
assert q['incremental']['abc_started'] == 0, q['incremental']
CHECK_AUTO
cmp "$W/cold.v" "$W/parallel.v"

# Check the serial and parallel cell netlists with the same Liberty models.
"$LHD" pass liberty gensim "$LIB" --emit-dir lg:"$W/model-lg" --workdir "$W/models-work" --result-json "$W/models-result.json" -q
"$LHD" compile lg:"$W/model-lg" --emit-dir verilog:"$W/models" --workdir "$W/models-compile" --result-json "$W/models-compile-result.json" -q
cat "$W/models/"*.v >> "$W/serial.v"
cat "$W/models/"*.v >> "$W/parallel.v"
"$LHD" lec --impl verilog:"$W/parallel.v" --ref verilog:"$W/serial.v" --top parallel.parallel \
  --set formal.solver=lgyosys --workdir "$W/lec" --result-json "$W/lec.json" -q
python3 - "$W/lec.json" <<'PY'
import json, sys
q = json.load(open(sys.argv[1]))
assert q['status'] == 'pass', q
PY
# A worker error must join the other workers before destroying region storage.
if "$LHD" synth "$W/parallel.prp" --top parallel --set synth.liberty="$LIB" \
    --set synth.opentimer=false --set synth.threads=2 --set abc.ware=false \
    --set abc.flow=invalid_parallel_test_command --workdir "$W/worker-error" \
    --result-json "$W/worker-error.json" -q; then
  echo 'invalid ABC command unexpectedly succeeded' >&2
  exit 1
fi
python3 - "$W/worker-error.json" <<'CHECK_ERROR'
import json, sys
q = json.load(open(sys.argv[1]))
assert 0 < q['exit_code'] < 128, q
assert q['diagnostics_count']['errors'] > 0, q
CHECK_ERROR

for bad in -1 1.5 wrong 4294967296; do
  if "$LHD" synth "$W/parallel.prp" --set synth.threads="$bad" --set synth.liberty="$LIB" \
      --workdir "$W/bad" --result-json "$W/bad.json" -q; then
    echo "invalid thread count accepted: $bad" >&2
    exit 1
  fi
  grep -q 'non-negative integer' "$W/bad.json"
done
"$LHD" list options | grep -q 'synth.threads'
echo 'PASS: parallel synthesis, worker limits, serial equivalence and cache reuse'
