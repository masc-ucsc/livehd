#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# A generated sim directory records the command that built it. `lhd sim` (or
# `lhd sim --setup-only`) with no sources, run from inside <workdir>/sim,
# replays that command: it re-reads the ORIGINAL source, so an edit there is
# picked up and rebuilt, and an unchanged source rebuilds nothing. There is no
# bazel or ninja project in the directory.
set -uo pipefail
LHD="${LHD:-lhd/lhd}"
LHD_ABS="$(cd "$(dirname "$LHD")" && pwd)/$(basename "$LHD")"
W="${TEST_TMPDIR:-/tmp/lhd_sim_replay_$$}"
mkdir -p "$W"
fail() { echo "FAIL: $*" >&2; exit 1; }

cat > "$W/tb.prp" <<'PRP'
mod echo(a:U8) -> (b:U8@[0]) { b = (a + 1)#[0..<8] }
test echo.run {
  mut dut = echo
  tick 3 { dut.a = 4; step; assert(dut.b == 5) }
}
PRP

(cd "$W" && "$LHD_ABS" sim tb.prp --workdir wd --set sim.tune.profile=off -q >"$W/first.log" 2>&1) \
  || { cat "$W/first.log" >&2; fail "initial lhd sim failed"; }
S="$W/wd/sim"
grep -q '"invocation"' "$S/manifest.json" || fail "no recorded invocation in the sim dir manifest.json"
for f in BUILD MODULE.bazel .bazelrc .bazelversion build.ninja; do
  [ ! -e "$S/$f" ] || fail "the sim dir still has $f"
done

# 1. unchanged source: replay rebuilds nothing.
touch "$W/marker"
(cd "$S" && "$LHD_ABS" sim -q >"$W/warm.log" 2>&1) || { cat "$W/warm.log" >&2; fail "replay failed"; }
[ -z "$(find "$S" \( -name '*.o' -o -name drv.bin \) -newer "$W/marker")" ] \
  || fail "an unchanged replay rebuilt objects or relinked the simulator"

# 2. a source edit made OUTSIDE the sim dir is picked up by the replay.
sed -i.bak 's/a + 1/a + 2/' "$W/tb.prp"
(cd "$S" && "$LHD_ABS" sim -q >"$W/edit.log" 2>&1) && fail "the edited design (b = a + 2) should fail its assert"
sed -i.bak 's/a + 2/a + 1/; s/dut.a = 4/dut.a = 5/; s/dut.b == 5/dut.b == 6/' "$W/tb.prp"
(cd "$S" && "$LHD_ABS" sim --setup-only -q >"$W/setup.log" 2>&1) || { cat "$W/setup.log" >&2; fail "replayed --setup-only failed"; }
(cd "$S" && "$LHD_ABS" sim -q >"$W/run.log" 2>&1) || { cat "$W/run.log" >&2; fail "replay after the edit failed"; }

# The recorded invocation and the unit index share ONE manifest.json, and both
# survive the regeneration the edit above triggered.
grep -q '"invocation"' "$S/manifest.json" && grep -q '"units"' "$S/manifest.json" \
  || fail "manifest.json lost its invocation or unit index after a regeneration"

# 3. the removed knob is gone.
if "$LHD" sim "$W/tb.prp" --set sim.ninja=false --workdir "$W/n" -q >"$W/ninja.log" 2>&1; then
  fail "sim.ninja should no longer be an accepted option"
fi

echo "PASS: lhd sim in the generated sim dir replays the recorded command incrementally"
