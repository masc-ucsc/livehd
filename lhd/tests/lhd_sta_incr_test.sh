#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# End-to-end test for INCREMENTAL `lhd pass opentimer` (docs/opt_loop_incr.md):
# a persistent whole-design STA result cache under --workdir, content-addressed
# by the NETLIST's canonical digest plus the timing environment.
#
# Why it exists: on the large blocks pass.opentimer is 70-97% of a warm
# `lhd synth`, so whole-flow synthesis reuse was capped at ~1.3x while its
# dominant pass had no cache at all. The properties under test:
#
#   1. A re-run over an UNCHANGED netlist replays the SAME timing report
#      (everything but the run's own reuse telemetry).
#   2. A hit never parses the Liberty (the lazy-startup rule pass.abc applies
#      to Abc_Start/read_lib) and still prints the `slowest delay:` line.
#   3. A CHANGED netlist misses, and reverting to the earlier netlist hits the
#      earlier record (the cache keeps one record per distinct netlist).
#   4. A changed option that moves the analysis (`margin`) misses.
#   5. `lhd.incremental=false` reports the tier as enabled=false and re-times to
#      exactly what the cache replays.
#   6. A salt mismatch (a new timing engine) drops every record.
#   7. Through `lhd synth --stats`, `resynth` is re-stamped from THIS run's
#      pass.abc and is the ONLY field a hit does not take from the cache.
#
# Hermetic: the vendored Liberty (inou/prp/tests/abc/test.lib), no PDK.
set -u

LHD=lhd/lhd
LIB=inou/prp/tests/abc/test.lib
FIX=inou/prp/tests/pyrope/abc_comb.prp
TOP=abc_comb.abc_comb
W="${TEST_TMPDIR:-/tmp/lhd_sta_incr_$$}"
mkdir -p "$W"
# The def name embeds the FILE name (internal naming = file.entity), so the
# edited and unedited sources live under the SAME name -- a real edit in place.
PRP="$W/abc_comb.prp"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}
run() { "$LHD" "$@" -q --result-json "$W/r.json" || fail "$* -> $(cat "$W/r.json" 2>/dev/null)"; }

# incremental.sta.<field> from the LAST run's envelope ("" when the tier is off).
sta_field() {
  python3 - "$W/r.json" "$1" <<'PY'
import json, sys
try:
    print(json.load(open(sys.argv[1]))["incremental"]["sta"][sys.argv[2]])
except Exception:
    print("")
PY
}
expect_sta() {  # HITS MISSES LABEL
  local h m
  h=$(sta_field hits)
  m=$(sta_field misses)
  [ "$h" = "$1" ] || fail "$3: expected $1 STA hit(s), got '$h'"
  [ "$m" = "$2" ] || fail "$3: expected $2 STA miss(es), got '$m'"
}

# The two timing reports must agree EXCEPT on the run's own reuse telemetry
# (`incremental`), which is the one member that is supposed to differ.
same_report() {  # FILE_A FILE_B
  python3 - "$1" "$2" <<'PY'
import json, sys

a = json.load(open(sys.argv[1]))
b = json.load(open(sys.argv[2]))
a.pop("incremental", None)
b.pop("incremental", None)
if a != b:
    sys.exit(f"report differs:\n  a={json.dumps(a)}\n  b={json.dumps(b)}")
PY
}

