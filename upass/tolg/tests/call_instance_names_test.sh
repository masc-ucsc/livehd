#!/usr/bin/env bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
# Two branch-local calls assigned to one variable must have distinct physical
# paths. Minion's txfma_c0 hit this in post-synthesis timing seed placement.
set -euo pipefail
W="${TEST_TMPDIR:-/tmp/lhd_call_instance_names_$$}"
mkdir -p "$W"
cat > "$W/calls.prp" <<'EOF'
comb mix(a:U1, b:U1) -> (z:U1) {
  z = a ^ b
}
mod top(clock:Clock, a:U1, b:U1, c:U1, choose:Bool) -> (y:U1@[0]) {
  mut result:U1 = 0
  if choose {
    result = mix(a=a, b=b)
  } else {
    result = mix(a=a, b=c)
  }
  y = result
}
EOF
echo 'create_clock -name clock -period 100 [get_ports clock]' > "$W/clock.sdc"
TECH=inou/lefdef/tests/data
lhd/lhd synth "$W/calls.prp" --top top --workdir "$W/work" \
  --set "synth.liberty=$TECH/sky130hd.lib" \
  --set "synth.lef=$TECH/sky130hd.tlef,$TECH/sky130hd.lef" \
  --set "synth.sdc=$W/clock.sdc" \
  --set synth.place=true --set synth.place.mode=full --set synth.place.final=true \
  --set synth.place.unknown_const=zero --set synth.memory=flops \
  --set pass.abc.drop_init=true --set pass.abc.derived_clock=true --set lhd.jobs=8 \
  --result-json "$W/result.json" > "$W/run.log" 2>&1 || {
    cat "$W/result.json" "$W/run.log"
    exit 1
  }
# Ensure the failing phase actually ran; a compile/synthesis-only run masks it.
grep -q '"name":"pass.place.post_synth_timing"' "$W/result.json"
echo 'PASS: repeated call destinations survive timing seed and full placement'
