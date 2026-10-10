#!/usr/bin/env bash
# A/B the normalized VERILOG emission of every CORE-ET block in the CANONICAL
# 122-name manifest, under two EXPLICIT lhd binaries.
#
# Front end + cgen only: no certificate, no Lean, no runtime, no corpus rerun.
#
# FAIL CLOSED, EVERYWHERE. This script publishes a table that is later quoted as
# evidence, so every way of producing a table that looks fine while measuring
# less than it claims is an error, not a warning:
#
#   * the manifest is ITERATED -- a directory glob silently measures whatever
#     happens to be on disk, and a block that vanished would simply stop being
#     counted;
#   * a manifest name with no retained input, a duplicate name, and a retained
#     block outside the manifest are all fatal;
#   * an `lhd pass` exit status other than 0 or 7 is an infrastructure failure,
#     not data -- with ONE declared exception, below;
#
# THE DECLARED EXCEPTION. `minion_top`'s retained `lgdb_raw` is EMPTY, so the
# pass exits 4 with "holds no graphs". The previous, glob-based version of this
# script recorded that as an ordinary `BOTH-REFUSE-SAME` row: an unusable input
# counted as a refusal, inflating that total by one and leaving 121 blocks
# actually measured behind a table that said 122. It is now a verdict of its
# own, `INPUT-UNUSABLE`, excluded from every other total and DECLARED by
# identity in the header. It is not fatal, because the retained corpus is
# authenticated and not this script's to repair; it is not data either. Both
# sides must fail that way for the same reason, or it is fatal after all.
#   * a refusal MUST carry a diagnostic code, or there is nothing to compare;
#   * an acceptance MUST produce cgen rc=0 and at least one NON-EMPTY `.v`
#     before anything is hashed -- otherwise "identical emission" can mean
#     "both sides emitted nothing";
#   * both binaries are re-hashed at the end; a binary that moved mid-run
#     invalidates every row above it.
#
# Nothing is published unless all of that holds: the table is written to a
# temporary file and renamed only on success, with its provenance written BY
# THIS SCRIPT -- never pasted in afterwards.
#
# IT TAKES COMMITS, NOT BINARIES. Review got a 166-byte forwarding wrapper and a
# HAND-WRITTEN binding -- naming the right commit, the right tree, the right
# configuration, and the wrapper's own hash and size -- accepted by the producer
# and then by the verifier. Agreement among self-asserted objects is not
# authentication, and the only way to stop that channel is to close it: this
# script builds each side itself, from the complete tree of the commit it names,
# and measures with what came out. There is no caller-supplied executable and no
# caller-supplied attestation to forge.
#
# Usage: coreet_frontend_ab.sh <A-commit> <B-commit> <out.tsv>
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../../../.." && pwd)"
# shellcheck source=d4_evidence_provenance.sh
. "$HERE/d4_evidence_provenance.sh" || { echo "FATAL: cannot source d4_evidence_provenance.sh" >&2; exit 2; }

A_COMMIT_IN="${1:?usage: coreet_frontend_ab.sh <A-commit> <B-commit> <out.tsv>}"
B_COMMIT_IN="${2:?missing B commit}"
OUT="${3:?missing output table}"
MANIFEST="${MANIFEST:-$HERE/coreet_122.list}"
CORPUS="${CORPUS:-$ROOT/temp/coreet_122_auth/blocks}"
BAZEL_CONFIG="${BAZEL_CONFIG:--c dbg --repo_env=CC=/usr/bin/gcc-15 --repo_env=CXX=/usr/bin/g++-15}"

cd "$ROOT" || d4_die "cannot cd to $ROOT"
d4_require_clean_source "pass/lean/tests/d4"

[[ -r "$MANIFEST" ]] || d4_die "no manifest at $MANIFEST"
[[ -d "$CORPUS"   ]] || d4_die "no retained corpus at $CORPUS"



# ---- the canonical identity set ------------------------------------------
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

# ---- build BOTH sides here, from their own complete trees -----------------
A_SOURCE_COMMIT="$(git rev-parse --verify --quiet "$A_COMMIT_IN^{commit}")" \
  || d4_die "A commit '$A_COMMIT_IN' is not in this repository"
