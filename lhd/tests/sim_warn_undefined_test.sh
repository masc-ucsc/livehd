#!/usr/bin/env bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# sim.warn_undefined (default on): the first time each site computes a value the
# source leaves undefined, `lhd sim` prints ONE warning naming the site and the
# cycle, on both backends; later hits at the same site are silent, and
# --set sim.warn_undefined=false prints none. The power-on evaluation (inputs
# not yet driven) never warns.
set -euo pipefail

LHD="${LHD:-lhd/lhd}"
[ -x "$LHD" ] || LHD="./bazel-bin/lhd/lhd"
D=lhd/tests/sim_warn_undefined
W=$(mktemp -d "${TEST_TMPDIR:-/tmp}/simwarn.XXXXXX")
trap 'rm -rf "$W"' EXIT
fail() { echo "FAIL: $*" >&2; exit 1; }

"$LHD" compile "$D/warn_div.v" --top warn_div --emit-dir "lg:$W/lg/" --workdir "$W/c" >"$W/c.log" 2>&1 || { cat "$W/c.log"; fail "compile"; }
for be in slop llvm; do
  "$LHD" sim "lg:$W/lg/" "$D/warn_div_tb.prp" --set sim.tune.backend=$be --workdir "$W/$be" >"$W/$be.out" 2>"$W/$be.err" ||
    { cat "$W/$be.err"; fail "$be: sim"; }
  grep 'lhd sim: warning:' "$W/$be.err" >"$W/$be.warn" || true
  [ "$(wc -l <"$W/$be.warn")" -eq 2 ] || { cat "$W/$be.warn"; fail "$be: expected 2 warnings (one per site)"; }
  grep -q 'division by zero at .*warn_div.v:3 (cycle 1)' "$W/$be.warn" || { cat "$W/$be.warn"; fail "$be: division site"; }
  grep -q 'remainder by zero at .*warn_div.v:4 (cycle 1)' "$W/$be.warn" || { cat "$W/$be.warn"; fail "$be: remainder site"; }
  "$LHD" sim "lg:$W/lg/" "$D/warn_div_tb.prp" --set sim.tune.backend=$be --set sim.warn_undefined=false --workdir "$W/$be" \
    >/dev/null 2>"$W/$be.off.err" || { cat "$W/$be.off.err"; fail "$be: sim (off)"; }
  ! grep -q 'lhd sim: warning:' "$W/$be.off.err" || { cat "$W/$be.off.err"; fail "$be: sim.warn_undefined=false still warns"; }
  "$LHD" sim "$D/warn_mem.prp" --set sim.tune.backend=$be --workdir "$W/m$be" >"$W/m$be.out" 2>"$W/m$be.err" ||
    { cat "$W/m$be.err"; fail "$be: memory sim"; }
  grep 'lhd sim: warning:' "$W/m$be.err" >"$W/m$be.warn" || true
  [ "$(wc -l <"$W/m$be.warn")" -eq 2 ] || { cat "$W/m$be.warn"; fail "$be: expected 2 memory warnings (one per port)"; }
  grep -q 'memory write outside the array at .*warn_mem.prp:6 (`a` write port 0) (cycle 2)' "$W/m$be.warn" ||
    { cat "$W/m$be.warn"; fail "$be: memory write site"; }
  grep -q 'memory read outside the array at .*warn_mem.prp:6 (`a` read port 0) (cycle 3)' "$W/m$be.warn" ||
    { cat "$W/m$be.warn"; fail "$be: memory read site"; }
  "$LHD" compile "$D/warn_vmem.v" --top warn_vmem --emit-dir "lg:$W/vlg/" --workdir "$W/vc" >"$W/vc.log" 2>&1 || { cat "$W/vc.log"; fail "verilog compile"; }
  "$LHD" sim "lg:$W/vlg/" "$D/warn_vmem_tb.prp" --set sim.tune.backend=$be --workdir "$W/v$be" >"$W/v$be.out" 2>"$W/v$be.err" ||
    { cat "$W/v$be.err"; fail "$be: verilog memory sim"; }
  grep 'lhd sim: warning:' "$W/v$be.err" >"$W/v$be.warn" || true
  [ "$(wc -l <"$W/v$be.warn")" -eq 2 ] || { cat "$W/v$be.warn"; fail "$be: expected 2 verilog memory warnings"; }
  grep -q 'memory write outside the array at .*warn_vmem.v:7 (cycle 2)' "$W/v$be.warn" || { cat "$W/v$be.warn"; fail "$be: verilog write site"; }
  grep -q 'memory read outside the array at .*warn_vmem.v:8 (cycle 3)' "$W/v$be.warn" || { cat "$W/v$be.warn"; fail "$be: verilog read site"; }
  echo "$be - success"
done
