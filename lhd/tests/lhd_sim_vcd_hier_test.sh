#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# Hierarchical VCD dump + the `sim.vcd_fake_delay` style knob:
#   * one VCD carries the WHOLE design tree: the root instance's writer is shared
#     down the sub-instances (__vcd_hier), each under a nested $scope, with the
#     sub's io + flop state traced (not just the top's io);
#   * a flopless wrapper whose `clock:u1` input is wired straight into its subs'
#     clock ports still resolves it as THE clock (the lecfail dut-pair shape) --
#     the waveform label is the real `clock`, never a `clock_vcd0` uniquify;
#   * sim.vcd_fake_delay=true (default): data settles at edge+3 and any signal
#     about to change shows X for the settle window; root inputs (testbench
#     pokes) change exactly AT the edge;
#   * sim.vcd_fake_delay=false: plain edge-aligned updates -- no X, no +3 offset.
# Structural checks drive `lhd sim --setup-only` (hermetic, no host compiler);
# when the sibling ../hlop + ../iassert headers are present the default-mode run
# also produces a real VCD and the nested-scope/X assertions run against it.

set -u

LHD="${LHD:-lhd/lhd}"
W="${TEST_TMPDIR:-/tmp/lhd_sim_vcdh_$$}"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

# The lecfail dut-pair shape: a FLOPLESS wrapper with an explicit clock port
# passed through to two stateful children (one nested two-deep).
cat > "$W/h.prp" <<'EOF'
/*
:name: h
:type: simulation
*/
mod leaf(clock:u1, reset:u1, en:u1) -> (value:u8@[]) {
  reg count:u8 = 0
  value = count
  if reset {
    count = 0
  } elif en {
    wrap count += 1
  }
}
mod mid(clock:u1, reset:u1, en:u1) -> (value:u8@[]) {
  const l = leaf(clock = clock, reset = reset, en = en)
  value = l.value
}
mod pair(clock:u1, reset:u1, en:u1) -> (a_value:u8@[], b_value:u8@[]) {
  a_value = mid(clock = clock, reset = reset, en = en)
  b_value = leaf(clock = clock, reset = reset, en = en)
}
test h {
  mut p = pair
  const _drv_reset = [1, 1, 0, 0, 0, 0, 0, 0]
  tick 8 {
    p.clock = 0
    p.reset = _drv_reset[clock]
    p.en    = 1
    step
  }
}
EOF

# ---- default mode (sim.vcd_fake_delay=true): settle window + X ------------------
"$LHD" sim "$W/h.prp" --setup-only --set sim.vcd=true --workdir "$W/fd" -q >/dev/null 2>&1 \
  || fail "default-mode setup failed"
WRAP="$W/fd/sim/h.pair.cpp"
WRAPH="$W/fd/sim/h.pair.hpp"
LEAF="$W/fd/sim/h.leaf.cpp"
[ -f "$WRAP" ] && [ -f "$LEAF" ] || fail "expected per-module sim bodies not generated"

# the flopless wrapper resolves its pass-through clock port: real label, no uniquify
grep -q '__clk_name = "clock"' "$WRAPH" || fail "wrapper clock label is not the real port"
grep -q 'clock_vcd' "$WRAPH" && fail "wrapper clock label was uniquified (clock_vcd*) -- pass-through detection broke"

# every module carries the VCD machinery; the writer is shared down the tree
grep -q '__vcd_hier' "$WRAP" || fail "wrapper lacks __vcd_hier"
grep -q '__vcd_hier' "$LEAF" || fail "sub-module lacks the VCD machinery (hierarchy would be silent)"
grep -q '\.__vcd_hier(__w, __s + "\.' "$WRAP" || fail "wrapper does not register sub-instances under nested scopes"
grep -q '\.__vcd_dump_data(__w' "$WRAP" || fail "wrapper does not recurse the data dump into its subs"
grep -q 'change(__vv_clk, "1")' "$WRAP" || fail "clock never rises in the trace"
grep -q 'change(__vv_clk, "0")' "$WRAP" || fail "clock never falls in the trace"

# fake-delay mode: pre-commit snapshots, X on change during the settle window
grep -q '__vs0 = ' "$WRAP" || fail "no pre-commit snapshot in cycle()"
grep -q '__vcd_dump_x' "$WRAP" || fail "default mode lacks the X settle window"
grep -q 'same_repr' "$WRAP" || fail "X window is not change-gated (same_repr)"
grep -q '__b + 3' "$WRAP" || fail "default mode lacks the +3 settle offset"

# ---- traditional mode (sim.vcd_fake_delay=false): edge-aligned, no X ------------
"$LHD" sim "$W/h.prp" --setup-only --set sim.vcd=true --set sim.vcd_fake_delay=false --workdir "$W/tr" -q >/dev/null 2>&1 \
  || fail "vcdfakedelay=false setup failed"
TWRAP="$W/tr/sim/h.pair.cpp"
[ -f "$TWRAP" ] || fail "traditional-mode body not generated"
grep -q '__vcd_dump_x' "$TWRAP" && fail "vcdfakedelay=false must not emit the X phase"
grep -q 'same_repr' "$TWRAP" && fail "vcdfakedelay=false must not emit change tracking"
grep -q '__b + 3' "$TWRAP" && fail "vcdfakedelay=false must not offset data from the edge"
grep -q '\.__vcd_hier(__w, __s + "\.' "$TWRAP" || fail "traditional mode lost the hierarchy"