# The pass's own stdout lands in its numbered per-step log under --workdir, and
# a repeated step number APPENDS -- so these are cumulative counters, compared
# across runs rather than grepped once.
step_log() { ls -1 "$1"/logs/*_lhd_pass_opentimer.log 2>/dev/null | tail -1; }
lib_parses() { grep -c "using liberty file" "$(step_log "$1")" 2>/dev/null || true; }
delay_lines() { grep -c "^slowest delay:" "$(step_log "$1")" 2>/dev/null || true; }

[ -f "$FIX" ] || fail "missing fixture $FIX"
[ -f "$LIB" ] || fail "missing liberty $LIB"
cp "$FIX" "$PRP"

# --- the netlist to time ----------------------------------------------------
map_netlist() {  # OUT_TAG
  run compile "$PRP" --top "$TOP" --recipe O1 --emit-dir lg:"$W/lg_$1" --workdir "$W/wc_$1"
  run pass color synth --top "$TOP" lg:"$W/lg_$1" --workdir "$W/wk_$1"
  run pass abc --top "$TOP" lg:"$W/lg_$1" --emit-dir lg:"$W/$1" --set abc.library="$LIB" --workdir "$W/wa_$1"
}
map_netlist net

sta() {  # NET_TAG [extra args...]
  local net=$1
  shift
  "$LHD" pass opentimer --top "$TOP" lg:"$W/$net" "$@" --workdir "$W/wt" -q --result-json "$W/r.json" \
    >/dev/null 2>&1 || fail "pass opentimer -> $(cat "$W/r.json" 2>/dev/null)"
}

# --- 1. cold: a miss that stores the analysis -------------------------------
sta net "$LIB"
expect_sta 0 1 "cold run"
[ -f "$W/wt/sta_cache/sta_cache.json" ] || fail "no sta_cache/sta_cache.json under <workdir>"
grep -q '"kind":"sta"' "$W/wt/timing.json" || fail "cold run wrote no timing report"
libs=$(lib_parses "$W/wt")
delays=$(delay_lines "$W/wt")
[ "${libs:-0}" -ge 1 ] || fail "cold run did not parse the Liberty"
[ "${delays:-0}" -ge 1 ] || fail "cold run printed no slowest-delay line"
cp "$W/wt/timing.json" "$W/t_cold.json"

# --- 2. warm: a hit, same report, no Liberty parse --------------------------
rm -f "$W/wt/timing.json"
sta net "$LIB"
expect_sta 1 0 "warm re-run"
[ -f "$W/wt/timing.json" ] || fail "a hit did not (re)write timing.json"
same_report "$W/t_cold.json" "$W/wt/timing.json" || fail "warm timing.json differs from the cold one"
[ "$(lib_parses "$W/wt")" = "$libs" ] || fail "a cache hit still parsed the Liberty"
[ "$(delay_lines "$W/wt")" -gt "$delays" ] || fail "a hit did not replay the slowest-delay line"
libs=$(lib_parses "$W/wt")
echo "PASS: an unchanged netlist replays the same timing report without touching the Liberty"

# --- 3. a CHANGED netlist misses; the earlier netlist still hits ------------
sed 's/z = ~a/z = ~(a \& c)/' "$FIX" > "$PRP"
grep -q 'z = ~(a & c)' "$PRP" || fail "edit did not apply"
map_netlist net2
sta net2 "$LIB"
expect_sta 0 1 "edited netlist"
cp "$W/wt/timing.json" "$W/t_edit.json"
if same_report "$W/t_cold.json" "$W/t_edit.json" >/dev/null 2>&1; then
  fail "the edited netlist timed identically -- step 3 would prove nothing"
fi
cp "$FIX" "$PRP"
sta net "$LIB"  # back to the first netlist
expect_sta 1 0 "revert to the first netlist"
same_report "$W/t_cold.json" "$W/wt/timing.json" || fail "the reverted hit replayed the wrong record"
libs=$(lib_parses "$W/wt")
echo "PASS: one record per distinct netlist; an edited netlist misses and a revert hits"

# --- 4. a changed option that moves the analysis misses --------------------
sta net "$LIB" --set pass.opentimer.margin=50
expect_sta 0 1 "changed margin"
sta net "$LIB"
expect_sta 1 0 "back to the default margin"
libs=$(lib_parses "$W/wt")
echo "PASS: an option that changes the analysis is part of the key"

# --- 5. lhd.incremental=false disables the tier ----------------------------
# The tier still REPORTS (enabled=false + zero counters), exactly as pass.abc
# does: a benchmark row must be able to tell an honestly disabled cache from an
# old binary that carries no telemetry at all.
sta net "$LIB" --set lhd.incremental=false
[ "$(sta_field enabled)" = False ] || fail "lhd.incremental=false still enabled the STA reuse tier"
[ "$(sta_field hits)" = 0 ] || fail "a disabled STA tier reported a hit"
[ "$(lib_parses "$W/wt")" -gt "$libs" ] || fail "an honest cold run skipped the Liberty"
same_report "$W/t_cold.json" "$W/wt/timing.json" \
  || fail "the cache-off run disagrees with the cached report (the cache is not reproducing STA)"
echo "PASS: lhd.incremental=false re-times, and agrees with what the cache replays"

# --- 6. a salt mismatch drops every record ---------------------------------
python3 - "$W/wt/sta_cache/sta_cache.json" <<'PY'
import json, sys

d = json.load(open(sys.argv[1]))
d["salt"] = "0" * 16  # a different timing engine
json.dump(d, open(sys.argv[1], "w"))
PY
sta net "$LIB"
expect_sta 0 1 "salt mismatch"
echo "PASS: a salt mismatch re-times instead of replaying a stale engine's report"

# --- 7. `lhd synth --stats`: resynth is THIS run's, everything else is cached
SW="$W/ws"
run synth "$PRP" --top "$TOP" --set synth.liberty="$LIB" --workdir "$SW" --stats
[ "$(sta_field misses)" = 1 ] || fail "cold synth did not miss the STA cache"
cp "$SW/synth/timing.json" "$W/s_cold.json"
run synth "$PRP" --top "$TOP" --set synth.liberty="$LIB" --workdir "$SW" --stats
expect_sta 1 0 "warm synth"
python3 - "$W/s_cold.json" "$SW/synth/timing.json" <<'PY'
import json, sys

cold = json.load(open(sys.argv[1]))
warm = json.load(open(sys.argv[2]))
for d in (cold, warm):
    d.pop("incremental", None)  # per-run telemetry, not part of the report


def rows(doc):
    return [c for d in doc["designs"] for c in d.get("colors", [])]


if not rows(cold):
    sys.exit("no per-color rows: --stats produced nothing to check")
if any(c["resynth"] != 1 for c in rows(cold)):
    sys.exit("a cold synth left a color row at resynth=0")
if any(c["resynth"] != 0 for c in rows(warm)):
    sys.exit("a warm synth replayed the cold run's resynth=1 rows")
# Blank the one run-dependent field; EVERYTHING else must be identical.
for c in rows(cold) + rows(warm):
    c["resynth"] = None
if cold != warm:
    sys.exit(f"the replayed report differs beyond resynth:\ncold={json.dumps(cold)}\nwarm={json.dumps(warm)}")
PY
echo "PASS: a warm synth replays the whole timing report and re-stamps only resynth"

echo "PASS: lhd_sta_incr_test"
