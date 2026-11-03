#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# Overlapping items of a plain case/casez/casex keep their first-match priority through the yosys-slang
# reader (`proc -ifx`; packages/yosys.patch, passes/proc/proc_mux.cc). The netlist must be equivalent to
# the source read by the native reader. Without the patch the overlapping items were one parallel $pmux:
# casez_priority.sv's oz gave 8 for f = 5'b00011 instead of 9, and lec did not pass.
set -u
W="${TEST_TMPDIR:-/tmp/yosys_casez_priority_$$}"
mkdir -p "$W"
LHD="${LHD:-$PWD/lhd/lhd}"
[[ -x "$LHD" ]] || LHD="$PWD/bazel-bin/lhd/lhd"
T=inou/yosys/tests
fail() { echo "FAIL: $*" >&2; exit 1; }

"$LHD" compile "$T/casez_priority.sv" --reader yosys-slang --top casez_priority --workdir "$W/c" \
  --emit "verilog:$W/out.v" -q >"$W/c.log" 2>&1 \
  || { cat "$W/c.log" >&2; fail "casez_priority.sv did not compile"; }
"$LHD" lec --impl "verilog:$W/out.v" --ref "verilog:$T/casez_priority.sv" --top casez_priority \
  --workdir "$W/lec" -q >"$W/lec.log" 2>&1 \
  || { cat "$W/lec.log" >&2; fail "overlapping case items lost their priority (netlist not equivalent to the source)"; }

echo "PASS: overlapping case/casez/casex items keep first-match priority"
