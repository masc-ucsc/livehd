#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# lhd_ff_merge (inou/yosys/yosys_ff_merge.cpp): a variable written by more than one clocked process
# (IEEE-illegal for always_ff) keeps every write, and the process later in the source wins in a cycle where
# several write (the native slang reader's rule; simulators differ in that cycle). Both yosys readers used to
# build one flop per process and the importer kept only one of them (yosys-slang lost the first process's
# writes, yosys-verilog the second's).
#
#   * ff_merge.sv through both yosys readers: a warning names each variable and its processes, and the
#     netlist is equivalent to the source read by the native reader (which already had this rule);
#   * ff_merge_two_clocks.sv: writers on different clocks cannot share one flop, so both readers refuse it.
set -u
W="${TEST_TMPDIR:-/tmp/yosys_ff_merge_$$}"
mkdir -p "$W"
LHD="${LHD:-$PWD/lhd/lhd}"
[[ -x "$LHD" ]] || LHD="$PWD/bazel-bin/lhd/lhd"
T=inou/yosys/tests
fail() { echo "FAIL: $*" >&2; exit 1; }

for reader in yosys-slang yosys-verilog; do
  "$LHD" compile "$T/ff_merge.sv" --reader "$reader" --top ff_merge --workdir "$W/w_$reader" \
    --emit "verilog:$W/ff_merge_$reader.v" >"$W/$reader.log" 2>&1 \
    || { tail -20 "$W/$reader.log" >&2; fail "ff_merge.sv ($reader): compile failed"; }
  for v in x y z; do
    grep -q "\\\\$v.* is written by [23] clocked processes" "$W/$reader.log" \
      || { grep -i warn "$W/$reader.log" >&2; fail "ff_merge.sv ($reader): no warning for variable $v"; }
  done
  grep -q "Merged 3 variable(s)" "$W"/w_$reader/logs/*yosys_tolg*.log \
    || fail "ff_merge.sv ($reader): lhd_ff_merge did not merge the three variables"
  "$LHD" lec --impl "verilog:$W/ff_merge_$reader.v" --ref "verilog:$T/ff_merge.sv" --top ff_merge \
    --workdir "$W/lec_$reader" -q >"$W/lec_$reader.log" 2>&1 \
    || { cat "$W/lec_$reader.log" >&2; fail "ff_merge.sv ($reader): the netlist is not equivalent to the source"; }

  if "$LHD" compile "$T/ff_merge_two_clocks.sv" --reader "$reader" --top ff_merge_two_clocks \
    --workdir "$W/w2_$reader" >"$W/two_clocks_$reader.log" 2>&1; then
    fail "ff_merge_two_clocks.sv ($reader): writers on two clocks were accepted"
  fi
  grep -rq "different clocks or edges" "$W/two_clocks_$reader.log" "$W/w2_$reader" \
    || { tail -20 "$W/two_clocks_$reader.log" >&2; fail "ff_merge_two_clocks.sv ($reader): refused without the reason"; }
done
echo "PASS: every clocked writer of a shared variable is kept; the later one wins; two clocks are refused"
