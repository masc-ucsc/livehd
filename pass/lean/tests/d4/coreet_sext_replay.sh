#!/usr/bin/env bash
# FOCUSED REPLAY of the static + differential gates over the 83 RETAINED
# artifacts of the fresh CORE-ET 122 run, after the structural Op_Sext amount
# exemption was added to cert_lgraph_diff.py.
#
# NOTHING IS RE-RUN.  No emitter, no lhd, no cgen.  The certificates, the Lean
# files and the cgen Verilog are the same bytes the fresh baseline retained --
# this script recomputes their hashes and refuses to proceed on any artifact
# whose hash moved.  Only the CHECKER changed, so only the checker is replayed.
#
# IT AUTHENTICATES MODULE IDENTITY BEFORE IT REPLAYS ANYTHING.  Counting rows
# is not the same as replaying 83 distinct modules: a baseline that duplicates
# `intpipe_alu` and omits `vpu_trans` still has 83 rows, genuine hashes and the
# intended tensorquant delta, and an earlier version of this driver reported
# "REPLAY OK: 83 of 83" for it.  The expected module keys now live in
# `coreet_83_modules.txt`, which is NOT overridable from the environment, and
# the consumed baseline must match that manifest as an EXACT SEQUENCE --
# duplicate, omitted, unknown or reordered keys are rejected before any verdict
# is taken.
#
# IT IS FAIL-CLOSED ON ITS OWN ADVERTISED RESULT.  An earlier version checked
# only "no regression, bytes unchanged, nothing missing, n=83", so a checker
# that repaired NOTHING would still have exited 0 while printing
# "REPLAY OK: 82 of 83".  The expected outcome is now stated exactly and
# enforced: all 83 rows pass all three gates, and the verdict delta is EXACTLY
# one row -- vpu_tensorquant, cert_differential, FAIL -> PASS -- with its
# vc_gates and cert_invariant columns unchanged.  Zero changes, a change in
# another module, a change in another column, an extra change, or 82/83 all
# exit nonzero.
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../../../.." && pwd)"

# The expected outcome.  Deliberately hard-coded: it IS the claim.
EXPECT_N=83
EXPECT_MODULE=vpu_tensorquant
EXPECT_DELTA='cert_differential:FAIL->PASS'

# Overridable only so the negative control can drive this script with a
# neutered checker or a doctored baseline.  Both default to the real thing.
GATE_DIFF="${D4_CERT_DIFF:-$ROOT/pass/lean/scripts/cert_lgraph_diff.py}"
BASE="${D4_REPLAY_BASELINE:-$HERE/coreet_122_fresh_gates.tsv}"
GATE_VC="$ROOT/pass/lean/scripts/vc_gates.py"
# NOT overridable: this file IS the identity claim.  If a doctored baseline
# could also restate which modules were replayed, authenticating it would be
# circular.
MANIFEST="$HERE/coreet_83_modules.txt"
AUTH="${COREET_AUTH_DIR:-$ROOT/temp/coreet_122_auth/blocks}"
CGEN="${COREET_CGEN_DIR:-$ROOT/temp/coreet_122_cgen}"
OUT="${1:-$HERE/coreet_122_sext_replay.tsv}"

for f in "$GATE_DIFF" "$GATE_VC" "$BASE" "$MANIFEST"; do
  [[ -r "$f" ]] || { echo "FATAL: missing $f" >&2; exit 2; }
done
[[ -d "$AUTH" && -d "$CGEN" ]] || { echo "FATAL: retained artifacts not present ($AUTH / $CGEN)" >&2; exit 2; }

h16() { sha256sum "$1" | cut -c1-16; }

# ---------------------------------------------------------------------------
# MODULE IDENTITY, authenticated before a single gate is run.
# ---------------------------------------------------------------------------
mapfile -t expect_mods < <(grep -v '^[[:space:]]*#' "$MANIFEST" | sed '/^[[:space:]]*$/d')
mapfile -t base_mods   < <(awk -F'\t' '$1!~/^#/ && $1!="module" && NF>1 {print $1}' "$BASE")

ident_fail=0
iwarn() { echo "REPLAY FAILED: $*"; ident_fail=1; }

