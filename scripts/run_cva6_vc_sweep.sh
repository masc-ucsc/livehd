#!/usr/bin/env bash
set -uo pipefail
# CVA6 verified-compiler sweep over GENERATED gate wrappers.
#
# Wrappers come from scripts/gen_cva6_wrappers.py (slang's elaborated AST); only
# those that actually ELABORATE are swept -- generation is not compilation, a
# distinction that cost one wrong report earlier (100 "generated", 9 compiling).
#
# NOTE the filelist: bender is asked for `--top <module>_gate`, which it cannot
# know, and silently produces an EMPTY filelist.  Use the static full-core
# filelist instead and let yosys pick the top.
R=/soe/czeng14/projects/livehd-new
LIST="${1:?usage: run_cva6_vc_sweep.sh <module-list> [jobs]}"
JOBS="${2:-4}"
FL="${CVA6_FILELIST_OVERRIDE:-$R/generated/cva6_filelists/cv64a6_imafdc_sv39_hpdcache_wb.top_cva6.flistplus.f}"
WRAP="${WRAP_DIR:-$R/temp/wrap_all}"
OUTDIR="$R/generated/cva6_vc"
TSV="$OUTDIR/emit.tsv"
mkdir -p "$OUTDIR/lean"

one() {
  local m="$1"
  local w="$WRAP/${m}_gate.sv"
  [[ -r "$w" ]] || { printf '%s\tNO_WRAPPER\t\t\t\t\n' "$m"; return; }
  timeout "${EMIT_TIMEOUT:-1800}" env CVA6_TOP="${m}_gate" CVA6_FILELIST="$FL" \
    CVA6_WRAPPER_FILE="$w" LEAN_MODE=verified_compiler RUN_LEAN=false \
    OUT="$OUTDIR/mod/$m" bash "$R/scripts/run_cva6_module_lean_stress.sh" \
    > "$OUTDIR/$m.log" 2>&1
  local rc=$?
  local f; f="$(find "$OUTDIR/mod/$m" -name "${m}_gate_Lgraph.lean" 2>/dev/null | head -1)"
  if [[ -n "$f" && -s "$f" ]]; then
    cp "$f" "$OUTDIR/lean/${m}_gate_Lgraph.lean"
    printf '%s\tEMITTED\t%s\t%s\t%s\t%s\n' "$m" \
      "$(grep -c 'SourceDesc\.' "$f")" "$(grep -c 'origin :=' "$f")" \
      "$(grep -c resetActiveLow "$f")" "$(grep -c nextImg "$f")"
  else
    local why
    why="$(grep -oP '"message":"\K[^"]{0,90}' "$OUTDIR/mod/$m"/logs/*result.json 2>/dev/null | head -1)"
    [[ "$rc" -eq 124 ]] && why="timeout ${EMIT_TIMEOUT:-1800}s"
    printf '%s\tNO_EMIT\t%s\t\t\t\n' "$m" "${why:-unknown}"
  fi
}
export -f one; export R WRAP FL OUTDIR

printf 'module\temit\tsources\tnodes\tflops\tmems\n' > "$TSV"
grep -vE '^\s*(#|$)' "$LIST" | xargs -P "$JOBS" -I{} bash -c 'one "$@"' _ {} >> "$TSV"
echo "CVA6-EMIT: $(tail -n +2 "$TSV" | grep -c EMITTED) emitted of $(tail -n +2 "$TSV" | wc -l)"

# gate before proving
gf=0
for f in "$OUTDIR"/lean/*_Lgraph.lean; do
  [[ -e "$f" ]] || continue
  m=$(basename "$f" _Lgraph.lean)
  python3 "$R/pass/lean/scripts/vc_gates.py" "$f" "$m" --quiet || { echo "GATE-FAIL $m"; gf=$((gf+1)); }
  python3 "$R/pass/lean/scripts/cert_lgraph_diff.py" "$f" --quiet || { echo "DIFF-FAIL $m"; gf=$((gf+1)); }
done
echo "CVA6-GATES: $gf failures"

mapfile -t files < <(ls "$OUTDIR"/lean/*_Lgraph.lean 2>/dev/null)
if [[ "${#files[@]}" -gt 0 ]]; then
  QUEUE_SUMMARY="$OUTDIR/prove.tsv" nice -n 19 ionice -c 3 \
    bash "$R/scripts/run_lean_queue.sh" "${files[@]}" || true
fi
echo "CVA6-SWEEP-DONE"
