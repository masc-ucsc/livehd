#!/usr/bin/env bash
# Produce the authenticated A/B certificate-churn table for ONE adjacent CVA6
# revision pair on `csr_regfile`.
#
# This answers `DIRECTION4_INCREMENTAL.md` section 6 item 6 -- "how big is a
# typical delta?" -- for ONE edit.  It measures certificate/graph churn.  It
# does NOT make Option A or the loader incremental, does not establish a
# population distribution, and does not turn empirical validation into a proof.
#
# THE ONLY EXPERIMENTAL VARIABLE MAY BE A VERSUS B.  Everything else is pinned,
# CHECKED, and recorded: one `lhd` binary, one wrapper corpus, one config
# package, one path-normalized filelist, one wrapper generator, one target, one
# module list, one set of limits.  Recording alone is not enough -- the first
# version of this script PRINTED the two wrapper hashes without comparing them,
# and published happily when one was replaced with `deadbeef`.
#
# GATE TABLES ARE ARGUMENTS, NOT GUESSES.  The first version derived them as
# `$(dirname RUN)/gates.tsv` and accepted any PASS row.  In the real
# finalization that silently picked up the FIRST pass's tables -- naming run
# `out` and binary `5209b4af` -- while the measurement was of `out2` under
# `9942dc1d`.  The gate evidence was real but it was evidence about a different
# run.  Each side's table is now named explicitly and must prove it is about
# THIS side: its run path, its `lhd` hash, its gate-tool hash and its row's
# certificate hash are all checked against that side's provenance and against
# the retained bytes being measured.
#
# Publication is fully pre-validated: every assertion runs against the candidate
# in $TMP, and the `mv` into place is the last fallible operation.
#
# Usage: cva6_csr_regfile_dg.sh <A-run> <A-gates.tsv> <B-run> <B-gates.tsv> <out.tsv>
set -uo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../../../../.." && pwd)"
DELTA="${D4_DG_TOOL:-$SCRIPT_DIR/dcert_delta.py}"
PY="${PYTHON:-python3}"
MOD=csr_regfile
GATE="${MOD}_gate"
die() { printf 'FATAL: %s\n' "$*" >&2; exit 2; }

A_RUN="${1:-}"; A_GATES="${2:-}"; B_RUN="${3:-}"; B_GATES="${4:-}"; OUT="${5:-}"
[[ -n "$A_RUN" && -n "$A_GATES" && -n "$B_RUN" && -n "$B_GATES" && -n "$OUT" ]] \
  || die "usage: $0 <A-run> <A-gates.tsv> <B-run> <B-gates.tsv> <out.tsv>"

# ---- the destination contract, BEFORE any work ----------------------------
case "$OUT" in */) die "destination $OUT is a directory path";; esac
[[ -d "$OUT" ]] && die "destination $OUT is a directory"
OUTDIR="$(dirname "$OUT")"
[[ -d "$OUTDIR" && -w "$OUTDIR" ]] || die "destination directory $OUTDIR is not a writable directory"
[[ -r "$DELTA" ]] || die "missing delta analyzer $DELTA"
rm -f "$OUT" || die "cannot clear the previous table at $OUT"

sha() { sha256sum < "$1" | cut -d' ' -f1; }
field() { grep -m1 "^$2=" "$1" | cut -d= -f2-; }
hdr() { grep -m1 "^# $2 *=" "$1" | sed 's/^[^=]*= *//' | sed 's/ *$//'; }

for side in A B; do
  eval "RUN=\$${side}_RUN; G=\$${side}_GATES"
  [[ -d "$RUN" ]] || die "$side run dir $RUN is not a directory"
  [[ -s "$G" ]] || die "$side gate table $G is missing or empty"
  for f in rows.tsv provenance.txt "blocks/$MOD/lean/$GATE.dcert"; do
    [[ -s "$RUN/$f" ]] || die "$side run $RUN is missing $f"
  done
  eval "${side}_ABS=\"\$(cd \"\$RUN\" && pwd)\""
