#!/usr/bin/env bash
# The canonical D3 corpus procedure: CORE-ET 122 and the frozen CVA6 30.
#
# What this is for
# ----------------
# `d3_sweep.py` measures certificates; a milestone is stated over TARGETS.  This
# driver runs the sweep over a manifest and then joins the results back onto
# that manifest's denominator with `d3_join.py --meta`, so the output has
# exactly as many rows as the manifest and every target carries a disposition:
# terminal (`agree`/`disagree`), refused, unavailable (no certificate), or a
# resource stop.
#
# Four corpora, two pairs
# -----------------------
#   coreet        the IMPORTED CORE-ET corpus, pinned by pass/lean/manifests/
#   cva6          the IMPORTED frozen CVA6-30 corpus, likewise
#   coreet-master the corpus THIS worktree emitted, pinned by manifests/master/
#   cva6-master   likewise
#
# The imported pair is pre-D2 and its certificates do NOT typecheck here: the
# emitted trailer applies `compileAndRun` at the old arity, so every one of them
# stops at the `typecheck` gate.  Running them is still the measurement that
# establishes that boundary, which is why they stay.  The master pair is what
# can reach `agree`.  See pass/lean/manifests/master/README.md.
#
# In both cases the corpus is the one the manifest's sha256 column names, never
# whatever happens to be in a directory: a run against the wrong bytes is a
# different experiment and the resolver refuses it.
#
# Enforcement
# -----------
# Serial (`--jobs 1`) and cgroup-enforced.  Serial because the aggregate budget
# for this sweep is ~20 GB and staging by memory is the only way a single large
# design cannot take the run down with it; cgroup because `--enforce sampled` is
# advisory -- a peak between samples passes unobserved -- and a resource
# disposition that might have missed the peak is not a disposition.  cgroup
# enforcement needs delegation, which is why the sweep runs inside
# `systemd-run --user --scope --property=Delegate=yes`; without it the sweep
# REFUSES rather than silently degrading, and so does this script.
#
# Exit status
# -----------
#   2  this driver refused to start: no corpus named, or the certificate
#      directory it would have read does not exist.  Nothing was written.
#   0  the sweep completed AND the join is complete and consistent at the
#      manifest's denominator.  That takes a whole corpus; a --only or --limit
#      run cannot reach it, by design.
#   1  anything else -- the sweep refused or failed, or it succeeded and the
#      join found the result incomplete or inconsistent.  Which one it was is
#      in the log as `sweep_rc=` and `join_rc=`, and those two are the lines to
#      read: an incomplete run and a failed one are different outcomes and the
#      status alone does not separate them.
#
# Usage:
#   pass/lean/scripts/run_d3_corpus.sh coreet [extra d3_sweep args...]
#   pass/lean/scripts/run_d3_corpus.sh cva6   [extra d3_sweep args...]
#
# Extra arguments are appended to the sweep command (e.g. --resume, --prove,
# --limit).  They are recorded in the run log so no run is ambiguous about what
# produced it.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
cd "$ROOT"

CORPUS="${1:-}"
shift || true
LIVEHD_NEW_ROOT="${LIVEHD_NEW_ROOT:-$ROOT/../livehd-new}"
case "$CORPUS" in
  coreet)
    MANIFEST="pass/lean/manifests/coreet_122.tsv"
    CERTS="$LIVEHD_NEW_ROOT/generated/vc_sweep2/lean"
    WHERE="the imported corpus named in pass/lean/manifests/PROVENANCE.tsv; set LIVEHD_NEW_ROOT to the checkout that holds it" ;;
  cva6)
    MANIFEST="pass/lean/manifests/cva6_30.tsv"
    CERTS="$LIVEHD_NEW_ROOT/generated/cva6_vc/lean"
    WHERE="the imported corpus named in pass/lean/manifests/PROVENANCE.tsv; set LIVEHD_NEW_ROOT to the checkout that holds it" ;;
  coreet-master)
    MANIFEST="pass/lean/manifests/master/coreet_122.tsv"
    CERTS="$ROOT/generated/master_port/d3_corpus/master_coreet/lean"
    WHERE="built by pass/lean/scripts/d3_master_corpus.py; see pass/lean/manifests/master/README.md for the command" ;;
  cva6-master)
    MANIFEST="pass/lean/manifests/master/cva6_30.tsv"
    CERTS="$ROOT/generated/master_port/d3_corpus/master_cva6/lean"
    WHERE="built by pass/lean/scripts/d3_master_corpus.py; see pass/lean/manifests/master/README.md for the command" ;;
  *) echo "usage: $0 <coreet|cva6|coreet-master|cva6-master> [extra d3_sweep args...]" >&2; exit 2 ;;