# The manifest must itself be the claim it is named for.
if [[ "${#expect_mods[@]}" -ne "$EXPECT_N" ]]; then
  iwarn "manifest $(basename "$MANIFEST") holds ${#expect_mods[@]} modules, expected $EXPECT_N"
fi
mdup="$(printf '%s\n' "${expect_mods[@]}" | sort | uniq -d | paste -sd' ' -)"
[[ -z "$mdup" ]] || iwarn "manifest itself contains duplicate module key(s): $mdup"

if [[ "${#base_mods[@]}" -ne "${#expect_mods[@]}" ]]; then
  iwarn "baseline declares ${#base_mods[@]} module rows, canonical manifest declares ${#expect_mods[@]}"
fi
dupes="$(printf '%s\n' "${base_mods[@]}" | sort | uniq -d | paste -sd' ' -)"
[[ -z "$dupes" ]] || iwarn "baseline contains DUPLICATE module key(s): $dupes"
omitted="$(comm -23 <(printf '%s\n' "${expect_mods[@]}" | sort -u) \
                    <(printf '%s\n' "${base_mods[@]}" | sort -u) | paste -sd' ' -)"
[[ -z "$omitted" ]] || iwarn "baseline OMITS module(s): $omitted"
unknown="$(comm -13 <(printf '%s\n' "${expect_mods[@]}" | sort -u) \
                    <(printf '%s\n' "${base_mods[@]}" | sort -u) | paste -sd' ' -)"
[[ -z "$unknown" ]] || iwarn "baseline contains UNKNOWN module(s): $unknown"

