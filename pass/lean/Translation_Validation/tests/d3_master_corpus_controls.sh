#!/usr/bin/env bash
# Controls for pass/lean/Translation_Validation/scripts/d3_master_corpus.py, on real fixtures.
#
# WHY.  This script decides which certificates become a CORPUS, and a corpus is
# what a manifest pins and a sweep measures.  Two ways it can be wrong and look
# right.  It can collect a certificate whose own emission run REFUSED it -- a
# D4 run writes the file and then fails its static gates, so trusting the file
# system picked up 4 CORE-ET and 4 CVA6 certificates the run had rejected.  And
# it can collect a PRE-D2 certificate, which pins bytes that cannot typecheck
# here, surfacing eighty designs later as a verdict instead of here as a
# refusal.  Every leg below is one of those two, or the bookkeeping that keeps
# a corpus directory equal to what the script says is in it.
#
# Fixtures are synthetic and tiny: the gates read the emitted trailer and a
# rows.tsv column, so a two-line certificate exercises them exactly.
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../../../.." && pwd)"
TOOL="$ROOT/pass/lean/Translation_Validation/scripts/d3_master_corpus.py"
[[ -r "$TOOL" ]] || { echo "FATAL: missing $TOOL" >&2; exit 2; }

fails=0
ok()  { printf 'PASS  %s\n' "$1"; }
bad() { printf 'FAIL  %s\n' "$1"; fails=$((fails + 1)); }

mkdir -p "$ROOT/temp"
W="$(mktemp -d "$ROOT/temp/d3mcctl.XXXXXX")"
trap 'rm -rf "$W"' EXIT

# cert <path> <base> <era>   era: post | pre | noclocks
cert() {
  local path="$1" base="$2" era="$3"
  mkdir -p "$(dirname "$path")"
  {
    echo "def ${base}_designCert : DesignCert :="
    echo "  {"
    [[ "$era" != "noclocks" ]] && echo "    clocks := #[]"
    echo "  }"
    echo ""
    echo "def ${base}_step : RuntimeInput → RuntimeState → RuntimeResult :="
    if [[ "$era" == "pre" ]]; then
      echo "  compileAndRun ${base}_designCert"
    else
      echo "  compileAndRun ${base}_designCert (allEdges ${base}_designCert)"
    fi
  } > "$path"
}

# mkrun <dir> then rows are appended with row()
mkrun() { mkdir -p "$1/blocks"; printf 'corpus\tmodule\temit_status\tnodes\tsources\tflops\tmemories\n' > "$1/rows.tsv"; }
row()   { printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "c" "$2" "$3" "$4" "$5" "$6" "$7" >> "$1/rows.tsv"; }

LOG=""
RC=0
run() { # run <case> <expect-rc> <args...>
  local case_name="$1" want="$2"; shift 2
  LOG="$W/$case_name.log"
  python3 -I "$TOOL" "$@" > "$LOG" 2>&1
  RC=$?
  if [[ "$RC" -eq "$want" ]]; then ok "$case_name (exit $RC)"
  else bad "$case_name: exit $RC, wanted $want"; sed -n '1,8p' "$LOG" | sed 's/^/      /'; fi
}

# --- acceptance is established, never assumed ------------------------------
R="$W/no_rows"; mkdir -p "$R/blocks"
cert "$R/blocks/a/lean/a_Lgraph.lean" a post
run no-rows-tsv 2 --from "$R" --out "$W/out_no_rows"
grep -q 'no rows.tsv' "$LOG" || bad "no-rows-tsv: refusal does not name rows.tsv"
[[ -d "$W/out_no_rows/lean" ]] && bad "no-rows-tsv: created a corpus directory anyway"

R="$W/no_row_for_block"; mkrun "$R"; row "$R" x EMITTED 1 1 0 0
cert "$R/blocks/a/lean/a_Lgraph.lean" a post
run block-without-a-row 2 --from "$R" --out "$W/out_norow"
grep -q 'no row in' "$LOG" || bad "block-without-a-row: refusal does not say the row is missing"