esac

if [ ! -d "$CERTS" ]; then
  echo "REFUSING: certificate directory $CERTS does not exist." >&2
  echo "It is $WHERE." >&2
  exit 2
fi

OUTDIR="${D3_CORPUS_OUT:-$ROOT/generated/master_port/d3_corpus}"
mkdir -p "$OUTDIR"
RESULTS="$OUTDIR/${CORPUS}_results.tsv"
JOINED="$OUTDIR/${CORPUS}_joined.tsv"
LOG="$OUTDIR/${CORPUS}_sweep.log"

JOBS="${D3_JOBS:-1}"
TIMEOUT="${D3_TIMEOUT:-3600}"
KILL_KB="${D3_KILL_OVER_RSS_KB:-18000000}"
SAMPLE_S="${D3_RSS_SAMPLE_SECONDS:-2}"
THREADS="${LEAN_NUM_THREADS:-4}"

{
  echo "=== run_d3_corpus $CORPUS ==="
  echo "started      $(date -Is)"
  echo "head         $(git -C "$ROOT" rev-parse HEAD)"
  echo "manifest     $MANIFEST"
  echo "certs        $CERTS"
  echo "results      $RESULTS"
  echo "joined       $JOINED"
  echo "jobs         $JOBS"
  echo "timeout      ${TIMEOUT}s"
  echo "kill_over_kb $KILL_KB (cgroup memory.max, hybrid with the sampler)"
  echo "sample_s     $SAMPLE_S"
  echo "threads      $THREADS"
  echo "extra        $*"
} | tee "$LOG"

# `--scope` runs the command as a child of this shell, so nice/ionice are
# inherited; Delegate=yes is what makes the sweep's cgroup_base() resolve.
set +e
nice -n 19 ionice -c3 \
systemd-run --user --scope --quiet --property=Delegate=yes \
  env LEAN_NUM_THREADS="$THREADS" \
  python3 -I pass/lean/scripts/d3_sweep.py \
    --manifest "$MANIFEST" \
    --certs "$CERTS" \
    --out "$RESULTS" \
    --jobs "$JOBS" \
    --timeout "$TIMEOUT" \
    --enforce cgroup \
    --kill-over-rss-kb "$KILL_KB" \
    --rss-sample-seconds "$SAMPLE_S" \
    "$@" 2>&1 | tee -a "$LOG"
SWEEP_RC=${PIPESTATUS[0]}
set -e
echo "sweep_rc=$SWEEP_RC" | tee -a "$LOG"

# The join runs whatever the sweep's exit code was: a run that stopped on a
# resource limit still has rows, and the whole point of the denominator is that
# an incomplete run reports as incomplete instead of not reporting at all.
if [ -f "$RESULTS" ]; then
  set +e
  python3 -I pass/lean/scripts/d3_join.py \
    --manifest "$MANIFEST" --results "$RESULTS" --out "$JOINED" \
    --meta "$RESULTS.meta.json" 2>&1 | tee -a "$LOG"
  JOIN_RC=${PIPESTATUS[0]}
  set -e
  echo "join_rc=$JOIN_RC" | tee -a "$LOG"
else
  echo "join skipped: $RESULTS was never written" | tee -a "$LOG"
  JOIN_RC=2
fi

echo "finished     $(date -Is)" | tee -a "$LOG"
[ "$SWEEP_RC" -eq 0 ] && [ "$JOIN_RC" -eq 0 ]
