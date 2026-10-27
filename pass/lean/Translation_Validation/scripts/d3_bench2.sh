#!/usr/bin/env bash
# Focused comparison: composed vs pruned.  `composed` measured at exponent ~2.0
# on chain, so the open question is only whether pruning the local context --
# the suspected second source of quadratic cost -- changes the slope.
set -uo pipefail
R=${D3_ROOT:-.}
# elan's bin, wherever it lives: $ELAN_HOME, else the default location.
export PATH="${ELAN_HOME:-$HOME/.elan}/bin:$PATH"
LAKE=lake
cd "$R/formal/lean"
# Overridable, like `d3_bench.sh`'s. The default is kept so a plain run lands
# where it always did, but a harness run should not drop a results file into a
# tracked source directory, and now it does not have to.
OUT="${OUT:-$R/pass/lean/d3_scaling2.tsv}"
TMO="${TMO:-3000}"
printf 'variant\tshape\tn\tcpu_s\tstatus\tartifact_sha256\n' > "$OUT"
# SCRATCH IS PROJECT-LOCAL, AND THE STATUS COMES FROM THE EXIT CODE.
# Same two defects as `d3_bench.sh`: /tmp is shared between parallel jobs on
# this box and is not the configured runtime area, and `st=OK` survived any
# nonzero exit that was neither 124 nor matched `^probes.*error` -- recording a
# partial run's CPU figure as a passing measurement.
SCRATCH="${SCRATCH:-$R/generated/d3_bench_scratch}"
mkdir -p "$SCRATCH" || { echo "cannot create $SCRATCH" >&2; exit 2; }

timeit() {  # writes "<cpu> <status>" to stdout; args: <file> <rowdir>
  local f="$1" rowdir="$2" tf lf cpu st rc
  tf="$rowdir/lean.time"; lf="$rowdir/lean.log"
  /usr/bin/time -f "%U %S" -o "$tf" timeout "$TMO" "$LAKE" env lean "$f" > "$lf" 2>&1
  rc=$?
  printf '%s\n' "$rc" > "$rowdir/lean.exit"
  cpu=$(tail -1 "$tf" | awk '{printf "%.2f", $1+$2}' 2>/dev/null)
  if [ "$rc" -eq 0 ]; then
    st=OK
    grep -qE "error" "$lf" && st=FAIL
  elif [ "$rc" -eq 124 ]; then
    st=TIMEOUT
  else
    st="FAIL(rc=$rc)"
  fi
  echo "${cpu:-NA} $st"
}
# GENERATION IS PART OF THE MEASUREMENT.
#
# The generator's exit status was discarded and its output never checked, so a
# STALE artifact from an earlier run was measured whenever generation failed --
# and the row came back OK, because the stale file still typechecks. Measured
# against this CLI with a generator forced to exit 17.
#
# Now: the artifact is removed first so nothing stale can survive, the
# generator must exit 0, a non-empty file must exist afterwards, and its hash
# is recorded. Per row the scratch directory retains gen.log, gen.exit,
# lean.log, lean.exit, lean.time and artifact.sha256.
#
# The TSV gains `artifact_sha256`: a timing with no record of what was timed
# is not a measurement. A failed generation takes the `status` column as
# `GEN_FAIL(rc=N)` with `cpu_s` NA, so it can never be read as a slow pass; the
# generator's exit also sits in `gen.exit` beside its log.
run() {
  local v sh n f res rowdir gen_rc asha
  v="$1"; sh="$2"; n="$3"
  f="probes/d3c2_${v}_${sh}_${n}.lean"
  rowdir="$SCRATCH/${v}_${sh}_${n}"
  rm -rf "$rowdir"; mkdir -p "$rowdir"
  rm -f "$f"
  python3 "$R/pass/lean/Translation_Validation/scripts/d3_gen.py" "$n" --variant "$v" --shape "$sh" --out "$f" \
    > "$rowdir/gen.log" 2>&1
  gen_rc=$?
  printf '%s\n' "$gen_rc" > "$rowdir/gen.exit"
  if [ "$gen_rc" -ne 0 ] || [ ! -s "$f" ]; then
    res="NA GEN_FAIL(rc=$gen_rc)"
    printf '%s\t%s\t%s\t%s\t%s\t%s\n' "$v" "$sh" "$n" NA "GEN_FAIL(rc=$gen_rc)" - >> "$OUT"
    echo "$v/$sh n=$n -> $res"
    return
  fi
  asha=$(sha256sum < "$f" | cut -d' ' -f1)
  printf '%s\n' "$asha" > "$rowdir/artifact.sha256"
  res=$(timeit "$f" "$rowdir")
  printf '%s\t%s\t%s\t%s\t%s\t%s\n' "$v" "$sh" "$n" \
    "$(echo "$res" | cut -d' ' -f1)" "$(echo "$res" | cut -d' ' -f2-)" "$asha" >> "$OUT"
  echo "$v/$sh n=$n -> $res"
}
# THE SWEEP POINTS ARE OVERRIDABLE.
#
# The published sweep is the default and is what the recorded numbers come
# from. `N_*` exist so the harness can be EXERCISED at a small n -- a scaling
# script whose only mode is a multi-hour run cannot have its own status logic
# checked without paying for a full sweep, which is how the "any nonzero exit
# is OK" defect survived. An overridden sweep is a harness run, not a
# measurement, and its row set makes that obvious by its size.
N_PRUNED_CHAIN="${N_PRUNED_CHAIN:-32 64 128 256}"
N_COMPOSED_FAR="${N_COMPOSED_FAR:-32 64 128}"
N_PRUNED_FAR="${N_PRUNED_FAR:-32 64 128}"
for n in $N_PRUNED_CHAIN;   do run pruned   chain "$n"; done
for n in $N_COMPOSED_FAR;   do run composed far   "$n"; done
for n in $N_PRUNED_FAR;     do run pruned   far   "$n"; done
echo D3-BENCH2-DONE
