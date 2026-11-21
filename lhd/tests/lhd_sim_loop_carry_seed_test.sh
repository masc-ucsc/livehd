#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
# Differential: the LLVM backend's embedded loops keep a register-fed carry
# seed (the carry-in sink's other driver is the loop's own carry-out); slop is
# the reference.
set -euo pipefail
LHD="${LHD:-lhd/lhd}"
W="${TEST_TMPDIR:-/tmp/lhd_loop_carry_seed_$$}"
mkdir -p "$W"
F=lhd/tests/lhd_sim_loop_carry_seed.prp
# The independent oracle is the same design with its loops unrolled: the rolled kernels must carry the same seeds.
for mode in rolled unrolled; do
  u=false
  [ "$mode" = unrolled ] && u=true
  "$LHD" sim "$F" --workdir "$W/$mode" --set compile.unroll=$u --diag-fmt pretty > "$W/$mode.log" 2>&1 &
  eval "pid_${mode}=$!"
done
for mode in rolled unrolled; do
  eval "wait \"\${pid_${mode}}\"" || { cat "$W/$mode.log" >&2; echo "FAIL: $mode simulation failed" >&2; exit 1; }
  grep -a ':puts:TRACE ' "$W/$mode.log" | sed 's/.*:puts://' > "$W/$mode.trace"
done
lines=$(wc -l < "$W/unrolled.trace")
[ "$lines" -eq 198 ] || { echo "FAIL: expected 198 trace lines, got $lines" >&2; exit 1; }
awk '$4 != 0' "$W/unrolled.trace" | grep -q . || { echo "FAIL: vacuous stimulus" >&2; exit 1; }
if ! diff -q "$W/unrolled.trace" "$W/rolled.trace" > /dev/null; then
  diff "$W/unrolled.trace" "$W/rolled.trace" | head -10 >&2
  echo "FAIL: rolled and unrolled traces differ ($(diff "$W/unrolled.trace" "$W/rolled.trace" | grep -c '^<') lines)" >&2
  exit 1
fi
echo "PASS: loop carry seeds agree when rolled and unrolled ($lines cycles)"
