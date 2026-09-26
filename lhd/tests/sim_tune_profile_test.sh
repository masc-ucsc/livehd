#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# `lhd sim` profile-guided tuning end to end (sim_profile.md; docs/simopt.md §13):
#
#   1. an idle-tables design converges within 2 runs under the default `auto`
#      (profile at the built-in L1 `d=on f=16` -> trial L2 judged -> converged),
#      and a converged workdir then runs with NO sampling;
#   2. sim.tune.export writes the decision and sim.tune.file imports it into a
#      fresh workdir (source=file) with the tuner off;
#   3. an always-toggling LFSR design descends from the default with ONE trial
#      (L0, accepted), converges, and a converged run regenerates nothing;
#   4. an explicit --set freezes its knob: no trial ever moves it;
#   5. sim.tune.profile=off and lhd.incremental=false read and write no tune data;
#   6. --run-only with a codegen knob that differs from the built binary is a
#      usage error (drv.bin's --set parser), while the equal value is accepted;
#   7. a direct `drv.bin --set sim.tune.profile=on` run is ingested by the next
#      `lhd sim` in that workdir -- of that design only: another design sharing
#      the workdir discards it;
#   8. an explicit sim.unknown_zero is forwarded to drv.bin whenever the user set
#      it, and the envelope's fill/reproduce say what the binary actually got;
#   9. a run that will not profile (explicit checkpoint settings) never applies a
#      pending trial; pinning the knob the trial moves (the L2 trial moves the
#      fence) makes it stale (closed without charging an attempt, never applied);
#  10. trials that can never be compared (the args change every run) converge
#      (`exhausted`) after 2 attempts instead of looping;
#  11. a crashing trial binary is closed as run-failed and reverted at the end of
#      that run, re-proposed only after a fresh incumbent run, and banned after
#      its second failure;
#  12. a design edit between a trial's setup and its verdict never swaps the
#      pre-edit tree back in: the next --run-only refuses the closed trial tree
#      and a setup rebuilds the incumbent of the edited design;
#  13. a trial run too short for the baseline's 0.2 s CPU gate is still judged;
#  14. a trial closed without a swap back (no retained clone) marks its tree
#      closed: a later --run-only, in any mode, is a usage error naming the
#      vector and the result, never a silent run reported as the incumbent;
#  15. the oracle is not gated by the trial's CPU gate: a diverging trial run
#      too short to time is a DIVERGENCE (banned, error), its unswapped tree is
#      closed and its binary removed;
#  16. a setup whose tree already holds the open trial keeps the SAME attempt:
#      --setup-only twice then --run-only judges it with one attempt, and (in a
#      fresh workdir) --setup-only then a full run judges it in the full run;
#  17. an explicit `on` re-opens a vector `auto` exhausted ONCE, then converges
#      `exhausted` itself instead of re-applying it every other run;
#  18. a --run-only of a tree another design generated in a shared workdir
#      runs as built (warning), unprofiled, never reported as the store's;
#  and (1b) an observation run (--list-signals) in a tuned workdir reuses the
#  tuned tree: nothing regenerates and the store is not touched.
#
# The lifecycle sections (9-17) drive the idle design's L2 trial out of the
# default L1. The fixtures need millions of cycles to pass the tuner's quality
# gate (>= 0.2 s CPU), passed through the tests' `cycles` parameter.

set -u

LHD="${LHD:-lhd/lhd}"
W="${TEST_TMPDIR:-/tmp/lhd_sim_tune_profile_$$}"
IDLE="${SIM_TUNE_IDLE:-inou/prp/tests/sim/tune_idle_tables.prp}"
LFSR="${SIM_TUNE_LFSR:-inou/prp/tests/sim/tune_lfsr.prp}"
N_IDLE="${SIM_TUNE_IDLE_CYCLES:-4000000}"
N_LFSR="${SIM_TUNE_LFSR_CYCLES:-6000000}"
JOBS=(--set sim.jobs=4)
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

# `tune FILE PATH`: one field of the envelope's sim_tune member, as JSON text
# (strings unquoted). PATH is dotted: `applied.dirty`, `verdict.result`.
tune() {
  python3 - "$1" "$2" <<'PY'
import json, sys
d = json.load(open(sys.argv[1]))
v = d.get("sim_tune")
for k in sys.argv[2].split("."):
    v = None if v is None else v.get(k)
print(v if isinstance(v, str) else json.dumps(v))
PY
}

# `records STORE KIND`: how many records of that kind a tune.jsonl holds.
records() {
  [ -f "$1" ] || { echo 0; return; }
  python3 - "$1" "$2" <<'PY'
import json, sys
n = 0
for line in open(sys.argv[1]):
    line = line.strip()
    if line and json.loads(line).get("kind") == sys.argv[2]:
        n += 1
print(n)
PY
}

sim() {
  local out="$1"
  shift
  "$LHD" sim "$@" "${JOBS[@]}" -q --result-json "$out" 2>"$out.err"
  local rc=$?
  [ -f "$out" ] || fail "lhd sim wrote no envelope ($*): $(cat "$out.err")"
  return $rc
}

# `simd`: like `sim`, but the diagnostics reach $out.err (one JSON object per
# line) for the checks that read their text.
simd() {
  local out="$1"
  shift
  "$LHD" sim "$@" "${JOBS[@]}" --result-json "$out" >/dev/null 2>"$out.err"
  local rc=$?
  [ -f "$out" ] || fail "lhd sim wrote no envelope ($*): $(cat "$out.err")"
  return $rc
}

