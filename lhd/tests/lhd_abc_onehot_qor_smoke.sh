#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# A timing target is a budget, not a request to minimize delay at any area cost.
# ABC's `upsize` command ignores that distinction and chases the fastest network:
# on this one-hot mux it used to add roughly 40% area even though the un-sized
# mapping already met the target. Compare the built-in buffered flow against the
# same mapping with fanout treatment disabled; buffering plus area recovery may
# move area slightly, but it must not trigger a wholesale speed-grade upsize.

set -u

LHD=lhd/lhd
LIB=inou/prp/tests/abc/timing.lib
SRC=lhd/tests/br_mux_onehot.sv
TOP=br_mux_onehot.br_mux_onehot
W="${TEST_TMPDIR:-/tmp/lhd_abc_onehot_qor_$$}"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

run() {
  "$LHD" "$@" -q --result-json "$W/result.json" || fail "$* -> $(cat "$W/result.json" 2>/dev/null)"
}

metric() {
  local name=$1
  local file=$2
  grep -o "\"${name}\":[0-9.]*" "$file" | head -1 | cut -d: -f2
}

run compile "$SRC" --reader slang --top br_mux_onehot --recipe O1 \
  --emit-dir lg:"$W/lg" --workdir "$W/compile"
run pass color synth --top "$TOP" lg:"$W/lg" --workdir "$W/color"

run pass abc --top "$TOP" lg:"$W/lg" --emit-dir lg:"$W/default" \
  --set abc.library="$LIB" --set abc.delay=1000 --set abc.register=false \
  --workdir "$W/default_work"
cp "$W/result.json" "$W/default.json"

run pass abc --top "$TOP" lg:"$W/lg" --emit-dir lg:"$W/no_fanout" \
  --set abc.library="$LIB" --set abc.delay=1000 --set abc.register=false \
  --set abc.max_fanout=0 --workdir "$W/no_fanout_work"
cp "$W/result.json" "$W/no_fanout.json"

# The same cone under an impossible target must still exercise the conditional
# speed-grade sweep; removing upsize altogether would trade away timing on every
# genuinely failing region.
run pass abc --top "$TOP" lg:"$W/lg" --emit-dir lg:"$W/tight" \
  --set abc.library="$LIB" --set abc.delay=1 --set abc.register=false \
  --workdir "$W/tight_work"
cp "$W/result.json" "$W/tight.json"

# The budget's OTHER half. `&nf -D` is silently ignored by ABC's mapper, so
# without an explicit relaxation the flow always chases MINIMUM delay and spends
# area no constraint asked for. `abc.area_relax=0` restores exactly that old
# behaviour, so the default must never come out LARGER than it -- and must still
# be inside the budget. (How much smaller depends on the cell library: this
# hermetic Liberty has six cells and one alternative drive strength, so it has
# almost no area/delay curve to trade along. The measured effect is in
# //pass/abc:abc_incr_test's policy tests and the ../lhdtrack sweep.)
run pass abc --top "$TOP" lg:"$W/lg" --emit-dir lg:"$W/norelax" \
  --set abc.library="$LIB" --set abc.delay=1000 --set abc.register=false \
  --set abc.area_relax=0 --workdir "$W/norelax_work"
cp "$W/result.json" "$W/norelax.json"

default_area=$(metric area "$W/default.json")
default_delay=$(metric max_delay "$W/default.json")
base_area=$(metric area "$W/no_fanout.json")
tight_delay=$(metric max_delay "$W/tight.json")
[ -n "$default_area" ] && [ -n "$default_delay" ] && [ -n "$base_area" ] && [ -n "$tight_delay" ] \
  || fail "missing QoR metric (default area=$default_area delay=$default_delay, base area=$base_area tight delay=$tight_delay)"

awk -v area="$default_area" -v base="$base_area" \
  'BEGIN { exit !(area <= base * 1.05) }' \
  || fail "default sizing inflated an already-clean one-hot mux (area=$default_area, no-fanout=$base_area)"
awk -v delay="$default_delay" 'BEGIN { exit !(delay > 0 && delay <= 1000) }' \
  || fail "default one-hot mux missed its 1000 ps budget (delay=$default_delay)"
awk -v tight="$tight_delay" -v relaxed="$default_delay" \
  'BEGIN { exit !(tight < relaxed * 0.9) }' \
  || fail "a missed target did not trigger timing recovery (tight=$tight_delay, relaxed=$default_delay)"

norelax_area=$(metric area "$W/norelax.json")
norelax_delay=$(metric max_delay "$W/norelax.json")
[ -n "$norelax_area" ] && [ -n "$norelax_delay" ] \
  || fail "missing QoR metric for area_relax=0 (area=$norelax_area delay=$norelax_delay)"
awk -v area="$default_area" -v base="$norelax_area" 'BEGIN { exit !(area <= base) }' \
  || fail "budget-aware area recovery INFLATED the netlist (area_relax=200 -> $default_area, =0 -> $norelax_area)"
awk -v delay="$norelax_delay" 'BEGIN { exit !(delay > 0 && delay <= 1000) }' \
  || fail "area_relax=0 one-hot mux missed its 1000 ps budget (delay=$norelax_delay)"

echo "PASS: one-hot mux avoids unconditional area inflation and still sizes a real miss (area=$default_area, no-fanout=$base_area, relaxed=$default_delay ps, tight=$tight_delay ps)"
