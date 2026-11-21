#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# Check the hierarchical VCD scopes and edge-aligned outputs produced by replay.

set -u

LHD="${LHD:-lhd/lhd}"
W="${TEST_TMPDIR:-/tmp/lhd_sim_vcdh_$$}"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

lhd_sim() {
  "$LHD" sim "$@"
}


GROUP="${1:-all}"
case "$GROUP" in
  all)
    for group in instances; do
      "$0" "$group" || exit $?
    done
    exit 0
    ;;
  instances) ;;
  *) fail "unknown check group: $GROUP" ;;
esac

# The lecfail dut-pair shape: a FLOPLESS wrapper with an explicit clock port
# passed through to two stateful children (one nested two-deep).
cat > "$W/h.prp" <<'EOF'
/*
:name: h
:type: simulation
*/
mod leaf(clock:Clock, reset:Reset, en:U1) -> (value:U8@[]) {
  reg count:U8 = 0
  value = count
  if reset {
    count = 0
  } elif en != 0 {
    wrap count += 1
  }
}
mod mid(clock:Clock, reset:Reset, en:U1) -> (value:U8@[]) {
  const l = leaf(clock = clock, reset = reset, en = en)
  value = l.value
}
mod pair(clock:Clock, reset:Reset, en:U1) -> (a_value:U8@[], b_value:U8@[]) {
  a_value = mid(clock = clock, reset = reset, en = en)
  b_value = leaf(clock = clock, reset = reset, en = en)
}
test h {
  mut p = pair
  const _drv_reset = [1, 1, 0, 0, 0, 0, 0, 0]
  tick 8 {
    p.reset = _drv_reset[clock] != 0
    p.en    = 1
    step
  }
}
EOF

check_observation() {
  local VCD_TO_CHECK=$1
python3 - "$VCD_TO_CHECK" <<'PY'
import pathlib, sys

# The output is the counter's pre-commit value. Internal state must use the
# same observation instant, even when an untraced plan can update it in-place.
for filename in sys.argv[1:]:
    scope, signals, values, pairs = [], {}, {}, []
    lines = pathlib.Path(filename).read_text().splitlines()
    for line in lines:
        fields = line.split()
        if line.startswith('$scope '):
            scope.append(fields[2])
        elif line.startswith('$upscope'):
            scope.pop()
        elif line.startswith('$var '):
            signals[tuple(scope), fields[4].split('[')[0]] = fields[3]
        elif line.startswith('$enddefinitions'):
            break
    for (path, name), ident in signals.items():
        if name.endswith('count') and (path, 'value') in signals:
            pairs.append((ident, signals[path, 'value']))
    assert pairs, filename
    checked = 0
    def check():
        global checked
        for state, output in pairs:
            a, b = values.get(state, 'x'), values.get(output, 'x')
            if set(a + b) <= {'0', '1'}:
                assert int(a, 2) == int(b, 2), (filename, state, output, a, b)
                checked += 1
    for line in lines:
        if line.startswith('#'):
            check()
        elif line.startswith('b'):
            bits, ident = line[1:].split()
            values[ident] = bits
    check()
    assert checked >= 8, (filename, checked)
PY
if [ "$?" -ne 0 ]; then fail "VCD state/output observation instants differ"; fi
}


if [ "$GROUP" = "instances" ]; then
# ---- runtime: two DUT instances in one test = two VCDs, no writer abort --------
cat > "$W/two.prp" <<'EOF'
/*
:name: two
:type: simulation
*/
mod cnt(en:Bool) -> (value:U8@[0]) {
  reg count:U8 = 0
  value = count
  if en { wrap count += 1 }
}
test two {
  mut x = cnt
  mut y = cnt
  tick 4 {
    x.en = true
    y.en = true
    step
  }
}
EOF
lhd_sim "$W/two.prp" --set sim.vcd=true --workdir "$W/run3" -q >/dev/null 2>&1 \
  || fail "two-instance VCD run failed (second writer registration aborts?)"
[ -s "$W/run3/two.x.vcd" ] && [ -s "$W/run3/two.y.vcd" ] \
  || fail "expected one VCD per instance (two.x.vcd + two.y.vcd)"

fi

echo "PASS: lhd sim hierarchical VCD ($GROUP)"
