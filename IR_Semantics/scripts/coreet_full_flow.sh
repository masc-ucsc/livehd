#!/usr/bin/env bash
set -uo pipefail
# CORE-ET full-flow sweep: filelist -> lhd compile -> pass.lean -> (optional)
# Lean typecheck, one row per module.
#
# This existed only as an ad-hoc shell loop and was rewritten from scratch three
# times, each time reintroducing a defect the previous one had fixed:
#   * `xargs` with a textual `{}` substitution, which mangles module names;
#   * a shared `OUT`, so every worker wrote into the same directory and the rows
#     described whichever module finished last;
#   * `>>` appends from parallel workers onto NFS, which interleave and lose
#     rows.
# Hence: absolute paths, one result FILE per module, concatenated at the end,
# and a COMPLETE sentinel so a truncated sweep cannot be mistaken for a clean
# one.
#
# EXPORT ONLY, deliberately.
#
# There is no RUN_LEAN=true path here and there must not be one. The inline
# runner inside run_coreet_module_lean.sh accepts Lean exit 0 without applying
# the axiom policy, and this wrapper would then label a nonzero typecheck
# EXPORT_FAIL -- so a module could be reported as proved without an axiom audit,
# or as an export failure when the export succeeded. Either way the wrapper
# becomes a weaker proof gate than scripts/run_lean_queue.sh, which is the one
# place the obligation set, the namespace resolution and the axiom allowlist are
# enforced.
#
# The intended shape is: bounded-parallel EXPORT here, then the SERIAL audited
# queue over whatever this emitted.
#
# Usage: coreet_full_flow.sh <module-list-file> [jobs]
#   COREET_ROOT=...  OUT=...

R="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
LIST="${1:?usage: coreet_full_flow.sh <module-list-file> [jobs]}"
JOBS="${2:-4}"
OUT="${OUT:-$R/generated/master_port/ff}"
export COREET_ROOT="${COREET_ROOT:?set COREET_ROOT}"
if [[ "${RUN_LEAN:-false}" == "true" ]]; then
  echo "FATAL: this sweep is export-only; it must not be used as a proof gate." >&2
  echo "  Export here, then typecheck with the audited serial queue:" >&2
  echo "    scripts/run_lean_queue.sh \$OUT/*/lean/*_Lgraph.lean" >&2
  exit 2
fi
RUN_LEAN=false

# PER-INVOCATION ROW STORAGE.
#
# Completion used to be "a non-empty row file exists for every requested
# module", which says nothing about WHO wrote it. Measured: with a prior run's
# rows present and the current launcher failing outright, the sweep reported
# COMPLETE and exited 0 having run ZERO workers. Filtering old rows by requested
# name does not make them fresh.
#
# Rows therefore go in a directory named for THIS run, and the completion check
# reads only that directory. Previous evidence is left where it is rather than
# cleaned up -- it is still evidence, just of a different run.
RUN_ID="${RUN_ID:-$(date -u +%Y%m%dT%H%M%SZ)-$$}"
ROWS="$OUT/rows/$RUN_ID"
mkdir -p "$ROWS"
rm -f "$OUT/COMPLETE" "$OUT/INCOMPLETE"
echo "[sweep] run id $RUN_ID -> $ROWS"

