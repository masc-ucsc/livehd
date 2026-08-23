#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# Native compact-loop simulation: one descriptor-bearing Sub becomes one
# std::array<Callee, count> plus runtime ordinal loops. The generated module
# source stays O(1) in count; occurrence-facing VCD/checkpoint/query names use
# the stable `instance[ordinal]` spelling.

set -u

LHD="${LHD:-lhd/lhd}"
W="${TEST_TMPDIR:-/tmp/lhd_sim_compact_loop_$$}"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

write_design() {
  local count="$1"
  cat > "$W/compact.prp" <<EOF
/*
:name: compact
:type: simulation
*/
mod lane(x:u8) -> (q:u16@[0]) {
  reg acc:u16 = 0
  q = acc
  wrap acc = acc + x
}
mod top(x:u8) -> (total:u32@[0]) {
  mut sum:u32 = 0
  for i in 0..<$count {
    if i == x { break }
    // Keep the descriptor compact while its lifted body contains a runtime
    // conditional Sub call and a runtime break. The simulator must emit BOTH
    // a runtime ordinal for and guarded calls, never source-unroll this shape.
    if x != 0 {
      const q = lane::[name=lane_state](x=x)
      sum = sum + q + i
    } else {
      sum = sum + i
    }
  }
  total = sum
}
test top.run {
  mut dut = top
  tick 3 { dut.x = 2; step }
  assert(true)
}
EOF
}

# Warm-workdir digest check and source-size scaling: changing only COUNT must
# regenerate the wrapper, while the parent module's source size remains flat.
write_design 4
"$LHD" sim "$W/compact.prp" --setup-only --workdir "$W/scale" -q >/dev/null 2>&1 \
  || fail "small compact setup failed"
H="$W/scale/sim/compact.top.hpp"
C="$W/scale/sim/compact.top.cpp"
[ -f "$H" ] && [ -f "$C" ] || fail "compact parent sources missing"
grep -q 'static constexpr std::size_t count = 4' "$H" || fail "small descriptor was not emitted natively"
SMALL_BYTES=$(( $(wc -c < "$H") + $(wc -c < "$C") ))

write_design 400
"$LHD" sim "$W/compact.prp" --setup-only --workdir "$W/scale" -q >/dev/null 2>&1 \
  || fail "large compact setup failed"
grep -q 'static constexpr std::size_t count = 400' "$H" || fail "descriptor-aware digest reused the count=4 source"
grep -q 'std::array<Callee, count> lanes' "$H" || fail "compact state is not a std::array"
grep -q 'for (std::size_t ordinal = 0; ordinal < count; ++ordinal)' "$H" || fail "runtime ordinal loop missing"
grep -Rq 'conditional activation (reset keeps it open)' "$W/scale/sim" \
  || fail "conditional Sub call was not emitted as a runtime if"
grep -q '"count":400' "$W/scale/sim/compact.top.iface.json" || fail "iface manifest lost the compact count"
if grep -q '__li399' "$H" "$C"; then
  fail "large compact sim physically emitted occurrence 399"
fi
LARGE_BYTES=$(( $(wc -c < "$H") + $(wc -c < "$C") ))
DELTA=$(( LARGE_BYTES > SMALL_BYTES ? LARGE_BYTES - SMALL_BYTES : SMALL_BYTES - LARGE_BYTES ))
[ "$DELTA" -le 512 ] || fail "generated module source grew with trip count: small=$SMALL_BYTES large=$LARGE_BYTES"

# Opportunistic host-compile/run checks need the sibling runtime headers.
HLOP_INC=""
IASSERT_INC=""
for d in ../hlop/hlop ../hlop; do [ -f "$d/slop.hpp" ] && HLOP_INC="$d" && break; done
for d in ../iassert/src ../iassert; do [ -f "$d/iassert.hpp" ] && IASSERT_INC="$d" && break; done
if [ -z "$HLOP_INC" ] || [ -z "$IASSERT_INC" ]; then
  echo "SKIP run checks: sibling hlop/iassert headers not found (structural checks passed)"
  echo "PASS: compact loop sim (source size + digest)"
  exit 0
fi

write_design 4
"$LHD" sim "$W/compact.prp" --workdir "$W/run" --set sim.vcd=true \
  --set sim.checkpoint_every=1 --set sim.checkpoint_max=2 -q >/dev/null 2>&1 || fail "compact host run failed"
VCD="$W/run/top.run.vcd"
[ -s "$VCD" ] || VCD="$W/run/top_run.vcd"
[ -s "$VCD" ] || fail "compact VCD missing"
grep -Fq 'u_loop_0[0]' "$VCD" || fail "VCD lacks occurrence 0 scope"
grep -Fq 'u_loop_0[3]' "$VCD" || fail "VCD lacks occurrence 3 scope"

LATEST="$(ls -d "$W/run"/ckpt/top_run/ckp* 2>/dev/null | sort -t p -k3 -n | tail -1)"
[ -n "$LATEST" ] && [ -s "$LATEST/regs.json" ] || fail "compact checkpoint missing"
grep -Fq 'u_loop_0[0]' "$LATEST/regs.json" || fail "checkpoint lacks occurrence 0 path"
grep -Fq 'u_loop_0[3]' "$LATEST/regs.json" || fail "checkpoint lacks occurrence 3 path"

"$LHD" sim "$W/compact.prp" top.run --list-signals --result-json "$W/signals.json" --workdir "$W/query" \
  -q >/dev/null 2>&1 || fail "compact list-signals failed"
python3 - "$W/signals.json" <<'PY' || fail "compact query paths are wrong"
import json, sys
names = {s["name"] for s in json.load(open(sys.argv[1]))["debug"]["signals"]}
assert any("dut.u_loop_0[0].lane_state.acc" == n for n in names), sorted(names)
assert any("dut.u_loop_0[3].lane_state.acc" == n for n in names), sorted(names)
PY

echo "PASS: compact loop sim (O(1) source, warm digest, VCD/checkpoint/query paths)"