store_of() { ls "$1"/incr/scopes/sim/*/tune.jsonl 2>/dev/null | head -1; }

# `cow`: whether $W supports the copy-on-write clone the tuner retains its
# incumbent tree with (lhd_tune.cpp clone_tree: `cp --reflink=always` on Linux,
# clonefile on macOS). Only without it may a losing or crashed trial keep no
# tree, so the "no retained incumbent tree" fallback is accepted only then.
cow() {
  echo x >"$W/.cow_src" || return 1
  rm -f "$W/.cow_dst"
  local rc=1
  case "$(uname -s)" in
    Linux) /bin/cp --reflink=always "$W/.cow_src" "$W/.cow_dst" 2>/dev/null && rc=0 ;;
    Darwin) python3 -c 'import ctypes, sys; sys.exit(ctypes.CDLL(None).clonefile(sys.argv[1].encode(), sys.argv[2].encode(), 0) != 0)' \
      "$W/.cow_src" "$W/.cow_dst" 2>/dev/null && rc=0 ;;
  esac
  rm -f "$W/.cow_src" "$W/.cow_dst"
  return $rc
}

# `row FILE KEY`: a field of the envelope's first test row.
row() {
  python3 - "$1" "$2" <<'PY'
import json, sys
t = json.load(open(sys.argv[1])).get("tests") or [{}]
v = t[0].get(sys.argv[2])
print(v if isinstance(v, str) else json.dumps(v))
PY
}

# `last STORE KIND FIELD`: a field of the newest record of that kind.
last() {
  python3 - "$1" "$2" "$3" <<'PY'
import json, sys
v = None
for line in open(sys.argv[1]):
    line = line.strip()
    if line:
        d = json.loads(line)
        if d.get("kind") == sys.argv[2]:
            v = d.get(sys.argv[3])
print(v if isinstance(v, str) else json.dumps(v))
PY
}

# `jget FILE KEY`: a top-level field of a JSON file.
jget() { python3 -c 'import json,sys; v=json.load(open(sys.argv[1])).get(sys.argv[2]); print(v if isinstance(v,str) else json.dumps(v))' "$1" "$2"; }

L0="tv1:d=off;f=none;lw=256;be=slop"
L1="tv1:d=on;f=16;lw=256;be=slop" # the built-in default: every workdir's first incumbent
L2="tv1:d=on;f=0;lw=256;be=slop"  # the idle design's trial out of L1

# Each Bazel target owns one independent lifecycle and a fresh scratch directory.
# Running this script without an argument still exercises every lifecycle.
group="${1:-all}"
case "$group" in
  all|profile|lfsr|pins|unknown_zero|stale|exhaustion|crash|edit|trial_gate|closed|divergence|resetup) ;;
  *) fail "unknown tuning test group: $group" ;;
esac
selected() { [ "$group" = all ] || [ "$group" = "$1" ]; }

if selected profile; then
# ---- 0. the integrated driver must carry the --set parser and the sampler ------
sim "$W/probe.json" "$LFSR" --workdir "$W/probe" --setup-only || fail "setup of $LFSR failed: $(cat "$W/probe.json")"
grep -q "driver-set-parser: 1" "$W/probe/sim/drv.cpp" || fail "drv.cpp lacks the driver-set-parser marker"
grep -q "tune-sampler: 1" "$W/probe/sim/drv.cpp" || fail "drv.cpp lacks the tune-sampler marker"
[ "$(tune "$W/probe.json" enabled)" = "true" ] || fail "tuner not enabled on a user --workdir: $(cat "$W/probe.json")"
fi

if selected profile; then
# ---- 1. idle tables converge within 2 runs (L1 default -> L2 judged) ------------
I="$W/idle"
for i in 1 2; do
  sim "$W/idle$i.json" "$IDLE" --workdir "$I" --arg cycles="$N_IDLE" || fail "idle run $i failed: $(cat "$W/idle$i.json")"
  echo "idle run $i: $(tune "$W/idle$i.json" note)"
done
[ "$(tune "$W/idle1.json" profiling)" = "true" ] || fail "run 1 did not profile"
[ "$(tune "$W/idle1.json" stats.qualifies)" = "true" ] || fail "run 1 is a smoke run (raise SIM_TUNE_IDLE_CYCLES): $(tune "$W/idle1.json" stats)"
[ "$(tune "$W/idle1.json" fill)" = "zero" ] || fail "a profiling run must zero-fill ? literals"
[ "$(tune "$W/idle1.json" applied.vector)" = "$L1" ] || fail "run 1 must build the default L1: $(tune "$W/idle1.json" note)"
[ "$(tune "$W/idle1.json" source.dirty)" = "default" ] || fail "run 1: dirty must come from the default"
[ "$(tune "$W/idle1.json" pending)" = "$L2" ] \
  || fail "run 1 must leave trial L2 pending: $(tune "$W/idle1.json" note)"
[ "$(tune "$W/idle2.json" trial.vector)" = "$L2" ] || fail "run 2 did not run the L2 trial"
case "$(tune "$W/idle2.json" verdict.result)" in
  accepted | rejected) ;;
  *) fail "run 2 must judge the L2 trial: $(tune "$W/idle2.json" verdict)" ;;
esac
[ "$(tune "$W/idle2.json" verdict.oracle)" = "equal" ] || fail "L2 oracle: $(tune "$W/idle2.json" verdict)"
[ "$(tune "$W/idle2.json" converged)" = "true" ] || fail "idle design not converged after 2 runs: $(tune "$W/idle2.json" note)"
# Converged: reused with no sampling; dirty=on from the store.
sim "$W/idle4.json" "$IDLE" --workdir "$I" --arg cycles=1000 || fail "idle run 4 failed"
[ "$(tune "$W/idle4.json" profiling)" = "false" ] || fail "a converged auto workdir must not profile"
[ "$(tune "$W/idle4.json" applied.dirty)" = "on" ] || fail "the converged decision is not applied: $(tune "$W/idle4.json" note)"
[ "$(tune "$W/idle4.json" source.dirty)" = "store" ] || fail "source must be store: $(tune "$W/idle4.json" source)"
[ "$(tune "$W/idle4.json" trial)" = "null" ] || fail "no trial after convergence"
STORE=$(store_of "$I")
[ -n "$STORE" ] || fail "no tune store under $I/incr/scopes/sim"
[ "$(records "$STORE" trial)" = "1" ] || fail "the idle design must take exactly 1 trial (L2)"
[ -f "$I/.lhd_sim.lock" ] || fail "no workdir lock file"
ls "$I"/sim_tune/v-* >/dev/null 2>&1 && fail "a retained incumbent tree outlived its verdict"
fi

if selected profile; then
# ---- 1b. an observation run reuses the tuned tree (options-ux:F2) ---------------
touch "$W/idle.marker"
sleep 1
lines_before=$(wc -l <"$STORE")
sim "$W/idle_obs.json" "$IDLE" --workdir "$I" --arg cycles=1000 --list-signals || fail "--list-signals failed: $(cat "$W/idle_obs.json")"
[ "$(tune "$W/idle_obs.json" reason)" = "observation" ] || fail "--list-signals must keep the tuner off"
[ "$(tune "$W/idle_obs.json" applied.dirty)" = "on" ] || fail "an observation run must build the tuned vector: $(tune "$W/idle_obs.json" note)"
[ "$(tune "$W/idle_obs.json" source.dirty)" = "store" ] || fail "an observation run reads the store's decision"
changed=$(find "$I/sim" -maxdepth 1 \( -name '*.cpp' -o -name '*.hpp' -o -name drv.bin \) -newer "$W/idle.marker" | head -5)
[ -z "$changed" ] || fail "an observation run in a tuned workdir regenerated/rebuilt: $changed"
[ "$(wc -l <"$STORE")" = "$lines_before" ] || fail "an observation run wrote to the tune store"
fi

if selected profile; then
# ---- 2. export -> import into a fresh workdir -----------------------------------
sim "$W/export.json" "$IDLE" --workdir "$I" --setup-only --set sim.tune.export="$W/idle.simtune.json" \
  || fail "export failed: $(cat "$W/export.json")"
python3 - "$W/idle.simtune.json" <<'PY' || fail "bad export file: $(cat "$W/idle.simtune.json")"
import json, sys
d = json.load(open(sys.argv[1]))
assert d["schema"] == "lhd-sim-tune-file-1", d
assert d["knobs"]["dirty"] == "on", d
assert d["provenance"]["converged"] is True, d
assert d["vector"].startswith("tv1:d=on;"), d
PY
grep -q "idle.simtune.json" "$W/export.json" || fail "the export is not listed in the envelope outputs"
sim "$W/import.json" "$IDLE" --workdir "$W/fresh" --setup-only --set sim.tune.profile=off \
  --set sim.tune.file="$W/idle.simtune.json" || fail "import failed: $(cat "$W/import.json")"
[ "$(tune "$W/import.json" applied.vector)" = "$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["vector"])' "$W/idle.simtune.json")" ] \
  || fail "the imported vector is not applied: $(tune "$W/import.json" note)"
[ "$(tune "$W/import.json" source.dirty)" = "file" ] || fail "an imported knob's source must be file"
[ -d "$W/fresh/incr/scopes/sim" ] && fail "profile=off must not create a tune store"
grep -q '"vector":"tv1:d=on' "$W/fresh/sim/tune_applied.json" || fail "tune_applied.json does not record the imported vector"
# A missing tune file is a missing_file error, not a silent default.
sim "$W/nofile.json" "$IDLE" --workdir "$W/fresh" --setup-only --set sim.tune.file="$W/nope.json" \
  && fail "a missing sim.tune.file must fail"
grep -q '"class":"missing_file"' "$W/nofile.json" || fail "missing sim.tune.file: wrong error class $(cat "$W/nofile.json")"
fi

if selected lfsr; then
# ---- 3. an LFSR descends to L0 with one trial, then regenerates nothing ------------
L="$W/lfsr"
for i in 1 2; do
  sim "$W/lfsr$i.json" "$LFSR" --workdir "$L" --arg cycles="$N_LFSR" || fail "lfsr run $i failed: $(cat "$W/lfsr$i.json")"
  echo "lfsr run $i: $(tune "$W/lfsr$i.json" note)"
done
[ "$(tune "$W/lfsr1.json" stats.qualifies)" = "true" ] || fail "lfsr run 1 is a smoke run (raise SIM_TUNE_LFSR_CYCLES): $(tune "$W/lfsr1.json" stats)"
[ "$(tune "$W/lfsr1.json" pending)" = "$L0" ] || fail "the LFSR must trial L0 out of the default: $(tune "$W/lfsr1.json" note)"
[ "$(tune "$W/lfsr2.json" trial.vector)" = "$L0" ] || fail "lfsr run 2 did not run the L0 trial"
# Which vector wins is a timing verdict (this small fixture spends ~18% more
# instructions at L1 but, under a loaded test host, can tie on cycles); that it
# is JUDGED, and the store converges on the verdict's vector, is the contract.
case "$(tune "$W/lfsr2.json" verdict.result)" in
  accepted) LFSR_KEPT="$L0" ;;
  rejected) LFSR_KEPT="$L1" ;;
  *) fail "lfsr run 2 must judge the L0 trial: $(tune "$W/lfsr2.json" verdict)" ;;
esac
[ "$(tune "$W/lfsr2.json" verdict.oracle)" = "equal" ] || fail "L0 oracle: $(tune "$W/lfsr2.json" verdict)"
[ "$(tune "$W/lfsr2.json" converged)" = "true" ] || fail "the LFSR must converge after its one trial: $(tune "$W/lfsr2.json" note)"
# A rejected trial on a filesystem without CoW has no retained tree to
# restore. Its documented fallback needs one setup to rebuild the incumbent;
# subsequent converged setups must still preserve their generated sources.
if tune "$W/lfsr2.json" note | grep -q "no retained incumbent tree"; then
  cow && fail "copy-on-write is available in $W but the rejected LFSR trial kept no incumbent tree"
  [ "$LFSR_KEPT" = "$L1" ] || fail "an accepted trial must not require restoration"
  sim "$W/lfsr_restore.json" "$LFSR" --workdir "$L" --setup-only || fail "incumbent restoration setup failed"
  [ "$(tune "$W/lfsr_restore.json" applied.vector)" = "$L1" ] || fail "restoration must select the incumbent"
fi
touch "$W/lfsr.marker"
sleep 1
sim "$W/lfsr3.json" "$LFSR" --workdir "$L" --arg cycles=1000 || fail "lfsr run 3 failed"
[ "$(tune "$W/lfsr3.json" profiling)" = "false" ] || fail "a converged LFSR workdir must not profile"
[ "$(tune "$W/lfsr3.json" applied.vector)" = "$LFSR_KEPT" ] || fail "the converged LFSR must run the verdict's vector ($LFSR_KEPT)"
changed=$(find "$L/sim" -maxdepth 1 \( -name '*.cpp' -o -name '*.hpp' \) -newer "$W/lfsr.marker" | head -5)
[ -z "$changed" ] || fail "a converged run regenerated: $changed"
[ "$(records "$(store_of "$L")" trial)" = "1" ] || fail "the LFSR must take exactly 1 trial"
fi

if selected lfsr; then
# ---- 6. --run-only codegen knobs are checked against the built binary -------------
# The LFSR tree holds the vector section 3 converged on.
BAKED=on OTHER=off
[ "$LFSR_KEPT" = "$L0" ] && BAKED=off OTHER=on
sim "$W/ro_bad.json" "$LFSR" --workdir "$L" --run-only --arg cycles=1000 --set sim.tune.dirty=$OTHER \
  && fail "--run-only with a different sim.tune.dirty must fail"
grep -q '"class":"usage"' "$W/ro_bad.json" || fail "codegen mismatch: wrong error class $(cat "$W/ro_bad.json")"
sim "$W/ro_ok.json" "$LFSR" --workdir "$L" --run-only --arg cycles=1000 --set sim.tune.dirty=$BAKED \
  || fail "--run-only with the baked sim.tune.dirty must pass: $(cat "$W/ro_ok.json") $(cat "$W/ro_ok.json.err")"
fi

if selected lfsr; then
# ---- 7. a direct drv.bin profiling run is ingested by the next lhd call -----------
"$L/sim/drv.bin" --set sim.tune.profile=on --cycles 1000 >/dev/null || fail "direct drv.bin run failed"
ls "$L"/sim/tune_runs/*.json >/dev/null 2>&1 || fail "a direct profiling drv.bin wrote no raw run file"
before=$(records "$(store_of "$L")" run)
sim "$W/lfsr4.json" "$LFSR" --workdir "$L" --setup-only || fail "setup after a direct run failed"
ls "$L"/sim/tune_runs/*.json >/dev/null 2>&1 && fail "the raw run file was not consumed"
[ "$(records "$(store_of "$L")" run)" = "$((before + 1))" ] || fail "the direct run was not recorded"
grep -q '"src":"direct"' "$(store_of "$L")" || fail "the direct run is not marked src=direct"
# ... but ONLY into its own design's store: another design sharing the workdir
# discards it (caching-retention:F4) instead of converging on its stats.
"$L/sim/drv.bin" --set sim.tune.profile=on --cycles 1000 >/dev/null || fail "direct drv.bin run 2 failed"
sim "$W/foreign.json" "$IDLE" --workdir "$L" --setup-only || fail "a second design's setup in the workdir failed"
ls "$L"/sim/tune_runs/*.json >/dev/null 2>&1 && fail "the foreign raw run file was left behind"
tune "$W/foreign.json" note | grep -q "discarded raw run file" || fail "the foreign run was not discarded: $(tune "$W/foreign.json" note)"
for st in "$L"/incr/scopes/sim/*/tune.jsonl; do
  case "$st" in
    */tune_lfsr/*) ;;
    *) [ "$(records "$st" run)" = "0" ] || fail "another design's run was ingested into $st" ;;
  esac
done
fi

if selected pins; then
# ---- 4. an explicit --set freezes its knob ------------------------------------------
P="$W/pin"
sim "$W/pin.json" "$IDLE" --workdir "$P" --arg cycles="$N_IDLE" --set sim.tune.profile=on --set sim.tune.dirty=off \
  || fail "pinned run failed: $(cat "$W/pin.json")"
[ "$(tune "$W/pin.json" profiling)" = "true" ] || fail "profile=on must profile"
[ "$(tune "$W/pin.json" source.dirty)" = "explicit" ] || fail "an explicit knob's source must be explicit"
[ "$(tune "$W/pin.json" pending)" = "null" ] || fail "a trial proposed to move a pinned knob: $(tune "$W/pin.json" note)"
[ "$(records "$(store_of "$P")" trial)" = "0" ] || fail "the pinned store holds a trial"
[ "$(records "$(store_of "$P")" run)" = "1" ] || fail "a pinned run still feeds the ledger"
fi

if selected pins; then
# ---- 5. off / incremental=false read and write nothing -----------------------------
for how in "sim.tune.profile=off" "lhd.incremental=false"; do
  d="$W/off_${how%%=*}"
  sim "$W/off.json" "$LFSR" --workdir "$d" --arg cycles=1000 --set "$how" || fail "$how run failed: $(cat "$W/off.json")"
  [ "$(tune "$W/off.json" enabled)" = "false" ] || fail "$how must disable the tuner"
  [ "$(tune "$W/off.json" profiling)" = "false" ] || fail "$how must not profile"
  [ -e "$d/incr/scopes/sim" ] && fail "$how created a tune store"
  [ -e "$d/sim_tune" ] && fail "$how created sim_tune/"
  ls "$d"/sim/tune_runs/*.json >/dev/null 2>&1 && fail "$how left a raw run file"
done
fi

if selected unknown_zero; then
# ---- 8. explicit sim.unknown_zero reaches drv.bin (options-ux:F1) ------------------
cat >"$W/uz.prp" <<'PRP'
pub mod uz(a:u64) -> (r:u64@[0]) {
  mut c:u64 = 0sb?
  r = a ^ c
}

test uz.show {
  mut dut       = uz
  mut first:u64 = 0
  tick 8 {
    dut.a = 0
    step
    if clock == 0 {
      first = dut.r
    }
  }
  puts("first={first}")
}
PRP
sim "$W/uz0.json" "$W/uz.prp" --workdir "$W/uz" --setup-only || fail "uz setup failed: $(cat "$W/uz0.json")"
sim "$W/uz1.json" "$W/uz.prp" --workdir "$W/uz" --run-only --set sim.unknown_zero=true || fail "uz run 1 failed"
[ "$(tune "$W/uz1.json" profiling)" = "true" ] || fail "uz run 1 must profile (auto, not converged)"
[ "$(row "$W/uz1.json" rng_draws)" = "0" ] || fail "explicit sim.unknown_zero=true under profiling drew random bits"
[ "$(tune "$W/uz1.json" fill)" = "zero" ] || fail "uz run 1: fill must be zero"
sim "$W/uz2.json" "$W/uz.prp" --workdir "$W/uz" --run-only --set sim.tune.profile=off || fail "uz run 2 failed"
[ "$(row "$W/uz2.json" rng_draws)" != "0" ] || fail "an unprofiled run with no sim.unknown_zero must draw random bits"
[ "$(tune "$W/uz2.json" fill)" = "random" ] || fail "uz run 2: the envelope must report fill=random"
tune "$W/uz2.json" reproduce | grep -q unknown_zero && fail "uz run 2: reproduce must not claim sim.unknown_zero"
sim "$W/uz3.json" "$W/uz.prp" --workdir "$W/uz" --run-only --set sim.unknown_zero=false || fail "uz run 3 failed"
[ "$(row "$W/uz3.json" rng_draws)" != "0" ] || fail "explicit sim.unknown_zero=false under profiling must draw"
[ "$(tune "$W/uz3.json" fill)" = "random" ] || fail "uz run 3: fill must be random"
sim "$W/uzb0.json" "$W/uz.prp" --workdir "$W/uzb" --setup-only --set sim.unknown_zero=true || fail "uz baked setup failed"
sim "$W/uzb1.json" "$W/uz.prp" --workdir "$W/uzb" --run-only --set sim.unknown_zero=false \
  && fail "sim.unknown_zero=false on a binary generated with true must be refused"
grep -q '"class":"usage"' "$W/uzb1.json" || fail "baked unknown_zero=false: not a usage error $(cat "$W/uzb1.json")"
sim "$W/uzb2.json" "$W/uz.prp" --workdir "$W/uzb" --run-only --set sim.tune.profile=off || fail "uz baked run failed"
[ "$(tune "$W/uzb2.json" fill)" = "zero" ] || fail "a binary generated with sim.unknown_zero=true zero-fills"
[ "$(row "$W/uzb2.json" rng_draws)" = "0" ] || fail "a baked unknown_zero binary drew random bits"
fi

if selected stale; then
# ---- 9. pins make a pending trial stale ----------------------------------------------
T="$W/stale"
sim "$W/st1.json" "$IDLE" --workdir "$T" --arg cycles="$N_IDLE" || fail "stale run 1 failed"
[ "$(tune "$W/st1.json" pending)" = "$L2" ] || fail "stale run 1 must propose L2: $(tune "$W/st1.json" note)"
# A run that will not profile (auto + explicit checkpoint settings) never applies
# the pending trial: it would run an unverified vector unjudged (policy:F4).
sim "$W/st1b.json" "$IDLE" --workdir "$T" --arg cycles=1000 --set sim.checkpoint_every=100000 || fail "checkpoint run failed"
[ "$(tune "$W/st1b.json" trial)" = "null" ] || fail "a non-profiled run applied the pending trial: $(tune "$W/st1b.json" note)"
[ "$(tune "$W/st1b.json" applied.vector)" = "$L1" ] || fail "a non-profiled run must build the incumbent"
[ "$(tune "$W/st1b.json" pending)" = "$L2" ] || fail "the trial must stay pending"
[ "$(records "$(store_of "$T")" attempt)" = "0" ] || fail "a non-profiled run charged an attempt"
sim "$W/st2.json" "$IDLE" --workdir "$T" --arg cycles="$N_IDLE" --set sim.tune.fence=16 || fail "stale run 2 failed"
[ "$(tune "$W/st2.json" trial)" = "null" ] || fail "a trial moving a pinned knob was applied: $(tune "$W/st2.json" note)"
[ "$(tune "$W/st2.json" applied.fence)" = "16" ] && [ "$(tune "$W/st2.json" source.fence)" = "explicit" ] \
  || fail "the explicit pin must win: $(tune "$W/st2.json" source)"
tune "$W/st2.json" verdict.reason | grep -q '^stale' || fail "the pinned trial must close as stale: $(tune "$W/st2.json" verdict)"
[ "$(records "$(store_of "$T")" attempt)" = "0" ] || fail "a stale trial charged an attempt"
[ "$(last "$(store_of "$T")" verdict charged)" = "false" ] || fail "a stale verdict must not be charged"
fi

if selected exhaustion; then
# ---- 10. incomparable trials converge instead of looping (policy:F1) ------------------
V="$W/vary"
for i in 1 2 3 4 5; do
  sim "$W/vary$i.json" "$IDLE" --workdir "$V" --arg cycles=$((N_IDLE + i)) || fail "vary run $i failed: $(cat "$W/vary$i.json")"
  echo "vary run $i: $(tune "$W/vary$i.json" note)"
done
[ "$(tune "$W/vary2.json" verdict.result)" = "abandoned" ] || fail "an incomparable trial must be abandoned"
[ "$(tune "$W/vary2.json" pending)" = "null" ] || fail "an abandoned trial was re-proposed at once"
[ "$(tune "$W/vary3.json" applied.vector)" = "$L1" ] || fail "after an abandoned attempt the incumbent must run first"
[ "$(tune "$W/vary5.json" converged)" = "true" ] || fail "auto did not converge after 2 attempts: $(tune "$W/vary5.json" note)"
[ "$(tune "$W/vary5.json" profiling)" = "false" ] || fail "a converged workdir kept profiling"
[ "$(records "$(store_of "$V")" attempt)" = "2" ] || fail "expected exactly 2 attempts"
[ "$(last "$(store_of "$V")" decision reason)" = "exhausted" ] || fail "convergence reason must be exhausted"
ls "$V"/sim_tune/v-* >/dev/null 2>&1 && fail "a retained tree outlived its abandoned attempt"
fi

if selected crash; then
# ---- 11. a crashing trial binary is reverted, then banned (caching-retention:F2) -----
C="$W/crash"
sim "$W/cr0.json" "$IDLE" --workdir "$C" --arg cycles="$N_IDLE" || fail "crash run 0 failed"
[ "$(tune "$W/cr0.json" pending)" = "$L2" ] || fail "crash run 0 must propose L2"
for k in 1 2; do
  sim "$W/cr${k}a.json" "$IDLE" --workdir "$C" --setup-only || fail "crash setup $k failed"
  [ "$(tune "$W/cr${k}a.json" trial.vector)" = "$L2" ] || fail "setup $k must apply the L2 trial: $(tune "$W/cr${k}a.json" note)"
  sim "$W/cr${k}b.json" "$IDLE" --workdir "$C" --run-only --set sim.compile_only=true --arg cycles="$N_IDLE" \
    || fail "crash build $k failed: $(cat "$W/cr${k}b.json")"
  printf '#!/bin/sh\nkill -SEGV $$\n' >"$C/sim/drv.bin" # stands in for a trial-vector miscompile that crashes
  chmod +x "$C/sim/drv.bin"
  sim "$W/cr${k}c.json" "$IDLE" --workdir "$C" --run-only --arg cycles="$N_IDLE" && fail "the crashing trial run must fail"
  [ "$(tune "$W/cr${k}c.json" verdict.result)" = "run-failed" ] || fail "crash $k: $(tune "$W/cr${k}c.json" note)"
  if cow; then
    tune "$W/cr${k}c.json" note | grep -q "reverted to the retained incumbent tree" \
      || fail "crash $k was not reverted (copy-on-write is available in $W): $(tune "$W/cr${k}c.json" note)"
  else
    tune "$W/cr${k}c.json" note | grep -q "no retained incumbent tree" || fail "crash $k did not report the no-CoW fallback"
    sim "$W/cr${k}_closed.json" "$IDLE" --workdir "$C" --run-only --arg cycles=1000 \
      && fail "the failed trial without a retained tree must refuse --run-only"
    [ "$(row "$W/cr${k}_closed.json" verdict)" != "pass" ] || fail "closed trial reported a passing test"
    grep -q '"class":"usage"' "$W/cr${k}_closed.json" || fail "closed trial must require a setup"
  fi
  sim "$W/cr${k}d.json" "$IDLE" --workdir "$C" --arg cycles="$N_IDLE" || fail "the incumbent after crash $k failed"
  [ "$(tune "$W/cr${k}d.json" applied.vector)" = "$L1" ] || fail "after crash $k the incumbent must run"
  echo "crash $k: $(tune "$W/cr${k}c.json" note) || next: $(tune "$W/cr${k}d.json" note)"
done
[ "$(tune "$W/cr1d.json" pending)" = "$L2" ] || fail "one crash must not ban: $(tune "$W/cr1d.json" note)"
tune "$W/cr2d.json" rejected | grep -q "d=on;f=0" || fail "two crashes on one structure must ban L2"
[ "$(tune "$W/cr2d.json" converged)" = "true" ] || fail "auto must converge after the ban"
fi

if selected edit; then
# ---- 12. an edit between the trial's setup and its verdict (policy:F3) ----------------
mkdir -p "$W/edit_src"
cp "$IDLE" "$W/edit_src/tune_idle_tables.prp"
ED="$W/edit_src/tune_idle_tables.prp"
E="$W/edit"
sim "$W/ed1.json" "$ED" --workdir "$E" --arg cycles="$N_IDLE" || fail "edit run 1 failed"
[ "$(tune "$W/ed1.json" pending)" = "$L2" ] || fail "edit run 1 must propose L2"
S1=$(jget "$E/sim/tune_applied.json" structure)
python3 - "$ED" <<'PY' || fail "could not edit the design"
import sys
p = sys.argv[1]
s = open(p).read()
assert "+ round\n" in s
open(p, "w").write(s.replace("+ round\n", "+ round + 1\n", 1))
PY
sim "$W/ed2.json" "$ED" --workdir "$E" --setup-only || fail "edit setup failed"
[ "$(tune "$W/ed2.json" trial.vector)" = "$L2" ] || fail "the edit setup must apply the trial"
S2=$(jget "$E/sim/tune_applied.json" structure)
{ [ -n "$S1" ] && [ -n "$S2" ] && [ "$S1" != "$S2" ]; } || fail "the edit did not change the structure ($S1 / $S2)"
sim "$W/ed3.json" "$ED" --workdir "$E" --run-only --arg cycles="$N_IDLE" || fail "edit trial run failed"
tune "$W/ed3.json" note | grep -q "reverted to the retained incumbent tree" && fail "the pre-edit tree was swapped in"
[ "$(jget "$E/sim/tune_applied.json" structure)" = "$S2" ] || fail "the tree no longer holds the edited design"
# The abandoned trial could not be swapped back (the retained clone was the
# pre-edit design), so its tree is marked closed: a --run-only refuses it
# (lifecycle:R2-1) instead of running an unjudged vector as the incumbent ...
sim "$W/ed4.json" "$ED" --workdir "$E" --run-only --arg cycles="$N_IDLE" \
  && fail "a --run-only of a trial tree closed without a swap must be refused: $(tune "$W/ed4.json" note)"
grep -q '"class":"usage"' "$W/ed4.json" || fail "the closed trial tree: not a usage error $(cat "$W/ed4.json")"
[ "$(jget "$E/sim/tune_applied.json" closed)" = "abandoned" ] || fail "the unswapped trial tree is not marked closed"
# ... and a setup rebuilds the incumbent of the EDITED design.
sim "$W/ed5.json" "$ED" --workdir "$E" --setup-only || fail "edit re-setup failed"
[ "$(tune "$W/ed5.json" applied.vector)" = "$L1" ] || fail "the re-setup must build the incumbent: $(tune "$W/ed5.json" note)"
[ "$(jget "$E/sim/tune_applied.json" structure)" = "$S2" ] || fail "the re-setup did not build the edited design"
sim "$W/ed6.json" "$ED" --workdir "$E" --run-only --arg cycles="$N_IDLE" || fail "edit run-only failed"
[ "$(last "$(store_of "$E")" run structure)" = "$S2" ] || fail "--run-only ran a stale design"
[ "$(last "$(store_of "$E")" run vector)" = "$L1" ] || fail "--run-only did not run the incumbent"
fi

if selected trial_gate; then
# ---- 13. a fast trial run is judged on the trial gate (policy:F2) ----------------------
F="$W/fast"
N_FAST="${SIM_TUNE_FAST_CYCLES:-1500000}"
sim "$W/fa1.json" "$IDLE" --workdir "$F" --arg cycles="$N_FAST" || fail "fast run 1 failed"
if [ "$(tune "$W/fa1.json" stats.qualifies)" = "true" ]; then
  sim "$W/fa2.json" "$IDLE" --workdir "$F" --arg cycles="$N_FAST" || fail "fast run 2 failed"
  case "$(tune "$W/fa2.json" verdict.result)" in
    accepted | rejected) ;;
    *) fail "the fast trial was not judged: $(tune "$W/fa2.json" note)" ;;
  esac
  if [ "$(tune "$W/fa2.json" stats.qualifies)" = "false" ]; then
    echo "fast trial: judged below the baseline gate ($(tune "$W/fa2.json" stats.cpu_ms) ms)"
  else
    echo "note: the trial run qualified anyway ($(tune "$W/fa2.json" stats.cpu_ms) ms): the trial gate was not exercised"
  fi
else
  echo "note: the fast baseline did not qualify on this host: section 13 skipped"
fi
fi

if selected closed; then
# ---- 14. a trial closed without a swap back is never run by --run-only (R2-1) ----------
# No retained incumbent (a host without copy-on-write, a clone of another
# design, a failed swap): stood in for by deleting the retained clone.
N="$W/noret"
sim "$W/nr1.json" "$IDLE" --workdir "$N" --arg cycles="$N_IDLE" || fail "noret run 1 failed"
[ "$(tune "$W/nr1.json" pending)" = "$L2" ] || fail "noret run 1 must propose L2"
sim "$W/nr2.json" "$IDLE" --workdir "$N" --setup-only || fail "noret setup failed"
[ "$(tune "$W/nr2.json" trial.vector)" = "$L2" ] || fail "noret setup must apply the trial"
rm -rf "$N"/sim_tune/v-*
# other arguments: no comparable baseline -> abandoned, and nothing to swap back
sim "$W/nr3.json" "$IDLE" --workdir "$N" --run-only --arg cycles=$((N_IDLE + 7)) || fail "noret trial run failed"
[ "$(tune "$W/nr3.json" verdict.result)" = "abandoned" ] || fail "noret trial: $(tune "$W/nr3.json" note)"
[ "$(jget "$N/sim/tune_applied.json" closed)" = "abandoned" ] || fail "the unswapped abandoned tree is not marked closed"
for mode in auto off; do
  sim "$W/nr4$mode.json" "$IDLE" --workdir "$N" --run-only --arg cycles=1000 --set sim.tune.profile=$mode \
    && fail "--run-only ($mode) ran a closed trial tree: $(tune "$W/nr4$mode.json" note)"
  grep -q '"class":"usage"' "$W/nr4$mode.json" || fail "closed trial tree ($mode): not a usage error $(cat "$W/nr4$mode.json")"
  python3 - "$W/nr4$mode.json" "$L2" <<'PY' || fail "the refusal must name the vector and the result: $(cat "$W/nr4$mode.json")"
import json, sys
m = json.load(open(sys.argv[1]))["error"]["message"]
assert sys.argv[2] in m and "abandoned" in m, m
PY
  [ "$(tune "$W/nr4$mode.json" source.dirty)" = "built" ] || fail "a refused tree must not be reported as the store's"
done
[ "$(records "$(store_of "$N")" attempt)" = "1" ] || fail "a refused --run-only charged an attempt"
sim "$W/nr5.json" "$IDLE" --workdir "$N" --setup-only || fail "noret re-setup failed"
[ "$(tune "$W/nr5.json" applied.vector)" = "$L1" ] || fail "the re-setup must rebuild the incumbent"
sim "$W/nr6.json" "$IDLE" --workdir "$N" --run-only --arg cycles=1000 || fail "--run-only after the re-setup failed"
[ "$(jget "$N/sim/tune_applied.json" trial)" = "false" ] || fail "the rebuilt tree is labeled a trial"
fi

if selected divergence; then
# ---- 15. the oracle ignores the trial gate: a short diverging trial (R2-2) ---------------
# A trial binary whose results differ from the incumbent's and whose run is too
# short to time (a miscompile that skips work). Stood in for by a wrapper that
# runs the real trial binary, then rewrites its raw record: another end-state
# digest and 1 ms of CPU (below the trial gate's 20 ms).
D="$W/diverge"
sim "$W/dv1.json" "$IDLE" --workdir "$D" --arg cycles="$N_IDLE" || fail "diverge run 1 failed"
[ "$(tune "$W/dv1.json" pending)" = "$L2" ] || fail "diverge run 1 must propose L2"
sim "$W/dv2.json" "$IDLE" --workdir "$D" --setup-only || fail "diverge setup failed"
sim "$W/dv2b.json" "$IDLE" --workdir "$D" --run-only --set sim.compile_only=true --arg cycles="$N_IDLE" \
  || fail "diverge build failed: $(cat "$W/dv2b.json")"
mv "$D/sim/drv.bin" "$D/sim/drv.real"
cat >"$D/sim/drv.bin" <<SH
#!/bin/sh
"$D/sim/drv.real" "\$@"
rc=\$?
python3 - "$D/sim_tune/inbox" <<'PY'
import glob, json, sys
for f in glob.glob(sys.argv[1] + "/*.json"):
    d = json.load(open(f))
    for t in d["tests"]:
        t["end_digest"] = "00000000deadbeef"
        t["cpu_ns"] = 1000000
    json.dump(d, open(f, "w"))
PY
exit \$rc
SH
chmod +x "$D/sim/drv.bin"
rm -rf "$D"/sim_tune/v-* # and no retained incumbent: the tree stays, closed
simd "$W/dv3.json" "$IDLE" --workdir "$D" --run-only --arg cycles="$N_IDLE" && fail "a diverging trial must fail the run"
echo "diverge: $(tune "$W/dv3.json" note)"
[ "$(tune "$W/dv3.json" stats.judgeable)" = "false" ] || fail "the tampered trial run must be below the trial gate"
[ "$(tune "$W/dv3.json" verdict.result)" = "divergence" ] || fail "a short diverging trial must be a divergence: $(tune "$W/dv3.json" note)"
tune "$W/dv3.json" rejected | grep -q "d=on;f=0" || fail "the diverging vector must be banned"
grep -q '"code":"sim-tune-divergence"' "$W/dv3.json.err" || fail "no sim-tune-divergence diagnostic: $(cat "$W/dv3.json.err")"
grep '"code":"sim-tune-divergence"' "$W/dv3.json.err" | grep -q "incumbent restored" \
  && fail "the divergence diag claims a restore that did not happen"
grep '"code":"sim-tune-divergence"' "$W/dv3.json.err" | grep -q "set aside" || fail "the divergence diag must say the tree was set aside"
[ -e "$D/sim/drv.bin" ] && fail "the diverging binary was left in the tree"
[ "$(jget "$D/sim/tune_applied.json" closed)" = "divergence" ] || fail "the diverged tree is not marked closed"
sim "$W/dv4.json" "$IDLE" --workdir "$D" --run-only --arg cycles=1000 && fail "--run-only of a diverged tree must be refused"
grep -q '"class":"usage"' "$W/dv4.json" || fail "diverged tree: not a usage error $(cat "$W/dv4.json")"
grep -q "diverged" "$W/dv4.json" || fail "the refusal must say the tree diverged"
sim "$W/dv5.json" "$IDLE" --workdir "$D" --arg cycles=1000 || fail "a full run after the divergence failed"
[ "$(tune "$W/dv5.json" applied.vector)" = "$L1" ] || fail "after the divergence the incumbent must be rebuilt"
fi

if selected resetup; then
# ---- 16. a setup whose tree holds the open trial keeps the attempt (R2-4) ----------------
R="$W/resetup"
sim "$W/rs1.json" "$IDLE" --workdir "$R" --arg cycles="$N_IDLE" || fail "resetup run 1 failed"
[ "$(tune "$W/rs1.json" pending)" = "$L2" ] || fail "resetup run 1 must propose L2"
sim "$W/rs2.json" "$IDLE" --workdir "$R" --setup-only || fail "resetup setup 1 failed"
sim "$W/rs3.json" "$IDLE" --workdir "$R" --setup-only || fail "resetup setup 2 failed"
[ "$(tune "$W/rs3.json" trial.vector)" = "$L2" ] || fail "a repeated --setup-only must keep building the trial: $(tune "$W/rs3.json" note)"
[ "$(tune "$W/rs3.json" verdict)" = "null" ] || fail "a repeated --setup-only closed the attempt: $(tune "$W/rs3.json" verdict)"
sim "$W/rs4.json" "$IDLE" --workdir "$R" --run-only --arg cycles="$N_IDLE" || fail "resetup run-only failed"
case "$(tune "$W/rs4.json" verdict.result)" in
  accepted | rejected) ;;
  *) fail "the trial set up twice was not judged by its run: $(tune "$W/rs4.json" note)" ;;
esac
[ "$(records "$(store_of "$R")" attempt)" = "1" ] || fail "two setups of one trial charged $(records "$(store_of "$R")" attempt) attempts"
# --setup-only then a FULL run: the full run judges the trial its setup applied.
R2="$W/resetup_full"
sim "$W/rs5.json" "$IDLE" --workdir "$R2" --arg cycles="$N_IDLE" || fail "resetup_full run 1 failed"
[ "$(tune "$W/rs5.json" pending)" = "$L2" ] || fail "resetup_full run 1 must propose L2"
sim "$W/rs6.json" "$IDLE" --workdir "$R2" --setup-only || fail "resetup_full setup failed"
[ "$(tune "$W/rs6.json" trial.vector)" = "$L2" ] || fail "the setup must apply the L2 trial"
sim "$W/rs7.json" "$IDLE" --workdir "$R2" --arg cycles="$N_IDLE" || fail "resetup_full full run failed"
[ "$(tune "$W/rs7.json" trial.vector)" = "$L2" ] || fail "the full run after --setup-only must run the trial"
case "$(tune "$W/rs7.json" verdict.result)" in
  accepted | rejected) ;;
  *) fail "the full run after --setup-only must judge the trial: $(tune "$W/rs7.json" note)" ;;
esac
[ "$(records "$(store_of "$R2")" attempt)" = "1" ] || fail "setup + full run charged more than one attempt"
fi

if selected exhaustion; then
# ---- 17. `on` re-opens an exhausted vector once, then converges (R2-3) -------------------
# Continues section 10's workdir, where `auto` exhausted L2 after 2 attempts.
for i in 6 7 8 9 10 11; do
  sim "$W/vary$i.json" "$IDLE" --workdir "$V" --arg cycles=$((N_IDLE + i)) --set sim.tune.profile=on \
    || fail "vary run $i (on) failed: $(cat "$W/vary$i.json")"
  echo "vary run $i (on): $(tune "$W/vary$i.json" note)"
done
# vary6: `on` profiles the incumbent (vary5 ran converged, unprofiled: no
# fresh baseline yet) and re-opens the exhausted L2; vary7 and vary9 are `on`'s
# own two attempts (abandoned: other arguments), vary8 the incumbent run in
# between; from vary10 on `on` has used its budget: converged, never applied
# again, still profiling.
[ "$(tune "$W/vary6.json" pending)" = "$L2" ] || fail "an explicit on must re-open the exhausted L2: $(tune "$W/vary6.json" note)"
for i in 7 9; do
  [ "$(tune "$W/vary$i.json" trial.vector)" = "$L2" ] || fail "on's attempt (run $i) did not run"
  [ "$(tune "$W/vary$i.json" verdict.result)" = "abandoned" ] || fail "the re-opened incomparable trial must be abandoned (run $i)"
done
[ "$(tune "$W/vary8.json" pending)" = "$L2" ] || fail "on must re-propose after a fresh incumbent run: $(tune "$W/vary8.json" note)"
for i in 10 11; do
  [ "$(tune "$W/vary$i.json" pending)" = "null" ] || fail "on kept re-proposing an incomparable trial (run $i): $(tune "$W/vary$i.json" note)"
  [ "$(tune "$W/vary$i.json" trial)" = "null" ] || fail "on re-applied the exhausted trial (run $i)"
  [ "$(tune "$W/vary$i.json" converged)" = "true" ] || fail "on did not converge after its own 2 attempts (run $i)"
  [ "$(tune "$W/vary$i.json" profiling)" = "true" ] || fail "on must keep profiling (run $i)"
done
[ "$(records "$(store_of "$V")" attempt)" = "4" ] || fail "expected 2 auto + 2 on attempts, got $(records "$(store_of "$V")" attempt)"
[ "$(last "$(store_of "$V")" decision reason)" = "exhausted" ] || fail "on must converge exhausted"
fi

if selected lfsr; then
# ---- 18. a --run-only of another design's tree (R2-5) ----------------------------------
# Section 7 left the IDLE design's tree in the LFSR workdir.
simd "$W/ro_foreign.json" "$LFSR" --workdir "$L" --run-only --arg cycles=1000 || fail "the foreign --run-only failed"
[ "$(tune "$W/ro_foreign.json" source.dirty)" = "built" ] || fail "a foreign tree must not be reported as the store's: $(tune "$W/ro_foreign.json" note)"
[ "$(tune "$W/ro_foreign.json" profiling)" = "false" ] || fail "a foreign tree must not be profiled"
tune "$W/ro_foreign.json" note | grep -q "generated for tune_idle_tables" || fail "the note must name the tree's design"
grep -q '"code":"sim-tune-foreign-tree"' "$W/ro_foreign.json.err" || fail "no sim-tune-foreign-tree warning: $(cat "$W/ro_foreign.json.err")"
fi

echo "PASS: sim.tune $group lifecycle"
