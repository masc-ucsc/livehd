#!/usr/bin/env bash
set -uo pipefail

# B1+B2 verified-compiler sweep.
#
# Two phases, deliberately separate:
#
#   1. EMIT   -- run each module through `pass.lean mode=verified_compiler`.  This
#                is parallelisable and cheap.
#   2. PROVE  -- typecheck each emitted file, ONE AT A TIME via
#                run_lean_queue.sh.  Concurrency is not an option here: this box
#                is a shared NFS server and each Lean is multi-GB.
#
# The verdict is bucketed against the MANUAL path's census
# (generated/core-et/coreet_census.tsv), because "this module fails" is only a
# finding if the manual path managed it:
#
#   proven            typecheck exit 0, no sorryAx
#   blocked-upstream  the manual census also failed before pass.lean -- filelist,
#                     compile or single_edge.  Not this branch's problem.
#   blocked-here      the manual census reached pass.lean (or READY) but the
#                     verified compiler did not.  THIS is what counts against the
#                     branch.
#
# Usage:
#   scripts/run_vc_sweep.sh <module-list-file> [emit-jobs]
#   PHASE=emit|prove|report scripts/run_vc_sweep.sh ...   (default: all)

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LIST="${1:?usage: run_vc_sweep.sh <module-list> [emit-jobs]}"
JOBS="${2:-6}"
PHASE="${PHASE:-all}"

OUTDIR="$ROOT/generated/vc_sweep"
LEANDIR="$OUTDIR/lean"
EMIT_TSV="$OUTDIR/emit.tsv"
PROVE_TSV="$OUTDIR/prove.tsv"
SWEEP_TSV="${SWEEP_TSV:-$ROOT/pass/lean/SWEEP_b1-b2.tsv}"
CENSUS="${CENSUS:-$ROOT/generated/core-et/coreet_census.tsv}"
mkdir -p "$LEANDIR"

# ---------------------------------------------------------------- phase 1: emit
emit_one() {
  local m="$1" log="$OUTDIR/$m.emit.log"
  local rc=0
  LEAN_MODE=verified_compiler RUN_LEAN=false RUN_LEC_GATE=false STOP_AFTER=lean \
    COREET_TOP="$m" OUT="$OUTDIR/mod/$m" \
    "$ROOT/scripts/run_coreet_module_lean.sh" > "$log" 2>&1 || rc=$?

  local f
  f="$(find "$OUTDIR/mod/$m" -name "${m}_Lgraph.lean" 2>/dev/null | head -1)"
  if [[ -n "$f" && -s "$f" ]]; then
    cp "$f" "$LEANDIR/${m}_Lgraph.lean"
    printf '%s\tEMITTED\t%s\t%s\t%s\t%s\n' "$m" \
      "$(grep -c 'SourceDesc\.' "$f")" "$(grep -c 'origin :=' "$f")" \
      "$(grep -c 'resetActiveLow' "$f")" "$(grep -c 'nextImg' "$f")"
  else
    # Name the stage, not just the exit code.
    local stage=unknown
    if   grep -q 'no \.sv\|missing RTL\|filelist' "$log" 2>/dev/null; then stage=filelist
    elif grep -q 'compile exit=[1-9]\|"status":"fail"' "$log" 2>/dev/null;  then stage=compile
    elif grep -q 'single_edge' "$log" 2>/dev/null;                          then stage=single_edge
    elif grep -q 'pass.lean' "$log" 2>/dev/null;                            then stage=pass_lean
    fi
    printf '%s\tNO_EMIT(%s)\t\t\t\t\n' "$m" "$stage"
  fi
}
export -f emit_one
export ROOT OUTDIR LEANDIR

if [[ "$PHASE" == "all" || "$PHASE" == "emit" ]]; then
  printf 'module\temit\tsources\tnodes\tflops\tmemories\n' > "$EMIT_TSV"
  # shellcheck disable=SC2016
  grep -v '^\s*#' "$LIST" | grep -v '^\s*$' \
    | xargs -P "$JOBS" -I{} bash -c 'emit_one "$@"' _ {} >> "$EMIT_TSV"
  echo "[vc-sweep] emit done: $(tail -n +2 "$EMIT_TSV" | grep -c EMITTED) emitted, $(tail -n +2 "$EMIT_TSV" | grep -c NO_EMIT) not"
fi

# --------------------------------------------------------------- phase 2: prove
if [[ "$PHASE" == "all" || "$PHASE" == "prove" ]]; then
  mapfile -t files < <(tail -n +2 "$EMIT_TSV" | awk -F'\t' '$2=="EMITTED"{print $1}' \
                        | while read -r m; do echo "$LEANDIR/${m}_Lgraph.lean"; done)
  if [[ "${#files[@]}" -gt 0 ]]; then
    echo "[vc-sweep] proving ${#files[@]} modules, one at a time"
    QUEUE_SUMMARY="$PROVE_TSV" nice -n 19 ionice -c 3 \
      bash "$ROOT/scripts/run_lean_queue.sh" "${files[@]}" || true
  fi
fi

# -------------------------------------------------------------- phase 3: report
if [[ "$PHASE" == "all" || "$PHASE" == "report" ]]; then
  printf 'module\tverdict\tbucket\tsources\tnodes\tflops\tmemories\twall_s\tpeak_rss_kb\tmanual_census\tdetail\n' > "$SWEEP_TSV"
  tail -n +2 "$EMIT_TSV" | while IFS=$'\t' read -r m emit src nodes flops mems; do
    manual="$(awk -F'\t' -v M="$m" '$1==M{print $2"/"$3}' "$CENSUS" 2>/dev/null | head -1)"
    [[ -z "$manual" ]] && manual="not-in-census"
    prove="$(awk -F'\t' -v M="${m}_Lgraph" '$1==M{print $7"\t"$8"\t"$9"\t"$5"\t"$6}' "$PROVE_TSV" 2>/dev/null | head -1)"
    exitc="$(cut -f1 <<<"$prove")"; ax="$(cut -f2 <<<"$prove")"
    verd="$(cut -f3 <<<"$prove")"; wall="$(cut -f4 <<<"$prove")"; rss="$(cut -f5 <<<"$prove")"

    if [[ "$emit" == "EMITTED" && "$exitc" == "0" && "$ax" != *sorryAx* ]]; then
      bucket=proven; verdict=PROVEN
    elif [[ "$emit" != "EMITTED" ]] && [[ "$manual" == filelist/* || "$manual" == compile/* || "$manual" == single_edge/* || "$manual" == */FAIL ]]; then
      bucket=blocked-upstream; verdict="$emit"
    elif [[ "$emit" != "EMITTED" ]]; then
      bucket=blocked-here; verdict="$emit"
    else
      bucket=blocked-here; verdict="${verd:-NO_PROVE}"
    fi
    printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
      "$m" "$verdict" "$bucket" "$src" "$nodes" "$flops" "$mems" "$wall" "$rss" "$manual" "${ax:-}"
  done >> "$SWEEP_TSV"

  echo "[vc-sweep] $SWEEP_TSV"
  tail -n +2 "$SWEEP_TSV" | awk -F'\t' '{print $3}' | sort | uniq -c | sort -rn | sed 's/^/  /'
fi
