#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# lhd_full_case (inou/yosys/yosys_full_case.cpp): a `case` whose labels list every value of the selector is
# marked full_case before proc_mux, so the "hold the previous value" start of a combinational block that
# read_slang --no-proc leaves is not wired into the dead unmatched path. Without it, such a block is a
# combinational loop through a mux and pass.legalize rejects the design with `comb-loop` (Vortex
# VX_fp_rounding, VX_alu_int and VX_fncp_unit).
#
#   * full_case_hold.sv: nested, 32-bit-label and empty-default exhaustive cases compile through the
#     yosys-slang reader and are equivalent to the source read by the native reader;
#   * full_case_dontcare.sv: exhaustive casez/casex compile (no equivalence check, see the fixture);
#   * full_case_incomplete.sv: a case that misses a selector value is NOT marked and keeps its latch.
set -u
W="${TEST_TMPDIR:-/tmp/yosys_full_case_$$}"
mkdir -p "$W"
LHD="${LHD:-$PWD/lhd/lhd}"
[[ -x "$LHD" ]] || LHD="$PWD/bazel-bin/lhd/lhd"
T=inou/yosys/tests
fail() { echo "FAIL: $*" >&2; exit 1; }

"$LHD" compile "$T/full_case_hold.sv" --reader yosys-slang --top full_case_hold --workdir "$W/hold" \
  --emit "verilog:$W/hold.v" -q >"$W/hold.log" 2>&1 \
  || { cat "$W/hold.log" >&2; fail "exhaustive cases were rejected (false combinational loop)"; }
grep -q 'Marked 3 exhaustive' "$W"/hold/logs/*yosys_tolg*.log \
  || fail "lhd_full_case did not mark the three exhaustive case statements of full_case_hold.sv"
# `default: ;` after a full label list is dead, but only the old hold wiring would make it a loop; the netlist
# must still compute the source function.
"$LHD" lec --impl "verilog:$W/hold.v" --ref "verilog:$T/full_case_hold.sv" --top full_case_hold \
  --workdir "$W/hold_lec" -q >"$W/hold_lec.log" 2>&1 \
  || { cat "$W/hold_lec.log" >&2; fail "the yosys-slang netlist is not equivalent to the source"; }

"$LHD" compile "$T/full_case_dontcare.sv" --reader yosys-slang --top full_case_dontcare --workdir "$W/dontcare" \
  -q >"$W/dontcare.log" 2>&1 \
  || { cat "$W/dontcare.log" >&2; fail "exhaustive casez/casex were rejected (false combinational loop)"; }
grep -q 'Marked 2 exhaustive' "$W"/dontcare/logs/*yosys_tolg*.log \
  || fail "lhd_full_case did not mark the two don't-care case statements of full_case_dontcare.sv"

"$LHD" compile "$T/full_case_incomplete.sv" --reader yosys-slang --top full_case_incomplete --workdir "$W/incomplete" \
  --emit "verilog:$W/incomplete.v" -q >"$W/incomplete.log" 2>&1 \
  || { cat "$W/incomplete.log" >&2; fail "the incomplete case did not compile"; }
grep -q 'Marked 0 exhaustive' "$W"/incomplete/logs/*yosys_tolg*.log \
  || fail "lhd_full_case marked a case that misses a selector value"
# The missing value really holds: the netlist keeps a latch instead of silently turning the hold into a constant.
grep -q 'always_latch' "$W/incomplete.v" \
  || fail "the incomplete case lost its latch"

echo "PASS: exhaustive case statements are not combinational loops; incomplete ones keep their hold"
