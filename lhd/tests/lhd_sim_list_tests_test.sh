#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# `lhd sim` single fast run path + `--list-tests`:
#   * `lhd sim file.prp --list-tests` emits the dotted test names + parameters as
#     JSON (a pure parse — no DUT build), so tooling can enumerate tests without
#     re-parsing the `.prp`;
#   * `--setup-only` generates ONE driver (drv.cpp) holding every `test` block,
#     with the registry + embedded `--list-tests` JSON + `--test`/`--seed` flags;
#   * with `--set sim.vcd=true` the driver resets the VCD timeline per test
#     (vcd::global_timestamp) so two VCD tests in one process don't collide.
# Structural checks use `--setup-only` / `--list-tests`. The runtime checks
# host-compile and run using declared dependencies, and verify that the
# built binary's `--list-tests` matches `lhd sim --list-tests`, that `--test`
# selects one test, and that a two-test VCD run produces both .vcd files without
# crashing.

set -u

LHD="${LHD:-lhd/lhd}"
W="${TEST_TMPDIR:-/tmp/lhd_sim_lt_$$}"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

# A two-test design (one parameterized, one not) so --list-tests + the multi-test
# single binary are exercised.
cat > "$W/two.prp" <<'EOF'
/*
:name: two
:type: simulation
*/
mod cnt(enable:Bool) -> (value:U8@[0]) {
  reg count:U8 = 0
  value = count
  if enable { wrap count += 1 }
}
test cnt.held(cycles:U20 = 20) {
  mut acc = cnt
  mut v = 0
  tick cycles {
    acc.enable = true
    acc.reset  = clock < 2
    step
    v = acc.value
  }
  assert(v == cycles - 2, "count must be cycles - 2")
}
test cnt.gated {
  mut acc = cnt
  mut en  = false
  mut v   = 0
  tick 20 { en = not en; acc.enable = en; step; v = acc.value }
  assert(v == 10)
}
EOF

# ---- `lhd sim --list-tests` is a pure parse -> JSON (no build); honors fmt ----
LT="$("$LHD" sim "$W/two.prp" --list-tests --diag-fmt jsonl 2>/dev/null | head -1)" || fail "--list-tests failed"
echo "$LT" | grep -q '"file":"' || fail "--list-tests JSON missing the file field: $LT"
echo "$LT" | grep -q '"name":"cnt.held"' || fail "--list-tests JSON missing cnt.held: $LT"
echo "$LT" | grep -q '"name":"cnt.gated"' || fail "--list-tests JSON missing cnt.gated: $LT"
echo "$LT" | grep -q '"name":"cycles","required":false,"default":"20"' \
  || fail "--list-tests JSON missing the cycles parameter (default 20): $LT"

# pretty mode renders a human listing (not JSON), honoring --diag-fmt
PT="$("$LHD" sim "$W/two.prp" --list-tests --diag-fmt pretty 2>/dev/null)" || fail "--list-tests pretty failed"
echo "$PT" | grep -q 'cnt.held(cycles = 20)' || fail "pretty --list-tests missing the readable cnt.held line: $PT"
echo "$PT" | grep -q '^  cnt.gated$'          || fail "pretty --list-tests missing the readable cnt.gated line: $PT"
echo "$PT" | grep -q '"name"'                 && fail "pretty --list-tests must not emit raw JSON: $PT"

# a test selector narrows the listing to one test
LT1="$("$LHD" sim "$W/two.prp" cnt.gated --list-tests --diag-fmt jsonl 2>/dev/null | head -1)" || fail "--list-tests (selector) failed"
echo "$LT1" | grep -q '"name":"cnt.gated"'  || fail "selected --list-tests missing cnt.gated: $LT1"
echo "$LT1" | grep -q '"name":"cnt.held"'   && fail "selected --list-tests must not include cnt.held: $LT1"

# ---- `--setup-only` lowers every test into ONE object and writes the registry ---
"$LHD" sim "$W/two.prp" --setup-only --workdir "$W/s" -q >/dev/null 2>&1 || fail "setup-only failed"
MAN="$W/s/sim/drv.tb.json"
[ -f "$W/s/sim/drv.tb.o" ] && [ -f "$MAN" ] || fail "single testbench object/manifest not generated"
[ -z "$(ls "$W"/s/sim/*.cpp "$W"/s/sim/*.hpp 2>/dev/null)" ] || fail "C++ was generated: $(ls "$W/s/sim")"
grep -q '"name":"cnt.held"'  "$MAN" || fail "manifest missing the cnt.held test"
grep -q '"name":"cnt.gated"' "$MAN" || fail "manifest missing the cnt.gated test"
# no trace code unless asked: the simulation object has none, and the trace object exists only for a VCD run
[ -f "$W/s/sim/drv.tb.trace.o" ] && fail "a non-VCD setup must not write the trace object"

# ---- with sim.vcd, the setup also writes the trace object the replay runs ----------
"$LHD" sim "$W/two.prp" --setup-only --set sim.vcd=true --workdir "$W/v" -q >/dev/null 2>&1 \
  || fail "setup-only (vcd) failed"
[ -f "$W/v/sim/drv.tb.trace.o" ] || fail "a VCD setup must write the trace object (drv.tb.trace.o)"

SIMRT="${LHD_SIMRT:-$(dirname "$LHD")/lhd_simrt}"
[ -x "$SIMRT" ] || fail "lhd_simrt not found next to lhd ($SIMRT)"

# a VCD run: both tests pass and each gets its own waveform, replayed from the start
"$LHD" sim "$W/two.prp" --set sim.vcd=true --workdir "$W/run" --diag-fmt pretty > "$W/run.out" 2>&1 \
  || fail "run failed: $(cat "$W/run.out")"
grep -q 'PASS cnt.held'  "$W/run.out" || fail "cnt.held did not pass: $(cat "$W/run.out")"
grep -q 'PASS cnt.gated' "$W/run.out" || fail "cnt.gated did not pass: $(cat "$W/run.out")"

# the runtime's --list-tests must match `lhd sim --list-tests` byte-for-byte
RT_JSON="$("$SIMRT" "$W/run/sim" --list-tests)"
LHD_JSON="$("$LHD" sim "$W/two.prp" --list-tests --diag-fmt jsonl 2>/dev/null | head -1)"
[ "$RT_JSON" = "$LHD_JSON" ] || fail "runtime --list-tests != lhd sim --list-tests:
  rt:  $RT_JSON
  lhd: $LHD_JSON"

# --test selects exactly one test
SEL_OUT="$("$SIMRT" "$W/run/sim" --test cnt.gated --no-checkpoint)"
echo "$SEL_OUT" | grep -q 'PASS cnt.gated' || fail "--test cnt.gated did not run it: $SEL_OUT"
echo "$SEL_OUT" | grep -q 'cnt.held'       && fail "--test cnt.gated must not run cnt.held: $SEL_OUT"

# both VCDs were produced
[ -s "$W/run/cnt.held.vcd" ]  || fail "cnt.held.vcd not produced"
[ -s "$W/run/cnt.gated.vcd" ] || fail "cnt.gated.vcd not produced"
grep -q '^#0' "$W/run/cnt.gated.vcd" || fail "cnt.gated.vcd does not start at time 0"

echo "PASS: lhd sim --list-tests + the single-object run path (VCD multi-test)"
