#!/usr/bin/env bash
# Direction 2 DIRECT-SIMULATOR census over the CORE-ET corpus.
#
# WHAT THIS MEASURES, AND WHAT IT DOES NOT. The D2 simulator milestone needs:
# certificate emission + static gates, `checkDesign`, and EXECUTED `runDirect`
# cycles. Full residual-compiler theorem elaboration is a STRONGER, separate
# gate (scripts/lean_validate.sh) and is deliberately not folded in here --
# collapsing them would let a weaker result be reported as the stronger one.
#
# LOWERING IS PER MODULE AND RECORDED. The default stays the maintained
# `proc -ifx`. Only the six modules whose plain-proc lowering has been
# validated end to end may use the experimental plain mode. Plain proc is NOT
# safe globally: measured, it blows up intpipe_decode (2.4 GB and climbing at
# kill) and makes intpipe_csr_file / minion_dcache_top fail latch-contract C
# while -ifx compiles them cleanly. txfmaexp_top stays BLOCKED: its emitted
# netlist disagrees with the RTL on 7 of 2980 differential vectors under BOTH
# lowerings, so the discrepancy predates this work and is unresolved.
#
# Blocked modules get an explicit row. A module that silently disappears from a
# census is indistinguishable from one that passed.
#
# Usage: scripts/coreet_d2_census.sh [--only m1,m2,..] [--jobs N] [--cycles N]
#                                    [--timeout S] [--out DIR]
set -u
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LIST="${LIST:-$ROOT/pass/lean/COREET_MODULES.txt}"
JOBS="${JOBS:-4}"          # generation only; Lean execution is serialized
CYCLES="${CYCLES:-4}"
TIMEOUT="${TIMEOUT:-2700}"
ONLY=""
OUT=""
RESUME=""
ACROSS=""
while [[ $# -gt 0 ]]; do
  case "$1" in
    # Aggregate + sweep an EXISTING generation directory. Generation can be
    # interrupted (a killed driver leaves its xargs orphaned at PPID 1 and the
    # workers finish with nobody left to aggregate), and rerunning 121 modules
    # to recover is pure waste. Fail-closed: see the checks below.
    --resume) RESUME="$2"; shift 2 ;;
    # Resume artifacts generated at a DIFFERENT commit. Requires a written
    # reason, which is recorded in the manifest next to both commits. The
    # default refusal is right -- resuming across a code change attributes old
    # artifacts to new code -- but a change that provably cannot affect
    # generation (e.g. only the aggregation/report path) should not force
    # regenerating the whole corpus. Making it explicit and recorded is the
    # difference between a judgement call and a silently weakened gate.
    --resume-across-commit) ACROSS="$2"; shift 2 ;;
    --only) ONLY="$2"; shift 2 ;;
    --jobs) JOBS="$2"; shift 2 ;;
    --cycles) CYCLES="$2"; shift 2 ;;
    --timeout) TIMEOUT="$2"; shift 2 ;;
    --out) OUT="$2"; shift 2 ;;
    *) echo "unknown argument: $1" >&2; exit 2 ;;
  esac
done

COMMIT_FULL="$(git -C "$ROOT" rev-parse HEAD)"
COMMIT="$(git -C "$ROOT" rev-parse --short HEAD)"
if [[ -n "$ACROSS" && -z "$RESUME" ]]; then
  echo "FATAL: --resume-across-commit has no meaning without --resume" >&2; exit 2
fi
if [[ -n "$RESUME" ]]; then
  OUT="$RESUME"
  [[ -d "$OUT" ]] || { echo "FATAL: --resume: no such directory: $OUT" >&2; exit 2; }
fi
# UNIQUE RUN ID, claimed ATOMICALLY.
#
# A second-resolution timestamp can collide between two rapid launches, and a
# collision here means two jobs share a tree. `mkdir` either creates the
# directory or fails, so the winner of the race is unambiguous.
if [[ -z "$OUT" ]]; then
  base="$ROOT/generated/census_d2"
  mkdir -p "$base"
  while :; do
    cand="$base/${RUN_ID:-${COMMIT}_$(date +%Y%m%d-%H%M%S)_$$_${RANDOM}}"
    if mkdir "$cand" 2>/dev/null; then OUT="$cand"; break; fi
    # An explicit RUN_ID names one directory; retrying would spin forever.
    [[ -n "${RUN_ID:-}" ]] && { echo "FATAL: RUN_ID directory already exists: $cand" >&2; exit 2; }
  done
