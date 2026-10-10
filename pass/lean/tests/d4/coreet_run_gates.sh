#!/usr/bin/env bash
# Static + differential gates over EVERY candidate RAN row of a D4 runtime
# sweep, with the cgen Verilog BOUND to each block's own emitted `lgdb_norm`.
#
# Run-directory generic: the focused memory pilot keeps its own frozen script
# (`coreet_memory_pilot_gates.sh`) so that accepted evidence is not disturbed;
# this is the same three gates applied to a whole-corpus run.
#
#   vc_gates.py <lean> <top>                                   (static shape)
#   cert_lgraph_diff.py <lean> --quiet                         (invariant half)
#   cert_lgraph_diff.py <lean> --verilog <v> --require-verilog (REAL differential)
#
# `--require-verilog` is what stops an invariant-only run being reported as the
# full gate, and <v> is regenerated HERE from the block's own lgdb_norm rather
# than taken on trust.
#
# IT ALSO AUTHENTICATES IDENTITY.  A row's own `cert_sha256` is recomputed from
# the retained certificate and must match; a block whose bytes moved is reported
# and never counted, because a gate result that is not bound to the artifact the
# row describes proves nothing about that row.
#
# COVERAGE IS THE INTERSECTION.  A block counts only when its runtime row says
# PARSED / true / RAN *and* its hash matches *and* all three gates pass.
# Emission alone is not coverage; a runtime row alone is not coverage.
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../../../.." && pwd)"
LHD="${LHD:-$ROOT/bazel-bin/lhd/lhd}"
GATE_DIFF="$ROOT/pass/lean/scripts/cert_lgraph_diff.py"
GATE_VC="$ROOT/pass/lean/scripts/vc_gates.py"
RUN="${1:?usage: coreet_run_gates.sh <run-outdir> <gates.tsv> [cgen-retention-dir]}"
OUT="${2:?missing output gates.tsv}"
KEEP="${3:-}"

for f in "$LHD" "$GATE_DIFF" "$GATE_VC"; do
  [[ -r "$f" ]] || { echo "FATAL: missing $f" >&2; exit 2; }
done
[[ -s "$RUN/rows.tsv" ]] || { echo "FATAL: no rows at $RUN/rows.tsv" >&2; exit 2; }
[[ -z "$KEEP" ]] || mkdir -p "$KEEP"

mkdir -p "$ROOT/temp"
TMP="$(mktemp -d "$ROOT/temp/rungates.XXXXXX")"
trap 'rm -rf "$TMP"' EXIT

tmp="$(mktemp "$ROOT/temp/rung.XXXXXX")"
{
  echo "# Static + differential gates over a D4 CORE-ET runtime sweep."
  echo "# cert_differential uses cgen Verilog regenerated from each block's own lgdb_norm."
  echo "# cert_hash_matches_row recomputes the row's own cert_sha256 from the retained bytes."
  echo "# hash columns are sha256, FIRST 16 HEX."
  echo "# run        = $RUN"
  echo "# lhd_sha256 = $(sha256sum "$LHD" | cut -c1-16)   gate_sha256 = $(sha256sum "$GATE_DIFF" | cut -c1-16)"
  echo "# gated_at   = $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  printf 'module\temit_status\truntime_status\tvc_gates\tcert_invariant\tcert_differential\tcert_sha256\tverilog_sha256\tlean_sha256\tcert_hash_matches_row\tfirst_failure\n'
} > "$tmp"