B_SOURCE_COMMIT="$(git rev-parse --verify --quiet "$B_COMMIT_IN^{commit}")" \
  || d4_die "B commit '$B_COMMIT_IN' is not in this repository"
[[ "$A_SOURCE_COMMIT" != "$B_SOURCE_COMMIT" ]] \
  || d4_die "both sides name $A_SOURCE_COMMIT; this table would compare nothing"
export A_SOURCE_COMMIT
BINDIR="${D4_BINDING_DIR:-$ROOT/temp/d4_evidence_bin}"
mkdir -p "$BINDIR" || d4_die "cannot create $BINDIR"
A_BINDING="$BINDIR/$A_SOURCE_COMMIT.binding"
B_BINDING="$BINDIR/$B_SOURCE_COMMIT.binding"
for side in A B; do
  c="$(eval echo \$${side}_SOURCE_COMMIT)"; att="$(eval echo \$${side}_BINDING)"
  "$HERE/d4_build_binding.sh" build "$c" "$att" $BAZEL_CONFIG >/dev/null \
    || d4_die "could not build side $side from $c"
  d4_binding_validate "$att"
done
export A_BINDING B_BINDING
A="$(sed -n 's/^retained *= *//p' "$A_BINDING" | head -1)"
B="$(sed -n 's/^retained *= *//p' "$B_BINDING" | head -1)"
d4_bin_check "$A" "A"
d4_bin_check "$B" "B"
SA="$(d4_sha "$A")"; SB="$(d4_sha "$B")"
[[ "$SA" != "$SB" ]] || d4_die "A and B are the same binary ($SA); this table would compare nothing"
d4_require_binding "$A" "$A_BINDING" "$A_SOURCE_COMMIT" "$BAZEL_CONFIG" "A"
d4_require_binding "$B" "$B_BINDING" "$B_SOURCE_COMMIT" "$BAZEL_CONFIG" "B"

# ---- markers that ACTUALLY discriminate -----------------------------------
# Asserted, not asserted-about. The correction only ADDED diagnostics, so every
# discriminating marker is "absent in A, present in B"; there is no removed
# string to point at and this file does not pretend otherwise. The last two
# entries are the control: one string both binaries must have, and one neither
# can, so a marker probe that always answered the same way would be caught.
MARKERS=()
while IFS= read -r line; do MARKERS+=("$line"); done < <(
  d4_marker_assert "$A" "$B" absent  present "gated-clock-latch-unsupported"
  d4_marker_assert "$A" "$B" absent  present "is clocked through a gate whose enable latch closes on the "
  d4_marker_assert "$A" "$B" absent  present "is clocked through a gate whose enable latch has a transparency window rooted at \`"
  d4_marker_assert "$A" "$B" present present "memory port gate(s) folded"
  d4_marker_assert "$A" "$B" absent  absent  "d4-marker-control-string-that-must-never-appear"
)
[[ "${#MARKERS[@]}" -eq 5 ]] || d4_die "marker assertions did not all run (got ${#MARKERS[@]} of 5)"

T="$ROOT/temp/frontend_ab_run"
rm -rf "$T"; mkdir -p "$T" || d4_die "cannot create $T"
TMP="$OUT.tmp.$$"
: > "$TMP" || d4_die "cannot write $TMP"
trap 'rm -f "$TMP"' EXIT

started="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
same=0; emis=0; acc=0; code=0; both=0; unusable=0; UNUSABLE=()

