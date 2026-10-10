#!/usr/bin/env bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# Verilog designs simulated by `lhd sim` (slop and llvm) against expectations
# taken from Icarus Verilog / Verilator: each lhd/tests/verilog_sim/<name>.v is
# compiled to an lg: library and driven by <name>_tb.prp, whose asserts hold the
# reference values. Cases come from the random Verilog sim fuzzer.
set -euo pipefail

LHD="${LHD:-lhd/lhd}"
[ -x "$LHD" ] || LHD="./bazel-bin/lhd/lhd"
W=$(mktemp -d "${TEST_TMPDIR:-/tmp}/vsimpar.XXXXXX")
trap 'rm -rf "$W"' EXIT
fail=0
for v in lhd/tests/verilog_sim/*.v; do
  name=$(basename "$v" .v)
  tb="lhd/tests/verilog_sim/${name}_tb.prp"
  "$LHD" compile "$v" --top "$name" --emit-dir "lg:$W/$name/lg/" --workdir "$W/$name/c" >"$W/$name.c.log" 2>&1 ||
    { echo "FAIL: $name: lhd compile"; tail -3 "$W/$name.c.log"; fail=1; continue; }
  for be in slop llvm; do
    if "$LHD" sim "lg:$W/$name/lg/" "$tb" --set sim.tune.backend=$be --workdir "$W/$name/$be" >"$W/$name.$be.log" 2>&1; then
      echo "$name ($be) - success"
    else
      echo "FAIL: $name ($be)"; { grep -E "assert|FAIL|rror" "$W/$name.$be.log" || tail -5 "$W/$name.$be.log"; } | head -5; fail=1
    fi
  done
done
exit $fail
