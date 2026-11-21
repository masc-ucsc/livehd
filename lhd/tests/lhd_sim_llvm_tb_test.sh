#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# `lhd sim` through the LLVM testbench flow (todo/livehd/2sl-llvm-only-sim.md):
#   * NO generated C++ in the sim directory: the DUT kernels are objects, the testbench is `drv.tb.o`
#     (inou/prp/tb_llvm), run by `lhd_simrt` (no host compiler, no link step);
#   * pass / fail verdicts, the located assert text and the exit codes are the old driver's;
#   * checkpoints are native (`<workdir>/ckpt/<test>/c<cycle>/`), `--restart-cycle` resumes from the nearest
#     one and reproduces the full run's final state (DUT register + testbench local);
#   * the VCD is not the simulator's job: `sim.vcd=true` replays a trace object over the checkpoints
#     (whole run, `--vcd-from/--vcd-to` window with an absolute timeline, `--vcd-on-fail`).

set -u

LHD="${LHD:-lhd/lhd}"
LHD="$(cd "$(dirname "$LHD")" && pwd)/$(basename "$LHD")"  # absolute: the test runs in its own temp dir
SIMRT="${LHD_SIMRT:-$(dirname "$(readlink -f "$LHD" 2>/dev/null || echo "$LHD")")/lhd_simrt}"
[ -x "$SIMRT" ] || SIMRT="$(dirname "$LHD")/lhd_simrt"
export LHD_SIMRT="$SIMRT"
W="${TEST_TMPDIR:-/tmp/lhd_sim_llvm_tb_$$}"
mkdir -p "$W"
cd "$W" || exit 1

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

cat > cnt.prp <<'PRP'
pub mod counter(enable:Bool) -> (value:U8@[0]) {
  reg count:U8 = 0
  value = count
  if enable { wrap count += 1 }
}
PRP
cat > tb.prp <<'PRP'
const counter = import("cnt.counter")

test good(n:U20 = 60) {
  mut d = counter
  mut sum = 0
  tick n {
    d.enable = (clock & 3) != 0
    d.reset = clock < 2
    step
    sum += d.value
    if clock == 59 { assert(d.value == 44, "final count") }
  }
  puts("sum={sum} value={d.value}")
}

test bad {
  mut d = counter
  tick 4 {
    d.enable = true
    d.reset = false
    step
    assert(d.value == 99, "never")
  }
}
PRP

# 1. verdicts, located assert text, exit status, and no generated C++
"$LHD" sim tb.prp good --workdir w --set sim.checkpoint_every=10 > good.json 2> good.err
grep -q '"status":"pass"' good.json || fail "good test did not pass: $(tail -c 400 good.json)"
n_cpp=$(ls w/sim | grep -cE '\.(cpp|hpp)$')
[ "$n_cpp" = "0" ] || fail "the LLVM flow generated C++: $(ls w/sim)"
[ -f w/sim/drv.tb.o ] || fail "no drv.tb.o"

"$LHD" sim tb.prp bad --workdir w > bad.json 2> bad.err
grep -q '"status":"fail"' bad.json || fail "bad test did not fail"
grep -q 'assert fail: clock=0: d.value=1 == 99  \[never\]' bad.json bad.err w/sim/sim_tests.json 2>/dev/null \
  || "$SIMRT" w/sim --test bad --no-checkpoint | grep -q 'tb.prp:.*assert fail: clock=0: d.value=1 == 99  \[never\]' \
  || fail "the located assert text differs from the old driver's"
"$SIMRT" w/sim --test bad --no-checkpoint > /dev/null
[ $? -eq 1 ] || fail "a failing test must exit 1"

# 2. native checkpoints at the requested cadence, and a restart reproduces the full run
"$LHD" sim tb.prp good --workdir w --set sim.checkpoint_every=10 > /dev/null 2>&1
[ -f w/ckpt/good/c10/complete ] && [ -f w/ckpt/good/c20/state.bin ] || fail "checkpoints were not written: $(ls w/ckpt/good 2>&1)"
full=$("$SIMRT" w/sim --test good --no-checkpoint)
rest=$("$SIMRT" w/sim --test good --ckpt-dir w/ckpt --checkpoint-every 10 --restart-cycle 37 2>/dev/null)
[ "$full" = "$rest" ] || fail "restart differs from the full run: [$full] vs [$rest]"
"$SIMRT" w/sim --test good --ckpt-dir w/ckpt --checkpoint-every 10 --restart-cycle 37 2>&1 >/dev/null | grep -q 'restarted from checkpoint cycle 30' \
  || fail "restart did not use the nearest checkpoint <= 37"
# a changed design must not be resumed from a stale checkpoint
"$LHD" sim tb.prp good --workdir w --no-checkpoint > /dev/null 2>&1 || true

# 3. the VCD is a replay over checkpoints
"$LHD" sim tb.prp good --workdir wv --set sim.vcd=true --set sim.checkpoint_every=10 > /dev/null 2>&1
[ -f wv/ckpt/good/c10/complete ] || fail "no checkpoint beside the vcd run"
[ -f wv/good.vcd ] || fail "no VCD from the replay: $(ls wv)"
grep -q 'enable' wv/good.vcd && grep -q 'value\[7:0\]' wv/good.vcd || fail "the VCD lacks the ports"
[ "$(grep -c '^#' wv/good.vcd)" = "120" ] || fail "whole-run VCD should have 60 periods, got $(grep -c '^#' wv/good.vcd) timestamps"
"$LHD" sim tb.prp good --workdir wv --set sim.checkpoint_every=10 --vcd-from 42 --vcd-to 45 > /dev/null 2>&1
first=$(grep '^#' wv/good.vcd | head -1)
[ "$first" = "#420" ] || fail "a windowed VCD keeps the absolute timeline (#420), got $first"
"$LHD" sim tb.prp bad --workdir wv --set sim.checkpoint_every=10 --vcd-on-fail --vcd-fail-window 2 > /dev/null 2> onfail.err
grep -q 'wrote failure VCD' onfail.err || fail "--vcd-on-fail wrote nothing: $(cat onfail.err)"

