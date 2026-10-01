#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
set -euo pipefail
LHD="${LHD:-lhd/lhd}"
W="${TEST_TMPDIR:-/tmp/livehd_readmem_$$}"
mkdir -p "$W/cwd"
LIB="$(pwd)/inou/prp/tests/abc/test.lib"
LHD="$(cd "$(dirname "$LHD")" && pwd)/$(basename "$LHD")"
cat > "$W/image.hex" <<'IMAGE'
/* sparse image */ 12 34 // two entries
@2 ab
IMAGE
cat > "$W/image.bin" <<'IMAGE'
00010010 @2 1010_1011
IMAGE
cat > "$W/load.prp" <<'PRP'
pub mod load(clk:Clock, rst:Reset, addr:U2, wen:Bool, din:U8) -> (data:U8@[], binary:U8@[]) {
  comptime const FILE = "image.hex"
  reg mem:[4]U8:[ordering="old"] = std.readmemh(FILE)
  reg bits:[4]U8 = std.readmemb("image.bin")
  if wen { mem[addr] = din }
  data = mem[addr]
  binary = bits[addr]
}
test preload(expected:U8=0x12) {
  mut m = load
  tick 6 {
    m.addr = if `clock` < 2 { 0 } else { 2 }
    m.rst = `clock` == 0 or `clock` == 5
    m.wen = `clock` == 3
    m.din = 0x42
    step
    if `clock` < 2 { assert(m.data == expected) }
    elif `clock` == 2 { assert(m.data == 0xab); assert(m.binary == 0xab) }
    elif `clock` >= 4 { assert(m.data == 0x42) }
  }
}
PRP
# Run from another directory: relative image paths belong to the source file.
cd "$W/cwd"
"$LHD" sim "$W/load.prp" --workdir "$W/run" --set sim.init_zero=true --set sim.unknown_zero=true --result-json "$W/result.json"
python3 - "$W/result.json" <<'PY'
import json,sys
r=json.load(open(sys.argv[1])); assert r['status']=='pass',r
PY
# A content-only change must be seen by the SAME generated executable.
printf '56 34 @2 ab\n' > "$W/image.hex"
"$W/run/sim/drv.bin" --init-zero +expected=86 > "$W/changed.log"
grep -q 'PASS preload' "$W/changed.log"
# Missing and malformed files are clean failures, including the filename.
for image in missing malformed; do
  if [ "$image" = missing ]; then rm "$W/image.hex"; else printf '12 xyz!\n' > "$W/image.hex"; fi
  if "$W/run/sim/drv.bin" --init-zero > "$W/error.log" 2>&1; then
    echo "loader accepted $image image" >&2; exit 1
  fi
  grep -q 'readmem:.*image.hex:' "$W/error.log"
done
printf '12 34 @2 ab\n' > "$W/image.hex"
"$LHD" compile "$W/load.prp" --top load --workdir "$W/export" --emit verilog:"$W/load.v" --emit-dir lg:"$W/lg" --emit-dir ln:"$W/ln"
grep -q 'blackbox, keep' "$W/load.v"
grep -q 'INIT_FILE' "$W/load.v"
# Both serialized IR forms retain startup loading and normal runtime writes.
for kind in ln lg; do
  { printf 'const load = import("%s:load.load")\n' "$kind"; sed -n '/^test preload/,$p' "$W/load.prp"; } > "$W/imported.prp"
  "$LHD" sim "$W/imported.prp" "$kind:$W/$kind" --workdir "$W/$kind-run" --set sim.init_zero=true --set sim.unknown_zero=true --result-json "$W/$kind.json"
done
# Slang imports both system tasks into the same startup representation.
cat > "$W/slang.sv" <<'SV'
module load(input clk, rst, input [1:0] addr, input wen, input [7:0] din,
            output [7:0] data, binary);
  reg [7:0] mem[0:3], bits[0:3];
  initial begin
    $readmemh("../image.hex", mem);
    $readmemb("../image.bin", bits);
  end
  always @(posedge clk) if(wen) mem[addr] <= din;
  assign data=mem[addr];
  assign binary=bits[addr];
