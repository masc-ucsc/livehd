#!/usr/bin/env bash
# Reproducer / debug harness for the cgen_sim false-combinational-loop bug.
#
# A pure-combinational sub-instance whose output feeds back (through PARENT comb
# logic) into one of its own inputs is a FALSE comb loop. cgen_sim treats a Sub
# atomically (one child.cycle(), all-inputs->all-outputs), so the fed-back input
# is read before it can be computed and is silently emitted as
#     create_integer(0) /*UNRESOLVED*/
# producing a WRONG simulation with NO error and NO warning. cgen_verilog handles
# the identical design correctly (declarative always_comb).
#
# This script compiles each fixture with the hierarchy-preserving `slang` reader
# (the real Pyrope/modular flow; the `yosys-verilog` reader would flatten and
# hide the bug) and greps the emitted <top>.cpp's cycle() for either bug
# signature:
#   * create_integer(0) /*UNRESOLVED*/  -- pure-comb Sub fed-back input (base/var_a/var_c)
#   * an UNKNOWN-BITS literal  from_pyrope("0ub...?...")  -- a loop_break Sub whose
#     output cprop could not resolve in the false loop (var_b). Slop sim has no
#     unknowns normally, so such a literal in cycle() is itself the bug.
#
# Usage:  inou/cgen/tests/comb_loop/repro.sh
# Run from anywhere; resolves the repo root and the lhd binary itself.
set -u

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../../../.." && pwd)"
LHD="$ROOT/bazel-bin/lhd/lhd"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/cgen_comb_loop.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT

if [[ ! -x "$LHD" ]]; then
  echo "lhd not built: $LHD  (run: bazel build -c dbg //lhd:lhd)"; exit 2
fi

# fixture | top module | golden (correct) note
FIXTURES=(
  "base_falseloop.v|top|a=10,b=20,d=5,topx=3 -> x_out=30 y_out=38 (bug: y_out=5)"
  "var_a_crosscoupled.v|top|a1=10,b1=20,d1=5,a2=2,b2=4,d2=7 -> o1=11 o2=37 (bug: 5,7)"
  "var_b_stateful_passthru.v|top|a=10,b=20,d=3 -> x=30, q=34 after 1 clk (bug: q=1)"
  "var_c_genuine_loop.v|top|GENUINE loop (no fixpoint) -> must stay an ERROR"
)

fail=0
for entry in "${FIXTURES[@]}"; do
  IFS='|' read -r f top note <<<"$entry"
  out="$WORK/${f%.v}"
  "$LHD" compile "$HERE/$f" --reader slang --top "$top" \
        --emit-dir "sim:$out/" --workdir "$out.wd" >"$out.log" 2>&1
  rc=$?
  cpp="$out/$top.cpp"
  echo "=== $f  ($note)"
  if grep -qE 'UNRESOLVED|from_pyrope\("0u?b[^"]*\?' "$cpp" 2>/dev/null; then
    echo "  BUG REPRODUCED: $top.cpp's cycle() carries a bogus fed-back value"
    grep -nE 'UNRESOLVED|from_pyrope\("0u?b[^"]*\?' "$cpp" | sed 's/^/    /'
    fail=1
  elif [[ $rc -ne 0 ]]; then
    echo "  compile errored (rc=$rc) -- after the fix, genuine loops SHOULD error here"
  else
    echo "  clean: no UNRESOLVED marker (this fixture is FIXED)"
  fi
  echo
done

if [[ $fail -ne 0 ]]; then
  echo "RESULT: bug present (at least one fixture emitted a bogus fed-back value)."
  echo "        See inou/cgen/tests/comb_loop/README.md and the staged fix plan."
else
  echo "RESULT: no bogus fed-back values -- the false-loop bug appears fixed."
fi
exit 0
