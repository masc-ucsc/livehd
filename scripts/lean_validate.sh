#!/usr/bin/env bash
# Per-module Lean acceptance for the D2 certificate flow, with the provenance
# needed to believe the result later.
#
# WHY THE BUILD ROOT IS RECORDED. `formal/lean/.lake` was a SYMLINK into a
# different worktree (livehd-new), so both shared one Lake build tree. That
# worktree's DesignCert has no `clocks` field, so a build there silently
# replaced this branch's own-library oleans and every module failed with
# "`clocks` is not a field of structure `DesignCert`" -- while the stale
# .olean's mtime was NEWER than the source, so the usual staleness check said
# it was fine. This branch now builds in its own tree
# (generated/lean_validation/lake; dependencies REFLINK-COPIED from the shared
# one -- distinct inodes, and on btrfs shared extents so the copy is
# near-free; `--reflink=auto` falls back to a real copy elsewhere, so the
# guaranteed properties are distinct inodes and non-mutation, not zero cost. NOT hardlinks: an in-place write to
# a hardlink hits both trees, which is not an isolation boundary.) The resolved build root goes in every
# log, and scripts/lean_local_lake.sh creates and verifies the arrangement.
#
# THE COLUMNS ARE DELIBERATELY SEPARATE. Typechecking a generated theory is not
# the same as the compiles theorem elaborating, which is not checkDesign
# accepting, which is not the simulator executing cycles, which is not
# agreement with the RTL. Collapsing them into one "passes Lean" would hide
# exactly the distinctions that matter here. `<Top>_compiles` closes over
# `native_decide`: a TRUSTED COMPUTATION boundary, reported per module rather
# than described as an axiom-free proof.
#
# PHASES. (A) generate + typecheck every requested module; (B) sweep ONLY the
# artifacts phase A just produced; (C) probe theorem elaboration and print.
# Running the sweep first would check whatever certificates happened to be left
# over from a previous run and report them beside freshly generated theories.
#
# Usage: scripts/lean_validate.sh <module>...
set -u
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VDIR="${VDIR:-$ROOT/generated/lean_validation}"
PLAIN="${YOSYS_SCRIPT:-$ROOT/generated/equiv7/read_plain_proc.ys}"
mkdir -p "$VDIR/logs" "$VDIR/probes"

