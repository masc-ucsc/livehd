#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# `lhd sim` OBSERVABILITY (sim_checkpoint_debug_plan): query signal values without
# re-instrumenting the testbench. cgen emits describe_signals/probe_signals per
# module (scalar signals by hierarchical name); the driver/kernel surface them in
# the result envelope's "debug" member:
#   * --list-signals        enumerate observable signals (name, bits, kind)
#   * --probe SIG,... [--probe-from A --probe-to B]   per-cycle JSON trajectory
#   * --break-when 'SIG OP V'   first cycle a condition holds (+ state snapshot)
# Structural checks run hermetically; the run checks need the sibling ../hlop +
# ../iassert headers.

set -u

LHD="${LHD:-lhd/lhd}"
W="${TEST_TMPDIR:-/tmp/lhd_sim_obs_$$}"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

# flop + stateful sub so the signal set spans the hierarchy.
cat > "$W/obs.prp" <<'EOF'
/*
:name: obs
:type: simulation
*/
mod sub(en:bool) -> (o:u8@[0]) { reg c:u8 = 0; o = c; if en { wrap c += 1 } }
mod top(en:bool, din:u8) -> (fout:u8@[0], sout:u8@[0]) {
  reg acc:u8 = 0
  if en { wrap acc += din }
  fout = acc
  sout = sub(en=en)
}
test top.run {
  mut acc = top
  tick 12 { acc.en = true; acc.din = 2; step }
  assert(true)
}
EOF

# ---- structural: cgen emits the snapshot methods; the driver has the modes ----
"$LHD" sim "$W/obs.prp" --setup-only --workdir "$W/s" -q >/dev/null 2>&1 || fail "setup-only failed"
TOPCPP="$(ls "$W"/s/sim/obs.top.cpp 2>/dev/null)"
DRV="$W/s/sim/drv.cpp"
grep -q '::describe_signals' "$TOPCPP" || fail "cgen did not emit describe_signals"
grep -q '::probe_signals'    "$TOPCPP" || fail "cgen did not emit probe_signals"
grep -q '"flop"'             "$TOPCPP" || fail "describe_signals lacks the flop kind"
grep -q '_dbg.list_signals'  "$DRV"    || fail "driver lacks --list-signals"
grep -q '_dbg_parse_break'   "$DRV"    || fail "driver lacks --break-when parsing"
grep -q '"--probe"'          "$DRV"    || fail "driver does not accept --probe"

# ---- opportunistic real build + run (needs the sibling runtime headers) -------
HLOP_INC=""
IASSERT_INC=""
for d in ../hlop/hlop ../hlop; do [ -f "$d/slop.hpp" ] && HLOP_INC="$d" && break; done
for d in ../iassert/src ../iassert; do [ -f "$d/iassert.hpp" ] && IASSERT_INC="$d" && break; done
if [ -z "$HLOP_INC" ] || [ -z "$IASSERT_INC" ]; then
  echo "SKIP run checks: sibling hlop/iassert headers not found (structural checks passed)"
  echo "PASS: lhd sim observability (structural)"
  exit 0
fi

# (1) --list-signals: the hierarchical flop names are present
"$LHD" sim "$W/obs.prp" --list-signals --result-json "$W/ls.json" --workdir "$W/run" -q >/dev/null 2>&1 \
  || fail "--list-signals run failed"
python3 - "$W/ls.json" <<'PY' || fail "--list-signals output wrong"
import json, sys
sigs = {s["name"]: s for s in json.load(open(sys.argv[1]))["debug"]["signals"]}
assert "acc.acc" in sigs and sigs["acc.acc"]["kind"] == "flop" and sigs["acc.acc"]["bits"] == 9, sigs.get("acc.acc")
assert "acc.u_sub_sout_0.c" in sigs, "sub-instance flop missing: %s" % list(sigs)
assert any(n.endswith(".__in.din") for n in sigs), "inputs missing"
print("  list-signals OK (%d signals)" % len(sigs))
PY

# (2) --probe: per-cycle trajectory over a window; acc += 2/cycle
"$LHD" sim "$W/obs.prp" --probe "acc.acc,acc.u_sub_sout_0.c" --probe-from 8 --probe-to 11 \
  --result-json "$W/pr.json" --workdir "$W/run" -q >/dev/null 2>&1 || fail "--probe run failed"
python3 - "$W/pr.json" <<'PY' || fail "--probe output wrong"
import json, sys
p = json.load(open(sys.argv[1]))["debug"]["probe"]
assert p["from"] == 8 and p["to"] == 11, p
cyc = [r["cycle"] for r in p["rows"]]
assert cyc == [8, 9, 10, 11], "window not honored: %s" % cyc
# acc accumulates din=2 each enabled cycle; strictly increasing by 2
accs = [r["acc.acc"] for r in p["rows"]]
assert all(accs[i+1] - accs[i] == 2 for i in range(len(accs)-1)), accs
assert all("acc.u_sub_sout_0.c" in r for r in p["rows"]), "sub signal not probed"
print("  probe OK (acc.acc = %s)" % accs)
PY