# the knob validates like any sim.* boolean
"$LHD" sim "$W/h.prp" --setup-only --set sim.vcd_fake_delay=bogus --workdir "$W/bad" -q >/dev/null 2>&1 \
  && fail "--set sim.vcd_fake_delay=bogus must be rejected"

# ---- runtime: real VCD with nested scopes + X settle window -------------------
HLOP_INC=""
IASSERT_INC=""
for d in ../hlop/hlop ../hlop; do [ -f "$d/slop.hpp" ] && HLOP_INC="$d" && break; done
for d in ../iassert/src ../iassert; do [ -f "$d/iassert.hpp" ] && IASSERT_INC="$d" && break; done
if [ -z "$HLOP_INC" ] || [ -z "$IASSERT_INC" ]; then
  echo "SKIP run checks: sibling hlop/iassert headers not found (structural checks passed)"
  echo "PASS: lhd sim hierarchical VCD + sim.vcd_fake_delay (structural)"
  exit 0
fi

"$LHD" sim "$W/h.prp" --set sim.vcd=true --workdir "$W/run" -q >/dev/null 2>&1 \
  || fail "default-mode run failed"
VCD="$W/run/h.vcd"
[ -s "$VCD" ] || fail "no VCD produced"

# nested $scope blocks: pair > mid > leaf (three levels), each with traced vars,
# and a BALANCED $scope/$upscope stack at $enddefinitions
[ "$(grep -c '^\$scope module' "$VCD")" -ge 3 ] || fail "VCD lacks the nested hierarchy scopes"
grep -q 'count\[' "$VCD" || fail "sub-module flop state not traced"
awk '/^\$scope/{d++; if(d>m)m=d} /^\$upscope/{d--} /^\$enddefinitions/{exit (m>=3 && d==0)?0:1}' "$VCD" \
  || fail "scopes are not NESTED 3 deep with a balanced header"
# a scope with both vars and children stays OPEN around its children (no
# close+reopen duplicate -- needs the sibling hlop write_header subtree fix)
[ "$(grep -c '^\$scope module u_mid_a_value_0' "$VCD")" = 1 ] \
  || fail "intermediate scope declared twice (closed and reopened around its child)"
# the FIRST dumped period must show the real poked inputs (reset=1 on cycle 0),
# not default-zero snapshots (regression: snapshot gated on a flag set too late)
RID=$(awk '/^\$var wire 1 .* reset \$end/{print $4; exit}' "$VCD")
[ -n "$RID" ] || fail "no top-scope reset var"
awk -v id="$RID" '/^#10$/{exit 1} $0=="1"id{f=1; exit} END{exit f?0:1}' "$VCD" \
  || fail "first period lost the poked reset=1 (default-zero first-cycle snapshots)"
# the clock is the real label and it toggles
grep -q '^\$var wire 1 . clock \$end' "$VCD" || fail "no 1-bit clock var under the top scope"
grep -q 'clock_vcd' "$VCD" && fail "VCD clock label was uniquified"
# the settle window shows X between the edge and the settled data
grep -qE '^(x.|bx )' "$VCD" || fail "no X settle window in the default-mode VCD"

"$LHD" sim "$W/h.prp" --set sim.vcd=true --set sim.vcd_fake_delay=false --workdir "$W/run2" -q >/dev/null 2>&1 \
  || fail "vcdfakedelay=false run failed"
VCD2="$W/run2/h.vcd"
[ -s "$VCD2" ] || fail "no traditional-mode VCD produced"
grep -qE '^(x.|bx )' "$VCD2" && fail "vcdfakedelay=false VCD must not contain X"
# edge-aligned: every timestamp is a clock edge (multiple of 5); +3 offsets are absent
grep -E '^#[0-9]+' "$VCD2" | grep -qvE '^#[0-9]*[05]$' && fail "traditional VCD has off-edge timestamps"
# ...and POSITIVE evidence it still traces: the counters actually count
grep -q '^b000000001 ' "$VCD2" || fail "traditional VCD carries no data changes (vacuous trace)"
awk '/^\$scope module/{s++} /^\$var/{v++} END{exit (s>=3 && v>=10)?0:1}' "$VCD2" \
  || fail "traditional VCD lost the hierarchy vars"

# ---- runtime: two DUT instances in one test = two VCDs, no writer abort --------
cat > "$W/two.prp" <<'EOF'
/*
:name: two
:type: simulation
*/
mod cnt(en:bool) -> (value:u8@[0]) {
  reg count:u8 = 0
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
"$LHD" sim "$W/two.prp" --set sim.vcd=true --workdir "$W/run3" -q >/dev/null 2>&1 \
  || fail "two-instance VCD run failed (second writer registration aborts?)"
[ -s "$W/run3/two.x.vcd" ] && [ -s "$W/run3/two.y.vcd" ] \
  || fail "expected one VCD per instance (two.x.vcd + two.y.vcd)"

echo "PASS: lhd sim hierarchical VCD + sim.vcd_fake_delay (structural + run)"
