#!/usr/bin/env bash
# Elaborate every generated CVA6 gate wrapper with slang and record the result.
#
# Generation is NOT compilation: an earlier report said "100 generated" when 9
# actually elaborated.  Every coverage figure for the wrapper generator must come
# from this script, never from the generator's own count.
set -uo pipefail
R=/soe/czeng14/projects/livehd-new
SL="${SLANG:-/soe/czeng14/projects/slang/build/bin/slang}"
WRAP="${1:?usage: elab_cva6_wrappers.sh <wrapper-dir> <out.tsv> [filelist] [jobs]}"
OUT="${2:?}"
FL="${3:-$R/generated/cva6_filelists/cv64a6_imafdc_sv39_hpdcache_wb.top_cva6.flistplus.f}"
JOBS="${4:-6}"

one() {
  local w="$1" m
  m="$(basename "$w" _gate.sv)"
  local err
  err="$(timeout "${ELAB_TIMEOUT:-300}" "$SL" -f "$FL" "$w" --top "${m}_gate" --quiet 2>&1 \
         | grep -m1 'error:' | sed 's/.*error: //' | cut -c1-90)"
  if [[ -z "$err" ]]; then printf '%s\tOK\t\n' "$m"; else printf '%s\tFAIL\t%s\n' "$m" "$err"; fi
}
export -f one; export SL FL

ls "$WRAP"/*_gate.sv | xargs -P "$JOBS" -I{} bash -c 'one "$@"' _ {} | sort > "$OUT"
printf 'ELAB %s: %s OK / %s total\n' "$WRAP" "$(grep -c $'\tOK\t' "$OUT")" "$(wc -l < "$OUT")"