# (3) --break-when: first cycle acc.acc > 15
"$LHD" sim "$W/obs.prp" --break-when "acc.acc > 15" --result-json "$W/bw.json" --workdir "$W/run" \
  --diag-fmt pretty > "$W/bw.out" 2>&1 || fail "--break-when run failed"
grep -q "break-when 'acc.acc > 15' first held at clock" "$W/bw.out" || fail "no break-when report: $(cat "$W/bw.out")"
python3 - "$W/bw.json" <<'PY' || fail "--break-when output wrong"
import json, sys
b = json.load(open(sys.argv[1]))["debug"]["break"]
assert b["hit"] is True, b
assert b["state"]["acc.acc"] > 15, "break state below threshold: %s" % b["state"].get("acc.acc")
# acc.acc reaches 16 (8 enabled cycles * 2) at cycle 7
assert b["cycle"] == 7, "expected first-cross at cycle 7, got %s" % b["cycle"]
print("  break-when OK (cycle %d, acc.acc=%d)" % (b["cycle"], b["state"]["acc.acc"]))
PY

# a never-true condition reports hit=false
"$LHD" sim "$W/obs.prp" --break-when "acc.acc > 9999" --result-json "$W/bw2.json" --workdir "$W/run" -q >/dev/null 2>&1
python3 - "$W/bw2.json" <<'PY' || fail "--break-when never-true output wrong"
import json, sys
b = json.load(open(sys.argv[1]))["debug"]["break"]
assert b["hit"] is False and "cycle" not in b, b
print("  break-when never-true OK")
PY

# a break on a NONEXISTENT signal must NOT fire a spurious cycle-0 hit (review bug)
"$LHD" sim "$W/obs.prp" --break-when "acc.nope == 0" --result-json "$W/bw3.json" --workdir "$W/run" -q >/dev/null 2>&1
python3 - "$W/bw3.json" <<'PY' || fail "--break-when on a missing signal fired a spurious hit"
import json, sys
b = json.load(open(sys.argv[1]))["debug"]["break"]
assert b["hit"] is False, "missing-signal break must not hit: %s" % b
print("  break-when missing-signal OK (no spurious cycle-0 hit)")
PY

# an invalid break value / empty LHS is rejected loudly (not silently 0)
"$LHD" sim "$W/obs.prp" --break-when "acc.acc == 0xZZ" --workdir "$W/run" -q 2>&1 | grep -q "not a valid integer" \
  || fail "an invalid --break-when value was not rejected"
"$LHD" sim "$W/obs.prp" --break-when " > 5" --workdir "$W/run" -q 2>&1 | grep -q "needs a signal" \
  || fail "an empty --break-when LHS was not rejected"

# a space after a comma in --probe must not drop the signal
"$LHD" sim "$W/obs.prp" --probe "acc.acc, acc.u_sub_sout_0.c" --probe-from 5 --probe-to 5 \
  --result-json "$W/pr2.json" --workdir "$W/run" -q >/dev/null 2>&1
python3 - "$W/pr2.json" <<'PY' || fail "--probe dropped a signal after a comma+space"
import json, sys
row = json.load(open(sys.argv[1]))["debug"]["probe"]["rows"][0]
assert "acc.acc" in row and "acc.u_sub_sout_0.c" in row, row
print("  probe comma-space OK")
PY

# observability needs a single test; a 2-test file without --test is rejected
cat > "$W/two.prp" <<'EOF'
/*
:name: two
:type: simulation
*/
mod cnt(enable:bool) -> (value:u8@[0]) { reg count:u8 = 0; value = count; if enable { wrap count += 1 } }
test a.x { mut acc = cnt; tick 3 { acc.enable = true; step }; assert(true) }
test b.y { mut acc = cnt; tick 3 { acc.enable = true; step }; assert(true) }
EOF
"$LHD" sim "$W/two.prp" --list-signals --workdir "$W/two" -q 2>&1 | grep -q "single test" \
  || fail "multi-test observability was not rejected"
# ...but the positional test selector narrows it to one and works
"$LHD" sim "$W/two.prp" a.x --list-signals --result-json "$W/two.json" --workdir "$W/two" -q >/dev/null 2>&1 \
  || fail "single-test --list-signals failed"
python3 -c 'import json,sys; assert json.load(open("'"$W"'/two.json"))["debug"]["signals"]' || fail "no signals for the selected test"

echo "PASS: lhd sim observability (list-signals, probe trajectory, break-when + edge cases)"