fi
mkdir -p "$OUT/logs" "$OUT/mod"

# EXCLUSIVE LOCK, on a path OUTSIDE the run tree.
#
# The first version of this put the lock at <out>/.lock, which does NOT stop
# the incident it was written for: a launcher that does `rm -rf <out>` first
# leaves the orphan holding a lock on an UNLINKED inode, and the new driver
# then creates a different .lock inode and proceeds. Both run. The lock must
# therefore live somewhere the run tree's removal cannot touch, keyed by the
# canonical output path.
#
# Children inherit the descriptor, so an orphaned `xargs` still holds it --
# which is exactly the case in generated/census_d2/INCIDENTS.md.
LOCK_DIR="$ROOT/generated/census_d2/runtime_locks"
mkdir -p "$LOCK_DIR"
OUT_CANON="$(readlink -m "$OUT")"
LOCK_FILE="$LOCK_DIR/$(printf '%s' "$OUT_CANON" | sha256sum | cut -d' ' -f1).lock"
exec 9>"$LOCK_FILE" || { echo "FATAL: cannot create $LOCK_FILE" >&2; exit 2; }
if ! flock -n 9; then
  echo "FATAL: another census is already using $OUT_CANON" >&2
  echo "       (lock held on $LOCK_FILE)" >&2
  echo "       A previous driver may have died leaving an orphaned xargs still" >&2
  echo "       writing there -- note the lock survives `rm -rf` of the run tree," >&2
  echo "       which is the point. Find the holder with:" >&2
  echo "         fuser -v $LOCK_FILE" >&2
  echo "       and stop that process GROUP before reusing this directory." >&2
  exit 2
fi
printf 'out=%s\npid=%s\nstarted=%s\n' "$OUT_CANON" "$$" "$(date -Is)" >&9
echo "run_id=$(basename "$OUT")  (lock: $LOCK_FILE)"

# ---- the corpus, validated -------------------------------------------------
# Count AND digest, so a stale or partially regenerated list cannot be swept as
# if it were the corpus.
# Every property is REQUIRED, not opportunistically checked. A corpus that is
# the wrong size, has duplicates, is unsorted, or carries no digest is a
# configuration error -- treating any of those as "good enough" is how an
# expensive run ends up authoritative over the wrong set.
EXPECT_N="${EXPECT_N:-122}"
mapfile -t ALL < <(grep -v '^#' "$LIST" | grep -v '^[[:space:]]*$')
if [[ "${#ALL[@]}" -ne "$EXPECT_N" ]]; then
  echo "FATAL: corpus has ${#ALL[@]} modules, expected $EXPECT_N ($LIST)" >&2; exit 2
fi
if [[ "$(printf '%s\n' "${ALL[@]}" | LC_ALL=C sort -u | wc -l)" -ne "${#ALL[@]}" ]]; then
  echo "FATAL: corpus contains duplicate module names" >&2; exit 2
fi
# LC_ALL=C: the list is written in BYTE order (python's sorted()), and the
# ambient locale collates '_' against letters differently -- `txfma_f6` vs
# `txfmactl_top` reorder, so an unpinned `sort -c` rejects a correct list.
# Byte order is also what the digest is taken over.
if ! printf '%s\n' "${ALL[@]}" | LC_ALL=C sort -c 2>/dev/null; then
  echo "FATAL: corpus is not in BYTE (LC_ALL=C) order; the digest is order-sensitive" >&2; exit 2
fi
DIGEST="$(printf '%s\n' "${ALL[@]}" | head -c -1 | sha256sum | cut -d' ' -f1)"
WANT="$(grep -oP '^#   \K[0-9a-f]{64}' "$LIST" | head -1)"
if [[ -z "$WANT" ]]; then
  echo "FATAL: $LIST carries no 64-hex expected digest; refusing to run" >&2; exit 2
fi
if [[ "$DIGEST" != "$WANT" ]]; then
  echo "FATAL: module list digest mismatch" >&2
  echo "  list says $WANT" >&2
  echo "  computed  $DIGEST" >&2
  exit 2
fi
echo "corpus: ${#ALL[@]} modules, digest ${DIGEST:0:16} (count, uniqueness, order and digest all validated)"

