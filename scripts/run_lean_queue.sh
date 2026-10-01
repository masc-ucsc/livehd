#!/usr/bin/env bash
set -uo pipefail

# Serial Lean typecheck queue.
#
# Runs ONE module at a time, in the order given, and records wall/RSS/exit for
# each.  One-at-a-time is enforced HERE rather than by discipline: this box is a
# shared NFS server, and concurrent multi-GB Lean jobs both starve its clients and
# can OOM each other -- an OOM-killed run looks exactly like a proof failure in the
# log, which is the worst possible failure mode for a verification pipeline.
#
# Usage:
#   scripts/run_lean_queue.sh <lean-file> [<lean-file> ...]
#   QUEUE_SUMMARY=<path> scripts/run_lean_queue.sh ...
#
# Intended to be launched detached, because a harness background job dies with the
# session (that already cost one completed-but-unrecorded 5 h run):
#   systemd-run --user -p CPUQuota=800% --unit=lean-queue \
#     nice -n 19 ionice -c 3 bash scripts/run_lean_queue.sh <files...>

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SUMMARY="${QUEUE_SUMMARY:-$ROOT/generated/lean_queue_summary.tsv}"
JOBS="${LEAN_JOBS:-8}"
LAKE="${LAKE:-lake}"
CPUSET="${LEAN_CPUSET:-0-7}"

export PATH="/mada/users/czeng14/.elan/bin:$PATH"
export TMPDIR="$ROOT/generated/pass_lean_runtime_tmp"
export LEAN_NUM_THREADS="$JOBS"
mkdir -p "$TMPDIR" "$(dirname "$SUMMARY")"

# Match the lean BINARY (pgrep -x), not a command line containing "lean":
# `pgrep -f bin/lean` also matches this script's own shell and would deadlock the
# guard against itself.
wait_for_free_slot() {
  local waited=0
  while pgrep -x lean >/dev/null 2>&1 || pgrep -x lake >/dev/null 2>&1; do
    if [[ $waited -eq 0 ]]; then
      echo "[queue] another Lean is running; waiting for the slot..."
    fi
    sleep 30
    waited=$((waited + 30))
    if [[ $waited -ge 43200 ]]; then
      echo "[queue] FATAL: slot still busy after 12 h; giving up" >&2
      exit 3
    fi
  done
}

# D1: `lake env` does NOT build.  It only sets LEAN_PATH and spawns the process, so
# `lake env lean <file>` elaborates against whatever .olean happens to be on disk --
# there is no guarantee the library sources were ever compiled.  Build explicitly
# first, once, before any block is checked.  Cheap (~12 s warm) and it converts
# "the environment appears consistent" into "the environment is consistent".
echo "[queue] building the Lean support library before any check..."
if ! ( cd "$ROOT/formal/lean" && "$LAKE" build LeanSemanticPrimitives \
                                        LeanSemanticPrimitives.Translation.OpBridge \
                                        LeanSemanticPrimitives.Compiler.CompileDesign ); then
  echo "[queue] FATAL: the Lean support library does not build; refusing to check anything" >&2
  exit 4
fi

[[ -s "$SUMMARY" ]] || printf 'module\tnodes\tflops\tmax_w\twall_s\tpeak_rss_kb\texit\taxioms\tverdict\n' > "$SUMMARY"

