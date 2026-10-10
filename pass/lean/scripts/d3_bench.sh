#!/usr/bin/env bash
# Direction 3 scaling benchmark: naive vs composed proof, two dependency shapes.
# CPU seconds (user+sys), so a loaded box does not distort the exponent.
set -uo pipefail
R=${D3_ROOT:-.}
# systemd-run --user does not inherit the interactive PATH, so `lake` must be
# absolute or every measurement silently reports ~0 seconds.
# elan's bin, wherever it lives: $ELAN_HOME, else the default location.
export PATH="${ELAN_HOME:-$HOME/.elan}/bin:$PATH"
LAKE=lake
cd "$R/formal/lean"
OUT="${OUT:-$R/pass/lean/d3_scaling.tsv}"
TMO="${TMO:-5400}"
printf 'variant\tshape\tn\tcpu_s\tstatus\tartifact_sha256\n' > "$OUT"

# SCRATCH IS PROJECT-LOCAL, AND THE STATUS COMES FROM THE EXIT CODE.
#
# Two defects, both recorded in the D3 backlog before they were repaired:
#
#   * `mktemp /tmp/...` -- /tmp is shared between parallel jobs on this box and
#     is not the configured runtime area; it also does not survive a rebuild.
#   * `st=OK` unless the exit was 124 or the log matched `^probes.*error`. Any
#     other nonzero exit -- a missing module, a killed process, a Lake error
#     that does not start with `probes` -- was recorded as a PASSING
#     measurement, with whatever CPU figure the partial run produced.
#
# The exit code now decides: 0 is OK, 124 is TIMEOUT, anything else is FAIL,
# and a zero exit whose log still carries an error is FAIL too.
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

# THE BASELINE IS A ROW LIKE ANY OTHER.
#
# It was `read -r BT BS < <(cat > file <<'B' ... B
# timeit file)` -- which, once `timeit` grew its row-directory argument, called
# it with `$2` unset. Under `set -u` that is an unbound-variable abort INSIDE
# the command substitution: the baseline child never ran, `BT`/`BS` came back
# empty, and the script emitted a five-field blank row beneath a six-field
# header and exited 0. A zero subtracted from every measurement, with nothing
# to show it was never taken.
#
# Now it has its own evidence directory, writes a fresh artifact, records its
# hash, and emits a complete row through the same path as every other.
baseline() {
  local f rowdir res bsha
  f="probes/d3_base.lean"
  rowdir="$SCRATCH/baseline"
  rm -rf "$rowdir"; mkdir -p "$rowdir"
  rm -f "$f"
  cat > "$f" <<'B'
import LeanSemanticPrimitives.Compiler.CompileDesign
set_option maxRecDepth 1000000
set_option maxHeartbeats 0
B
  local wr=$?
  printf '%s\n' "$wr" > "$rowdir/gen.exit"
  if [ "$wr" -ne 0 ] || [ ! -s "$f" ]; then
    printf 'baseline\t-\t0\tNA\tGEN_FAIL(rc=%s)\t-\n' "$wr" >> "$OUT"
    echo "baseline -> GEN_FAIL(rc=$wr)"
    return
  fi
  bsha=$(sha256sum < "$f" | cut -d' ' -f1)
  printf '%s\n' "$bsha" > "$rowdir/artifact.sha256"
  res=$(timeit "$f" "$rowdir")
  printf 'baseline\t-\t0\t%s\t%s\t%s\n' \
    "$(echo "$res" | cut -d' ' -f1)" "$(echo "$res" | cut -d' ' -f2-)" "$bsha" >> "$OUT"
  echo "baseline -> $res"
}
baseline

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
  f="probes/d3b_${v}_${sh}_${n}.lean"
  rowdir="$SCRATCH/${v}_${sh}_${n}"
  rm -rf "$rowdir"; mkdir -p "$rowdir"
  rm -f "$f"
  python3 "$R/pass/lean/scripts/d3_gen.py" "$n" --variant "$v" --shape "$sh" --out "$f" \
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
N_NAIVE_CHAIN="${N_NAIVE_CHAIN:-32 64 128 256 512}"
N_COMPOSED_CHAIN="${N_COMPOSED_CHAIN:-32 64 128 256 512 1024 2048 4096}"
N_COMPOSED_FAR="${N_COMPOSED_FAR:-32 64 128 256 512 1024}"
for n in $N_NAIVE_CHAIN;    do run naive    chain "$n"; done
for n in $N_COMPOSED_CHAIN; do run composed chain "$n"; done
for n in $N_COMPOSED_FAR;   do run composed far   "$n"; done
echo D3-BENCH-DONE
