#!/usr/bin/env bash
# Replay RETAINED DCERT1 certificates through the runtime path.
#
# The sweep (`run_d4_runtime_sweep.sh`) re-emits every block from RTL, which for
# CORE-ET's 122 is ~1.5 h and re-measures the FRONT END.  When the question is
# "did a change to the Lean runtime move any answer?", re-emitting is not just
# slow, it is the wrong experiment: a new front end would confound the result.
# This script holds the certificates FIXED -- it refuses to run one whose
# SHA-256 does not match the recorded table -- and re-runs only
#
#     <mod>.dcert --CertIO.loadCert--> runChecked
#
# at the ORDINARY stack limit, then diffs every digest against the recorded one.
#
# Usage:
#   scripts/replay_d4_certs.sh <recorded-rows.tsv> <outdir>
#
# Env:
#   LAKE         lake executable (default $HOME/.elan/bin/lake, else PATH)
#   RUN_TIMEOUT  per-certificate timeout, seconds (default 900)
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
die() { printf 'FATAL: %s\n' "$*" >&2; exit 2; }

RECORDED="${1:?usage: replay_d4_certs.sh <recorded-rows.tsv> <outdir>}"
OUTDIR="${2:?missing output directory}"

# Same eighteen columns as the sweep, through one helper, so a replay row and a
# sweep row are directly comparable and no branch can emit a short row.
ROW_FIELDS=18
HEADER=$'corpus\tmodule\temit_status\tcert_path\tcert_sha256\tsources\tnodes\tflops\tmemories\tparse_status\tparse_ms\tcompiles_ok\truntime_status\trun_ms\tdigest\tdiagnostic\twall_s\tpeak_rss_kb'
row() {
  [[ $# -eq $ROW_FIELDS ]] || die "internal: row() given $# fields, need $ROW_FIELDS"
  local IFS=$'\t'
  printf '%s\n' "$*"
}

[[ -s "$RECORDED" ]] || die "recorded rows $RECORDED is missing or empty"
[[ "$(head -1 "$RECORDED")" == "$HEADER" ]] || die "$RECORDED does not carry the 18-column sweep header"

if [[ -n "${LAKE:-}" ]]; then
  [[ -x "$LAKE" ]] || die "LAKE=$LAKE is not an executable; an explicit setting is never replaced by a fallback"
else
  LAKE="$HOME/.elan/bin/lake"
  [[ -x "$LAKE" ]] || LAKE="$(command -v lake 2>/dev/null || true)"
  [[ -n "$LAKE" && -x "$LAKE" ]] || die "no usable lake executable; set LAKE=<path>"
fi
# Measured WHERE THE ROWS ARE PRODUCED.  elan picks the toolchain from the cwd,
# so a version taken anywhere else can name a toolchain no row ever used.
LAKE_VERSION_RAW="$(cd "$ROOT/formal/lean" && "$LAKE" --version 2>&1)" || die "$LAKE --version failed"
LAKE_VERSION="$(grep -m1 '^Lake version ' <<<"$LAKE_VERSION_RAW" || true)"
[[ -n "$LAKE_VERSION" ]] || die "$LAKE --version printed no 'Lake version ' line"

ROW_DRIVER="$ROOT/formal/lean/probes/d4_sweep_row.lean"
[[ -r "$ROW_DRIVER" ]] || die "missing the row driver at $ROW_DRIVER"
[[ -d "$ROOT/formal/lean/.lake/build" ]] || die "no local Lean build at $ROOT/formal/lean/.lake/build"

mkdir -p "$OUTDIR" || die "cannot create $OUTDIR"

# THE PREFLIGHT'S DISPOSITION, PARSED -- NOT SEARCHED FOR.
#
# The row driver's eleven fields are
#   1 sources  2 nodes  3 flops  4 memories  5 parse_status  6 parse_ms
#   7 compiles_ok  8 runtime_status  9 run_ms  10 digest  11 diagnostic
#
# A missing file must come back as READ_ERROR / NOT_RUN with NO compile verdict
# and NO digest. The previous check was `grep -q READ_ERROR` over the whole
# row, which a perfectly successful `PARSED / true / RAN` row satisfies as soon
# as the two words appear anywhere in its diagnostic PROSE -- so a preflight
# that proved nothing passed, and the toolchain was never actually exercised on
# the failure path it exists to test.
preflight_fields_ok() { # <row>
  awk -F'\t' 'NF==11 && $5=="READ_ERROR" && $7=="-" && $8=="NOT_RUN" && $10=="-" { ok = 1 }
               END { exit ok ? 0 : 1 }' <<<"$1"
}

# PREFLIGHT: both the disposition AND the exit status.
#
# The driver must answer a missing file with a correctly shaped READ_ERROR row
# -- and must do so by EXITING 0. Checking only the shape accepted a child that
# printed the right row and then failed, which is the same defect as below one
# step earlier: the preflight would bless a toolchain that cannot run.
( cd "$ROOT/formal/lean" && "$LAKE" env lean --run "$ROW_DRIVER" /nonexistent.dcert \
    > "$OUTDIR/preflight.out" 2>"$OUTDIR/preflight.err" )
PRE_RC=$?
PRE="$(tail -1 "$OUTDIR/preflight.out" 2>/dev/null)"
[[ "$PRE_RC" -eq 0 ]] || die "row driver preflight exited $PRE_RC (see $OUTDIR/preflight.err)"
[[ "$(awk -F'\t' '{print NF}' <<<"$PRE")" == "11" ]] || die "row driver preflight returned the wrong shape"
preflight_fields_ok "$PRE" \
  || die "row driver preflight did not report READ_ERROR/NOT_RUN with no compile verdict and no digest for a missing file (got: $PRE)"

RUN_TIMEOUT="${RUN_TIMEOUT:-900}"
STACK_KB="$(ulimit -s)"
[[ "$STACK_KB" == "8192" ]] || echo "NOTE: stack limit is ${STACK_KB} KB, not the ordinary 8192" >&2

mkdir -p "$OUTDIR" || die "cannot create $OUTDIR"
OUTDIR="$(cd "$OUTDIR" && pwd)"
ROWS="$OUTDIR/rows.tsv"
PROV="$OUTDIR/provenance.txt"
CMP="$OUTDIR/digest_compare.tsv"
WORK="$OUTDIR/work"; mkdir -p "$WORK"

# The certificates to replay: every recorded row that HAS one.
mapfile -t CERTROWS < <(awk -F'\t' 'NR>1 && $4!="-"{print $1"\t"$2"\t"$3"\t"$4"\t"$5"\t"$15}' "$RECORDED")
N_CERTS="${#CERTROWS[@]}"
[[ "$N_CERTS" -gt 0 ]] || die "$RECORDED names no certificates"
N_NOCERT="$(awk -F'\t' 'NR>1 && $4=="-"' "$RECORDED" | wc -l)"
N_RECORDED="$(( $(wc -l < "$RECORDED") - 1 ))"

{
  echo "mode=replay"
  echo "d4_head=$(cd "$ROOT" && git rev-parse HEAD)"
  echo "d4_head_subject=$(cd "$ROOT" && git log -1 --format=%s)"
  echo "d4_tracked_dirty=$(cd "$ROOT" && [ -z "$(git status --porcelain --untracked-files=no)" ] && echo no || echo YES)"
  echo "worktree=$ROOT"
  echo "recorded_rows=$RECORDED"
  echo "recorded_rows_sha256=$(sha256sum "$RECORDED" | cut -d' ' -f1)"
  echo "recorded_total=$N_RECORDED"
  echo "recorded_with_cert=$N_CERTS"
  echo "recorded_without_cert=$N_NOCERT"
  echo "lake=$LAKE"
  echo "lake_version=$LAKE_VERSION"
  echo "lake_version_cwd=$ROOT/formal/lean"
  echo "stack_limit_kb=$STACK_KB"
  echo "run_timeout_s=$RUN_TIMEOUT"
  echo "row_fields=$ROW_FIELDS"
  echo "started=$(date -Is)"
} > "$PROV"

printf '%s\n' "$HEADER" > "$ROWS"
printf 'module\told_digest\tnew_digest\tverdict\n' > "$CMP"

preserved=0; newdig=0; changed=0; badsha=0; notran=0
for rec in "${CERTROWS[@]}"; do
  IFS=$'\t' read -r corpus m emit cert sha old_digest <<<"$rec"
  t_start=$(date +%s.%N)

  # A certificate that moved, vanished or changed is NOT the thing that was
  # measured, so it is never run: a replay that silently measures a different
  # input is worse than a replay that refuses.
  if [[ ! -r "$cert" ]]; then
    wall=$(echo "$(date +%s.%N) - $t_start" | bc)
    row "$corpus" "$m" "$emit" "$cert" "$sha" - - - - NOT_REPLAYED - - NOT_RUN - - \
        "certificate is gone or unreadable" "$wall" - >> "$ROWS"
    printf '%s\t%s\t-\tCERT_MISSING\n' "$m" "$old_digest" >> "$CMP"
    badsha=$((badsha + 1)); continue
  fi
  now="$(sha256sum "$cert" | cut -d' ' -f1)"
  if [[ "$now" != "$sha" ]]; then
    wall=$(echo "$(date +%s.%N) - $t_start" | bc)
    row "$corpus" "$m" "$emit" "$cert" "$now" - - - - NOT_REPLAYED - - NOT_RUN - - \
        "sha256 changed: recorded $sha, on disk $now" "$wall" - >> "$ROWS"
    printf '%s\t%s\t-\tCERT_SHA_MISMATCH\n' "$m" "$old_digest" >> "$CMP"
    badsha=$((badsha + 1)); continue
  fi

# THE CHILD'S EXIT STATUS, CAPTURED AND REQUIRED.
#
# Two defects, both reproduced against the actual CLI:
#
#  1. The captured status was IGNORED. (It was not wrong: these scripts set
#     `-o pipefail`, so `out="$(... | tail -1)"` does carry a failing `lake`
#     through. My first description of this said `tail` made the status always
#     zero -- that is false with pipefail set, and the status was there to be
#     used.) The acceptance test simply never consulted it.
#  2. So a row was admitted on SHAPE alone. A child printing a correct
#     11-field PARSED/true/RAN row and then exiting 17 was accepted, digest and
#     all; `runtime_complete` said YES.
#
# The pipe is gone anyway -- stdout to a file, status straight from the child,
# so the guard does not depend on `pipefail` staying set --
# and a nonzero or timed-out child can no longer contribute an accepted runtime
# result, however well-formed its row. The logs are kept, an 18-column FAILURE
# row is emitted with NO accepted digest, and the run does not complete.
#
# This is a harness guard. It does not touch the checker, the resource verdict,
# or any Lean semantics.
  tf="$WORK/$m.time"
  ( cd "$ROOT/formal/lean" && timeout "$RUN_TIMEOUT" \
      /usr/bin/time -v -o "$tf" \
      "$LAKE" env lean --run "$ROW_DRIVER" "$cert" \
      > "$WORK/$m.out" 2>"$WORK/$m.err" )
  run_rc=$?
  out="$(tail -1 "$WORK/$m.out" 2>/dev/null)"
  peak="$(grep -oP 'Maximum resident set size \(kbytes\): \K[0-9]+' "$tf" 2>/dev/null)"
  wall=$(echo "$(date +%s.%N) - $t_start" | bc)

  nf="$(awk -F'\t' '{print NF}' <<<"$out" 2>/dev/null)"
  if [[ "$run_rc" -ne 0 || -z "$out" || "$nf" != "11" ]]; then
    why="runtime produced no usable row (rc=$run_rc, fields=${nf:-0})"
    # A well-formed row behind a failed process is the case that used to pass.
    [[ "$run_rc" -ne 0 && "$nf" == "11" ]] && \
      why="runtime exited $run_rc after printing a complete row; the row is NOT accepted"
    # A timeout is named as one even when a complete row arrived first: the
    # cause is the limit, not an ordinary failure.
    [[ "$run_rc" -eq 124 ]] && why="runtime timeout ${RUN_TIMEOUT}s; any row already printed is NOT accepted"
    diag="$(tr '\t\n' '  ' < "$WORK/$m.err" | cut -c1-100)"
    [[ -n "$diag" ]] && why="$why; $diag"
    row "$corpus" "$m" "$emit" "$cert" "$sha" - - - - NO_ROW - - NOT_RUN - - \
        "$why" "$wall" "${peak:--}" >> "$ROWS"
    printf '%s\t%s\t-\tNO_ROW\n' "$m" "$old_digest" >> "$CMP"
    notran=$((notran + 1)); continue
  fi

  IFS=$'\t' read -r d_src d_nodes d_flops d_mems d_parse d_pms d_ok d_rt d_rms d_dig d_diag <<<"$out"
  row "$corpus" "$m" "$emit" "$cert" "$sha" "$d_src" "$d_nodes" "$d_flops" "$d_mems" \
      "$d_parse" "$d_pms" "$d_ok" "$d_rt" "$d_rms" "$d_dig" "$d_diag" "$wall" "${peak:--}" >> "$ROWS"

  if [[ "$d_rt" != "RAN" ]]; then
    printf '%s\t%s\t%s\t%s\n' "$m" "$old_digest" "$d_dig" "NOT_RAN:$d_rt" >> "$CMP"
    notran=$((notran + 1))
  elif [[ "$old_digest" == "-" ]]; then
    printf '%s\t-\t%s\tNEW\n' "$m" "$d_dig" >> "$CMP"
    newdig=$((newdig + 1))
  elif [[ "$old_digest" == "$d_dig" ]]; then
    printf '%s\t%s\t%s\tPRESERVED\n' "$m" "$old_digest" "$d_dig" >> "$CMP"
    preserved=$((preserved + 1))
  else
    printf '%s\t%s\t%s\tCHANGED\n' "$m" "$old_digest" "$d_dig" >> "$CMP"
    changed=$((changed + 1))
  fi
done

N_ROWS="$(( $(wc -l < "$ROWS") - 1 ))"
BAD_SHAPE="$(awk -F'\t' 'NR>1 && NF!=18' "$ROWS" | wc -l)"
N_RAN="$(awk -F'\t' 'NR>1 && $13=="RAN"' "$ROWS" | wc -l)"
N_PARSED="$(awk -F'\t' 'NR>1 && $10=="PARSED"' "$ROWS" | wc -l)"
N_COMPILES_OK="$(awk -F'\t' 'NR>1 && $12=="true"' "$ROWS" | wc -l)"

# TWO SEPARATE VERDICTS.  They answer different questions and conflating them is
# how a replay reports success while nothing ran.
#
#   reconciled        SHAPE only: every declared certificate produced exactly one
#                     18-field row.  Says NOTHING about what those rows contain.
#   runtime_complete  RESULT: every declared certificate PARSED, compiled
#                     (`compiles_ok=true`) and RAN, none was rejected, and no
#                     recorded digest changed.
#
# A malformed certificate carrying its recorded SHA-256 reconciles perfectly --
# one well-shaped PARSE_ERROR row -- and must still fail the command.
RECONCILED=$([[ "$N_ROWS" -eq "$N_CERTS" && "$BAD_SHAPE" -eq 0 ]] && echo YES || echo NO)
RUNTIME_COMPLETE=NO
if [[ "$RECONCILED" == YES \
   && "$N_RAN"        -eq "$N_CERTS" \
   && "$N_PARSED"     -eq "$N_CERTS" \
   && "$N_COMPILES_OK" -eq "$N_CERTS" \
   && "$notran" -eq 0 && "$badsha" -eq 0 && "$changed" -eq 0 ]]; then
  RUNTIME_COMPLETE=YES
fi

{
  echo "finished=$(date -Is)"
  echo "replayed=$N_ROWS"
  echo "rows_wrong_shape=$BAD_SHAPE"
  echo "parsed=$N_PARSED"
  echo "compiles_ok=$N_COMPILES_OK"
  echo "ran=$N_RAN"
  echo "digest_preserved=$preserved"
  echo "digest_new=$newdig"
  echo "digest_changed=$changed"
  echo "cert_rejected=$badsha"
  echo "not_ran=$notran"
  echo "reconciled=$RECONCILED"
  echo "runtime_complete=$RUNTIME_COMPLETE"
} >> "$PROV"

printf 'replayed %s/%s  parsed=%s  compiles_ok=%s  ran=%s  preserved=%s  new=%s  changed=%s  rejected=%s  not_ran=%s  runtime_complete=%s\n' \
  "$N_ROWS" "$N_CERTS" "$N_PARSED" "$N_COMPILES_OK" "$N_RAN" \
  "$preserved" "$newdig" "$changed" "$badsha" "$notran" "$RUNTIME_COMPLETE"

# The command's exit status IS the runtime verdict.  Shape alone never passes.
[[ "$RUNTIME_COMPLETE" == YES ]] || exit 1
exit 0
