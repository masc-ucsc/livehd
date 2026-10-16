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
for backend in llvm slop; do
  "$LHD" sim "$F" --workdir "$W/$backend" --set "sim.tune.backend=$backend" --set sim.tune.profile=off \
    --diag-fmt pretty > "$W/$backend.log" 2>&1 &
  eval "pid_${backend}=$!"
done
for backend in llvm slop; do
  eval "wait \"\${pid_${backend}}\"" || { cat "$W/$backend.log" >&2; echo "FAIL: $backend simulation failed" >&2; exit 1; }
  grep -a ':puts:TRACE ' "$W/$backend.log" | sed 's/.*:puts://' > "$W/$backend.trace"
done
lines=$(wc -l < "$W/slop.trace")
[ "$lines" -eq 198 ] || { echo "FAIL: expected 198 trace lines, got $lines" >&2; exit 1; }
# The seed is observable: the table holds a nonzero value in quiet cycles.
awk '$4 != 0' "$W/slop.trace" | grep -q . || { echo "FAIL: vacuous stimulus" >&2; exit 1; }
if ! diff -q "$W/slop.trace" "$W/llvm.trace" > /dev/null; then
  diff "$W/slop.trace" "$W/llvm.trace" | head -10 >&2
  echo "FAIL: llvm and slop traces differ ($(diff "$W/slop.trace" "$W/llvm.trace" | grep -c '^<') lines)" >&2
  exit 1
fi
echo "PASS: loop carry seeds agree on llvm and slop ($lines cycles)"
