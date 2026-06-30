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
# The structural checks run hermetically (`--setup-only` / `--list-tests`, no
# compiler). When the sibling ../hlop + ../iassert headers are present (a dev /
# repo-root run), it ALSO host-compiles + runs the single binary to check that the
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
mod cnt(enable:bool) -> (value:u8@[0]) {
  reg count:u8 = 0
  value = count
  if enable { wrap count += 1 }
}
test cnt.held(cycles:u20 = 20) {
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

# ---- `--setup-only` generates the single driver with the registry + flags -----
"$LHD" sim "$W/two.prp" --setup-only --workdir "$W/s" -q >/dev/null 2>&1 || fail "setup-only failed"
DRV="$W/s/sim/drv.cpp"
[ -f "$DRV" ] || fail "single driver drv.cpp not generated"
[ -f "$W/s/sim/drv_cnt_held.cpp" ] && fail "a per-test driver was generated (expected one drv.cpp)"
grep -q '_tests_json'        "$DRV" || fail "driver missing the embedded --list-tests JSON"
grep -q '"--list-tests"'     "$DRV" || fail "driver missing --list-tests handling"
grep -q '"--test"'           "$DRV" || fail "driver missing the --test selector"
grep -q '_run_cnt_held'      "$DRV" || fail "driver missing the cnt.held run function"
grep -q '_run_cnt_gated'     "$DRV" || fail "driver missing the cnt.gated run function"
grep -q '"cnt.held"'         "$DRV" || fail "driver registry missing cnt.held"
grep -q '"cnt.gated"'        "$DRV" || fail "driver registry missing cnt.gated"
# no VCD machinery unless asked
grep -q 'vcd::global_timestamp' "$DRV" && fail "non-VCD driver must not touch the VCD timeline"

# ---- with sim.vcd, the driver resets the VCD timeline between tests -----------
"$LHD" sim "$W/two.prp" --setup-only --set sim.vcd=true --workdir "$W/v" -q >/dev/null 2>&1 \
  || fail "setup-only (vcd) failed"
VDRV="$W/v/sim/drv.cpp"
grep -q '#include "vcd_writer.hpp"' "$VDRV" || fail "VCD driver does not include the VCD writer header"
grep -q 'vcd::global_timestamp = 0' "$VDRV" || fail "VCD driver does not reset the per-test VCD timeline"

# ---- opportunistic real build + run (needs the sibling runtime headers) -------
# Locate slop.hpp / iassert.hpp the way the kernel does (dev layout: ../hlop,
# ../iassert). If absent (e.g. a sandboxed `bazel test`), skip the run checks --
# the prp-sim-* targets cover the end-to-end run there.
HLOP_INC=""
IASSERT_INC=""
for d in ../hlop/hlop ../hlop; do [ -f "$d/slop.hpp" ] && HLOP_INC="$d" && break; done
for d in ../iassert/src ../iassert; do [ -f "$d/iassert.hpp" ] && IASSERT_INC="$d" && break; done
if [ -z "$HLOP_INC" ] || [ -z "$IASSERT_INC" ]; then
  echo "SKIP run checks: sibling hlop/iassert headers not found (structural checks passed)"
  echo "PASS: lhd sim --list-tests + single-driver structure"
  exit 0
fi

# the built binary's --list-tests must match `lhd sim --list-tests` byte-for-byte
"$LHD" sim "$W/two.prp" --set sim.vcd=true --workdir "$W/run" --diag-fmt pretty > "$W/run.out" 2>&1 \
  || fail "fast run path failed: $(cat "$W/run.out")"
grep -q 'PASS cnt.held'  "$W/run.out" || fail "cnt.held did not pass: $(cat "$W/run.out")"
grep -q 'PASS cnt.gated' "$W/run.out" || fail "cnt.gated did not pass (VCD multi-test crash?): $(cat "$W/run.out")"

BIN="$W/run/sim/drv.bin"
[ -x "$BIN" ] || fail "the host-compiled binary $BIN was not produced"
BIN_JSON="$("$BIN" --list-tests)"
LHD_JSON="$("$LHD" sim "$W/two.prp" --list-tests --diag-fmt jsonl 2>/dev/null | head -1)"
[ "$BIN_JSON" = "$LHD_JSON" ] || fail "binary --list-tests != lhd sim --list-tests:
  bin: $BIN_JSON
  lhd: $LHD_JSON"

# --test selects exactly one test
SEL_OUT="$("$BIN" --test cnt.gated)"
echo "$SEL_OUT" | grep -q 'PASS cnt.gated' || fail "--test cnt.gated did not run it: $SEL_OUT"
echo "$SEL_OUT" | grep -q 'cnt.held'       && fail "--test cnt.gated must not run cnt.held: $SEL_OUT"

# both VCDs were produced (no registration crash from the shared timeline)
[ -s "$W/run/cnt.held.vcd" ]  || fail "cnt.held.vcd not produced"
[ -s "$W/run/cnt.gated.vcd" ] || fail "cnt.gated.vcd not produced"
grep -q '^#0' "$W/run/cnt.gated.vcd" || fail "cnt.gated.vcd does not start at time 0"

echo "PASS: lhd sim --list-tests + single fast run path (host-compile, VCD multi-test)"
