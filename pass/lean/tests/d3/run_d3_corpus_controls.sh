#!/usr/bin/env bash
# Controls for pass/lean/scripts/run_d3_corpus.sh, invoked as the REAL
# entrypoint with real arguments.
#
# WHY.  The driver's product is a joined table plus an exit status, and the
# status is what a later step reads.  Ways it could lie and still look fine:
# `tee` swallowing the sweep's exit code (a pipeline's status is the tee's), a
# refused sweep reported as success because the join was skipped rather than
# failed, an INCOMPLETE run reported as a complete one, and a wrong corpus
# silently measured because the certificate directory came from wherever
# LIVEHD_NEW_ROOT happened to point.  Every leg requires a specific exit status,
# and the refusal legs require the refusal to be VISIBLE in the log rather than
# only in the status.
#
# WHAT A ZERO EXIT MEANS, and why no leg asserts one.  The driver returns 0 iff
# the sweep completed AND the join is complete and consistent at the manifest's
# denominator -- which takes a whole corpus, 85 or 30 real designs.  Re-running
# that here would not be a control, it would be the procedure.  So the positive
# leg runs ONE design and pins the thing that actually distinguishes the two
# failure modes: the sweep exits 0 while the join exits 1, because 84 targets
# have no result row.  A driver that returned 0 there would be claiming a
# corpus result from one design.
#
# Legs that must reach the sweep's manifest RESOLVER pass --allow-dirty, since
# otherwise they would be testing the dirty-worktree refusal instead of what
# they name; that refusal has its own leg, which runs only when the worktree is
# in fact dirty.
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../../../.." && pwd)"
DRIVER="$ROOT/pass/lean/scripts/run_d3_corpus.sh"
[[ -x "$DRIVER" ]] || { echo "FATAL: $DRIVER is not executable" >&2; exit 2; }

fails=0
ok()  { printf 'PASS  %s\n' "$1"; }
bad() { printf 'FAIL  %s\n' "$1"; fails=$((fails + 1)); }

mkdir -p "$ROOT/temp"
W="$(mktemp -d "$ROOT/temp/d3corpusctl.XXXXXX")"
KEEP="${D3_CTL_KEEP:-0}"
cleanup() { [[ "$KEEP" == "1" ]] && { echo "kept $W"; return; }; rm -rf "$W"; }
trap cleanup EXIT

LOG=""
RC=0
run() { # run <case> <expect-rc> <livehd-new-root> <driver args...>
  local case_name="$1" want="$2" lnr="$3"; shift 3
  LOG="$W/$case_name.log"
  mkdir -p "$W/out_$case_name"
  D3_CORPUS_OUT="$W/out_$case_name" LIVEHD_NEW_ROOT="$lnr" \
    "$DRIVER" "$@" > "$LOG" 2>&1
  RC=$?
  if [[ "$RC" -eq "$want" ]]; then
    ok "$case_name (exit $RC)"
  else
    bad "$case_name: exit $RC, wanted $want"
    sed -n '1,14p' "$LOG" | sed 's/^/      /'
  fi
}

REAL_LNR="${LIVEHD_NEW_ROOT:-$ROOT/../livehd-new}"
HAVE_CORPUS=0
[[ -d "$REAL_LNR/generated/vc_sweep2/lean" ]] && HAVE_CORPUS=1
MASTER_CERTS="$ROOT/generated/master_port/d3_corpus/master_coreet/lean"
HAVE_MASTER=0
[[ -d "$MASTER_CERTS" ]] && HAVE_MASTER=1

# --- argument handling -----------------------------------------------------
run no-corpus          2 "$REAL_LNR"
grep -q '^usage:' "$LOG" || bad "no-corpus: no usage line"

run unknown-corpus     2 "$REAL_LNR" wobble
grep -q '^usage:' "$LOG" || bad "unknown-corpus: no usage line"

# A corpus name that differs only in spelling is NOT the corpus.
run case-mismatch      2 "$REAL_LNR" CORE-ET