done
A_CERT="$A_RUN/blocks/$MOD/lean/$GATE.dcert"
B_CERT="$B_RUN/blocks/$MOD/lean/$GATE.dcert"
A_PROV="$A_RUN/provenance.txt"
B_PROV="$B_RUN/provenance.txt"

# ---- both sides must be INDEPENDENTLY valid -------------------------------
# A failed side is a failed experiment, not a 100 % delta.
for side in A B; do
  eval "RUN=\$${side}_RUN; G=\$${side}_GATES; ABS=\$${side}_ABS; PROV=\$${side}_PROV; CERT=\$${side}_CERT"
  row="$(awk -F'\t' -v m="$MOD" 'NR>1 && $2==m' "$RUN/rows.tsv")"
  [[ -n "$row" ]] || die "$side has no $MOD row"
  es="$(cut -f3 <<<"$row")"; ps="$(cut -f10 <<<"$row")"
  ok="$(cut -f12 <<<"$row")"; rs="$(cut -f13 <<<"$row")"
  [[ "$es" == EMITTED && "$ps" == PARSED && "$ok" == true && "$rs" == RAN ]] \
    || die "$side $MOD did not independently pass: emit=$es parse=$ps compilesOk=$ok runtime=$rs
       A failed side is a failed experiment, not a Delta=100% row."
  row_sha="$(cut -f5 <<<"$row")"
  cert_sha="$(sha "$CERT")"
  [[ "$row_sha" == "$cert_sha" ]] \
    || die "$side row cert_sha256 $row_sha does not match the retained bytes at $CERT"
  eval "${side}_CERT_SHA=\"\$cert_sha\""
  eval "${side}_DIGEST=\"\$(cut -f15 <<<\"\$row\")\""

  # --- the gate table must be ABOUT THIS RUN, not merely a passing table ---
  g_run="$(hdr "$G" run)"
  [[ -n "$g_run" ]] || die "$side gate table $G has no '# run' line; it predates the explicit binding"
  [[ "$g_run" == "$ABS" ]] || die "$side gate table $G is about a different run:
       table: $g_run
       this run: $ABS
       A gate result that is not bound to the artifact being measured proves nothing about it."
  g_lhd="$(hdr "$G" lhd_sha256)"
  p_lhd="$(field "$PROV" lhd_sha256)"
  [[ -n "$g_lhd" && ${#g_lhd} -eq 64 ]] || die "$side gate table $G does not record a full lhd_sha256"
  [[ "$g_lhd" == "$p_lhd" ]] || die "$side gate table was produced with lhd $g_lhd, but the run records $p_lhd"
  g_tool="$(hdr "$G" gate_tool_sha256)"
  [[ -n "$g_tool" && ${#g_tool} -eq 64 ]] || die "$side gate table $G does not record a full gate_tool_sha256"
  eval "${side}_GTOOL=\"\$g_tool\""

  g_row="$(awk -F'\t' -v m="$MOD" '$1==m' "$G")"
  [[ -n "$g_row" ]] || die "$side gate table $G has no $MOD row"
  IFS=$'\t' read -r _m _e _r vc inv dif g_cert _v _l hmatch _f <<<"$g_row"
  [[ "$vc" == PASS && "$inv" == PASS && "$dif" == PASS ]] \
    || die "$side $MOD did not pass all three gates (shape=$vc invariant=$inv differential=$dif)"
  [[ "$hmatch" == yes ]] || die "$side gate table reports cert_hash_matches_row=$hmatch"
  [[ "$g_cert" == "$cert_sha" ]] \
    || die "$side gate table gated a different certificate:
       gated: $g_cert
       measured: $cert_sha"
done
[[ "$A_GTOOL" == "$B_GTOOL" ]] || die "the two sides were gated by different gate tools:
       A: $A_GTOOL
       B: $B_GTOOL"

# ---- one binary, one generator, one target, one recipe --------------------
same() {  # same <label> <key>
  local a b; a="$(field "$A_PROV" "$2")"; b="$(field "$B_PROV" "$2")"
  [[ -n "$a" && "$a" == "$b" ]] || die "the two sides disagree on $1 ($2):
       A: ${a:-<absent>}
       B: ${b:-<absent>}
       The only experimental variable may be A versus B."
  printf '%s' "$a"
}
LHD_SHA="$(same "the lhd binary"      lhd_sha256)"       || exit 2
TARGET="$(same "the CVA6 target"      cva6_target)"      || exit 2
MODLIST="$(same "the module list"     module_list_sha256)" || exit 2
CFGPKG="$(same "the config package"   cva6_config_pkg_sha256)" || exit 2
STACK="$(same "the stack limit"       stack_limit_kb)"   || exit 2
ETMO="$(same "the emission timeout"   emit_timeout_s)"   || exit 2
RTMO="$(same "the run timeout"        run_timeout_s)"    || exit 2
ECEIL="$(same "the emission ceiling"  emit_max_rss_kb)"  || exit 2
JOBS="$(same "the job count"          jobs_requested)"   || exit 2

# The wrapper corpus is an INPUT.  Equal wrappers are what licence calling the
# RTL revision the only variable, so this is ENFORCED, not merely printed.
WRAP="$(same "the wrapper corpus" cva6_wrappers_declared_sha256)" || exit 2
[[ ${#WRAP} -ge 32 ]] || die "the wrapper corpus hash is implausibly short: $WRAP"

# The generator that MADE those wrappers lives in the foundation manifest, not
# in the sweep provenance.  Equal wrapper bytes from two different generators
# would still be two different experiments.
A_FND="$(field "$A_PROV" cva6_foundation)"; B_FND="$(field "$B_PROV" cva6_foundation)"
[[ -s "$A_FND" && -s "$B_FND" ]] || die "a side does not name a readable foundation manifest"
A_WGEN="$(field "$A_FND" wrapper_generator_sha256)"
B_WGEN="$(field "$B_FND" wrapper_generator_sha256)"
[[ -n "$A_WGEN" && "$A_WGEN" == "$B_WGEN" ]] \
  || die "the two sides used different wrapper generators:
       A: ${A_WGEN:-<absent>}
       B: ${B_WGEN:-<absent>}"
WRAPGEN="$A_WGEN"

A_REV="$(field "$A_PROV" cva6_root_rev)"
B_REV="$(field "$B_PROV" cva6_root_rev)"
[[ -n "$A_REV" && -n "$B_REV" ]] || die "a provenance file does not name its CVA6 revision"
[[ "$A_REV" != "$B_REV" ]] || die "both sides name the same CVA6 revision $A_REV; there is no delta to measure"

A_SRC="$(field "$A_PROV" cva6_root)"
B_SRC="$(field "$B_PROV" cva6_root)"
# The retained roots must STILL be what the provenance says they were.  A
# revision alone is not a source state: a dirty tree at R is not R.
for side in A B; do
  eval "SRC=\$${side}_SRC; REV=\$${side}_REV"
  [[ -d "$SRC" ]] || die "$side retained root $SRC is gone; the experiment cannot be re-checked"
  now="$(git -C "$SRC" rev-parse HEAD 2>/dev/null || true)"
  [[ "$now" == "$REV" ]] || die "$side retained root $SRC is at ${now:-<not a checkout>}, not the recorded $REV"
  dirty="$(git -C "$SRC" status --porcelain --untracked-files=no 2>/dev/null)"
  [[ -z "$dirty" ]] || die "$side retained root $SRC has TRACKED modifications; a dirty tree at $REV is not $REV"
  eval "${side}_CLEAN=yes"
done

PARENT="$(git -C "$B_SRC" rev-parse "$B_REV^" 2>/dev/null || true)"
[[ "$PARENT" == "$A_REV" ]] || die "B^ is ${PARENT:-<unknown>}, not A ($A_REV); these are not adjacent revisions"

# The two filelists differ ONLY by the source path they point into.  Comparing
# raw hashes would always fail; not comparing them at all would let a different
# file set through.  So: normalize each side's own root away, then compare.
A_FL="$(field "$A_PROV" cva6_filelist)"
B_FL="$(field "$B_PROV" cva6_filelist)"
[[ -s "$A_FL" && -s "$B_FL" ]] || die "a recorded filelist is missing ($A_FL / $B_FL)"
A_FLN="$(sed "s#$A_SRC#@ROOT@#g" "$A_FL" | sha256sum | cut -d' ' -f1)"
B_FLN="$(sed "s#$B_SRC#@ROOT@#g" "$B_FL" | sha256sum | cut -d' ' -f1)"
[[ "$A_FLN" == "$B_FLN" ]] || die "the two filelists differ by more than their source path:
       A normalized: $A_FLN
       B normalized: $B_FLN"

RTLDIFF="$(git -C "$B_SRC" diff "$A_REV" "$B_REV" 2>/dev/null)"
[[ -n "$RTLDIFF" ]] || die "the two revisions have no diff; there is nothing to measure"
RTLFILES="$(git -C "$B_SRC" diff --name-only "$A_REV" "$B_REV" | tr '\n' ',' | sed 's/,$//')"
RTLSTAT="$(git -C "$B_SRC" diff --shortstat "$A_REV" "$B_REV" | sed 's/^ *//')"
RTLSHA="$(printf '%s' "$RTLDIFF" | sha256sum | cut -d' ' -f1)"

# ---- measure --------------------------------------------------------------
TMP="$(mktemp -d "${TMPDIR:-/tmp}/d4dg.XXXXXX")" || die "cannot create a scratch directory"
trap 'rm -rf "$TMP"' EXIT
JSON="$TMP/delta.json"
"$PY" "$DELTA" "$A_CERT" "$B_CERT" --json > "$JSON" 2>"$TMP/delta.err"
rc=$?
[[ "$rc" -eq 0 ]] || die "the delta analyzer exited $rc; nothing is published
$(sed 's/^/       /' "$TMP/delta.err" | head -5)"
[[ -s "$JSON" ]] || die "the delta analyzer printed no readable result"

get() { "$PY" - "$JSON" "$1" "$2" <<'EOF'
import json, sys
d = json.load(open(sys.argv[1]))
print(d[sys.argv[2]][sys.argv[3]])
EOF
}

{
  echo "# schema            = d4-cert-delta/2"
  echo "# measures          = certificate/graph churn for ONE adjacent RTL edit"
  echo "# not_a_claim       = this does NOT make Option A or the loader incremental,"
  echo "# not_a_claim       = does NOT establish a population distribution, and does"
  echo "# not_a_claim       = NOT turn empirical validation into a proof"
  echo "# module            = $MOD (emitted as $GATE)"
  echo "# cva6_a_rev        = $A_REV"
  echo "# cva6_b_rev        = $B_REV"
  echo "# cva6_b_parent     = $PARENT   (== A)"
  echo "# cva6_a_root_clean = $A_CLEAN (at $A_REV)"
  echo "# cva6_b_root_clean = $B_CLEAN (at $B_REV)"
  echo "# cva6_target       = $TARGET"
  echo "# rtl_files_changed = $RTLFILES"
  echo "# rtl_diffstat      = $RTLSTAT"
  echo "# rtl_diff_sha256   = $RTLSHA"
  echo "# lhd_sha256        = $LHD_SHA   (equal on both sides, ENFORCED)"
  echo "# wrappers_sha256   = $WRAP   (equal on both sides, ENFORCED)"
  echo "# config_pkg_sha256 = $CFGPKG   (equal on both sides, ENFORCED)"
  echo "# filelist_norm_sha = $A_FLN   (equal after normalizing each side's root, ENFORCED)"
  echo "# module_list_sha   = $MODLIST"
  echo "# wrapper_gen_sha   = $WRAPGEN   (equal on both sides, ENFORCED)"
  echo "# gate_tool_sha256  = $A_GTOOL   (equal on both sides, ENFORCED)"
  echo "# a_gates           = $A_GATES"
  echo "# b_gates           = $B_GATES"
  echo "# limits            = stack ${STACK} KB, emit ${ETMO}s, run ${RTMO}s, ceiling ${ECEIL} KB, jobs ${JOBS}"
  echo "# a_run             = $A_ABS"
  echo "# b_run             = $B_ABS"
  echo "# a_cert_sha256     = $A_CERT_SHA"
  echo "# b_cert_sha256     = $B_CERT_SHA"
  echo "# a_digest          = $A_DIGEST"
  echo "# b_digest          = $B_DIGEST"
  echo "# tool_sha256       = $(sha "$DELTA")"
  echo "# producer_sha256   = $(sha "${BASH_SOURCE[0]}")"
  echo "# generated_by      = pass/lean/Lgraph_IR_Semantics_Simulator/tests/d4/cva6_csr_regfile_dg.sh"
  printf 'metric\ta\tb\tdelta\n'
  for k in bytes sources nodes outputs flops memories records signatures; do
    a="$(get a "$k")"; b="$(get b "$k")"
    printf '%s\t%s\t%s\t%s\n' "$k" "$a" "$b" "$((b - a))"
  done
  printf 'ordinal_common_record_prefix\t-\t-\t%s\n' "$(get ordinal common_record_prefix)"
  printf 'ordinal_common_record_suffix\t-\t-\t%s\n' "$(get ordinal common_record_suffix)"
  printf 'ordinal_same_slot_changed\t-\t-\t%s\n'    "$(get ordinal same_slot_changed)"
  printf 'ordinal_inserted\t-\t-\t%s\n'             "$(get ordinal inserted)"
  printf 'ordinal_deleted\t-\t-\t%s\n'              "$(get ordinal deleted)"
  printf 'ordinal_churn\t-\t-\t%s\n'                "$(get ordinal churn)"
  printf 'ordinal_churn_pct_of_max\t-\t-\t%s\n'     "$(get ordinal churn_pct_of_max)"
  printf 'structural_only_in_a\t-\t-\t%s\n'         "$(get structural only_in_a)"
  printf 'structural_only_in_b\t-\t-\t%s\n'         "$(get structural only_in_b)"
  printf 'structural_churn\t-\t-\t%s\n'             "$(get structural churn)"
  printf 'structural_churn_pct_of_max\t-\t-\t%s\n'  "$(get structural churn_pct_of_max)"
  for sec in src node out flop mem; do
    "$PY" - "$JSON" "$sec" <<'EOF'
import json, sys
s = json.load(open(sys.argv[1]))["sections"][sys.argv[2]]
print("section_%s\t%d\t%d\t%+d" % (sys.argv[2], s["a"], s["b"], s["b"] - s["a"]))
print("section_%s_same_slot_changed\t-\t-\t%d" % (sys.argv[2], s["same_slot_changed"]))
EOF
  done
} > "$TMP/out.tsv" 2>"$TMP/build.err"
[[ -s "$TMP/out.tsv" ]] || die "building the table produced nothing
$(sed 's/^/       /' "$TMP/build.err" | head -5)"

# ---- assertions, all against the CANDIDATE --------------------------------
hdrs="$(grep -c '^#' "$TMP/out.tsv")"
[[ "$hdrs" -ge 28 ]] || die "the candidate has only $hdrs provenance lines"
grep -q '^metric	a	b	delta$' "$TMP/out.tsv" || die "the candidate has no column header"
rows="$(grep -vc '^#' "$TMP/out.tsv")"
[[ "$rows" -ge 20 ]] || die "the candidate has only $rows rows"
awk -F'\t' '!/^#/ && NF != 4 { exit 1 }' "$TMP/out.tsv" || die "the candidate has a row that is not 4 fields"
for need in structural_churn ordinal_churn a_cert_sha256 b_cert_sha256 cva6_b_parent \
            rtl_diff_sha256 wrappers_sha256 config_pkg_sha256 filelist_norm_sha \
            gate_tool_sha256 wrapper_gen_sha a_gates b_gates a_run b_run; do
  grep -q "$need" "$TMP/out.tsv" || die "the candidate does not record $need"
done
grep -qE '^nodes	[0-9]+	[0-9]+	' "$TMP/out.tsv" || die "the candidate has no node counts"

mv "$TMP/out.tsv" "$OUT" || die "cannot publish to $OUT"
echo "wrote $OUT"
grep -E '^(ordinal_churn|structural_churn|ordinal_churn_pct_of_max|structural_churn_pct_of_max)	' "$OUT"
exit 0
