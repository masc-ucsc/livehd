#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
set -euo pipefail
LHD="${LHD:-lhd/lhd}"
YOSYS="${YOSYS:-inou/yosys/yosys2}"
FIX=inou/slang/tests/interfaces/passive.sv
W=$(mktemp -d "${TEST_TMPDIR:-/tmp}/passive-interfaces.XXXXXX")
trap 'rm -rf "$W"' EXIT
for top in interface_smoke interface_types interface_arrays interface_state interface_shapes interface_state_feedback; do
  "$LHD" compile "$FIX" --top "$top" --emit verilog:"$W/$top.v" --workdir "$W/$top.compile" \
    > "$W/compile.log" 2>&1 || { cat "$W/compile.log"; exit 1; }
  "$YOSYS" -Q -T -p "read_slang --top $top $FIX; proc; flatten; opt; check -assert; write_verilog -noattr $W/$top.gold.v" \
    > "$W/yosys.log" 2>&1 || { cat "$W/yosys.log"; exit 1; }
  "$LHD" lec --impl "$W/$top.v" --ref "$W/$top.gold.v" --top "$top" \
    --set formal.effort=proof --set formal.timeout=10 --workdir "$W/$top.lec" \
    --result-json "$W/result.json" > "$W/lec.log" 2>&1 || { cat "$W/lec.log"; exit 1; }
  python3 - "$W/result.json" <<'PY'
import json, sys
r = json.load(open(sys.argv[1]))
assert r['lec']['verdict'].startswith('proven'), r
PY
  echo "PASS: $top matches Yosys"
done
for case in active:unsupported-interface-member bidir:unsupported-interface-modport undirected:unsupported-interface-modport; do
  top=interface_${case%%:*}
  if "$LHD" compile inou/slang/tests/interfaces/unsupported.sv --top "$top" --workdir "$W/$top" \
      > "$W/reject.log" 2>&1; then
    echo "FAIL: accepted $top"
    exit 1
  fi
  grep -q "${case#*:}" "$W/reject.log" || { cat "$W/reject.log"; exit 1; }
  echo "PASS: rejected $top"
done
if "$LHD" compile "$FIX" --top interface_loop --workdir "$W/loop" > "$W/loop.log" 2>&1; then
  echo "FAIL: accepted a true combinational interface loop"
  exit 1
fi
grep -qi 'combinational.*loop' "$W/loop.log" || { cat "$W/loop.log"; exit 1; }
echo "PASS: rejected true combinational interface loop"
