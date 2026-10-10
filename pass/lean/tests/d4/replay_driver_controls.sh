#!/usr/bin/env bash
# Controls for coreet_sext_replay.sh -- the EVIDENCE DRIVER, not the checker.
#
# WHY.  The first version of that driver checked only "no regression, bytes
# unchanged, nothing missing, n=83".  None of those is its advertised result.
# A checker that repaired NOTHING would have produced 82 of 83 passing and zero
# verdict changes, and the driver would still have exited 0 while printing
# "REPLAY OK: 82 of 83".  A green evidence driver that cannot fail is not
# evidence.
#
# Each control below drives the real driver over the real retained bytes with
# either a neutered checker or a doctored baseline, and requires it to FAIL.
# Nothing is re-run: no emitter, no lhd, no cgen, and the committed
# coreet_122_sext_replay.tsv is never written (every scenario passes its own
# output path).
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../../../.." && pwd)"
DRIVER="$HERE/coreet_sext_replay.sh"
GATE="$ROOT/pass/lean/scripts/cert_lgraph_diff.py"
BASE="$HERE/coreet_122_fresh_gates.tsv"

fails=0
ok()  { printf 'PASS  %s\n' "$1"; }
bad() { printf 'FAIL  %s\n' "$1"; fails=$((fails + 1)); }
for f in "$DRIVER" "$GATE" "$BASE" "$HERE/coreet_83_modules.txt"; do
  [[ -r "$f" ]] || { echo "FATAL: missing $f" >&2; exit 2; }
done
if [[ ! -d "$ROOT/temp/coreet_122_auth/blocks" || ! -d "$ROOT/temp/coreet_122_cgen" ]]; then
  echo "N/A   retained CORE-ET artifacts are not present; nothing to drive" >&2
  exit 0
fi

mkdir -p "$ROOT/temp"
TMP="$(mktemp -d "$ROOT/temp/replayctl.XXXXXX")"
trap 'rm -rf "$TMP"' EXIT

# Columns of the baseline: 1 module 2 emit_rc 3 vc_gates 4 cert_invariant
# 5 cert_differential 6 cert_sha256 7 verilog_sha256 8 lean_sha256 ...
doctor() {  # doctor <out> <awk-body>
  local out="$1"; shift
  awk -F'\t' -v OFS='\t' '/^#/ {print; next} $1=="module" {print; next} '"$1"' {print}' "$BASE" > "$out"
}

drive() {  # drive <label> <checker> <baseline> -> sets $out_txt, $out_rc
  local tag="$1" gate="$2" base="$3"
  out_txt="$(D4_CERT_DIFF="$gate" D4_REPLAY_BASELINE="$base" \
             "$DRIVER" "$TMP/$tag.tsv" 2>&1)"
  out_rc=$?
}

# --- 0. POSITIVE: the real checker over the real baseline ----------------
drive real "$GATE" "$BASE"
if [[ "$out_rc" -ne 0 ]]; then
  bad "the real replay no longer exits 0: $(grep -m1 'REPLAY FAILED' <<<"$out_txt")"
elif ! grep -q 'sole change vpu_tensorquant cert_differential:FAIL->PASS' <<<"$out_txt"; then
  bad "the real replay exits 0 without reporting the expected sole change"
else
  ok "real checker + real baseline: exits 0 and names the sole expected change"
fi

expect_reject() {  # expect_reject <label> <needle in the failure text>
  if [[ "$out_rc" -eq 0 ]]; then
    bad "$1: driver ACCEPTED"
  elif ! grep -q "$2" <<<"$out_txt"; then
    bad "$1: rejected for another reason -- $(grep -m1 'REPLAY FAILED' <<<"$out_txt")"
  else ok "$1"; fi
}

# --- 1. a checker that repairs NOTHING: 82 of 83 and zero changes --------
# The exact pre-repair behaviour, reproduced by neutering only the exemption.
sed 's/^def structural_sext_amounts(path):/def structural_sext_amounts(path):\n    return set()   # NEUTERED: pre-repair behaviour/' \
  "$GATE" > "$TMP/prefix_gate.py"
drive prefix "$TMP/prefix_gate.py" "$BASE"
if ! grep -q 'all three gates PASS     82' <<<"$out_txt"; then
  bad "pre-repair checker: expected 82 of 83 passing, got $(grep -m1 'all three gates PASS' <<<"$out_txt")"
else
  expect_reject "pre-repair checker (82 of 83, zero changes) is REJECTED" \
                '82 of 83 rows pass all three gates'
fi
grep -q 'verdict changes          0' <<<"$out_txt" \
  && ok "pre-repair checker produces zero verdict changes, and that too is rejected" \
  || bad "pre-repair checker did not produce zero verdict changes"

# --- 2. ZERO intended changes: baseline already says PASS ----------------
doctor "$TMP/b_zero.tsv" '$1=="vpu_tensorquant" {$5="PASS"; $10="-"}'
drive zero "$GATE" "$TMP/b_zero.tsv"
expect_reject "zero verdict changes is REJECTED" 'verdict delta has 0 row(s)'

