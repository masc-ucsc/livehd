#!/usr/bin/env bash
# What `pass.single_edge` says today about every block of the CANONICAL 122-name
# CORE-ET manifest, compared with the verdict that block's RETAINED baseline
# recorded.
#
# It re-runs ONLY the front-end pass, on each block's retained `lgdb_raw`. No
# emitter, no certificate, no Lean: this is not a corpus re-run and it does not
# touch any runtime table.
#
# THE MANIFEST IS ITERATED, not globbed, and every one of its names is accounted
# for. A block whose baseline recorded no `single_edge` diagnostic has nothing
# to compare against, so it produces no row -- but its identity is written into
# the header under `skipped_no_diagnostic`, because an identity that simply
# stops appearing is how coverage quietly shrinks. Rows + skipped must equal the
# manifest exactly, and the verifier requires it.
#
# FAIL CLOSED: a manifest name with no retained input, a duplicate, a retained
# block outside the manifest, an exit status other than 0 or 7, or a refusal
# with no diagnostic code all abort without publishing. The table is written to
# a temporary file and renamed only on success, with its provenance written BY
# THIS SCRIPT.
#
# Usage: coreet_frontend_refusal_probe.sh [out.tsv]
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../../../.." && pwd)"
# shellcheck source=d4_evidence_provenance.sh
. "$HERE/d4_evidence_provenance.sh" || { echo "FATAL: cannot source d4_evidence_provenance.sh" >&2; exit 2; }

# A COMMIT, not a binary: see the note at the top of `coreet_frontend_ab.sh`.
PROBE_COMMIT="${COMMIT:-HEAD}"
OUT="${1:-$HERE/coreet_frontend_refusals.tsv}"
MANIFEST="${MANIFEST:-$HERE/coreet_122.list}"
CORPUS="${CORPUS:-$ROOT/temp/coreet_122_auth/blocks}"
BAZEL_CONFIG="${BAZEL_CONFIG:--c dbg --repo_env=CC=/usr/bin/gcc-15 --repo_env=CXX=/usr/bin/g++-15}"

cd "$ROOT" || d4_die "cannot cd to $ROOT"
d4_require_clean_source "pass/lean/tests/d4"
[[ -r "$MANIFEST" ]] || d4_die "no manifest at $MANIFEST"
[[ -d "$CORPUS"   ]] || d4_die "no retained corpus at $CORPUS"

mapfile -t NAMES < <(grep -vE '^\s*(#|$)' "$MANIFEST")
COUNT="${#NAMES[@]}"
[[ "$COUNT" -gt 0 ]] || d4_die "manifest $MANIFEST names no blocks"
dups="$(printf '%s\n' "${NAMES[@]}" | sort | uniq -d | tr '\n' ' ')"
[[ -z "$dups" ]] || d4_die "manifest has duplicate identities: $dups"
for m in "${NAMES[@]}"; do
  [[ -d "$CORPUS/$m/lgdb_raw" ]] || d4_die "manifest names '$m' but $CORPUS/$m/lgdb_raw is not there"
done
extra="$(comm -13 <(printf '%s\n' "${NAMES[@]}" | sort) <(find "$CORPUS" -mindepth 1 -maxdepth 1 -type d -printf '%f\n' | sort) | tr '\n' ' ')"
[[ -z "$extra" ]] || d4_die "retained corpus holds blocks the manifest does not name: $extra"

CID="$(d4_corpus_identity "$CORPUS" "${NAMES[@]}")"

B_SOURCE_COMMIT="$(git rev-parse --verify --quiet "$PROBE_COMMIT^{commit}")" \
  || d4_die "commit '$PROBE_COMMIT' is not in this repository"
BINDIR="${D4_BINDING_DIR:-$ROOT/temp/d4_evidence_bin}"
mkdir -p "$BINDIR" || d4_die "cannot create $BINDIR"
B_BINDING="$BINDIR/$B_SOURCE_COMMIT.binding"
"$HERE/d4_build_binding.sh" build "$B_SOURCE_COMMIT" "$B_BINDING" $BAZEL_CONFIG >/dev/null \
  || d4_die "could not build the probe binary from $B_SOURCE_COMMIT"
d4_binding_validate "$B_BINDING"
export B_BINDING
LHD="$(sed -n 's/^retained *= *//p' "$B_BINDING" | head -1)"
d4_bin_check "$LHD" "LHD"
d4_require_binding "$LHD" "$B_BINDING" "$B_SOURCE_COMMIT" "$BAZEL_CONFIG" "LHD"
SB="$(d4_sha "$LHD")"
T="$ROOT/temp/frontend_probe_run"
rm -rf "$T"; mkdir -p "$T" || d4_die "cannot create $T"
TMP="$OUT.tmp.$$"
: > "$TMP" || d4_die "cannot write $TMP"
trap 'rm -f "$TMP" "$TMP.final"' EXIT

code_of() { grep -oE '"code":"[a-z0-9-]+"' "$1" | head -1 | cut -d'"' -f4; }
# A REFUSAL is an error-severity diagnostic, not merely a diagnostic: the pass
# also emits info-severity `single-edge-skipped` / `single-edge-applied` on
# blocks it handled perfectly well.
refused() { grep -q '"severity":"error"' "$1"; }

