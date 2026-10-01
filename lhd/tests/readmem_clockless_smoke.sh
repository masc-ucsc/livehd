#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
set -euo pipefail
LHD="${LHD:-lhd/lhd}"
W="${TEST_TMPDIR:-/tmp/livehd_readmem_clockless_$$}"
mkdir -p "$W"
printf '12 34 56 78\n' > "$W/image.hex"

cat > "$W/rom.prp" <<'PRP'
pub mod rom(addr:U2) -> (data:U8@[]) {
  reg mem:[4]U8 = std.readmemh("image.hex")
  data = mem[addr]
}
PRP
cat > "$W/top.prp" <<'PRP'
const rom = import("rom.rom")
pub mod top(addr:U2) -> (data:U8@[]) {
  data = rom(addr=addr).data
}
PRP
"$LHD" compile "$W/top.prp" --top top --set compile.upass.inline=false \
  --workdir "$W/hier" --emit verilog:"$W/hier.v" --emit-dir lg:"$W/lg"

# An explicit public clock remains part of the child/parent interface even
# when the ROM itself does not use it. No compiler-minted clock is needed.
sed 's/rom(addr:U2)/rom(clk:Clock, addr:U2)/' "$W/rom.prp" > "$W/explicit.prp"
cat > "$W/caller.prp" <<'PRP'
const rom = import("explicit.rom")
pub mod caller(clk:Clock, addr:U2) -> (data:U8@[]) {
  data = rom(clk=clk, addr=addr).data
}
PRP
"$LHD" compile "$W/caller.prp" --top caller --set compile.upass.inline=false \
  --workdir "$W/explicit" --emit verilog:"$W/explicit.v"

# Both entry writes and whole-array replacement still require a clock.
cat > "$W/writable.prp" <<'PRP'
pub mod writable(addr:U2, wen:Bool, din:U8) -> (data:U8@[]) {
  reg mem:[4]U8 = std.readmemh("image.hex")
  if wen { mem[addr] = din }
  data = mem[addr]
}
pub mod registered(addr:U2) -> (data:U8@[]) {
  reg mem:[4]U8 = std.readmemh("image.hex")
  reg q:U8 = nil
  q = mem[addr]
  data = q
}
PRP
"$LHD" compile "$W/writable.prp" --workdir "$W/writable" --emit verilog:"$W/writable.v"
cat > "$W/replace.prp" <<'PRP'
pub mod replace(next:[4]U8) -> (data:U8@[]) {
  reg mem:[4]U8 = std.readmemh("image.hex")
  mem = next
  data = mem[0]
}
PRP
# Bulk updates lower to LGraph; external-memory Verilog emission rejects
# them independently, so inspect the lowered interface directly here.
"$LHD" compile "$W/replace.prp" --workdir "$W/replace" --emit-dir lg:"$W/replace-lg"

python3 - "$W" <<'PY'
import pathlib, re, sys
w = pathlib.Path(sys.argv[1])
def header(file, module):
    text = (w / file).read_text()
    match = re.search(r'\bmodule\s+' + module + r'\s*\((.*?)\);', text, re.S)
    assert match, (file, module, text)
    return match.group(1)
for module in ('rom', 'top'):
    assert not re.search(r'\b(clock|reset|clk)\b', header('hier.v', module))
for module in ('rom', 'caller'):
    h = header('explicit.v', module)
    assert re.search(r'\binput\s+clk\b', h), h
    assert not re.search(r'\bclock\b', h), h
assert '.clk(clk)' in (w / 'explicit.v').read_text()
assert ".clk(1'b0)" in (w / 'hier.v').read_text()
for module in ('writable', 'registered'):
    h = header('writable.v', module)
    assert re.search(r'\binput\s+clock\b', h), h
assert re.search(r'\binput\s+\d+\s+clock\b', (w / 'replace-lg/library.txt').read_text())
PY

# Compare against the original clockless Verilog interface, and make sure a
# real data difference still refutes. These are unbounded native proofs.
cat > "$W/ref.v" <<VERILOG
module rom(input [1:0] addr, output [7:0] data);
  reg [7:0] mem [0:3];
  initial \$readmemh("$W/image.hex", mem);
  assign data = mem[addr];
endmodule
VERILOG
"$LHD" lec --ref "$W/ref.v" --impl "$W/rom.prp" --top rom \
  --workdir "$W/proof" --result-json "$W/proof.json"
sed 's/data = mem\[addr\]/data = mem[addr] ^ 1/' "$W/rom.prp" > "$W/bad.prp"
if "$LHD" lec --ref "$W/ref.v" --impl "$W/bad.prp" --top rom \
  --workdir "$W/refutation" --result-json "$W/refutation.json"; then
  echo 'FAIL: altered ROM output proved equivalent' >&2
  exit 1
fi
python3 - "$W" <<'PY'
import json, pathlib, sys
w = pathlib.Path(sys.argv[1])
p = json.loads((w / 'proof.json').read_text())
assert p['lec']['verdict'] == 'proven' and not p['lec']['bounded'], p
r = json.loads((w / 'refutation.json').read_text())
assert r['lec']['verdict'] == 'refuted', r
PY
echo 'PASS: clockless file-preloaded memory preserves its interface and behavior'