endmodule
SV
"$LHD" compile "$W/slang.sv" --reader slang --top load --workdir "$W/sv-compile" --emit-dir lg:"$W/sv-lg" --emit diagnostics:"$W/slang.jsonl" --result-json "$W/slang.json"
if grep -q 'initial-ignored' "$W/slang.jsonl"; then echo 'Slang ignored file preload' >&2; exit 1; fi
{ printf 'const load = import("lg:load")\n'; sed -n '/^test preload/,$p' "$W/load.prp"; } > "$W/sv-tb.prp"
"$LHD" sim "$W/sv-tb.prp" "lg:$W/sv-lg" --workdir "$W/sv-run" --set sim.init_zero=true --set sim.unknown_zero=true --result-json "$W/sv-sim.json"
# Unsupported placement/bounds fail explicitly instead of ignoring the load.
for shape in bounds conditional mixed; do
  if [ "$shape" = bounds ]; then
    sed 's/"..\/image.hex", mem/"..\/image.hex", mem, 1/' "$W/slang.sv" > "$W/bad.sv"
  elif [ "$shape" = conditional ]; then
    sed 's/initial begin/initial if(wen) begin/' "$W/slang.sv" > "$W/bad.sv"
  else
    sed "s/initial begin/initial begin mem = '{default:0};/" "$W/slang.sv" > "$W/bad.sv"
  fi
  if "$LHD" compile "$W/bad.sv" --reader slang --workdir "$W/bad-$shape" --emit diagnostics:"$W/bad.jsonl" > "$W/bad.log" 2>&1; then
    echo "accepted unsupported readmem $shape" >&2; exit 1
  fi
  grep -q 'readmem-' "$W/bad.jsonl"
done
# Even an all-zero image and no RTL writer must remain externally loadable.
printf '00 00 00 00\n' > "$W/zero.hex"
printf '01 02 03 04\n' > "$W/other.hex"
cat > "$W/state.prp" <<'PRP'
pub mod state(clk:Clock, addr:U2) -> (data:U8@[]) {
  reg mem:[4]U8 = std.readmemh("zero.hex")
  data = mem[addr]
}
PRP
sed 's/zero.hex/other.hex/' "$W/state.prp" > "$W/other.prp"
cat > "$W/constant.prp" <<'PRP'
pub mod state(clk:Clock, addr:U2) -> (data:U8@[]) { data = 0 }
PRP
"$LHD" lec --ref "$W/state.prp" --impl "$W/other.prp" --top state --workdir "$W/equal" --result-json "$W/equal.json" > "$W/equal.log" 2>&1
if "$LHD" lec --ref "$W/state.prp" --impl "$W/constant.prp" --top state --workdir "$W/unequal" --result-json "$W/unequal.json" > "$W/unequal.log" 2>&1; then
  echo 'external memory was treated as constant zero' >&2; exit 1
fi
python3 - "$W/equal.json" "$W/unequal.json" <<'PYTHON'
import json,sys
assert json.load(open(sys.argv[1]))['lec']['verdict']=='proven'
assert json.load(open(sys.argv[2]))['lec']['verdict']=='refuted'
PYTHON
"$LHD" synth "$W/state.prp" --top state --set synth.liberty="$LIB" --set synth.opentimer=false --workdir "$W/synth" --emit verilog:"$W/mapped.v" --result-json "$W/synth.json" > "$W/synth.log" 2>&1
grep -q 'blackbox, keep' "$W/mapped.v"
grep -q '\.INIT_FILE(' "$W/mapped.v"
# Optional independent simulation of the emitted Verilog.
if [ -n "${LHD_EXTERNAL_SIM:-}" ]; then
  command -v verilator >/dev/null || { echo 'requested verilator missing' >&2; exit 1; }
  cat > "$W/oracle.cpp" <<'CPP'
#include "Vload.h"
#include <cassert>
int main() {
  Vload d;
  d.clk=0; d.rst=0; d.wen=0; d.addr=0; d.din=0; d.eval();
  assert(d.data == 0x12 && d.binary == 0x12);
  d.addr=2; d.eval(); assert(d.data == 0xab && d.binary == 0xab);
  d.wen=1; d.din=0x42; d.clk=1; d.eval();
  d.clk=0; d.wen=0; d.rst=1; d.eval(); d.clk=1; d.eval();
  assert(d.data == 0x42);
  d.final();
}
CPP
  verilator --cc --exe --build --top-module load --Mdir "$W/obj" "$W/load.v" "$W/oracle.cpp" > "$W/verilator.log" 2>&1
  "$W/obj/Vload" > "$W/verilator-run.log"
fi
echo 'PASS readmem smoke'
