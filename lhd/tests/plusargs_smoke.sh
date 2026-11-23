#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
set -euo pipefail
LHD="${LHD:-lhd/lhd}"
W="${TEST_TMPDIR:-/tmp/livehd_plusargs_$$}"
mkdir -p "$W"
cat > "$W/args.prp" <<'PRP'
mod echo(a:U8) -> (b:U8@[0]) { b = a }
test echo.run(cycles:U8=3, seed:U8=7, verbose:Bool=false, file="program.hex", derived:U8=std.valueplusarg("fallback_derived", 2)) {
  mut dut = echo
  const n = std.valueplusarg("cycles", default=3)
  const path = std.valueplusarg("file", "program.hex")
  assert(n == cycles)
  assert(seed == 7)
  assert(derived == 2)
  assert(path == file)
  assert(not std.testplusarg("absent"))
  assert(not std.testplusarg("cyc"))
  if std.testplusarg("request_missing") { const missing = std.valueplusarg("missing"); assert(missing == 1) }
  if std.testplusarg("required") { const required = std.valueplusarg("required"); assert(required == 1) }
  tick cycles { dut.a = 1; step; assert(dut.b == 1) }
  puts("cycles={cycles} verbose={verbose} file={path}")
}
test echo.defaults(cycles:U8=9, n:U8=1+2, offset:S8=-2+n, cooked="A\x42{{}}\u{43}\0D", raw='A\n',
                   label="n={n+1}", wide:U80=0x1234_5678_9abc_def0_1234, signed_wide:S80=0sx80000000000000000000,
                   tail:U8=n+std.valueplusarg("extra", 1)) {
  assert(n == 3)
  assert(offset == 1)
  assert(cooked == "AB{{}}C\x00D")
  assert(cooked != "AB{{}}C")
  assert(raw == "A\\n")
  assert(label == 'n=4')
  assert(tail == 4)
  assert(1K == 1024)
  assert(0so777 == -1)
  assert(0o111 == 73)
  if std.testplusarg("override_wide") {
    assert(wide == 0xffff_ffff_ffff_ffff_ffff)
    assert(signed_wide == 0x7fff_ffff_ffff_ffff_ffff)
  } else {
    assert(wide == 0x1234_5678_9abc_def0_1234)
    assert(signed_wide == -0x8000_0000_0000_0000_0000)
  }
  puts("decoded=\x41{n+1}")
  puts('raw={n}\n')
  puts("nul={cooked}!")
  assert(cycles == std.valueplusarg("cycles", 9))
  if not std.testplusarg("cycles") { assert(cycles == 9) }
}
PRP
"$LHD" sim "$W/args.prp" --workdir "$W/run" --result-json "$W/default.json"
SIMRT="${LHD_SIMRT:-$(dirname "$LHD")/lhd_simrt}"
[ -x "$SIMRT" ] || { echo "lhd_simrt not found next to lhd ($SIMRT)" >&2; exit 1; }
# the simulator runtime, run directly on the setup `lhd sim` wrote
drv() { "$SIMRT" "$W/run/sim" "$@"; }
"$LHD" sim "$W/args.prp" --run-only --workdir "$W/run" +cycles=4 +verbose '+file=a=b.hex' --seed 42 +seed=7 --result-json "$W/cli.json"
drv +cycles=4 +cycles=8 +verbose '+file=a=b.hex' --seed 42 +seed=7 --result-json "$W/direct.json" > "$W/direct.log"
grep -q 'cycles=4 verbose=1 file=a=b.hex' "$W/direct.log"
# Compatibility aliases are intentional coverage, not the preferred spelling.
"$LHD" sim "$W/args.prp" --run-only --workdir "$W/run" --arg cycles=4 --result-json "$W/alias.json"
drv --cycles=4 --result-json "$W/old.json" > /dev/null
drv +derived=2 +fallback_derived=typo > /dev/null
drv --test echo.defaults +override_wide +wide=1208925819614629174706175 +signed_wide=0x7fffffffffffffffffff > "$W/wide.log"
grep -Fq 'decoded=A4' "$W/wide.log"
grep -Fq 'raw={n}\n' "$W/wide.log"
python3 - "$W/wide.log" <<'PYEND'
import pathlib, sys
assert b'nul=AB{}C\0D!' in pathlib.Path(sys.argv[1]).read_bytes()
PYEND
if drv --test echo.defaults +extra=253 > "$W/default-overflow.log" 2>&1; then
  echo 'accepted an out-of-range computed default' >&2; exit 1
fi
grep -q 'outside U8 range' "$W/default-overflow.log"
drv --test echo.defaults +extra=253 +tail=4 > /dev/null
for bad in +wide=0x100000000000000000000 +signed_wide=0x80000000000000000000; do
  if drv --test echo.defaults "$bad" > "$W/wide-error.log" 2>&1; then echo "accepted $bad" >&2; exit 1; fi
  grep -q 'outside .* range' "$W/wide-error.log"
done
drv +file= --result-json "$W/empty.json" > /dev/null
for bad in '+cycles' '+cycles=256' '+cycles=-1' '+cycles=typo' '+cycles=0b11' '+required' '+verbose=typo' '+request_missing'; do
  if drv "$bad" > "$W/bad.log" 2>&1; then echo "accepted invalid argument $bad" >&2; exit 1; fi
  grep -Eq 'requires a value|outside U8 range|expects an integer|expects true|missing simulation argument' "$W/bad.log"
done
# Replay may reuse only checkpoints from the same supplied/resolved arguments.
drv --test echo.run +cycles=4 --ckpt-dir "$W/checkpoints" --checkpoint-every 1 > /dev/null
drv --test echo.run +cycles=4 --ckpt-dir "$W/checkpoints" --restart-cycle 2 > /dev/null
if drv --test echo.run +cycles=5 --ckpt-dir "$W/checkpoints" --restart-cycle 2 > "$W/replay.log" 2>&1; then
  echo 'accepted checkpoint with different arguments' >&2; exit 1
fi
grep -q 'checkpoint simulation arguments differ' "$W/replay.log"
python3 - "$W" <<'PY'
import json, pathlib, sys
w=pathlib.Path(sys.argv[1])
def rows(name):
    result=json.loads((w/name).read_text())
    return result['tests'] if isinstance(result,dict) else result
r=rows('default.json')
assert [x['parameters']['cycles'] for x in r]==['3','9'],r
assert all(x['arguments']==[] and x['status']=='pass' for x in r),r
p=r[1]['parameters']
assert (p['n'],p['offset'],p['label'],p['tail'],p['cooked']) == ('3','1','n=4','4','AB{}C\0D'),p
for name in ['cli.json','direct.json','alias.json','old.json']:
    r=rows(name)
    assert all(x['status']=='pass' and x['parameters']['cycles']=='4' for x in r),(name,r)
assert rows('empty.json')[0]['parameters']['file']==''
assert rows('direct.json')[0]['arguments'][:2]==['+cycles=4','+cycles=8']
PY
# Runtime argument reads cannot accidentally specialize synthesizable hardware.
cat > "$W/bad.prp" <<'PRP'
mod bad(a:U8) -> (b:U8@[0]) { b = if std.testplusarg("feature") { a } else { 0 } }
PRP
if "$LHD" compile "$W/bad.prp" --workdir "$W/bad" > "$W/hardware.log" 2>&1; then
  echo 'accepted a runtime argument in hardware' >&2; exit 1
fi
grep -q 'simulation arguments may only be read inside a test block' "$W/hardware.log"
echo 'PASS: plusargs, typed defaults, aliases, metadata, and hardware rejection'
