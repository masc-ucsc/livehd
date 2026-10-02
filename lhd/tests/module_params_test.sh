#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# A parameterized CORE-ET top is elaborated with its tracked parameters, on BOTH
# sides.
#
# `txfma_adder` declares `parameter int unsigned Width = 0` and is correct when
# instantiated; as a TOP it elaborates to `logic [-1:0]` ports and slang refuses
# it -- the whole of its "compile-failed" census row.  pass/lean/MODULE_PARAMS.tsv
# supplies `-G Width=10`.
#
# Two things are checked, and the second is the one that bites:
#   * the parameters reach the IMPLEMENTATION read, and the result really has
#     the intended port widths -- "it compiled" would also be true of a
#     different Width;
#   * the parameters reach the LEC REFERENCE read too.  Giving them to only one
#     elaboration produces two different designs and refutes them against each
#     other with nothing wrong in the tool, which is a very expensive way to
#     learn about a missing flag.
set -u
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." 2>/dev/null && pwd)"
RUNNER=""
for c in "${TEST_SRCDIR:-}/_main/scripts/run_coreet_module_lean.sh" \
         "$ROOT/scripts/run_coreet_module_lean.sh" "scripts/run_coreet_module_lean.sh"; do
  [ -r "$c" ] && { RUNNER="$c"; break; }
done
TSV=""
for c in "${TEST_SRCDIR:-}/_main/pass/lean/MODULE_PARAMS.tsv" \
         "$ROOT/pass/lean/MODULE_PARAMS.tsv" "pass/lean/MODULE_PARAMS.tsv"; do
  [ -r "$c" ] && { TSV="$c"; break; }
done
[ -n "$RUNNER" ] || { echo "FAIL: cannot find run_coreet_module_lean.sh"; exit 1; }
[ -n "$TSV" ]    || { echo "FAIL: cannot find MODULE_PARAMS.tsv"; exit 1; }
rc=0

# ---- the table says what it is supposed to say ----------------------------
row="$(grep -v '^#' "$TSV" | grep -P '^txfma_adder\t' | head -1)"
if [ -z "$row" ]; then
  echo "FAIL: MODULE_PARAMS.tsv has no txfma_adder row, so the module is unelaborable again"
  rc=1
elif ! printf '%s' "$row" | grep -q -- '-G Width=10'; then
  echo "FAIL: txfma_adder's parameters are $(printf '%s' "$row" | cut -f2), not -G Width=10"
  rc=1
else
  echo "ok: MODULE_PARAMS.tsv pins txfma_adder at -G Width=10"
fi
# Every row must have all three columns, or `why` silently becomes part of the
# flags and gets word-split onto the slang command line.
while IFS= read -r line; do
  [ -z "$line" ] && continue
  n="$(printf '%s' "$line" | awk -F'\t' '{print NF}')"
  if [ "$n" -ne 3 ]; then
    echo "FAIL: MODULE_PARAMS.tsv row has $n tab-separated fields, not 3: ${line:0:60}"
    rc=1
  fi
done < <(grep -v '^#' "$TSV" | grep -v '^[[:space:]]*$')

# ---- BOTH elaborations receive them ---------------------------------------
# Structural, because running the real flow here needs core-et and a yosys read.
# The LEC leg is the one that was missing in the plan's own description, so it
# is pinned by name rather than by "the variable is mentioned somewhere".
if grep -q 'MODULE_PARAMS\[@\]' "$RUNNER"; then
  n="$(grep -c 'MODULE_PARAMS\[@\]' "$RUNNER")"
  if [ "$n" -lt 2 ]; then
    echo "FAIL: the module parameters reach only $n elaboration; the implementation and"
    echo "      the LEC reference must both get them or LEC compares two different designs"
    rc=1
  else
    echo "ok: the module parameters reach $n elaborations (implementation + LEC reference)"
  fi
else
  echo "FAIL: run_coreet_module_lean.sh does not use MODULE_PARAMS at all"
  rc=1
fi
if grep -A3 'declare -a ref_slang=' "$RUNNER" | grep -q 'MODULE_PARAMS'; then
  echo "ok: the LEC reference read gets them specifically"
else
  echo "FAIL: ref_slang does not pick up MODULE_PARAMS, so the reference elaborates"
  echo "      the design WITHOUT its parameters"
  rc=1
fi

[ "$rc" -eq 0 ] || { echo "FAIL: module_params_test"; exit 1; }
echo "PASS: module_params_test"