# --- 3. the sole change is in the WRONG MODULE ---------------------------
doctor "$TMP/b_mod.tsv" '$1=="vpu_tensorquant" {$5="PASS"; $10="-"} $1=="intpipe_alu" {$5="FAIL"}'
drive mod "$GATE" "$TMP/b_mod.tsv"
expect_reject "a sole change in the WRONG MODULE is REJECTED" "the single verdict change is 'intpipe_alu"

# --- 4. the sole change is in the WRONG COLUMN ---------------------------
doctor "$TMP/b_col.tsv" '$1=="vpu_tensorquant" {$4="FAIL"; $5="PASS"; $10="-"}'
drive col "$GATE" "$TMP/b_col.tsv"
expect_reject "a sole change in the WRONG COLUMN is REJECTED" 'cert_invariant:FAIL->PASS'

# --- 5. an EXTRA change alongside the expected one -----------------------
doctor "$TMP/b_extra.tsv" '$1=="intpipe_alu" {$5="FAIL"}'
drive extra "$GATE" "$TMP/b_extra.tsv"
expect_reject "an EXTRA verdict change is REJECTED" 'verdict delta has 2 row(s)'

# --- 6. a retained artifact whose bytes no longer match ------------------
doctor "$TMP/b_hash.tsv" '$1=="intpipe_alu" {$6="deadbeefdeadbeef"}'
drive hash "$GATE" "$TMP/b_hash.tsv"
expect_reject "a retained artifact whose hash MOVED is REJECTED" 'changed bytes since the baseline'

# --- 7. a short baseline (fewer than 83 rows) ----------------------------
grep -v $'^vpu_trans\t' "$BASE" > "$TMP/b_short.tsv"
drive short "$GATE" "$TMP/b_short.tsv"
expect_reject "a baseline short of 83 rows is REJECTED" \
              'baseline declares 82 module rows, canonical manifest declares 83'

# --- 8. MODULE IDENTITY: the exact duplicate/omission substitution -------
# 83 rows, genuine hashes, the intended tensorquant delta -- and `vpu_trans`
# replaced by a second genuine `intpipe_alu`.  Counting rows cannot see this.
awk -F'\t' -v OFS='\t' '
  /^#/ {print; next} $1=="module" {print; next}
  $1=="intpipe_alu" {alu=$0; print; next}
  $1=="vpu_trans" {print alu; next}
  {print}' "$BASE" > "$TMP/b_dup.tsv"
rows="$(awk -F'\t' '$1!~/^#/ && $1!="module" && NF>1' "$TMP/b_dup.tsv" | wc -l)"
if [[ "$rows" -ne 83 ]]; then
  bad "the duplicate/omission fixture has $rows rows, not 83; it would not test identity"
else
  drive dup "$GATE" "$TMP/b_dup.tsv"
  if [[ "$out_rc" -eq 0 ]]; then
    bad "duplicate intpipe_alu + omitted vpu_trans (83 rows) was ACCEPTED"
  elif ! grep -q 'DUPLICATE module key(s): intpipe_alu' <<<"$out_txt" \
    || ! grep -q 'OMITS module(s): vpu_trans' <<<"$out_txt"; then
    bad "duplicate/omission rejected for another reason -- $(grep -m1 'REPLAY FAILED' <<<"$out_txt")"
  elif ! grep -q 'nothing was replayed' <<<"$out_txt"; then
    bad "duplicate/omission rejected only AFTER replaying; identity must be checked first"
  elif [[ -e "$TMP/dup.tsv" ]]; then
    bad "duplicate/omission wrote a replay table despite failing authentication"
  else
    ok "duplicate intpipe_alu + omitted vpu_trans (83 rows) is REJECTED before anything is replayed"
  fi
fi

# --- 9. MODULE IDENTITY: the same set, REORDERED -------------------------
awk -F'\t' -v OFS='\t' '
  /^#/ {print; next} $1=="module" {print; next}
  $1=="vpu_trans" {held=$0; next}
  {print}
  END {}' "$BASE" > "$TMP/b_ord.tsv"
awk -F'\t' '$1=="vpu_trans"' "$BASE" >> "$TMP/b_ord.tsv"   # same 83 keys, last
drive ord "$GATE" "$TMP/b_ord.tsv"
expect_reject "the same 83 keys in a DIFFERENT ORDER are REJECTED" \
              'module sequence diverges from the manifest at row'

# --- 10. MODULE IDENTITY: an UNKNOWN module key -------------------------
awk -F'\t' -v OFS='\t' '/^#/ {print; next} $1=="module" {print; next}
  $1=="vpu_trans" {$1="vpu_trans_v2"} {print}' "$BASE" > "$TMP/b_unk.tsv"
drive unk "$GATE" "$TMP/b_unk.tsv"
expect_reject "an UNKNOWN module key is REJECTED" 'UNKNOWN module(s): vpu_trans_v2'

[[ "$fails" -eq 0 ]] && { echo "ALL CONTROLS PASS"; exit 0; }
echo "$fails CONTROL(S) FAILED"; exit 1
