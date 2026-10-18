#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# A run-time $info/$warning/$error/$fatal is not hardware: the reader ignores it
# with one `severity-task-ignored` warning per call instead of refusing the design.
set -eu
LHD="${LHD:-lhd/lhd}"
FIX="${FIXTURES:-inou/slang/tests/sv}"
W="$(mktemp -d)"
trap 'rm -rf "$W"' EXIT
"$LHD" compile "$FIX/severity_tasks_ignored.sv" --reader slang --top severity_tasks_ignored \
  --emit-dir verilog:"$W/v/" --workdir "$W/w" > "$W/log" 2>&1 || { cat "$W/log"; echo "FAIL: compile aborted on a severity task"; exit 1; }
n=$(grep -c '"code":"severity-task-ignored"' "$W/log" || true)
[ "$n" -eq 4 ] || { cat "$W/log"; echo "FAIL: expected 4 severity-task-ignored warnings, got $n"; exit 1; }
for t in warning error info fatal; do
  grep -q "run-time severity task '\\\$$t'" "$W/log" || { echo "FAIL: no warning names \$$t"; exit 1; }
done
echo 'PASS: run-time severity tasks are ignored with a warning each'