# --- the certificate directory must exist and be named, not guessed ---------
# These refuse BEFORE anything is written, so the control for them is that the
# output directory stays EMPTY -- a stronger property than "a log appeared".
mkdir -p "$W/empty_root/generated"
run missing-certs-coreet 2 "$W/empty_root" coreet
grep -q 'REFUSING: certificate directory' "$LOG" \
  || bad "missing-certs-coreet: refusal not in the log"
grep -q 'PROVENANCE.tsv' "$LOG" \
  || bad "missing-certs-coreet: refusal does not say which corpus it wanted"
[[ -z "$(ls -A "$W/out_missing-certs-coreet")" ]] \
  || bad "missing-certs-coreet: wrote into the output directory before refusing"

run missing-certs-cva6   2 "$W/empty_root" cva6
grep -q 'REFUSING: certificate directory' "$LOG" \
  || bad "missing-certs-cva6: refusal not in the log"
[[ -z "$(ls -A "$W/out_missing-certs-cva6")" ]] \
  || bad "missing-certs-cva6: wrote into the output directory before refusing"

# The master corpora live inside the worktree, so LIVEHD_NEW_ROOT cannot make
# them disappear and a missing one is a different message: it names the script
# that builds it, not a checkout to point at.  The leg that matters is that
# each master corpus name resolves to the manifest and certificates it claims,
# and that the two pairs do not cross.
if [[ "$HAVE_MASTER" == "1" ]]; then
  run master-names-resolve 1 "$REAL_LNR" coreet-master --dry-run --allow-dirty
  grep -q 'manifests/master/coreet_122.tsv' "$LOG" \
    || bad "master-names-resolve: coreet-master did not take the master manifest"
  grep -q 'master_coreet/lean' "$LOG" \
    || bad "master-names-resolve: coreet-master did not take the master certificates"
  grep -q '^sweep_rc=0$' "$LOG" || bad "master-names-resolve: the dry run did not succeed"

  run master-cva6-names 1 "$REAL_LNR" cva6-master --dry-run --allow-dirty
  grep -q 'manifests/master/cva6_30.tsv' "$LOG" \
    || bad "master-cva6-names: cva6-master did not take the master manifest"
  grep -q 'master_cva6/lean' "$LOG" \
    || bad "master-cva6-names: cva6-master did not take the master certificates"

  # The master corpus is the one that can reach `agree`; the imported one stops
  # at `typecheck`.  One design each, so the difference is measured, not assumed.
  run master-one-design 1 "$REAL_LNR" coreet-master --only tima_adder --allow-dirty
  grep -q '^sweep_rc=0$' "$LOG" || bad "master-one-design: the sweep did not succeed"
  JM="$W/out_master-one-design/coreet-master_joined.tsv"
  if [[ -f "$JM" ]]; then
    line=$(grep -P '^tima_adder\t' "$JM")
    [[ "$(echo "$line" | cut -f6)" == "agree" ]] \
      || bad "master-one-design: verdict is '$(echo "$line" | cut -f6)', wanted agree"
    [[ "$(echo "$line" | cut -f10)" == "1" ]] \
      || bad "master-one-design: typecheck was not credited"
  else
    bad "master-one-design: no joined table at $JM"
  fi
else
  printf 'SKIP  master corpus legs (no %s)\n' "$MASTER_CERTS"
fi

