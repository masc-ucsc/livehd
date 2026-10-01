#!/usr/bin/env bash
# Re-emit every certificate of a stored census run WITH THE CURRENT BINARY and
# diff it byte for byte against what that run emitted.
#
# The run keeps both the normalized LGraph (`lgdb_norm`) and the certificate it
# produced from it, so this re-runs ONLY pass.lean: no yosys, no cprop, no
# single_edge.  That makes the diff attributable -- any difference is this
# change's, not a re-elaboration's.
#
# A certificate-ORDER change is expected to move memory designs and nothing
# else, and "nothing else" is the part worth proving: the claim is only as good
# as the number of modules it was checked on.
#
# Usage: CENSUS_RUN=<dir> scripts/cert_reemit_diff.sh [module...]
set -u
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LHD="${LHD:-$ROOT/bazel-bin/lhd/lhd}"
RUN="${CENSUS_RUN:?set CENSUS_RUN=<generated/census_d2/...>}"
OUT="${OUT:-$ROOT/generated/cert_reemit_diff}"
TIMEOUT="${TIMEOUT:-900}"
LEAN_MODE="${LEAN_MODE:-verified_compiler}"

[[ -x "$LHD" ]] || { echo "FATAL: missing $LHD"; exit 2; }
[[ -d "$RUN/mod" ]] || { echo "FATAL: $RUN/mod is not a census run directory"; exit 2; }
mkdir -p "$OUT"

mods=("$@")
if [[ ${#mods[@]} -eq 0 ]]; then
  mapfile -t mods < <(cd "$RUN/mod" && for d in */; do
    [[ -s "${d}lean/${d%/}_Lgraph.lean" ]] && echo "${d%/}"
  done | sort)
fi

same=0; moved=0; failed=0; missing=0
# The rule this script EXISTS to enforce: a design with no memory must not move.
unexpected=0; mem_seen=0; nonmem_seen=0
printf '%-34s %-10s %-8s %s\n' MODULE VERDICT MEMORY NOTE
for m in "${mods[@]}"; do
  old="$RUN/mod/$m/lean/${m}_Lgraph.lean"
  norm="$RUN/mod/$m/lgdb_norm"
  if [[ ! -s "$old" || ! -d "$norm" ]]; then
    printf '%-34s %-10s %-8s %s\n' "$m" MISSING - "no stored certificate or lgdb_norm"
    missing=$((missing+1)); continue
  fi
  # Does this design have a memory?  The committed-array source is spelled
  # `memenc` on the legacy path and `SourceDesc.memImg` / `SourceDesc.romImg` in
  # a verified_compiler certificate, so test for all of them -- a predicate that
  # only matched the legacy spelling reported `no` for every D2 memory design
  # and would have made this sweep's central claim vacuous.
  mem=no; grep -qE 'memenc |SourceDesc\.(memImg|romImg)|Op_MemRead|Op_MemWrite' "$old" && mem=yes
  d="$OUT/$m"; rm -rf "$d"; mkdir -p "$d"
  timeout "$TIMEOUT" nice -n19 ionice -c3 "$LHD" compile "lg:$norm" --top "$m" \
    --workdir "$d/w" --emit-dir "lean:$d/lean" \
    --set formal.lean.strict=true --set formal.lean.normalize=true \
    --set formal.lean.emit_cert=true --set formal.lean.emit_fast_bridge=true \
    --set formal.lean.cert_wf=skip --set formal.lean.max_width=1048576 \
    --set formal.lean.mode="$LEAN_MODE" > "$d/emit.log" 2>&1
  rc=$?
  new="$d/lean/${m}_Lgraph.lean"
  if [[ "$rc" -ne 0 || ! -s "$new" ]]; then
    printf '%-34s %-10s %-8s %s\n' "$m" EMIT-FAIL "$mem" \
      "rc=$rc $(grep -oP '"code":"\K[^"]*' "$d/emit.log" | sort -u | paste -sd, -)"
    failed=$((failed+1)); continue
  fi
  [[ "$mem" == yes ]] && mem_seen=$((mem_seen+1)) || nonmem_seen=$((nonmem_seen+1))
  if cmp -s "$old" "$new"; then
    printf '%-34s %-10s %-8s %s\n' "$m" IDENTICAL "$mem" ""
    same=$((same+1))
  else
    note="$(diff <(wc -l < "$old") <(wc -l < "$new") >/dev/null && echo "same line count" || echo "line count differs")"
    if [[ "$mem" == no ]]; then
      note="REGRESSION: a design with no memory must not move -- $note"
      unexpected=$((unexpected+1))
    fi
    printf '%-34s %-10s %-8s %s\n' "$m" CHANGED "$mem" "$note"
    moved=$((moved+1))
  fi
done

echo
echo "identical=$same changed=$moved emit-failed=$failed missing=$missing of ${#mods[@]}"
echo "memory designs=$mem_seen  non-memory designs=$nonmem_seen  unexpected changes=$unexpected"

bad=0
# A CHANGED non-memory design is the regression this script exists to catch.
# Counting it and then exiting 0 anyway -- which this did -- made the whole
# sweep decorative.
if [[ "$unexpected" -ne 0 ]]; then
  echo "FAIL: $unexpected design(s) with no memory changed" >&2; bad=1
fi
[[ "$failed" -eq 0 ]]  || { echo "FAIL: $failed design(s) failed to re-emit" >&2; bad=1; }
[[ "$missing" -eq 0 ]] || { echo "FAIL: $missing design(s) had no stored certificate" >&2; bad=1; }
# ...and the classification must not be VACUOUS.  "no non-memory design moved"
# proves nothing over a corpus with no non-memory designs in it, and the
# memory/non-memory predicate itself is only exercised if both kinds appear.
if [[ "$mem_seen" -eq 0 || "$nonmem_seen" -eq 0 ]]; then
  echo "FAIL: the sweep saw $mem_seen memory and $nonmem_seen non-memory design(s);" \
       "it must see at least one of each or its classification says nothing" >&2
  bad=1
fi
[[ "$bad" -eq 0 ]] || exit 3
echo "PASS: every non-memory certificate is byte-identical"
