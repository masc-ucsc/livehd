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
while [[ $# -gt 0 ]]; do
  case "$1" in
    --only) ONLY="$2"; shift 2 ;;
    --jobs) JOBS="$2"; shift 2 ;;
    --cycles) CYCLES="$2"; shift 2 ;;
    --timeout) TIMEOUT="$2"; shift 2 ;;
    --out) OUT="$2"; shift 2 ;;
    *) echo "unknown argument: $1" >&2; exit 2 ;;
  esac
done

COMMIT="$(git -C "$ROOT" rev-parse --short HEAD)"
OUT="${OUT:-$ROOT/generated/census_d2/$COMMIT}"
mkdir -p "$OUT/logs" "$OUT/mod"

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
if [[ "$(printf '%s\n' "${ALL[@]}" | sort -u | wc -l)" -ne "${#ALL[@]}" ]]; then
  echo "FATAL: corpus contains duplicate module names" >&2; exit 2
fi
if ! printf '%s\n' "${ALL[@]}" | sort -c 2>/dev/null; then
  echo "FATAL: corpus is not sorted; the digest is order-sensitive" >&2; exit 2
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
cat > "$OUT/manifest.json" <<JSON
{
  "kind": "coreet_d2_census",
  "commit": "$COMMIT",
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
  "git_dirty": $(if [[ -n "$(git -C "$ROOT" status --porcelain 2>/dev/null)" ]]; then echo true; else echo false; fi),
  "driver_sha256": "$(sha256sum "$ROOT/scripts/coreet_d2_census.sh" | cut -c1-16)",
  "report_sha256": "$(sha256sum "$ROOT/scripts/coreet_d2_census_report.py" | cut -c1-16)",
  "runner_sha256": "$(sha256sum "$ROOT/scripts/run_coreet_module_lean.sh" | cut -c1-16)",
  "direct_sweep_sha256": "$(sha256sum "$ROOT/pass/lean/scripts/direct_sweep.py" | cut -c1-16)",
  "lhd_sha256": "$(sha256sum "$ROOT/bazel-bin/lhd/lhd" 2>/dev/null | cut -c1-16)",
  "lhd_mtime": "$(stat -c '%y' "$ROOT/bazel-bin/lhd/lhd" 2>/dev/null | cut -d. -f1)",
  "lean_build_root": "$(readlink -f "$ROOT/formal/lean/.lake")",
  "lean": "$(cd "$ROOT/formal/lean" && lake env lean --version 2>/dev/null | head -1)",
  "yosys_in_lhd": "linked into lhd",
  "started": "$START"
}
JSON

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

echo "phase 1: generating ${#MODULES[@]} module(s), jobs=$JOBS"
printf '%s\n' "${MODULES[@]}" | xargs -P "$JOBS" -I{} bash -c 'gen_one "$@"' _ {} 
echo "phase 1 done"

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
echo "phase 2: ${#PRESENT[@]} certificate(s) to sweep, serialized, cycles=$CYCLES"
SWEEP_TSV="$OUT/direct_sweep.tsv"
rm -f "$SWEEP_TSV"
sweep_rc=0
if [[ "${#PRESENT[@]}" -gt 0 ]]; then
  export TMPDIR="$OUT/runtime_tmp"; mkdir -p "$TMPDIR"
  python3 "$ROOT/pass/lean/scripts/direct_sweep.py" --out "$SWEEP_TSV" \
    --jobs 1 --cycles "$CYCLES" --max-rec-depth 20000000 \
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
