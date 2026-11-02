#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
# Formerly refused multi-clock loop fixtures must execute correctly now that
# compact loops carry independent per-port period events. Exercise holding,
# reopening, and shared root clocks on LLVM and Slop.
set -eu
LHD="${LHD:-lhd/lhd}"
DESIGN=${1:?design}
BACKEND=${2:?backend}
case "$DESIGN" in twoclk | twoclk_ungated | fusedg) ;; *) exit 2 ;; esac
case "$BACKEND" in llvm | slop) ;; *) exit 2 ;; esac
W="${TEST_TMPDIR:-/tmp/lhd_loop_independent_clocks_$$}"
mkdir -p "$W"
fail() { echo "FAIL: $*" >&2; exit 1; }

cat >"$W/twoclk.prp" <<'EOF'
mod pair(clk:Clock, gclk:Clock, inc:Bool) -> (a:U4@[0], b:U4@[0]) {
  reg fa:U4:[clock_pin=clk] = 0
  reg fb:U4:[clock_pin=gclk] = 0
  a = fa
  b = fb
  if inc {
    wrap fa += 1
    wrap fb += 1
  }
}
mod lanes(clk:Clock, gclk:Clock, inc:Bool) -> (sa:U8@[0], sb:U8@[0]) {
  mut acc_a:U8 = 0
  mut acc_b:U8 = 0
  for j in 0..<2 {
    const p = pair(clk=clk, gclk=gclk, inc=inc)
    wrap acc_a = acc_a + p.a
    wrap acc_b = acc_b + p.b
  }
  sa = acc_a
  sb = acc_b
}
pub mod twoclk(clk:Clock, en:Bool, inc:Bool) -> (sa:U8@[], sb:U8@[]) {
  const g = Clock(clock_pin=clk, enable=en)
  const r = lanes(clk=clk, gclk=g, inc=inc)
  sa = r.sa
  sb = r.sb
}
test twoclk.t {
  mut x = twoclk
  mut expected_a:U8 = 0
  mut expected_b:U8 = 0
  tick 6 {
    const active = (clock % 2) == 0
    x.en = active
    x.inc = true
    expected_a += 2
    if active { expected_b += 2 }
    step
    assert(x.sa == expected_a, "ungated domain advances while the other clock is closed")
    assert(x.sb == expected_b, "secondary domain holds and resumes independently")
  }
}
EOF
# The same loop with both ports on the top clock must advance both domains.
sed -e 's/pub mod twoclk(clk:Clock, en:Bool, inc:Bool)/pub mod twoclk(clk:Clock, inc:Bool)/' \
  -e '/const g = Clock(clock_pin=clk, enable=en)/d' -e 's/gclk=g,/gclk=clk,/' -e '/x.en = active/d' -e 's/const active = (clock % 2) == 0/const active = true/' \
  "$W/twoclk.prp" >"$W/twoclk_ungated.prp"
grep -qF 'gclk=clk,' "$W/twoclk_ungated.prp" || fail "the ungated two-clock fixture was not derived"
# Two loops fused into one body, one lane on a gated clock: no secondary-clock state,
# but a withheld period on the gated port would also hold the ungated lanes.
cat >"$W/fusedg.prp" <<'EOF'
mod lane1(clk:Clock, a:U8) -> (q:U8@[0]) {
  reg acc:U8:[clock_pin=clk] = 0
  wrap acc = acc + a
  q = acc
}
mod body(clk:Clock, gclk:Clock, a:U8) -> (first:U8@[0], second:U8@[0]) {
  mut x:U8 = 0
  for i in 0..<2 {
    const value = lane1(clk=clk, a=(a + i)#[0..<8])
    wrap x = x + value
  }
  first = x
  mut y:U8 = 0
  for i in 0..<2 {
    const value = lane1(clk=gclk, a=(a + i + 1)#[0..<8])
    wrap y = y + value
  }
  second = y
}
pub mod fusedg(clk:Clock, en:Bool, a:U8) -> (first:U8@[], second:U8@[]) {
  const g = Clock(clock_pin=clk, enable=en)
  const r = body(clk=clk, gclk=g, a=a)
  first = r.first
  second = r.second
}
test fusedg.t {
  mut x = fusedg
  mut expected_first:U8 = 0
  mut expected_second:U8 = 0
  tick 6 {
    const active = (clock % 2) == 0
    x.en = active
    x.a = 1
    expected_first += 3
    if active { expected_second += 5 }
    step
    assert(x.first == expected_first, "fused ungated lanes advance independently")
    assert(x.second == expected_second, "fused gated lanes hold and resume")
  }
}
EOF
"$LHD" sim "$W/$DESIGN.prp" --set sim.tune.backend=$BACKEND --workdir "$W/run" -q >"$W/run.log" 2>&1 \
  || fail "$BACKEND/$DESIGN: $(cat "$W/run.log")"
echo "PASS: $BACKEND/$DESIGN preserves independent clock domains"