BUILD_ROOT="$(readlink -f "$ROOT/formal/lean/.lake" 2>/dev/null || echo '<none>')"
LEAN_VER="$(cd "$ROOT/formal/lean" && lake env lean --version 2>/dev/null | head -1)"
COMMIT="$(git -C "$ROOT" rev-parse --short HEAD 2>/dev/null)"
case "$BUILD_ROOT" in
  "$ROOT"/*) ;;
  *) echo "REFUSING: formal/lean/.lake resolves OUTSIDE this worktree ($BUILD_ROOT)."
     echo "          A shared Lake tree is what produced the stale-olean failure this"
     echo "          script exists to prevent. Run scripts/lean_local_lake.sh first."; exit 2 ;;
esac
echo "build_root=$BUILD_ROOT"
echo "lean=$LEAN_VER"
echo "commit=$COMMIT"

declare -A EMIT GATES TC WALL
GENERATED=()

# ---- phase A: generate + typecheck ----------------------------------------
for TOP in "$@"; do
  OUT="$ROOT/generated/equiv7/$TOP"
  LOG="$VDIR/logs/$TOP.log"
  { echo "module=$TOP"; echo "commit=$COMMIT"; echo "lean=$LEAN_VER"
    echo "build_root=$BUILD_ROOT"; echo "yosys_script=$PLAIN"; echo "date=$(date -Is)"
  } > "$LOG"
  # A certificate left over from an earlier run must not be swept as if this
  # run produced it.
  rm -f "$OUT/lean/${TOP}_Lgraph.lean"
  YOSYS_SCRIPT="$PLAIN" COREET_TOP="$TOP" OUT="$OUT" LEAN_MODE=verified_compiler \
    RUN_LEAN=true RUN_LEC_GATE=false LEAN_NUM_THREADS=8 \
    nice -n19 ionice -c3 timeout 5400 "$ROOT/scripts/run_coreet_module_lean.sh" >> "$LOG" 2>&1
  rc=$?
  EMIT[$TOP]="$(grep -oP 'lean emit exit=\K[0-9]+' "$LOG" | head -1)"
  GATES[$TOP]="$(grep -oP 'static gates: gate_status=\K[0-9]+' "$LOG" | head -1)"
  TC[$TOP]="$(grep -oP 'lean typecheck exit=\K[0-9]+' "$LOG" | head -1)"
  WALL[$TOP]="$(grep -oP 'WALL=\K[0-9.]+' "$LOG" | tail -1)"
  echo "runner_exit=$rc peak_rss_kb=$(grep -oP 'PEAK_RSS=\K[0-9]+' "$LOG" | tail -1)" >> "$LOG"
  [[ "${TC[$TOP]:-1}" == "0" && -r "$OUT/lean/${TOP}_Lgraph.lean" ]] && GENERATED+=("$TOP")
done

# ---- phase B: checkDesign + direct simulator over phase A's artifacts ------
# pass/lean/scripts/direct_sweep.py runs designErrors/checkDesign, directStepRaw
# and runDirect. It is used instead of make_sim_launcher.py because it needs no
# per-design [[lean_exe]] stanza in lakefile.toml and leaves no generated
# launcher in the source tree. --filter matches the MODULE NAME.
# Explicit, so an inherited variable or a leftover TSV from a previous run
# cannot be joined as if this run had produced it.
SWEEP_TSV=""
sweep_rc=0
if [[ "${SKIP_SWEEP:-0}" != "1" && "${#GENERATED[@]}" -gt 0 ]]; then
  SWEEP_TSV="$VDIR/direct_sweep.tsv"
  rm -f "$SWEEP_TSV"
  SWEEP_RE="^($(IFS='|'; echo "${GENERATED[*]}"))$"
  export TMPDIR="$VDIR/runtime_tmp"; mkdir -p "$TMPDIR"
  python3 "$ROOT/pass/lean/scripts/direct_sweep.py" --out "$SWEEP_TSV" \
    --jobs 1 --cycles "${SWEEP_CYCLES:-4}" --filter "$SWEEP_RE" \
    --max-rec-depth 20000000 "$ROOT/generated/equiv7" \
    > "$VDIR/logs/direct_sweep.log" 2>&1
  sweep_rc=$?
  echo "direct_sweep exit=$sweep_rc filter=$SWEEP_RE tsv=$SWEEP_TSV"
fi

# ---- phase C: theorem elaboration, then the table ------------------------
# The TSV is joined by scripts/lean_validate_join.py, NOT by
# `while IFS=$'\t' read`: bash treats tab as IFS whitespace, so the empty
# `reason` column on every successful row collapsed and every later field
# shifted left -- the table printed step1_ms as the checkDesign time and
# wall_s as the cycle count. The evidence was right; the join lied about it.
FACTS="$VDIR/facts.json"
: > "$FACTS.tmp"
for TOP in "$@"; do
  OUT="$ROOT/generated/equiv7/$TOP"
  gen="$OUT/lean/${TOP}_Lgraph.lean"
  thm="not-run"; ax="not-run"
  if [[ "${TC[$TOP]:-1}" == "0" && -r "$gen" ]]; then
    probe="$VDIR/probes/${TOP}_probe.lean"
    { cat "$gen"; echo ""; echo "-- appended by scripts/lean_validate.sh"
      echo "#check @${TOP}_step"; echo "#check @${TOP}_step_correct"
      echo "#print axioms ${TOP}_compiles"; echo "#print axioms ${TOP}_step_correct"
    } > "$probe"
    plog="$VDIR/logs/${TOP}_probe.log"
    # rc captured from the command itself, never from a pipeline tail, where
    # `$?` is the LAST stage's status and a `head` can SIGPIPE the producer.
    ( cd "$ROOT/formal/lean" && nice -n19 ionice -c3 timeout 3600 lake env lean "$probe" ) \
      > "$plog" 2>&1
    prc=$?
    if [[ $prc -eq 0 ]] && ! grep -q "error" "$plog"; then thm="elaborated"
    else thm="PROBE-FAIL(rc=$prc)"; fi
    if grep -q "native_decide" "$plog"; then ax="native_decide(trusted)"
    elif grep -qE "does not depend on any axioms" "$plog"; then ax="none"
    else ax="$(grep -oP 'depends on axioms: \[\K[^]]*' "$plog" | head -1 | tr -d ' ')"; ax="${ax:-unknown}"; fi
  fi
  ds="$(grep -oP '"verdict": "\K[^"]*' "$OUT/diffsim/diffsim.json" 2>/dev/null | tail -1)"
  case "${ds:-}" in
    # PRIOR, not current-run: this artifact comes from the earlier
    # scripts/coreet_equiv7.sh run and is not cryptographically tied to the
    # certificate phase A just regenerated. Its seed/hashes are in its own
    # diffsim.json. Reported, never gated on.
    DIFFSIM-PASS*)     dsv="prior-smoke" ;;
    DIFFSIM-MISMATCH*) dsv="MISMATCH" ;;
    "")                dsv="not-run" ;;
    *)                 dsv="not-measured" ;;
  esac
  printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "$TOP" "${EMIT[$TOP]:-?}" "${GATES[$TOP]:-?}" \
    "${TC[$TOP]:-?}" "$thm" "$dsv" "$ax" >> "$FACTS.tmp"
done
python3 - "$FACTS.tmp" "$FACTS" <<'PYJSON'
import json, sys
facts = {}
with open(sys.argv[1]) as fh:
    for line in fh:
        p = line.rstrip("\n").split("\t")
        if len(p) == 7:
            facts[p[0]] = {"emit": p[1], "gates": p[2], "tc": p[3],
                           "thm": p[4], "diffsim": p[5], "ax": p[6]}
json.dump(facts, open(sys.argv[2], "w"), indent=2)
PYJSON
rm -f "$FACTS.tmp"

# The gate is over every REQUESTED module, not just the phase-A survivors: a
# module that failed to generate must fail the run, not vanish from the
# expected set.
REQUESTED="$(IFS=,; echo "$*")"
GEN_CSV="$(IFS=,; echo "${GENERATED[*]:-}")"
python3 "$ROOT/scripts/lean_validate_join.py" \
  ${SWEEP_TSV:+--tsv "$SWEEP_TSV"} --facts "$FACTS" \
  --requested "$REQUESTED" --generated "$GEN_CSV" \
  --sweep-rc "$sweep_rc" --cycles "${SWEEP_CYCLES:-4}"