# run_side <binary> <block> <tag> -> sets rc_<tag>, code_<tag>, sha_<tag>
run_side() {
  local bin="$1" m="$2" tag="$3" rc dcode vdir vsha
  rm -rf "${T:?}/$tag"
  "$bin" pass single_edge --top "$m" "lg:$CORPUS/$m/lgdb_raw" \
      --emit-dir "lg:$T/$tag/lg" --workdir "$T/$tag/w" >"$T/$tag.log" 2>&1
  rc=$?
  case "$rc" in
    0|7) ;;
    4) grep -q "holds no graphs" "$T/$tag.log" \
         || d4_die "$m [$tag]: lhd pass single_edge exited 4 for a reason other than an empty input (see $T/$tag.log)" ;;
    *) d4_die "$m [$tag]: lhd pass single_edge exited $rc -- an infrastructure failure, not data (see $T/$tag.log)" ;;
  esac
  dcode="$(grep -oE '"code":"[a-z0-9-]+"' "$T/$tag.log" | head -1 | cut -d'"' -f4)"
  vsha="-"
  if [[ "$rc" -eq 4 ]]; then
    dcode="-"
  elif [[ "$rc" -eq 0 ]]; then
    [[ -n "$dcode" ]] || dcode="single-edge-applied"
    vdir="$T/$tag/v"
    rm -rf "$vdir"
    "$bin" compile "lg:$T/$tag/lg" --top "$m" --recipe O0 --emit-dir "verilog:$vdir" \
        --workdir "$T/$tag/vw" -q >"$T/$tag.emit.log" 2>&1 \
      || d4_die "$m [$tag]: cgen emission exited nonzero (see $T/$tag.emit.log)"
    local bytes
    bytes="$(cat "$vdir"/*.v 2>/dev/null | wc -c)"
    [[ "${bytes:-0}" -gt 0 ]] \
      || d4_die "$m [$tag]: the pass accepted the block but cgen produced no Verilog; an empty emission is not a measurement"
    vsha="$(cat "$vdir"/*.v | sha256sum | cut -c1-16)"
  else
    [[ -n "$dcode" ]] \
      || d4_die "$m [$tag]: refused with rc=$rc but emitted no diagnostic code; there is nothing to compare"
  fi
  printf -v "rc_$tag"   '%s' "$rc"
  printf -v "code_$tag" '%s' "$dcode"
  printf -v "sha_$tag"  '%s' "$vsha"
}

for m in "${NAMES[@]}"; do
  run_side "$A" "$m" a
  run_side "$B" "$m" b
  if [[ "$rc_a" -eq 4 || "$rc_b" -eq 4 ]]; then
    [[ "$rc_a" -eq 4 && "$rc_b" -eq 4 ]] \
      || d4_die "$m: one side found the retained input unusable and the other did not (a=$rc_a b=$rc_b)"
    v=INPUT-UNUSABLE; unusable=$((unusable+1)); UNUSABLE+=("$m")
  elif [[ "$rc_a" -ne 0 && "$rc_b" -ne 0 ]]; then
    if [[ "$code_a" == "$code_b" ]]; then v=BOTH-REFUSE-SAME; both=$((both+1));
    else v=REFUSAL-CODE-CHANGED; code=$((code+1)); fi
  elif [[ "$rc_a" -ne "$rc_b" ]]; then
    v=ACCEPTANCE-CHANGED; acc=$((acc+1))
  elif [[ "$sha_a" == "$sha_b" ]]; then
    v=IDENTICAL; same=$((same+1))
  else
    v=EMISSION-CHANGED; emis=$((emis+1))
  fi
  printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
      "$m" "$rc_a" "$rc_b" "$code_a" "$code_b" "$sha_a" "$sha_b" "$v" >> "$TMP"
done

# Nothing above is believable if either binary moved underneath it.
[[ "$(d4_sha "$A")" == "$SA" ]] || d4_die "binary A changed during the run"
[[ "$(d4_sha "$B")" == "$SB" ]] || d4_die "binary B changed during the run"

rows="$(wc -l < "$TMP")"
[[ "$rows" -eq "$COUNT" ]] || d4_die "produced $rows rows for $COUNT manifest names"

{
  d4_provenance_head "d4-frontend-ab/1" "$A" "$B" "$MANIFEST" "$CORPUS" "$COUNT" "$CID" "$BAZEL_CONFIG"
  printf '%s\n' "${MARKERS[@]}"
  echo "# started        = $started"
  echo "# finished       = $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "# rows           = $rows"
  echo "# input_unusable_count = $unusable"
  printf '# input_unusable =%s\n' "${UNUSABLE[*]+ ${UNUSABLE[*]}}"
  echo "# totals         = identical=$same both-refused=$both emission-changed=$emis acceptance-changed=$acc refusal-code-changed=$code input-unusable=$unusable"
  printf 'module\ta_rc\tb_rc\ta_code\tb_code\ta_sha\tb_sha\tverdict\n'
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
echo "wrote $OUT ($rows rows)"
echo "identical=$same both-refused=$both emission-changed=$emis acceptance-changed=$acc refusal-code-changed=$code input-unusable=$unusable"
