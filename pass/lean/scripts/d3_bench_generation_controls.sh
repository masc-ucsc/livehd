#!/usr/bin/env bash
# Controls for GENERATION ADMISSION in the two D3 scaling benchmark CLIs.
#
# WHAT THIS GUARDS. `run()` discarded the generator's exit status and never
# checked that it had produced anything, so when generation failed the harness
# measured whatever `probes/<name>.lean` happened to be there from an earlier
# run -- and reported OK, because a stale artifact still typechecks. A scaling
# number attached to the wrong n is worse than a missing one.
#
# Three legs, each driving the ACTUAL CLI:
#   1. a clean generation produces a row with a recorded artifact hash;
#   2. a generator forced to exit nonzero produces GEN_FAIL, not a timing;
#   3. with a STALE artifact pre-seeded at the exact path the row would use,
#      a failing generator still produces GEN_FAIL -- the stale file is not
#      measured, and is removed rather than silently reused.
#
# No Lean semantics are involved; this is harness evidence only.
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
BENCH="$ROOT/pass/lean/scripts/d3_bench2.sh"
GEN="$ROOT/pass/lean/scripts/d3_gen.py"

fails=0
ok()  { printf '  ok   %s\n' "$1"; }
bad() { printf '  FAIL %s\n' "$1"; fails=$((fails + 1)); }
for f in "$BENCH" "$GEN"; do
  [[ -r "$f" ]] || { echo "FATAL: missing $f" >&2; exit 2; }
done

W="$ROOT/generated/d3_gen_controls"
rm -rf "$W"; mkdir -p "$W/bin"

# THE REAL GENERATOR IS RESTORED WHATEVER HAPPENS.
# Two legs swap a failing stub in for `d3_gen.py`. A timeout or an interrupt
# between the swap and the copy-back would leave the tracked generator broken,
# so the restore is a trap and not a line at the end.
cp "$ROOT/pass/lean/scripts/d3_gen.py" "$W/gen_real.py"
restore_gen() { cp -f "$W/gen_real.py" "$ROOT/pass/lean/scripts/d3_gen.py" 2>/dev/null; }
trap restore_gen EXIT INT TERM
PROBE="$ROOT/formal/lean/probes/d3c2_pruned_chain_4.lean"
GEN_SHA_BEFORE="$(sha256sum < "$GEN")"

common=(D3_ROOT="$ROOT" SCRATCH="$W/scratch" TMO=600
        N_PRUNED_CHAIN=4 N_COMPOSED_FAR=4 N_PRUNED_FAR=4)

# 1. POSITIVE.
rm -f "$PROBE"
env "${common[@]}" OUT="$W/ok.tsv" nice -n19 ionice -c3 timeout 2400 bash "$BENCH" \
  > "$W/ok.log" 2>&1
row="$(awk -F'\t' '$1=="pruned" && $2=="chain" && $3==4 {print; exit}' "$W/ok.tsv")"
st="$(cut -f5 <<<"$row")"; ash="$(cut -f6 <<<"$row")"
if [[ "$st" == "OK" && "$ash" =~ ^[0-9a-f]{64}$ ]]; then
  ok "a clean generation is measured, with the artifact hash recorded"
else
  bad "positive leg: status='$st' artifact='$ash'"
fi
[[ -s "$W/scratch/pruned_chain_4/gen.log" && -s "$W/scratch/pruned_chain_4/lean.exit" ]] \
  && ok "per-row gen.log, lean.exit, lean.time and artifact.sha256 are retained" \
  || bad "per-row evidence was not retained"

# A stub generator that fails. The bench calls `python3 <gen>`, so the stub is
# a python file, invoked exactly where the real one would be.
printf 'import sys\nsys.exit(17)\n' > "$W/gen_fail.py"

# 2. FAILED GENERATION, nothing pre-seeded.
rm -f "$PROBE"
cp "$W/gen_fail.py" "$GEN"
env "${common[@]}" OUT="$W/genfail.tsv" nice -n19 ionice -c3 timeout 600 bash "$BENCH" \
  > "$W/genfail.log" 2>&1
cp "$W/gen_real.py" "$GEN"
st="$(awk -F'\t' '$1=="pruned" && $2=="chain" && $3==4 {print $5; exit}' "$W/genfail.tsv")"
cpu="$(awk -F'\t' '$1=="pruned" && $2=="chain" && $3==4 {print $4; exit}' "$W/genfail.tsv")"
if [[ "$st" == "GEN_FAIL(rc=17)" && "$cpu" == "NA" ]]; then
  ok "a generator that exits 17 yields GEN_FAIL(rc=17) with no timing"
else
  bad "failed generation: status='$st' cpu='$cpu'"
fi

# 3. FAILED GENERATION WITH A STALE ARTIFACT AT THE EXACT PATH.
#    This is the case that used to report OK.
env "${common[@]}" OUT="$W/seed.tsv" nice -n19 ionice -c3 timeout 2400 bash "$BENCH" \
  > "$W/seed_build.log" 2>&1     # rebuild a real artifact to use as the stale one
cp "$PROBE" "$W/stale.lean"
stale_sha="$(sha256sum < "$W/stale.lean")"
cp "$W/gen_fail.py" "$GEN"
cp "$W/stale.lean" "$PROBE"
env "${common[@]}" OUT="$W/stale.tsv" nice -n19 ionice -c3 timeout 600 bash "$BENCH" \
  > "$W/stale.log" 2>&1
cp "$W/gen_real.py" "$GEN"
st="$(awk -F'\t' '$1=="pruned" && $2=="chain" && $3==4 {print $5; exit}' "$W/stale.tsv")"
if [[ "$st" == "GEN_FAIL(rc=17)" ]]; then
  ok "a pre-seeded stale artifact is NOT measured when generation fails"
else
  bad "stale artifact leg: status='$st' (this is the defect)"
fi
[[ -e "$PROBE" ]] && bad "the stale artifact survived; it must be removed before generation" \
                  || ok "the stale artifact was removed rather than reused"

# The generator itself must come back exactly as it was.
if [[ "$(sha256sum < "$GEN")" == "$GEN_SHA_BEFORE" ]]; then
  ok "d3_gen.py is byte-identical after the controls"
else
  bad "d3_gen.py was left modified"
fi

if [[ $fails -eq 0 ]]; then echo "D3-GENERATION CONTROLS OK"; else echo "D3-GENERATION CONTROLS FAILED ($fails)"; fi
exit $fails
