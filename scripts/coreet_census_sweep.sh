#!/usr/bin/env bash
set -uo pipefail

# Classify every CORE-ET minion module by RUNNING the pipeline (compile ->
# single_edge -> pass.lean emit -> static gates), not by pattern-matching the
# source.  CVA6_COVERAGE_PLAN.md: a static regex mis-classified `lzc` as
# memory-blocked and propagated that to `alu`/`pmp`, both already proven.
# Memory blockage is also transitive through children, which only the real
# elaboration can see.
#
# Emits a TSV work-list.  The LEC gate is skipped here (it runs per module
# before proving); this is a triage pass, not a verification pass.
#
# Usage: scripts/coreet_census_sweep.sh <module-list-file> [jobs]

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LIST="${1:?usage: coreet_census_sweep.sh <module-list> [jobs]}"
JOBS="${2:-6}"
OUTDIR="$ROOT/generated/core-et"
TSV="${CENSUS_TSV:-$OUTDIR/coreet_census.tsv}"
mkdir -p "$OUTDIR"

printf 'module\tstage\tverdict\tnodes\tflops\tmax_w\tslots\tdetail\n' > "$TSV"

classify_one() {
  local m="$1"
  local out="$OUTDIR/$m" log="$OUTDIR/$m/logs"
  local stage=filelist verdict=FAIL nodes= flops= maxw= slots=1 detail=

  if ! COREET_TOP="$m" RUN_LEC_GATE=false STOP_AFTER=lean \
       "$ROOT/scripts/run_coreet_module_lean.sh" > "$OUTDIR/$m.sweep.log" 2>&1; then
    # Where did it stop, and why?  A named diagnostic beats an exit code.
    if   [[ ! -s "$log/filelist.log" && ! -r "$log/lhd_compile.log" ]]; then
      stage=filelist; detail="$(tail -1 "$OUTDIR/$m.sweep.log" 2>/dev/null | cut -c1-120)"
    elif grep -q 'compile exit=0' "$OUTDIR/$m.sweep.log"; then
      if grep -q 'single_edge exit=0' "$OUTDIR/$m.sweep.log"; then
        stage=lean-emit
        # The diagnostic lands in --result-json, NOT the log: when --result-json is
        # given, lhd writes the result record to that file instead of stdout, so the
        # log is EMPTY on failure.  Reading the log here reported every memory
        # blocker as a blank detail.
        detail="$(python3 -c "import json,sys
try: d=json.load(open(sys.argv[1]))
except Exception: sys.exit()
print((d.get('error') or {}).get('message','')[:150])" "$log/lhd_lean_result.json" 2>/dev/null)"
        [[ -z "$detail" ]] && detail="$(grep -ohP '"message":"\K[^"]{0,110}' "$log/lhd_lean.log" 2>/dev/null | tail -1)"
        # static gates ran but failed?
        if [[ -r "$log/static_gates.log" ]] && grep -q 'gate_status=1' "$log/static_gates.log"; then
          stage=static-gates
          detail="$(grep -m2 '^FAIL' "$log/static_gates.log" | paste -sd'; ' - | cut -c120)"
          [[ -z "$detail" ]] && detail="$(grep -m1 -oP 'unhandled.*' "$log/static_gates.log" | cut -c1-120)"
        fi
      else
        stage=single_edge
        detail="$(grep -ohP '"message":"\K[^"]{0,110}' "$log/single_edge.log" 2>/dev/null | tail -1)"
      fi
    else
      stage=compile
      detail="$(grep -ohP '"code":"\K[^"]*' "$log/lhd_compile.log" 2>/dev/null | sort -u | paste -sd, - | cut -c1-120)"
      [[ -z "$detail" ]] && detail="$(grep -m1 -oP '"message":"\K[^"]{0,110}' "$log/lhd_compile.log" 2>/dev/null)"
    fi
  else
    stage=static-gates; verdict=READY
    detail="$(grep -oE '_(comb|next|step)_refines_fast' "$log/static_gates.log" 2>/dev/null | sort -u | paste -sd, -)"
  fi

  if [[ -r "$log/static_gates.log" ]]; then
    nodes="$(grep -oP 'cert nodes\s+:\s+\K[0-9]+' "$log/static_gates.log" | head -1)"
    flops="$(grep -oP 'state fields\s+:\s+\K[0-9]+' "$log/static_gates.log" | head -1)"
    maxw="$(grep -oP 'node output widths : max=\K[0-9]+' "$log/static_gates.log" | head -1)"
  fi
  slots="$(grep -oP 'slots=\K[0-9]+' "$log/single_edge.log" 2>/dev/null | tail -1)"; slots="${slots:-1}"

  printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
    "$m" "$stage" "$verdict" "$nodes" "$flops" "$maxw" "$slots" "${detail//$'\t'/ }" >> "$TSV"
  printf '%-38s %-13s %-6s nodes=%-6s %s\n' "$m" "$stage" "$verdict" "${nodes:--}" "${detail:0:70}"
}
export -f classify_one
export ROOT OUTDIR TSV

grep -v '^\s*#' "$LIST" | grep -v '^\s*$' | \
  xargs -P "$JOBS" -I{} nice -n 19 ionice -c 3 bash -c 'classify_one "$@"' _ {}

echo
echo "census -> $TSV"
