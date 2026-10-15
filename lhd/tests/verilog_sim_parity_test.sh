#!/usr/bin/env bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# Verilog designs simulated by `lhd sim` (slop and llvm) against expectations
# taken from Icarus Verilog / Verilator: each lhd/tests/verilog_sim/<name>.v is
# compiled to an lg: library and driven by <name>_tb.prp, whose asserts hold the
# reference values. Cases come from the random Verilog sim fuzzer.
#
# Usage: verilog_sim_parity_test.sh [<case> [<backend>]]
# No argument runs every case on both backends (a manual sweep). Each bazel
# target (verilog_sim_parity_<case>_<backend>, grouped by the
# verilog_sim_parity_test suite) runs ONE case on ONE backend: every `lhd sim`
# pays a full host C++ build, so the whole sweep cannot fit the per-test budget.
set -euo pipefail

LHD="${LHD:-lhd/lhd}"
[ -x "$LHD" ] || LHD="./bazel-bin/lhd/lhd"
only_case="${1:-}"
backends="${2:-slop llvm}"
W=$(mktemp -d "${TEST_TMPDIR:-/tmp}/vsimpar.XXXXXX")
trap 'rm -rf "$W"' EXIT
fail=0
ran=0
for v in lhd/tests/verilog_sim/*.v; do
  name=$(basename "$v" .v)
  [ -z "$only_case" ] || [ "$name" = "$only_case" ] || continue
  ran=$((ran + 1))
  tb="lhd/tests/verilog_sim/${name}_tb.prp"
  "$LHD" compile "$v" --top "$name" --emit-dir "lg:$W/$name/lg/" --workdir "$W/$name/c" >"$W/$name.c.log" 2>&1 ||
    { echo "FAIL: $name: lhd compile"; tail -3 "$W/$name.c.log"; fail=1; continue; }
  for be in $backends; do
    if "$LHD" sim "lg:$W/$name/lg/" "$tb" --set sim.tune.backend=$be --workdir "$W/$name/$be" >"$W/$name.$be.log" 2>&1; then
      echo "$name ($be) - success"
    else
      echo "FAIL: $name ($be)"; { grep -E "assert|FAIL|rror" "$W/$name.$be.log" || tail -5 "$W/$name.$be.log"; } | head -5; fail=1
    fi
  done
done
[ "$ran" -gt 0 ] || { echo "FAIL: no verilog_sim case named '$only_case'"; exit 1; }
exit $fail