MODULES=("${ALL[@]}")
if [[ -n "$ONLY" ]]; then
  IFS=',' read -r -a MODULES <<< "$ONLY"
  if [[ "$(printf '%s\n' "${MODULES[@]}" | sort -u | wc -l)" -ne "${#MODULES[@]}" ]]; then
    echo "FATAL: --only lists a module more than once" >&2; exit 2
  fi
  for m in "${MODULES[@]}"; do
    printf '%s\n' "${ALL[@]}" | grep -qx "$m" \
      || { echo "FATAL: --only names '$m', which is not in the corpus" >&2; exit 2; }
  done
  echo "restricted to ${#MODULES[@]} module(s): ${MODULES[*]}"
fi

# ---- per-module lowering policy -------------------------------------------
PLAIN_SET=" txfma_f0 txfma_f2 txfma_f3 txfma_f5 txfma_e5 txfma_f6 "
BLOCKED_SET=" txfmaexp_top "
blocked_reason() { echo "RTL differential mismatch (7/2980) under BOTH lowerings; pre-existing, unresolved"; }
PLAIN_YS="$OUT/read_plain_proc.ys"
sed 's/^proc -ifx$/proc/' "$ROOT/inou/yosys/inou_yosys_read.ys" > "$PLAIN_YS"
grep -qx 'proc' "$PLAIN_YS" || { echo "FATAL: the -ifx substitution did not apply"; exit 2; }

START="$(date -Is)"
# On resume the generation manifest must be PRESERVED, not overwritten: it is
# the record of how those artifacts were produced, and rewriting it would
# re-stamp old artifacts with the resuming run's provenance.
# The resume gate runs BEFORE anything is written: a REFUSED resume must
# leave the run directory byte-for-byte unchanged, or the refusal has
# already damaged the evidence it was protecting.
if [[ -n "$RESUME" ]]; then
  # ---- fail-closed resume gate -------------------------------------------
  # Everything here is a REFUSAL, not a warning: a resumed census that
  # silently aggregates a partial or foreign generation directory is worse
  # than no census, because it looks authoritative.
  fail=0
  MF="$OUT/manifest.json"
  if [[ ! -r "$MF" ]]; then
    echo "FATAL: --resume: manifest.json is missing or unreadable at $MF" >&2
    echo "       without it the generation's provenance is unknown and the artifacts" >&2
    echo "       cannot be attributed to any corpus, commit or toolchain" >&2
    exit 2
  fi
  mf_digest="$(grep -oP '"module_list_sha256":\s*"\K[0-9a-f]+' "$MF" | head -1)"
  mf_commit="$(grep -oP '"commit":\s*"\K[^"]+' "$MF" | head -1)"
  if [[ "$mf_digest" != "$DIGEST" ]]; then
    echo "FATAL: --resume: that run used module list digest $mf_digest, this corpus is $DIGEST" >&2
    fail=1
  fi
  if [[ "$mf_commit" != "$COMMIT" && "$mf_commit" != "$COMMIT_FULL" ]]; then
    if [[ -n "$ACROSS" ]]; then
      echo "NOTE: resuming ACROSS a commit change, by explicit request." >&2
      echo "      artifacts generated at: $mf_commit" >&2
      echo "      aggregating at HEAD:    $COMMIT" >&2
      echo "      stated reason: $ACROSS" >&2
      echo "      generation inputs are unchanged by that delta; this is recorded in the manifest." >&2
    else
      echo "FATAL: --resume: that run was generated at commit $mf_commit, HEAD is $COMMIT" >&2
      echo "       resuming across a code change would attribute old artifacts to new code." >&2
      echo "       If the delta provably cannot affect GENERATION, re-run with" >&2
      echo "       --resume-across-commit '<why>' and it will be recorded." >&2
      fail=1
    fi
  fi
  n_status="$(find "$OUT/mod" -mindepth 2 -maxdepth 2 -name status 2>/dev/null | wc -l)"
  if [[ "$n_status" -ne "${#MODULES[@]}" ]]; then
    echo "FATAL: --resume: $n_status per-module status file(s), expected ${#MODULES[@]}" >&2
    echo "       generation is incomplete; let it finish before resuming" >&2
    fail=1
  fi
  for m in "${MODULES[@]}"; do
    [[ -r "$OUT/mod/$m/status" ]] || { echo "FATAL: --resume: no status for $m" >&2; fail=1; }
  done
  extra="$(find "$OUT/mod" -mindepth 1 -maxdepth 1 -type d -printf '%f\n' 2>/dev/null \
            | LC_ALL=C sort | comm -13 <(printf '%s\n' "${MODULES[@]}" | LC_ALL=C sort) -)"
  if [[ -n "$extra" ]]; then
    echo "FATAL: --resume: module directories not in the corpus: $(echo $extra)" >&2
    fail=1
  fi
  # Whatever the stated reason, the inputs that actually DETERMINE generation
  # must be unchanged. A free-text justification is not evidence.
  HASH_REPORT=""
  hashcheck() {  # <manifest-key> <file> <label>
    local key="$1" file="$2" label="$3"
    local old new
    old="$(grep -oP "\"$key\":\s*\"\\K[0-9a-f]+" "$MF" | head -1)"
    new="$(sha256sum "$file" 2>/dev/null | cut -d' ' -f1)"
    if [[ -z "$old" ]]; then
      HASH_REPORT+="$label=not-recorded "
      if [[ -n "$ACROSS" ]]; then
        echo "FATAL: --resume-across-commit: cannot be shown that $label is unchanged" >&2
        echo "       (the run recorded no $key, so there is nothing to compare against)" >&2
        fail=1
      fi
      return
    fi
    # Older runs stored a 16-char prefix; compare on the recorded length.
    if [[ "${new:0:${#old}}" == "$old" ]]; then
      HASH_REPORT+="$label=match "
    else
      HASH_REPORT+="$label=CHANGED "
      echo "FATAL: --resume: $label changed since generation ($key)" >&2
      echo "       recorded $old, current ${new:0:${#old}}" >&2
      echo "       artifacts generated by a different $label cannot be aggregated as this run" >&2
      fail=1
    fi
  }
  # Generation-critical only: the binary that compiled, and the runner that
  # drove it. The driver/report/sweep are the AGGREGATION path and are allowed
  # to differ -- that is the whole point of resuming.
  hashcheck runner_sha256 "$ROOT/scripts/run_coreet_module_lean.sh" "module runner"
  hashcheck lhd_sha256    "$ROOT/bazel-bin/lhd/lhd"                 "lhd binary"

  [[ "$fail" -eq 0 ]] || exit 2
  echo "resume: $n_status/${#MODULES[@]} status files, digest ok -- aggregating"
  echo "resume: generation-critical inputs: $HASH_REPORT"
