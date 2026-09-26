#!/usr/bin/env bash
set -euo pipefail
emitter="$PWD/$1"
lhd="$PWD/$2"
reference="$PWD/$3"
native_helper="$PWD/$4"
cd "$TEST_TMPDIR"
"$emitter" --gtest_filter=CgenVerilog.VariadicEqualityIncludesEveryOperand
"$lhd" lec --impl verilog:eq_variadic/eq_variadic.v --ref "verilog:$reference" \
  --top eq_variadic --workdir work --set pass.satopt=false \
  --set formal.timeout=5 --set formal.simfail=false --result-json result.json
python3 - <<'PY'
import json
from pathlib import Path
result = json.loads(Path('result.json').read_text())
assert result['lec']['verdict'] == 'proven', result
assert not result['lec']['bounded'], result
PY
python3 - "$lhd" "$native_helper" <<'PY'
import importlib.util
from pathlib import Path
import sys
spec = importlib.util.spec_from_file_location('native_sim', sys.argv[2])
native = importlib.util.module_from_spec(spec)
spec.loader.exec_module(native)
vectors = []
cases = [(-2, 0, -2), (-1, 3, -1), (0, 0, 1), (1, 1, 1),
         (0, 0, 0), (-1, 0, 0), (1, 0, 0), (-1, 0, 1),
         (1, 3, -4), (1, 0, 3), (0, 1, 0), (1, 1, 2),
         (0, 1 << 80, 0), (-1, (1 << 128) - 1, -1),
         (1, (1 << 80) + 1, 1), (-2, (1 << 128) - 2, -2)]
for a, b, c in cases:
    outputs = {f'eq{i}': int(a == b == c) for i in range(6)}
    outputs.update(eq6=int(a < b and c < b), eq7=int(a > b and c > b), eq8=int(a == b))
    vectors.append(dict(inputs=dict(a=a, b=b, c=c), outputs=outputs))
native.simulate(sys.argv[1], 'lg:' + str(Path('lgdb_eq_variadic').resolve()),
                'eq_variadic', vectors, Path('native').resolve())
PY
cp result.json "$TEST_UNDECLARED_OUTPUTS_DIR/lec.json"
cp native/native-result.json "$TEST_UNDECLARED_OUTPUTS_DIR/native.json"
cp eq_variadic/eq_variadic.v "$TEST_UNDECLARED_OUTPUTS_DIR/emitted.v"
