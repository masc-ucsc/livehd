#!/usr/bin/env bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
set -euo pipefail
LHD="${LHD:-lhd/lhd}"
work="${TEST_TMPDIR:-/tmp/lhd_sim_reachable_$$}"
mkdir -p "$work"
cat > "$work/design.prp" <<'PRP'
pub comb unused(a:U8) -> (z:U8) { z = a ^ 7 }
pub comb external(a:U8) -> (z:U8) { z = a ^ 11 }
pub mod first(a:U8) -> (z:U8@[0]) { z = a ^ 3 }
pub mod second(a:U8) -> (z:U8@[0]) { z = a ^ 5 }
test first.check {
  mut dut = first
  tick 2 { dut.a = 10; step; assert(dut.z == 9) }
}
test second.check {
  mut dut = second
  tick 2 { dut.a = 10; step; assert(dut.z == 15) }
}
PRP
args=(sim "$work/design.prp" --workdir "$work/SW" --set sim.tune.backend=llvm
      --set sim.tune.profile=off --set sim.jobs=2 --set sim.ninja=false)
"$LHD" "${args[@]}" --setup-only > "$work/setup.log" 2>&1 || { cat "$work/setup.log"; exit 1; }
# If the host build still enumerates all generated modules, these unused
# translation units/objects force a failure. Both live DUTs must still execute.
python3 - "$work/SW/sim" <<'PY'
import json,pathlib,sys
root=pathlib.Path(sys.argv[1])
modules=json.loads((root/'gen_digests.json').read_text())['modules']
unused=[row for name,row in modules.items() if name.endswith('.unused')]
assert len(unused)==1, modules.keys()
files=unused[0]['f']
assert any(f.endswith('.llvm.o') for f in files), files
for name in files:
    if name.endswith('.cpp'): (root/name).write_text('#error unreachable simulator module compiled\n')
    elif name.endswith('.llvm.o'): (root/name).write_bytes(b'not an object')
PY
cat > "$work/SW/sim/support.cpp" <<'CPP'
#include "design.external.hpp"
const void* standalone_support() { return &design_external::__tune_support(); }
CPP
"$LHD" "${args[@]}" --run-only --result-json "$work/result.json" > "$work/run.log" 2>&1 || {
  cat "$work/run.log"
  exit 1
}
python3 - "$work/result.json" <<'PY'
import json,pathlib,sys
# The kernel result includes all test outcomes, independent of the runtime's
# private sidecar name. Check both names as well as the aggregate verdict.
data=json.loads(pathlib.Path(sys.argv[1]).read_text())
assert data['status']=='pass',data
text=json.dumps(data)
assert 'first.check' in text and 'second.check' in text,data
PY