# Exact sequence, not merely the same set: the replay table is read positionally
# by every downstream comparison, so ORDER is part of the identity.
if [[ "${#base_mods[@]}" -eq "${#expect_mods[@]}" && -z "$dupes$omitted$unknown" ]]; then
  for ((i = 0; i < ${#expect_mods[@]}; i++)); do
    if [[ "${base_mods[i]}" != "${expect_mods[i]}" ]]; then
      iwarn "baseline module sequence diverges from the manifest at row $((i + 1)): got '${base_mods[i]}', expected '${expect_mods[i]}'"
      break
    fi
  done
fi

if [[ "$ident_fail" -ne 0 ]]; then
  echo "REPLAY ABORTED: module identity not authenticated; nothing was replayed." >&2
  exit 2
fi

tmp="$(mktemp "$ROOT/temp/sextreplay.XXXXXX")"
{
  echo "# FOCUSED REPLAY of coreet_122_fresh_gates.tsv after the structural Op_Sext"
  echo "# amount exemption.  Same retained bytes; only cert_lgraph_diff.py changed."
  echo "# No emitter, lhd or cgen was run.  hash columns are sha256, FIRST 16 HEX."
  echo "# replay_checker_sha256 = $(sha256sum "$GATE_DIFF" | cut -d' ' -f1)"
  echo "# replay_baseline       = $(basename "$BASE")"
  echo "# module_manifest       = $(basename "$MANIFEST") ($EXPECT_N keys, sha256 $(sha256sum "$MANIFEST" | cut -c1-16))"
  echo "# replayed_at           = $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "# expected delta        = exactly 1 row: $EXPECT_MODULE $EXPECT_DELTA"
  printf 'module\tvc_gates\tcert_invariant\tcert_differential\tcert_sha256\tverilog_sha256\tlean_sha256\tbytes_unchanged\tdelta\tfirst_failure\n'
} > "$tmp"

n=0; moved=0; missing=0
while IFS=$'\t' read -r m emit_rc b_vc b_inv b_diff b_cert b_ver b_lean b_match b_first; do
  [[ "$m" == \#* || -z "$m" || "$m" == "module" ]] && continue
  n=$((n + 1))
  lean="$AUTH/$m/lean/${m}_Lgraph.lean"
  cert="$AUTH/$m/lean/${m}.dcert"
  ver="$CGEN/$m.v"
  if [[ ! -s "$lean" || ! -s "$cert" || ! -s "$ver" ]]; then
    missing=$((missing + 1))
    printf '%s\t-\t-\t-\t-\t-\t-\tMISSING\tNOT-REPLAYED\tretained artifact absent\n' "$m" >> "$tmp"
    continue
  fi
  c_cert="$(h16 "$cert")"; c_ver="$(h16 "$ver")"; c_lean="$(h16 "$lean")"
  if [[ "$c_cert" != "$b_cert" || "$c_ver" != "$b_ver" || "$c_lean" != "$b_lean" ]]; then
    moved=$((moved + 1)); unchanged=no
  else unchanged=yes; fi

  python3 -B "$GATE_VC" "$lean" "$m" --quiet >/dev/null 2>&1 && r_vc=PASS || r_vc=FAIL
  python3 -B "$GATE_DIFF" "$lean" --quiet >/dev/null 2>&1 && r_inv=PASS || r_inv=FAIL
  dout="$(python3 -B "$GATE_DIFF" "$lean" --verilog "$ver" --require-verilog 2>&1)"
  if [[ $? -eq 0 ]]; then r_diff=PASS; first='-'
  else r_diff=FAIL; first="$(grep -m1 '^FAIL' <<<"$dout")"; [[ -z "$first" ]] && first="$(tail -n1 <<<"$dout")"; fi

  # The delta names the COLUMN and the direction, so a change in the wrong
  # column or the wrong direction cannot be mistaken for the expected one.
  delta=""
  [[ "$r_vc"   != "$b_vc"   ]] && delta="${delta}${delta:+;}vc_gates:$b_vc->$r_vc"
  [[ "$r_inv"  != "$b_inv"  ]] && delta="${delta}${delta:+;}cert_invariant:$b_inv->$r_inv"
  [[ "$r_diff" != "$b_diff" ]] && delta="${delta}${delta:+;}cert_differential:$b_diff->$r_diff"
  [[ -z "$delta" ]] && delta="same"

  printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
    "$m" "$r_vc" "$r_inv" "$r_diff" "$c_cert" "$c_ver" "$c_lean" "$unchanged" "$delta" "$first" >> "$tmp"
done < "$BASE"

pass_all="$(awk -F'\t' '$1!~/^#/ && $1!="module" && $2=="PASS" && $3=="PASS" && $4=="PASS"' "$tmp" | wc -l)"
changed_rows="$(awk -F'\t' '$1!~/^#/ && $1!="module" && $9!="same" && $9!="delta" {print $1"\t"$9}' "$tmp")"
n_changed="$(printf '%s' "$changed_rows" | grep -c . || true)"

{
  echo "#"
  echo "# replayed                 $n of $EXPECT_N retained artifacts"
  echo "# all three gates PASS     $pass_all"
  echo "# retained bytes unchanged $((n - moved - missing)) of $n   (moved: $moved, missing: $missing)"
  echo "# verdict changes          $n_changed"
} >> "$tmp"

mv -f "$tmp" "$OUT"
echo "wrote $OUT"
tail -n 5 "$OUT"
[[ -n "$changed_rows" ]] && { echo "changed rows:"; sed 's/^/  /' <<<"$changed_rows"; }

rc=0
fail() { echo "REPLAY FAILED: $*"; rc=1; }
n_uniq="$(awk -F'\t' '$1!~/^#/ && $1!="module" && NF>1 {print $1}' "$OUT" | sort -u | wc -l)"
[[ "$n" -eq "$EXPECT_N" ]]      || fail "replayed $n, expected $EXPECT_N"
[[ "$n_uniq" -eq "$EXPECT_N" ]] || fail "replay table holds $n_uniq distinct modules, expected $EXPECT_N"
[[ "$missing" -eq 0 ]]          || fail "$missing retained artifact(s) absent"
[[ "$moved" -eq 0 ]]            || fail "$moved artifact(s) changed bytes since the baseline"
[[ "$pass_all" -eq "$EXPECT_N" ]] || fail "$pass_all of $EXPECT_N rows pass all three gates, expected $EXPECT_N"
if [[ "$n_changed" -ne 1 ]]; then
  fail "verdict delta has $n_changed row(s), expected exactly 1"
elif [[ "$changed_rows" != "$EXPECT_MODULE	$EXPECT_DELTA" ]]; then
  fail "the single verdict change is '$changed_rows', expected '$EXPECT_MODULE	$EXPECT_DELTA'"
fi

if [[ "$rc" -eq 0 ]]; then
  echo "REPLAY OK: $n_uniq distinct authenticated modules; $pass_all of $n pass all three gates; sole change $EXPECT_MODULE $EXPECT_DELTA"
fi
exit $rc