overall=0
for f in "$@"; do
  if [[ ! -r "$f" ]]; then
    echo "[queue] SKIP (unreadable): $f" >&2
    overall=1
    continue
  fi
  # Absolute, because the lean invocation below runs from $ROOT/formal/lean (the
  # lake package root) -- a relative path would resolve against the wrong dir and
  # fail in ~1 s with "no such file or directory", which is easy to misread as a
  # fast proof failure.
  f="$(cd "$(dirname "$f")" && pwd)/$(basename "$f")"
  name="$(basename "$f" _Lgraph.lean)"
  log="$(dirname "$f")/../logs/lean_typecheck.log"
  mkdir -p "$(dirname "$log")"

  # Static gates are a precondition, not a formality: a file with a `sorry` would
  # "pass" a typecheck while proving nothing.
  n_sorry="$(grep -cw sorry "$f" || true)"
  if [[ "$n_sorry" != "0" ]]; then
    echo "[queue] SKIP $name: $n_sorry sorry(s) present -- fix the emitter first" >&2
    printf '%s\t\t\t\t\t\tSKIP\t%s sorries\n' "$name" "$n_sorry" >> "$SUMMARY"
    overall=1
    continue
  fi

  wait_for_free_slot
  echo "[queue] START $name  ($(date '+%H:%M:%S'))"
  : > "$log"
  ( cd "$ROOT/formal/lean" && \
    taskset -c "$CPUSET" nice -n 19 ionice -c 3 \
      /usr/bin/time -f "WALL=%e s PEAK_RSS=%M KB CPU=%U+%S" \
      stdbuf -oL lake env lean "$f" ) > "$log" 2>&1
  rc=$?
  echo "lean_exit=$rc" >> "$log"

  wall="$(grep -oP 'WALL=\K[0-9.]+' "$log" | tail -1)"
  rss="$(grep -oP 'PEAK_RSS=\K[0-9]+' "$log" | tail -1)"
  errs="$(grep -c 'error:' "$log" || true)"
  gl="$(dirname "$f")/../logs/static_gates.log"
  nodes="$(grep -oP 'cert nodes\s+:\s+\K[0-9]+' "$gl" 2>/dev/null || true)"
  flops="$(grep -oP 'state fields\s+:\s+\K[0-9]+' "$gl" 2>/dev/null || true)"
  maxw="$(grep -oP 'node output widths : max=\K[0-9]+' "$gl" 2>/dev/null || true)"

  # D2: exit 0 is not proof.  Require the emitted `#print axioms` audit to have run
  # for every expected theorem and to be free of `sorryAx`.  This catches a `sorry` in
  # an IMPORTED module (a warning at the library build, invisible to the importer) and
  # a truncated file whose `_refines_fast` theorem never existed.
  ax_lines="$(grep -c "depends on axioms\|does not depend on any axioms" "$log" || true)"
  ax_sorry="$(grep -c "sorryAx" "$log" || true)"
  # Expected audit count comes from the FILE, not from a scraped external log: the
  # emitter writes exactly one `#print axioms` per top-level theorem it produced, so
  # this cannot drift.  (Deriving it from static_gates.log would fail lenient -- a
  # missing log would set the expectation to 1 and let a sequential design pass on
  # its `_comb` line alone.)
  ax_want="$(grep -c '^#print axioms ' "$f" || true)"
  if [[ "$ax_want" == "0" ]]; then
    echo "[queue] WARNING $name: no '#print axioms' lines in the file -- emitted by an" >&2
    echo "        older emitter?  Re-emit to get the axiom gate." >&2
  fi
  ax_note="${ax_lines}/${ax_want}"
  [[ "$ax_sorry" != "0" ]] && ax_note="${ax_note},sorryAx"

  if [[ "$rc" -eq 0 && "$errs" -eq 0 && "$ax_sorry" == "0" \
        && "$ax_want" -gt 0 && "$ax_lines" -ge "$ax_want" ]]; then
    verdict=PROVEN
  elif [[ "$rc" -eq 0 && "$errs" -eq 0 ]]; then
    # typechecked but the audit is missing or dirty -- do NOT call this proven
    verdict="AXIOM-GATE-FAIL(${ax_note})"
    overall=1
  elif [[ "$errs" -gt 0 ]]; then
    verdict="FAIL(${errs} errors)"
    overall=1
  else
    # Nonzero exit with no `error:` line means lean never got as far as checking
    # anything (bad path, import failure, OOM kill).  Do NOT report that as a
    # proof failure -- it is an infrastructure failure and needs a different fix.
    verdict="DID-NOT-RUN(exit $rc)"
    overall=1
  fi
  printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
    "$name" "$nodes" "$flops" "$maxw" "$wall" "$rss" "$rc" "$ax_note" "$verdict" >> "$SUMMARY"
  echo "[queue] DONE  $name  exit=$rc errors=$errs axioms=$ax_note wall=${wall}s rss=${rss}KB -> $verdict"
done

echo "[queue] summary: $SUMMARY"
exit "$overall"