cand=0; accepted=0; gatefail=0; hashfail=0
while IFS=$'\t' read -r corpus m emit cert_path cert_sha f6 f7 f8 f9 parse f11 compiles runst rest; do
  [[ "$corpus" == "corpus" || "$corpus" == \#* || -z "$m" ]] && continue
  # A CANDIDATE is a row that claims runtime coverage. Anything else is listed
  # with its status and never gated, so it can never be mistaken for coverage.
  if [[ "$emit" != "EMITTED" || "$parse" != "PARSED" || "$compiles" != "true" || "$runst" != "RAN" ]]; then
    printf '%s\t%s\t%s\t-\t-\t-\t-\t-\t-\t-\tnot a candidate RAN row\n' "$m" "$emit" "${runst:--}" >> "$tmp"
    continue
  fi
  cand=$((cand + 1))
  lean="$RUN/blocks/$m/lean/${m}_Lgraph.lean"
  cert="$RUN/blocks/$m/lean/${m}.dcert"
  norm="$RUN/blocks/$m/lgdb_norm"
  if [[ ! -s "$lean" || ! -s "$cert" || ! -d "$norm" ]]; then
    gatefail=$((gatefail + 1))
    printf '%s\t%s\t%s\t-\t-\t-\t-\t-\t-\t-\tretained artifact or lgdb_norm missing\n' "$m" "$emit" "$runst" >> "$tmp"
    continue
  fi
  full="$(sha256sum "$cert" | cut -d' ' -f1)"
  if [[ "$full" == "$cert_sha" ]]; then match=yes; else match=no; hashfail=$((hashfail + 1)); fi

  rm -rf "$TMP/v" "$TMP/w"
  if ! "$LHD" compile "lg:$norm" --top "$m" --recipe O0 --emit-dir "verilog:$TMP/v" \
        --workdir "$TMP/w" -q >"$TMP/cgen.log" 2>&1; then
    gatefail=$((gatefail + 1))
    printf '%s\t%s\t%s\t-\t-\t-\t%s\t-\t%s\t%s\tcgen from lgdb_norm failed\n' \
      "$m" "$emit" "$runst" "${full:0:16}" "$(sha256sum "$lean" | cut -c1-16)" "$match" >> "$tmp"
    continue
  fi
  cat "$TMP"/v/*.v > "$TMP/$m.v" 2>/dev/null
  if [[ ! -s "$TMP/$m.v" ]]; then
    gatefail=$((gatefail + 1))
    printf '%s\t%s\t%s\t-\t-\t-\t%s\t-\t%s\t%s\tcgen produced no verilog\n' \
      "$m" "$emit" "$runst" "${full:0:16}" "$(sha256sum "$lean" | cut -c1-16)" "$match" >> "$tmp"
    continue
  fi
  [[ -z "$KEEP" ]] || cp -f "$TMP/$m.v" "$KEEP/$m.v"

  python3 -B "$GATE_VC" "$lean" "$m" --quiet >/dev/null 2>&1 && vc=PASS || vc=FAIL
  python3 -B "$GATE_DIFF" "$lean" --quiet >/dev/null 2>&1 && inv=PASS || inv=FAIL
  dout="$(python3 -B "$GATE_DIFF" "$lean" --verilog "$TMP/$m.v" --require-verilog 2>&1)"
  if [[ $? -eq 0 ]]; then diff=PASS; first='-'
  else diff=FAIL; first="$(grep -m1 '^FAIL' <<<"$dout")"; [[ -z "$first" ]] && first="$(tail -n1 <<<"$dout")"; fi

  if [[ "$vc" == PASS && "$inv" == PASS && "$diff" == PASS && "$match" == yes ]]; then
    accepted=$((accepted + 1))
  elif [[ "$match" == yes ]]; then
    gatefail=$((gatefail + 1))
  fi
  printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "$m" "$emit" "$runst" "$vc" "$inv" "$diff" \
    "${full:0:16}" "$(sha256sum "$TMP/$m.v" | cut -c1-16)" "$(sha256sum "$lean" | cut -c1-16)" "$match" "$first" >> "$tmp"
done < "$RUN/rows.tsv"

declared="$(( $(wc -l < "$RUN/rows.tsv") - 1 ))"
{
  echo "#"
  echo "# declared rows                $declared"
  echo "# candidate RAN rows           $cand"
  echo "# ACCEPTED (row + hash + all three gates)   $accepted"
  echo "# gate failures                $gatefail"
  echo "# certificate hash mismatches  $hashfail"
} >> "$tmp"
# The table is written BEFORE the verdict so a failing run still leaves its
# evidence on disk -- the per-block reasons are the point of the failure.
mv -f "$tmp" "$OUT"
echo "wrote $OUT"
tail -n 6 "$OUT"
awk -F'\t' '$1!~/^#/ && $1!="module" && $11!="-" && $11!="not a candidate RAN row"{printf "  %-32s %s\n",$1,$11}' "$OUT"

# FAIL CLOSED ON THE RESULT, not merely on hashes. An acceptance driver that
# exits 0 while reporting gate failures is not an acceptance driver: a caller
# that checks the exit status would read a corpus with a broken differential as
# a clean run. Every way the set of accepted blocks can be smaller than the set
# of candidates is a nonzero exit, and so is having no candidates at all --
# "0 of 0 accepted" is an empty measurement, never a pass.
rc=0
fail() { echo "GATES FAILED: $*"; rc=1; }
[[ "$hashfail" -eq 0 ]] || fail "$hashfail certificate hash mismatch(es): a gate result not bound to the row's own artifact proves nothing about that row"
[[ "$gatefail" -eq 0 ]] || fail "$gatefail candidate row(s) failed cgen, an artifact check, or a semantic gate"
[[ "$cand" -gt 0 ]]     || fail "no candidate RAN rows at all; there is nothing to accept and nothing was verified"
[[ "$accepted" -eq "$cand" ]] || fail "$accepted of $cand candidate rows accepted"
if [[ "$rc" -ne 0 ]]; then
  echo "(the per-block table above is retained at $OUT)"
  exit "$rc"
fi
echo "GATES OK: $accepted of $cand candidate rows accepted; 0 gate failures, 0 hash mismatches"