one() {
  local m="$1"
  local mo="$OUT/$m"
  local row="$ROWS/$m.row"
  # RUN_LEC_GATE is set EXPLICITLY, not left to a default: these rows are
  # exports, and an export is not an RTL equivalence result.
  COREET_TOP="$m" OUT="$mo" LEAN_MODE=verified_compiler RUN_LEAN="$RUN_LEAN" \
    RUN_LEC_GATE=false \
    timeout "${MODULE_TIMEOUT:-3600}" bash "$R/IR_Semantics/scripts/run_coreet_module_lean.sh" \
    > "$OUT/$m.exp.log" 2>&1
  local rc=$?
  local lean; lean="$(find "$mo" -name "${m}_Lgraph.lean" 2>/dev/null | head -1)"
  # DISTINGUISH the outcomes. "EXPORT_FAIL" for everything was the reason a
  # whole class of static-gate refusals was reported with no reason attached.
  local gl="$mo/logs/static_gates.log"
  if [[ "$rc" -ne 0 && -r "$gl" ]] && grep -q '^FAIL:' "$gl"; then
    printf '%s\tGATE_FAIL\t-\t-\t-\tstatic gate: %s [%s total]\n' "$m" \
      "$(grep -m1 '^FAIL:' "$gl" | cut -c7-)" "$(grep -c '^FAIL:' "$gl")" > "$row"
    return
  fi
  if [[ "$rc" -ne 0 || -z "$lean" ]]; then
    local why
    # A STATIC GATE failure is not a JSON diagnostic, and reading only the JSON
    # reported a whole defect class as "unknown" -- 18 const-width modules with
    # no reason attached, which is not a result anyone can act on. Check the
    # gate log FIRST: when the gates failed, that IS the reason.
    local gl="$mo/logs/static_gates.log"
    if [[ -r "$gl" ]] && grep -q '^FAIL:' "$gl"; then
      why="static gate: $(grep -m1 '^FAIL:' "$gl" | cut -c7-) [$(grep -c '^FAIL:' "$gl") total]"
    fi
    [[ -z "${why:-}" ]] && why="$(grep -ohE '"severity":"error"[^}]*"message":"[^"]{0,160}' "$OUT/$m.exp.log" 2>/dev/null | sed 's/.*"message":"//' | head -1)"
    [[ -z "${why:-}" ]] && why="$(grep -ohE '"message":"[^"]{0,160}' "$OUT/$m.exp.log" 2>/dev/null | head -1)"
    [[ "$rc" -eq 124 ]] && why="timeout ${MODULE_TIMEOUT:-3600}s"
    printf '%s\tEXPORT_FAIL\t-\t-\t-\t%s\n' "$m" "${why:-unknown}" > "$row"
    return
  fi
  if [[ "$RUN_LEAN" != "true" ]]; then
    printf '%s\tEXPORTED\t-\t-\t-\t\n' "$m" > "$row"
    return
  fi
  local lg="$mo/logs/lean_typecheck.log" wall rss nsorry
  wall="$(grep -oP 'WALL=\K[0-9.]+' "$lg" 2>/dev/null | tail -1)"
  rss="$(grep -oP 'PEAK_RSS=\K[0-9]+' "$lg" 2>/dev/null | tail -1)"
  nsorry="$(grep -c sorryAx "$lg" 2>/dev/null || true)"
  if grep -q 'error:' "$lg" 2>/dev/null; then
    printf '%s\tLEAN_FAIL\t%s\t%s\t%s\t%s\n' "$m" "${wall:--}" "${rss:--}" "$nsorry" \
      "$(grep -m1 'error:' "$lg" | cut -c1-160)" > "$row"
  else
    printf '%s\tPASS\t%s\t%s\t%s\t\n' "$m" "${wall:--}" "${rss:--}" "$nsorry" > "$row"
  fi
}
export -f one
export R OUT RUN_LEAN COREET_ROOT ROWS

# The launcher's exit status is CHECKED. Without this a failed xargs -- a bad
# -P argument, a missing bash, a killed pipeline -- is indistinguishable from a
# clean sweep, and the completion check below would then be the only thing
# standing between that and a COMPLETE marker.
launch_rc=0
grep -vE '^\s*(#|$)' "$LIST" | xargs -P "$JOBS" -I{} bash -c 'one "$@"' _ {} || launch_rc=$?
if [[ "$launch_rc" -ne 0 ]]; then
  echo "[sweep] WARNING: the worker launcher exited $launch_rc" >&2
fi

# The sentinel must mean "every requested module produced a row IN THIS RUN",
# not "the loop exited". Stale rows from an earlier sweep are excluded by name,
# and a missing row is named rather than silently dropped -- a truncated sweep
# that looks complete is worse than one that admits it stopped.
want="$(grep -vE '^\s*(#|$)' "$LIST" | sort -u)"
missing=""
while read -r m; do
  [[ -s "$ROWS/$m.row" ]] || missing+=" $m"
done <<< "$want"
{
  printf 'module\tresult\twall_s\trss_kb\tsorryAx\tdetail\n'
  while read -r m; do
    [[ -s "$ROWS/$m.row" ]] && cat "$ROWS/$m.row"
  done <<< "$want"
} > "$OUT/coreet.tsv"
# COMPLETE means: the launcher succeeded AND every requested module produced a
# row IN THIS RUN. Either alone has been observed to be insufficient.
if [[ -n "$missing" || "$launch_rc" -ne 0 ]]; then
  {
    [[ "$launch_rc" -ne 0 ]] && echo "launcher exited $launch_rc"
    [[ -n "$missing" ]] && echo "no row from run $RUN_ID for:$missing"
  } | tee "$OUT/INCOMPLETE" >&2
  rm -f "$OUT/COMPLETE"
  sweep_rc=1
else
  rm -f "$OUT/INCOMPLETE"
  printf 'run_id=%s\nrows=%s\n' "$RUN_ID" "$ROWS" > "$OUT/COMPLETE"
  sweep_rc=0
fi
echo "rows: $(($(wc -l < "$OUT/coreet.tsv") - 1)) -> $OUT/coreet.tsv"
awk -F'\t' 'NR>1{print $2}' "$OUT/coreet.tsv" | sort | uniq -c | sort -rn
exit "$sweep_rc"