# --- a directory that exists but holds the wrong bytes ---------------------
# The dangerous case: the driver's own check passes and the sweep's manifest
# resolver has to be the one that refuses.  The status must survive `tee`.
if [[ "$HAVE_CORPUS" == "1" ]]; then
  mkdir -p "$W/wrong_root/generated/vc_sweep2/lean"
  for f in "$REAL_LNR"/generated/vc_sweep2/lean/*_Lgraph.lean; do
    { cat "$f"; echo "-- control: one byte of drift"; } \
      > "$W/wrong_root/generated/vc_sweep2/lean/$(basename "$f")"
  done
  # Exit 1, not 2: the driver started, so 2 is reserved for its own refusal.
  # `sweep_rc=2` below is what says WHICH stage refused.
  run drifted-certs 1 "$W/wrong_root" coreet --allow-dirty
  grep -q 'hash drift' "$LOG" || bad "drifted-certs: the drift refusal is not in the log"
  grep -q '^sweep_rc=2$' "$LOG" \
    || bad "drifted-certs: sweep_rc not recorded as 2 (tee swallowed the status?)"
  grep -q 'join skipped' "$LOG" || bad "drifted-certs: join neither ran nor reported why"
  [[ -e "$W/out_drifted-certs/coreet_results.tsv" ]] \
    && bad "drifted-certs: wrote a results table anyway"

  # --- a sweep that writes nothing must not report success -----------------
  run dry-run-no-table 1 "$REAL_LNR" coreet --dry-run --allow-dirty
  grep -q '^sweep_rc=0$' "$LOG" || bad "dry-run-no-table: sweep_rc was not 0"
  grep -q 'join skipped'  "$LOG" || bad "dry-run-no-table: did not say the join was skipped"

  # --- one design: the sweep succeeds, the JOIN reports it as incomplete ---
  run positive-one-design 1 "$REAL_LNR" coreet --only tima_adder --allow-dirty
  grep -q '^sweep_rc=0$' "$LOG" \
    || bad "positive-one-design: the sweep itself did not succeed"
  grep -q '^join_rc=1$'  "$LOG" \
    || bad "positive-one-design: an 84-target hole was not reported by the join"
  grep -q 'no result row' "$LOG" \
    || bad "positive-one-design: the join did not name the missing targets"
  J="$W/out_positive-one-design/coreet_joined.tsv"
  if [[ -f "$J" ]]; then
    # The join is stated over the DENOMINATOR: a one-design run still reports
    # 122 targets, 121 of them uncredited.
    rows=$(grep -cv '^#' "$J")
    [[ "$rows" -eq 123 ]] || bad "positive-one-design: joined table has $rows lines, wanted 123"
    line=$(grep -P '^tima_adder\t' "$J")
    [[ -n "$line" ]] || bad "positive-one-design: tima_adder missing from the join"
    # The imported corpus is pre-D2: `compileAndRun` is applied at the old
    # arity, so the certificate reaches `reify` and stops at `typecheck`.
    # Asserting it here keeps the boundary measured rather than remembered.
    [[ "$(echo "$line" | cut -f10)" == "0" ]] \
      || bad "positive-one-design: the pre-D2 certificate typechecked; the boundary moved"
    [[ "$(echo "$line" | cut -f6)" == "reify" ]] \
      || bad "positive-one-design: verdict is '$(echo "$line" | cut -f6)', wanted reify"
  else
    bad "positive-one-design: no joined table at $J"
  fi
else
  for c in drifted-certs dry-run-no-table positive-one-design; do
    printf 'SKIP  %s (no imported CORE-ET corpus at %s)\n' "$c" "$REAL_LNR"
  done
fi

# --- the dirty-worktree refusal, when it applies ---------------------------
if [[ "$HAVE_CORPUS" == "1" && -n "$(git -C "$ROOT" status --porcelain)" ]]; then
  run dirty-refused 1 "$REAL_LNR" coreet --dry-run
  grep -q 'REFUSING a manifest run from a dirty worktree' "$LOG" \
    || bad "dirty-refused: the dirty-worktree refusal is not in the log"
  grep -q '^sweep_rc=2$' "$LOG" || bad "dirty-refused: sweep_rc was not 2"
else
  printf 'SKIP  dirty-refused (worktree is clean, or no corpus)\n'
fi

# --- nothing leaked into the real output directory -------------------------
REAL_OUT="$ROOT/generated/master_port/d3_corpus"
if [[ -d "$REAL_OUT" ]]; then
  newer=$(find "$REAL_OUT" -maxdepth 1 -newer "$W" -type f | wc -l)
  [[ "$newer" -eq 0 ]] || bad "a control wrote into the real corpus output directory"
fi

if [[ "$fails" -eq 0 ]]; then
  echo "D3-CORPUS-DRIVER CONTROLS OK"
  exit 0
fi
echo "D3-CORPUS-DRIVER CONTROLS: $fails failure(s)"
exit 1