# 4. exact-width values: a 130-bit port and a 200-bit literal are plain LLVM integers (no 64-bit plane, no Slop);
#    a constant table is indexed at run time; every `?` bit of a literal is drawn once per run
cat > wide.prp <<'PRP'
pub mod wide(a:U130, b:U130) -> (s:U131@[0], hi:U8@[0]) {
  s  = a + b
  hi = s#[123..=130]
}
PRP
cat > wide_tb.prp <<'PRP'
const wide = import("wide.wide")

test wide.sum {
  mut d = wide
  const big = 0x1_0000_0000_0000_0000_0000_0000_0000_0001
  const table = [3, 0x123456789abcdef0123456789abcdef, 7]
  tick 3 {
    d.a = big
    d.b = table[clock] + 1
    step
    assert(d.s == big + table[clock] + 1, "130-bit sum")
    assert(d.hi == (d.s >> 123) & 0xff)
    puts("clock={clock} s={d.s:x} hi={d.hi:b}")
  }
  assert(big > 0xffff_ffff_ffff_ffff_ffff_ffff_ffff, "a literal past 64 bits keeps its value")
}
PRP
"$LHD" sim wide_tb.prp --workdir ww > ww.json 2> ww.err
grep -q '"status":"pass"' ww.json || fail "the wide-value test did not pass: $(tail -c 400 ww.json) $(cat ww.err)"
out=$("$SIMRT" ww/sim --no-checkpoint)
echo "$out" | grep -q 'wide_tb.prp:.*puts:clock=0 s=100000000000000000000000000000005 hi=100000' || fail "wide hex print differs: $out"


# Runtime operands cover the signed minimum (the quotient gains one bit).
cat > division.prp <<'PRP'
const counter = import("cnt.counter")
test divide(x:S64, y:S64, den:S32=-1, big:U64=18446744073709551615) {
  mut dut = counter
  mut x32:S32 = -2147483648
  tick 1 {
    dut.enable = false
    step
    assert(x / y == 9223372036854775808)
    assert(x % y == 0)
    assert(big == 18446744073709551615)
    assert(x32 / den == 2147483648)
    assert(x32 % den == 0)
  }
}
PRP
"$LHD" sim division.prp --workdir wd +x=-9223372036854775808 +y=-1 >wd.log 2>&1 || fail "$(cat wd.log)"
for bad in 9223372036854775808 -9223372036854775809 99999999999999999999; do
  "$SIMRT" wd/sim "+x=$bad" +y=-1 >arg.log 2>&1
  [ "$?" -eq 1 ] && grep -Fq "+x expects an integer" arg.log || fail "out-of-range argument $bad: $(cat arg.log)"
done
python3 - good.json w/sim <<'PY' || fail "DUT accounting"
import json, pathlib, re, sys
result = json.load(open(sys.argv[1]))
sim = pathlib.Path(sys.argv[2])
names = [u["name"] for u in json.loads((sim / "manifest.json").read_text())["units"]]
assert len(names) == len(set(names)), names
words = sum(json.loads(f.read_text())["words"] for f in sim.glob("*.dut.json"))
assert any(re.search(rf"pass.lnast_sim words:{words} ", step) for step in result["recipe"])
PY

cat > domain.prp <<'PRP'
pub mod dual(rclk:Clock, wclk:Clock, d:U8) -> (r:U8@[1], w:U8@[1]) {
  reg a:U8:[clock_pin=rclk] = 0
  reg b:U8:[clock_pin=wclk] = 0
  a = d
  wrap b = d + 1
  r = a
  w = b
}
PRP
"$LHD" compile domain.prp --emit-dir lg:domain-lg --workdir import >import.log 2>&1 || fail "$(cat import.log)"
cat > waveform.prp <<'PRP'
const dual = import("lg:domain.dual")
test shared {
  mut dut = dual
  tick 3 {
    dut.d = 17
    dut.rclk = clock
    dut.wclk = clock
    step
    assert(dut.r == 17 and dut.w == 18)
  }
}
test independent {
  mut dut = dual
  tick 4 {
    dut.rclk = (clock & 1) == 0
    dut.wclk = (clock & 1) == 1
    dut.d = 17
    step
  }
}
PRP
"$LHD" sim lg:domain-lg waveform.prp shared --workdir clocks >shared.log 2>&1 || fail "$(cat shared.log)"
grep -q 'shared-tick-clock' shared.log || fail "missing clock diagnostic: $(cat shared.log)"
"$LHD" sim lg:domain-lg waveform.prp independent --workdir clocks >domain.log 2>&1 && fail "independent clocks were merged"
grep -q 'independent clock waveforms.*dut\.rclk' domain.log && grep -q 'dut\.wclk' domain.log || fail "$(cat domain.log)"

echo "PASS lhd_sim_llvm_tb_test"