started="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
skipped=(); unusable=(); CONSUMED=(); n=0; nref=0; cleared=0; held=0; renamed=0; regressed=0; stillok=0
for m in "${NAMES[@]}"; do
  # An EMPTY retained input is its own fact, and a different one from "this
  # block carried no diagnostic". `minion_top`'s `lgdb_raw` holds no files at
  # all, so the pass cannot even read it; saying so by identity is the point.
  if [[ -z "$(find "$CORPUS/$m/lgdb_raw" -type f -print -quit 2>/dev/null)" ]]; then
    unusable+=("$m"); continue
  fi
  log="$CORPUS/$m/logs/single_edge.log"
  if [[ ! -r "$log" ]] || [[ -z "$(code_of "$log")" ]]; then
    skipped+=("$m"); continue
  fi
  base="$(code_of "$log")"
  [[ "$base" == "normalize-refused" ]] && base="$(grep -oE '"code":"[a-z0-9-]+"' "$log" | sed -n 2p | cut -d'"' -f4)"
  [[ -n "$base" ]] || d4_die "$m: baseline log has a diagnostic but no readable code"
  n=$((n + 1))
  CONSUMED+=("$m")
  if refused "$log"; then was=yes; nref=$((nref + 1)); else was=no; fi
  rm -rf "${T:?}/o" "${T:?}/w"
  "$LHD" pass single_edge --top "$m" "lg:$CORPUS/$m/lgdb_raw" --emit-dir "lg:$T/o" \
      --workdir "$T/w" >"$T/log" 2>&1
  rc=$?
  case "$rc" in
    0|7) ;;
    *) d4_die "$m: lhd pass single_edge exited $rc -- an infrastructure failure, not data (see $T/log)" ;;
  esac
  now="$(code_of "$T/log")"
  if [[ "$rc" -ne 0 ]]; then
    [[ -n "$now" ]] || d4_die "$m: refused with rc=$rc but emitted no diagnostic code"
  fi
  [[ -n "$now" ]] || now="-"
  if [[ "$was" == no ]]; then
    if [[ "$rc" -ne 0 ]]; then change=REGRESSED; regressed=$((regressed + 1));
    else change=STILL-OK; stillok=$((stillok + 1)); fi
  elif [[ "$rc" -eq 0 ]]; then
    change=CLEARED; cleared=$((cleared + 1))
  elif [[ "$now" == "$base" ]]; then
    change=UNCHANGED; held=$((held + 1))
  else
    change=RENAMED; renamed=$((renamed + 1))
  fi
  printf '%s\t%s\t%s\t%s\t%s\t%s\n' "$m" "$was" "$base" "$rc" "$now" "$change" >> "$TMP"
done

[[ "$(d4_sha "$LHD")" == "$SB" ]] || d4_die "the binary changed during the run"
rows="$(wc -l < "$TMP")"
[[ "$rows" -eq "$n" ]] || d4_die "produced $rows rows for $n probed blocks"
[[ $((rows + ${#skipped[@]} + ${#unusable[@]})) -eq "$COUNT" ]] \
  || d4_die "rows ($rows) + skipped (${#skipped[@]}) + unusable (${#unusable[@]}) != manifest ($COUNT); an identity went missing"

{
  d4_provenance_head "d4-frontend-classification/1" "" "$LHD" "$MANIFEST" "$CORPUS" "$COUNT" "$CID" "$BAZEL_CONFIG"
  echo "# started        = $started"
  echo "# finished       = $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "# rows           = $rows"
  echo "# baseline_identity = $(d4_baseline_identity "$CORPUS" "${CONSUMED[@]}")"
  echo "# skipped_count  = ${#skipped[@]}"
  printf '# skipped_no_diagnostic =%s\n' "${skipped[*]+ ${skipped[*]}}"
  echo "# input_unusable_count = ${#unusable[@]}"
  printf '# input_unusable =%s\n' "${unusable[*]+ ${unusable[*]}}"
  echo "# totals         = baseline_refusals=$nref cleared=$cleared unchanged=$held renamed=$renamed regressed=$regressed still-ok=$stillok"
  printf 'module\tbaseline_refused\tbaseline_code\tnow_rc\tnow_code\tchange\n'
  cat "$TMP"
} > "$TMP.final" || d4_die "cannot assemble the table"
# A table is a COMMITTED file, so it must not carry trailing whitespace -- a
# marker literal that ends in a space, or an empty identity list, would
# otherwise fail `git show --check` after the fact instead of here.
if grep -nE '[[:space:]]+$' "$TMP.final" >/dev/null 2>&1; then
  grep -nE '[[:space:]]+$' "$TMP.final" | head -3 >&2
  d4_die "the assembled table has trailing whitespace on the lines above"
fi
mv -f "$TMP.final" "$OUT" || d4_die "cannot publish $OUT"
rm -f "$TMP"; trap - EXIT

echo "wrote $OUT"
echo "# manifest names            $COUNT"
echo "# blocks probed             $rows"
echo "# skipped (no diagnostic)   ${#skipped[@]}"
echo "# retained input unusable   ${#unusable[@]}"
echo "# baseline REFUSALS (rc=7)  $nref"
echo "#   now NORMALIZE (rc 0)    $cleared"
echo "#   refuse, SAME code       $held"
echo "#   refuse, NEW named code  $renamed"
echo "# baseline non-refusals that now REFUSE   $regressed"
rc=0
if [[ "$regressed" -ne 0 ]]; then
  echo "REGRESSION: $regressed block(s) that used to pass the front end now refuse"
  rc=1
fi
[[ "$rc" -eq 0 ]] && echo "PROBE OK" || echo "PROBE FAILED"
exit "$rc"
