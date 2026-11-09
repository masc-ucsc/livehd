#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# The incremental API of `lhd synth`, same shape as `lhd sim`: with a --workdir,
# <workdir>/synth/manifest.json records the invocation and the finished run.
# Run from inside that directory with no sources, `lhd synth`
#   1. replays the recorded command; with nothing changed it does NO work and
#      reports the stored qor (`--stats` re-shows it, no flow re-run);
#   2. re-runs the flow when the source, or a file an option names (the
#      Liberty), changed;
#   3. keeps the unit index and the invocation in the one manifest.json.
set -uo pipefail
LHD="${LHD:-lhd/lhd}"
LHD_ABS="$(cd "$(dirname "$LHD")" && pwd)/$(basename "$LHD")"
LIB="$(cd "$(dirname inou/prp/tests/abc/test.lib)" && pwd)/test.lib"
W="${TEST_TMPDIR:-/tmp/lhd_synth_replay_$$}"
mkdir -p "$W"
fail() { echo "FAIL: $*" >&2; exit 1; }
recipe() { python3 -c "import json,sys; print(json.load(open(sys.argv[1]))['recipe'][0])" "$1"; }

cp inou/prp/tests/pyrope/hier_seq.prp "$W/dut.prp"
cp "$LIB" "$W/test.lib"
(cd "$W" && "$LHD_ABS" synth dut.prp --top top --set synth.liberty=test.lib --workdir wd -q >"$W/first.log" 2>&1) \
  || { cat "$W/first.log" >&2; fail "initial lhd synth failed"; }
S="$W/wd/synth"
grep -q '"invocation"' "$S/manifest.json" && grep -q '"synth"' "$S/manifest.json" \
  || fail "synth manifest.json lacks the invocation or the synth entry"

# 1. unchanged: replay does nothing, and stats come back from the manifest.
touch "$W/marker"
(cd "$S" && "$LHD_ABS" synth --result-json "$W/warm.json" -q >"$W/warm.log" 2>&1) || { cat "$W/warm.log" >&2; fail "replay failed"; }
case "$(recipe "$W/warm.json")" in
  "synth: up to date"*) ;;
  *) fail "an unchanged replay re-ran the flow: $(recipe "$W/warm.json")" ;;
esac
[ -z "$(find "$S" -type f -newer "$W/marker" ! -name '*.json' | head -1)" ] || fail "an unchanged replay rewrote artifacts"
(cd "$S" && "$LHD_ABS" synth --stats --diag-fmt pretty -q >"$W/stats.out" 2>&1) || fail "replay with --stats failed"
grep -q '^  qor: abc-map' "$W/stats.out" || fail "the replay did not show the stored qor: $(head -5 "$W/stats.out")"

# 2. a source edit, then a Liberty edit, each re-run the flow.
echo "// edit" >>"$W/dut.prp"
(cd "$S" && "$LHD_ABS" synth --result-json "$W/edit.json" -q >"$W/edit.log" 2>&1) || { cat "$W/edit.log" >&2; fail "replay after a source edit failed"; }
case "$(recipe "$W/edit.json")" in
  "synth: up to date"*) fail "a source edit was not noticed" ;;
esac
echo "/* edit */" >>"$W/test.lib"
(cd "$S" && "$LHD_ABS" synth --result-json "$W/lib.json" -q >"$W/lib.log" 2>&1) || { cat "$W/lib.log" >&2; fail "replay after a Liberty edit failed"; }
case "$(recipe "$W/lib.json")" in
  "synth: up to date"*) fail "a Liberty edit was not noticed" ;;
esac

# 3. and after being re-run it is up to date again.
(cd "$S" && "$LHD_ABS" synth --result-json "$W/again.json" -q >/dev/null 2>&1) || fail "second replay failed"
case "$(recipe "$W/again.json")" in
  "synth: up to date"*) ;;
  *) fail "the run after the edits did not record itself as done" ;;
esac

echo "PASS: lhd synth in the generated synth dir replays, skips when done, re-shows stats, reruns on a change"
