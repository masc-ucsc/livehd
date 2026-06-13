#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# Kernel debug observables not covered by the flow matrix: `--dump lg` with a
# multi-graph design (the stderr textual LGraph dump, sorted by name),
# `--verbose` (step logs mirrored to stderr), and `--depfile` (gcc -MF style
# prerequisites for the build-system integration).

set -u

LHD=lhd/lhd
PRP=inou/prp/tests/equiv/trivial_if.prp
MIX=lhd/tests/bw_mix.v
W="${TEST_TMPDIR:-/tmp/lhd_debug_obs_$$}"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

# 1. --dump lg on a two-lambda design: both graphs land on stderr, sorted.
cat > "$W/twog.prp" <<'EOF'
comb addone(a:u8) -> (z:u9) { z = a + 1 }
comb xorit(a:u8, b:u8) -> (z:u8) { z = a ^ b }
EOF
"$LHD" compile "$W/twog.prp" --emit-dir lg:"$W/lgs/" --dump lg \
  --workdir "$W/w_dump" 2>"$W/dump.err" >/dev/null || fail "--dump lg compile failed"
grep -q '^//---- lg twog.addone' "$W/dump.err" || fail "--dump lg stderr is missing the addone graph"
grep -q '^//---- lg twog.xorit' "$W/dump.err" || fail "--dump lg stderr is missing the xorit graph"
[ "$(grep -n '^//---- lg' "$W/dump.err" | head -1 | grep -c addone)" = 1 ] \
  || fail "--dump lg graphs are not sorted by name"

# 2. --verbose mirrors the per-step logs to stderr (yosys chatter included).
"$LHD" compile "$MIX" --reader yosys-verilog --top bw_mix --recipe O1 \
  --emit verilog:"$W/mix.gen.v" --verbose --workdir "$W/w_verb" 2>"$W/verb.err" \
  >/dev/null || fail "--verbose compile failed"
[ -s "$W/verb.err" ] || fail "--verbose produced no stderr mirror"
grep -qi 'yosys' "$W/verb.err" || fail "--verbose stderr does not include step logs"

# 3. --depfile writes a make-style prerequisite list naming the source.
"$LHD" compile "$PRP" --emit-dir ln:"$W/lns/" --depfile "$W/dep.d" \
  --workdir "$W/w_dep" -q >/dev/null 2>&1 || fail "--depfile compile failed"
[ -s "$W/dep.d" ] || fail "depfile was not written"
grep -q 'trivial_if.prp' "$W/dep.d" || fail "depfile does not list the source: $(cat "$W/dep.d")"

echo "PASS lhd_debug_obs_test"
