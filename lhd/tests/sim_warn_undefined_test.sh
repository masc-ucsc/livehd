#!/usr/bin/env bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# sim.warn_undefined (default on): the first time each site computes a value the
# source leaves undefined, `lhd sim` prints ONE warning naming the site and the
# cycle, on both backends; later hits at the same site are silent, and
# --set sim.warn_undefined=false prints none. The power-on evaluation (inputs
# not yet driven) never warns.
#
# Usage: sim_warn_undefined_test.sh [<backend> [<case>]]
#   backend: slop | llvm | lnast    (default: all three; slop and llvm are
#            inou.cgen.sim's backends, lnast the LNAST flow -- the `lhd sim`
#            default -- required, so a fallback fails instead of hiding a gap)
#   case:    div | mem | vmem       (default: all three)
# No argument runs everything (a manual sweep). Each bazel target
# (sim_warn_undefined_<backend>_<case>, grouped by the sim_warn_undefined_test
# suite) runs one backend and one case: every `lhd sim` pays a full host C++
# build, so the whole matrix in one target could not fit the per-test budget.
set -euo pipefail

LHD="${LHD:-lhd/lhd}"
[ -x "$LHD" ] || LHD="./bazel-bin/lhd/lhd"
D=lhd/tests/sim_warn_undefined
backends="${1:-slop llvm lnast}"
cases="${2:-div mem vmem}"
W=$(mktemp -d "${TEST_TMPDIR:-/tmp}/simwarn.XXXXXX")
trap 'rm -rf "$W"' EXIT
fail() { echo "FAIL: $*" >&2; exit 1; }
has_case() { [[ " $cases " == *" $1 "* ]]; }

for c in $cases; do
  case "$c" in div | mem | vmem) ;; *) fail "unknown case '$c' (div|mem|vmem)" ;; esac
done
for be in $backends; do
  case "$be" in slop | llvm | lnast) ;; *) fail "unknown backend '$be' (slop|llvm|lnast)" ;; esac
done

if has_case div; then
  "$LHD" compile "$D/warn_div.v" --top warn_div --emit-dir "lg:$W/lg/" --workdir "$W/c" >"$W/c.log" 2>&1 || { cat "$W/c.log"; fail "compile"; }
fi
if has_case vmem; then
  "$LHD" compile "$D/warn_vmem.v" --top warn_vmem --emit-dir "lg:$W/vlg/" --workdir "$W/vc" >"$W/vc.log" 2>&1 || { cat "$W/vc.log"; fail "verilog compile"; }
fi
for be in $backends; do
  # slop / llvm: inou.cgen.sim with that backend. lnast: the LNAST flow, required (LHD_SIM_LNAST=1).
  if [ "$be" = lnast ]; then
    export LHD_SIM_LNAST=1
    tune=llvm
  else
    export LHD_SIM_LNAST=0
    tune=$be
  fi
  if has_case div; then
    "$LHD" sim "lg:$W/lg/" "$D/warn_div_tb.prp" --set sim.tune.backend=$tune --workdir "$W/$be" >"$W/$be.out" 2>"$W/$be.err" ||
      { cat "$W/$be.err"; fail "$be: sim"; }
    grep 'lhd sim: warning:' "$W/$be.err" >"$W/$be.warn" || true
    [ "$(wc -l <"$W/$be.warn")" -eq 2 ] || { cat "$W/$be.warn"; fail "$be: expected 2 warnings (one per site)"; }
    grep -q 'division by zero at .*warn_div.v:3 (cycle 1)' "$W/$be.warn" || { cat "$W/$be.warn"; fail "$be: division site"; }
    grep -q 'remainder by zero at .*warn_div.v:4 (cycle 1)' "$W/$be.warn" || { cat "$W/$be.warn"; fail "$be: remainder site"; }
    "$LHD" sim "lg:$W/lg/" "$D/warn_div_tb.prp" --set sim.tune.backend=$tune --set sim.warn_undefined=false --workdir "$W/$be" \
      >/dev/null 2>"$W/$be.off.err" || { cat "$W/$be.off.err"; fail "$be: sim (off)"; }
    ! grep -q 'lhd sim: warning:' "$W/$be.off.err" || { cat "$W/$be.off.err"; fail "$be: sim.warn_undefined=false still warns"; }
  fi
  if has_case mem; then
    # Pyrope has no run-time out-of-range memory access: pass.bitwidth rejects an
    # address it cannot prove inside [0, size) (mem-address-overflow), so the
    # run-time warning is only reachable from Verilog (the vmem case below).
    if "$LHD" sim "$D/warn_mem.prp" --set sim.tune.backend=$tune --workdir "$W/m$be" >"$W/m$be.out" 2>"$W/m$be.err"; then
      fail "$be: warn_mem.prp must be rejected at compile time (mem-address-overflow)"
    fi
    grep -q 'mem-address-overflow' "$W/m$be.out" "$W/m$be.err" || { cat "$W/m$be.err"; fail "$be: expected a mem-address-overflow diagnostic"; }
  fi
  if has_case vmem; then
    "$LHD" sim "lg:$W/vlg/" "$D/warn_vmem_tb.prp" --set sim.tune.backend=$tune --workdir "$W/v$be" >"$W/v$be.out" 2>"$W/v$be.err" ||
      { cat "$W/v$be.err"; fail "$be: verilog memory sim"; }
    grep 'lhd sim: warning:' "$W/v$be.err" >"$W/v$be.warn" || true
    [ "$(wc -l <"$W/v$be.warn")" -eq 2 ] || { cat "$W/v$be.warn"; fail "$be: expected 2 verilog memory warnings"; }
    grep -q 'memory write outside the array at .*warn_vmem.v:7 (cycle 2)' "$W/v$be.warn" || { cat "$W/v$be.warn"; fail "$be: verilog write site"; }
    grep -q 'memory read outside the array at .*warn_vmem.v:8 (cycle 3)' "$W/v$be.warn" || { cat "$W/v$be.warn"; fail "$be: verilog read site"; }
  fi
  echo "$be ($cases) - success"
done