# --- a certificate beside a FAILED emission is skipped, and recorded -------
R="$W/mixed"; mkrun "$R"
row "$R" good EMITTED 12 3 1 0
row "$R" bad  EMIT_FAILED - - - -
cert "$R/blocks/good/lean/good_Lgraph.lean" good post
cert "$R/blocks/bad/lean/bad_Lgraph.lean"   bad  post
run skips-unaccepted 0 --from "$R" --out "$W/out_mixed"
[[ -f "$W/out_mixed/lean/good_Lgraph.lean" ]] || bad "skips-unaccepted: dropped the accepted block"
[[ -f "$W/out_mixed/lean/bad_Lgraph.lean"  ]] && bad "skips-unaccepted: collected a refused emission"
grep -q 'skipped_row	bad	bad_Lgraph.lean	emit_status=EMIT_FAILED' "$W/out_mixed/PROVENANCE.tsv" \
  || bad "skips-unaccepted: the skip is not recorded with its reason"
grep -q '^module	nodes	sources	flops	mems$' "$W/out_mixed/emit.tsv" \
  || bad "skips-unaccepted: emit.tsv header is not the one d3_manifest.py reads"
grep -q '^good	12	3	1	0$' "$W/out_mixed/emit.tsv" \
  || bad "skips-unaccepted: emit.tsv lost the accepted block's counts"
SHA1=$(grep '^corpus_sha256' "$LOG" | awk '{print $2}')

# re-running is a no-op with the same digest
run rerun-identical 0 --from "$R" --out "$W/out_mixed"
grep -q '1 already identical' "$LOG" || bad "rerun-identical: did not recognise the existing file"
[[ "$(grep '^corpus_sha256' "$LOG" | awk '{print $2}')" == "$SHA1" ]] \
  || bad "rerun-identical: corpus digest is not stable"

# --- schema era ------------------------------------------------------------
R="$W/pre"; mkrun "$R"; row "$R" p EMITTED 1 1 0 0
cert "$R/blocks/p/lean/p_Lgraph.lean" p pre
run pre-d2-refused 2 --from "$R" --out "$W/out_pre"
grep -q 'pre-d2' "$LOG" || bad "pre-d2-refused: refusal does not name the era"

R="$W/noclk"; mkrun "$R"; row "$R" n EMITTED 1 1 0 0
cert "$R/blocks/n/lean/n_Lgraph.lean" n noclocks
run no-clocks-refused 2 --from "$R" --out "$W/out_noclk"
grep -q 'no clocks field' "$LOG" || bad "no-clocks-refused: refusal does not say what is missing"

# --- two runs offering the same certificate name ---------------------------
R1="$W/dup1"; mkrun "$R1"; row "$R1" d EMITTED 1 1 0 0
cert "$R1/blocks/d/lean/d_Lgraph.lean" d post
R2="$W/dup2"; mkrun "$R2"; row "$R2" d EMITTED 1 1 0 0
cert "$R2/blocks/d/lean/d_Lgraph.lean" d post
run duplicate-name 2 --from "$R1" --from "$R2" --out "$W/out_dup"
grep -q 'offered by both' "$LOG" || bad "duplicate-name: refusal does not name both sources"

# --- the destination is not silently rewritten -----------------------------
R="$W/change"; mkrun "$R"; row "$R" c EMITTED 1 1 0 0
cert "$R/blocks/c/lean/c_Lgraph.lean" c post
run change-first 0 --from "$R" --out "$W/out_change"
printf '\n-- a later emission\n' >> "$R/blocks/c/lean/c_Lgraph.lean"
run change-refused 2 --from "$R" --out "$W/out_change"
grep -q 'exists with different bytes' "$LOG" || bad "change-refused: refusal does not say why"
grep -q 're-generate every manifest' "$LOG" \
  || bad "change-refused: does not say the manifests pinned it"
run change-forced 0 --from "$R" --out "$W/out_change" --force
grep -q 'a later emission' "$W/out_change/lean/c_Lgraph.lean" \
  || bad "change-forced: --force did not replace the file"

# --- a certificate that is no longer collected must not linger -------------
cert "$W/out_change/lean/ghost_Lgraph.lean" ghost post
run stale-refused 2 --from "$R" --out "$W/out_change"
grep -q 'no longer' "$LOG" || bad "stale-refused: refusal does not name the leftovers"
run stale-removed 0 --from "$R" --out "$W/out_change" --force
[[ -e "$W/out_change/lean/ghost_Lgraph.lean" ]] && bad "stale-removed: the leftover survived --force"

if [[ "$fails" -eq 0 ]]; then
  echo "D3-MASTER-CORPUS CONTROLS OK"
  exit 0
fi
echo "D3-MASTER-CORPUS CONTROLS: $fails failure(s)"
exit 1