fi

if [[ -z "$RESUME" ]]; then
cat > "$OUT/manifest.json" <<JSON
{
  "kind": "coreet_d2_census",
  "commit": "$COMMIT_FULL",
  "commit_short": "$COMMIT",
  "module_list": "$LIST",
  "module_list_sha256": "$DIGEST",
  "modules_requested": ${#MODULES[@]},
  "corpus_size": ${#ALL[@]},
  "default_lowering": "proc -ifx (maintained)",
  "plain_proc_modules": "$(echo $PLAIN_SET)",
  "blocked_modules": "$(echo $BLOCKED_SET)",
  "cycles": $CYCLES,
  "per_module_timeout_s": $TIMEOUT,
  "generation_jobs": $JOBS,
  "tracked_dirty": $(if [[ -n "$(git -C "$ROOT" status --porcelain --untracked-files=no 2>/dev/null)" ]]; then echo true; else echo false; fi),
  "untracked_paths": $(git -C "$ROOT" status --porcelain 2>/dev/null | grep -c '^??'),
  "driver_sha256": "$(sha256sum "$ROOT/scripts/coreet_d2_census.sh" | cut -d' ' -f1)",
  "report_sha256": "$(sha256sum "$ROOT/scripts/coreet_d2_census_report.py" | cut -d' ' -f1)",
  "runner_sha256": "$(sha256sum "$ROOT/scripts/run_coreet_module_lean.sh" | cut -d' ' -f1)",
  "direct_sweep_sha256": "$(sha256sum "$ROOT/pass/lean/scripts/direct_sweep.py" | cut -d' ' -f1)",
  "lhd_sha256": "$(sha256sum "$ROOT/bazel-bin/lhd/lhd" 2>/dev/null | cut -d' ' -f1)",
  "lhd_mtime": "$(stat -c '%y' "$ROOT/bazel-bin/lhd/lhd" 2>/dev/null | cut -d. -f1)",
  "lean_build_root": "$(readlink -f "$ROOT/formal/lean/.lake")",
  "lean": "$(cd "$ROOT/formal/lean" && lake env lean --version 2>/dev/null | head -1)",
  "yosys_in_lhd": "linked into lhd",
  "started": "$START"
}
JSON
else
  COMMIT_FULL="$COMMIT_FULL" ACROSS="$ACROSS" HASH_REPORT="$HASH_REPORT" \
  TRACKED_DIRTY="$(if [[ -n "$(git -C "$ROOT" status --porcelain --untracked-files=no 2>/dev/null)" ]]; then echo true; else echo false; fi)" \
  H_DRIVER="$(sha256sum "$ROOT/scripts/coreet_d2_census.sh" | cut -d' ' -f1)" \
  H_REPORT="$(sha256sum "$ROOT/scripts/coreet_d2_census_report.py" | cut -d' ' -f1)" \
  H_SWEEP="$(sha256sum "$ROOT/pass/lean/scripts/direct_sweep.py" | cut -d' ' -f1)" \
  python3 - "$OUT/manifest.json" "$START" <<'PYR'
import json, sys
p, started = sys.argv[1], sys.argv[2]
try:
    d = json.load(open(p))
except Exception:
    d = {}
import os
d.setdefault("resumed", []).append({
    "started": started,
    "aggregated_at_commit": os.environ.get("COMMIT_FULL", ""),
    "aggregated_tracked_dirty": os.environ.get("TRACKED_DIRTY", "") == "true",
    "across_commit_reason": os.environ.get("ACROSS", "") or None,
    "generation_input_hash_check": os.environ.get("HASH_REPORT", "").strip(),
    "aggregation_driver_sha256": os.environ.get("H_DRIVER", ""),
    "aggregation_report_sha256": os.environ.get("H_REPORT", ""),
    "aggregation_direct_sweep_sha256": os.environ.get("H_SWEEP", ""),
    "note": ("generation was interrupted and happened earlier, possibly at another "
             "commit; this run only aggregated + swept those existing artifacts")})
json.dump(d, open(p, "w"), indent=2)
PYR
  echo "resume: generation manifest preserved; resume recorded"
fi

# ---- phase 1: generate (bounded, fail-closed, per module) ------------------
gen_one() {
  local m="$1"
  local mo="$OUT/mod/$m" log="$OUT/logs/$m.log"
  mkdir -p "$mo"
  if [[ "$BLOCKED_SET" == *" $m "* ]]; then
    printf 'BLOCKED\t%s\n' "$(blocked_reason)" > "$mo/status"
    return 0
  fi
  local ys="" low="ifx"
  if [[ "$PLAIN_SET" == *" $m "* ]]; then ys="$PLAIN_YS"; low="plain"; fi
  # A certificate left by an earlier run must never be swept as this run's.
  rm -f "$mo/lean/${m}_Lgraph.lean"
  # `env`, not a bare assignment prefix: bash decides at PARSE time whether a
  # word is an assignment, so a leading parameter expansion (the optional
  # ${ys:+YOSYS_SCRIPT=...}) makes every following VAR=VAL a COMMAND NAME
  # instead -- every module then failed with
  #   "COREET_TOP=<module>: command not found"  (exit 127).
  env ${ys:+YOSYS_SCRIPT="$ys"} \
      COREET_TOP="$m" OUT="$mo" LEAN_MODE=verified_compiler \
      RUN_LEAN=false RUN_LEC_GATE=false STOP_AFTER=lean \
      nice -n19 ionice -c3 timeout "$TIMEOUT" "$ROOT/scripts/run_coreet_module_lean.sh" \
    > "$log" 2>&1
  printf '%s\t%s\n' "$low" "$?" > "$mo/status"
}
export -f gen_one blocked_reason
export OUT ROOT TIMEOUT PLAIN_SET BLOCKED_SET PLAIN_YS

if [[ -z "$RESUME" ]]; then
echo "phase 1: generating ${#MODULES[@]} module(s), jobs=$JOBS"
printf '%s\n' "${MODULES[@]}" | xargs -P "$JOBS" -I{} bash -c 'gen_one "$@"' _ {} 
echo "phase 1 done"
fi

# ---- phase 2: checkDesign + runDirect over THIS run's certificates ---------
# Serialized (jobs=1): the Lean runs are memory-hungry and parallelism here is
# how a census turns into an aggregate OOM.
# PRESENT = cleared EVERY generation gate, not merely "a certificate file
# exists". A certificate can be written and then fail a static gate; sweeping
# it on file existence alone let such a module come back ACCEPTED.
PRESENT=()
for m in "${MODULES[@]}"; do
  cert="$OUT/mod/$m/lean/${m}_Lgraph.lean"
  [[ -r "$cert" ]] || continue
  log="$OUT/logs/$m.log"
  rc="$(cut -f2 "$OUT/mod/$m/status" 2>/dev/null)"
  [[ "$rc" == "0" ]] || continue
  ok=1
  for pat in 'compile exit=' 'single_edge exit=' 'lean emit exit=' 'static gates: gate_status='; do
    v="$(grep -oP "${pat}\K[0-9]+" "$log" 2>/dev/null | head -1)"
    [[ "$v" == "0" ]] || ok=0
  done
  [[ "$ok" == "1" ]] && PRESENT+=("$m")
done
# An aggregate generation record, written BEFORE the sweep, so an interrupted
# run still leaves a readable account of what generation produced.
GEN_TSV="$OUT/generation.tsv"
printf 'module\tlowering\trunner_rc\tcompile\tsingle_edge\temit\tstatic_gates\tcert\tpresent\n' > "$GEN_TSV"
for m in "${MODULES[@]}"; do
  st="$OUT/mod/$m/status"; lg="$OUT/logs/$m.log"
  low="$(cut -f1 "$st" 2>/dev/null)"; rc="$(cut -f2 "$st" 2>/dev/null)"
  g() { grep -oP "$1\K[0-9]+" "$lg" 2>/dev/null | head -1; }
  inp=no; for x in "${PRESENT[@]:-}"; do [[ "$x" == "$m" ]] && inp=yes; done
  printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "$m" "${low:--}" "${rc:--}" \
    "$(g 'compile exit=')" "$(g 'single_edge exit=')" "$(g 'lean emit exit=')" \
    "$(g 'static gates: gate_status=')" \
    "$([[ -r "$OUT/mod/$m/lean/${m}_Lgraph.lean" ]] && echo yes || echo no)" "$inp" >> "$GEN_TSV"
done
echo "generation summary: $GEN_TSV"

echo "phase 2: ${#PRESENT[@]} certificate(s) to sweep, serialized, cycles=$CYCLES"
SWEEP_TSV="$OUT/direct_sweep.tsv"
rm -f "$SWEEP_TSV"
sweep_rc=0
if [[ "${#PRESENT[@]}" -gt 0 ]]; then
  export TMPDIR="$OUT/runtime_tmp"; mkdir -p "$TMPDIR"
  # --timeout IS FORWARDED.  The driver parses --timeout (and defaults to
  # 2700) and used it for phase 1 only, so phase 2 silently ran under
  # direct_sweep's own 1800s default: a run asked for 7200 and was cut at 1800,
  # and the row said TIMEOUT as though the model had failed.
  python3 "$ROOT/pass/lean/scripts/direct_sweep.py" --out "$SWEEP_TSV" \
    --jobs 1 --cycles "$CYCLES" --timeout "$TIMEOUT" --max-rec-depth 20000000 \
    --filter "^($(IFS='|'; echo "${PRESENT[*]}"))$" "$OUT/mod" \
    > "$OUT/logs/direct_sweep.log" 2>&1
  sweep_rc=$?
fi
echo "phase 2 done (direct_sweep exit=$sweep_rc)"

# ---- phase 3: the census TSV ----------------------------------------------
END="$(date -Is)"
python3 "$ROOT/scripts/coreet_d2_census_report.py" \
  --out-dir "$OUT" --modules "$(IFS=,; echo "${MODULES[*]}")" \
  --sweep-tsv "$SWEEP_TSV" --sweep-rc "$sweep_rc" --cycles "$CYCLES" \
  --present "$(IFS=,; echo "${PRESENT[*]:-}")" \
  --started "$START" --ended "$END"
rc=$?
echo "census TSV: $OUT/census.tsv   manifest: $OUT/manifest.json"
exit $rc
