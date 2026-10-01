#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
set -euo pipefail
LHD="${LHD:-lhd/lhd}"
W="${TEST_TMPDIR:-/tmp/livehd_slang_plusargs_$$}"
mkdir -p "$W"
cat > "$W/args.sv" <<'SV'
module child;
  if (1) begin : debug_scope
  int tag = 17;
  initial begin
    assert (tag == 17);
    if ($test$plusargs("bar")) $display("CHILD bar");
  end
  end
endmodule
module args(input [7:0] a, output [7:0] b);
  assign b = a;
  child c0();
  child c1();
  int number = 19;
  int status;
  string file = "default.hex";
  initial begin
    int h, binary, octal;
    logic [15:0] packed_text;
    logic signed [7:0] narrow;
    h = 0; binary = 0; octal = 0; packed_text = 0; narrow = 0;
    status = $value$plusargs("number=%d", number);
    if ($test$plusargs("number=")) assert (status == 1); else assert (number == 19 && status == 0);
    status = $value$plusargs("file=%s", file);
    status = $value$plusargs("hex=%h", h);
    status = $value$plusargs("binary=%b", binary);
    status = $value$plusargs("octal=%o", octal);
    status = $value$plusargs("packed=%s", packed_text);
    status = $value$plusargs("narrow=%d", narrow);
    $display("SV number=%0d file=%s h=%0d b=%0d o=%0d p=%0h n=%0d bar=%0d", number, file, h, binary, octal, packed_text, narrow, $test$plusargs("bar"));
    if ($test$plusargs("fail")) $fatal(1, "requested SV failure");
  end
endmodule
SV
"$LHD" compile "$W/args.sv" --reader slang --top args --workdir "$W/compile" --emit-dir lg:"$W/lg" --emit-dir ln:"$W/ln" --emit verilog:"$W/emitted.sv" > "$W/compile.log" 2>&1
for kind in lg ln; do
  cat > "$W/bench.prp" <<PRP
const dut = import("$kind:args")
test run(cycles:U8=3) {
  mut d = dut
  tick cycles { d.a = 42; step; assert(d.b == 42) }
}
PRP
  "$LHD" sim "$W/bench.prp" "$kind:$W/$kind" --workdir "$W/$kind-run" --set sim.ninja=false --set sim.tune.profile=off > "$W/$kind.log" 2>&1
  "$W/$kind-run/sim/drv.bin" > "$W/$kind.log" 2>&1
  grep -q 'SV number=19 file=default.hex h=0 b=0 o=0 p=0 n=0 bar=0' "$W/$kind.log"
done
DRV="$W/lg-run/sim/drv.bin"
ARGS=(+number=-7 +number=99 '+file=a=b path.hex' +hex=ff +binary=101 +octal=17 +packed=AB +narrow=255 +barrel)
"$DRV" "${ARGS[@]}" --result-json "$W/plus.json" > "$W/plus.log" 2>&1
grep -q 'SV number=-7 file=a=b path.hex h=255 b=5 o=15 p=4142 n=-1 bar=1' "$W/plus.log"
[ "$(grep -c '^CHILD bar' "$W/plus.log")" = 2 ]
"$DRV" +number +number=9 +file= > "$W/empty.log"
grep -q 'SV number=9 file= h=0 b=0 o=0 p=0 n=0 bar=0' "$W/empty.log"
if "$DRV" +fail > "$W/fail.log" 2>&1; then echo 'accepted fatal' >&2; exit 1; fi
grep -q 'requested SV failure' "$W/fail.log"
# A simulation-only edit must invalidate generated code in the same workdir.
sed 's/int number = 19/int number = 20/; s/number == 19/number == 20/' "$W/args.sv" > "$W/edit.sv"
cp "$W/edit.sv" "$W/args.sv"
"$LHD" compile "$W/args.sv" --reader slang --top args --workdir "$W/compile" --emit-dir lg:"$W/lg" --emit verilog:"$W/changed.sv" > "$W/warm.log" 2>&1
grep -q "32'sd20" "$W/changed.sv"
sed 's/ln:args/lg:args/' "$W/bench.prp" > "$W/bench-next.prp"
cp "$W/bench-next.prp" "$W/bench.prp"
"$LHD" sim "$W/bench.prp" "lg:$W/lg" --workdir "$W/lg-run" --set sim.ninja=false --set sim.tune.profile=off > "$W/edited-sim.log" 2>&1
"$DRV" > "$W/edited.log"
grep -q 'SV number=20 file=default.hex' "$W/edited.log"
# Argument-controlled data, control, and output aliases are hardware influence.
for statement in 'assign b = x;' 'assign b = x ? a : 0;' 'always_comb if (x) b = a; else b = 0;'; do
  cat > "$W/bad.sv" <<SV
module bad(input [7:0] a, output logic [7:0] b);
  int x;
  initial x = \$test\$plusargs("bar");
  $statement
endmodule
SV
  if "$LHD" compile "$W/bad.sv" --reader slang --workdir "$W/bad" > "$W/bad.log" 2>&1; then echo 'accepted hardware influence' >&2; exit 1; fi
  grep -q 'plusarg-hardware' "$W/bad.log"
done
cat > "$W/hierarchy.sv" <<'SV'
module child;
  int x;
  initial x = $test$plusargs("bar");
endmodule
module hierarchy(output [31:0] y);
  child c();
  assign y = c.x;
endmodule
SV
if "$LHD" compile "$W/hierarchy.sv" --reader slang --top hierarchy --workdir "$W/hierarchy" > "$W/hierarchy.log" 2>&1; then
  echo 'accepted debug state escaping through hierarchy' >&2; exit 1
fi
grep -q 'plusarg-hardware' "$W/hierarchy.log"
for body in 'int x = $test$plusargs("bar");' 'initial begin int x; #1 x = $test$plusargs("bar"); end'; do
  printf 'module bad(input a, output b); assign b=a; %s endmodule\n' "$body" > "$W/bad.sv"
  if "$LHD" compile "$W/bad.sv" --reader slang --workdir "$W/placement" > "$W/placement.log" 2>&1; then
    echo 'accepted unsupported plusarg placement' >&2; exit 1
  fi
  grep -q 'plusarg-' "$W/placement.log"
done
if "$LHD" compile "$W/args.sv" --reader slang --top args --emit-dir pyrope:"$W/pyrope" --workdir "$W/pyrope-run" > "$W/pyrope.log" 2>&1; then
  echo 'silently omitted SV initialization from Pyrope source' >&2; exit 1
fi
grep -q 'SV plusarg initial blocks' "$W/pyrope.log"
cat > "$W/plain.sv" <<'SV'
module args(input [7:0] a, output [7:0] b);
  assign b = a;
endmodule
SV
"$LHD" lec --impl "$W/args.sv" --ref "$W/plain.sv" --top args --workdir "$W/lec" --result-json "$W/lec.json" > "$W/lec.log" 2>&1
python3 - "$W/lec.json" "$W/plus.json" <<'PY'
import json, sys
assert json.load(open(sys.argv[1]))['lec']['verdict'] == 'proven'
result = json.load(open(sys.argv[2]))
rows = result['tests'] if isinstance(result, dict) else result
assert rows[0]['status'] == 'pass'
assert rows[0]['arguments'][:2] == ['+number=-7', '+number=99']
assert rows[0]['parameters']['cycles'] == '3'
PY
# Optional independent SV reference checks the original and emitted sources.
if [ -n "${LHD_EXTERNAL_SIM:-}" ]; then
  command -v verilator >/dev/null || { echo 'requested verilator missing' >&2; exit 1; }
  cat > "$W/oracle.cpp" <<'CPP'
#include "Vargs.h"
#include "verilated.h"
int main(int argc, char** argv) { Verilated::commandArgs(argc, argv); Vargs d; d.a=42; d.eval(); return d.b != 42; }
CPP
  for source in edit emitted; do
    verilator --cc --exe --build --assert --top-module args --Mdir "$W/obj-$source" "$W/$source.sv" "$W/oracle.cpp" > "$W/verilator-$source.log" 2>&1
    "$W/obj-$source/Vargs" "${ARGS[@]}" > "$W/oracle-$source.log"
    grep '^SV\|^CHILD' "$W/oracle-$source.log" | sort > "$W/oracle-$source.out"
    grep '^SV\|^CHILD' "$W/plus.log" | sort > "$W/livehd.out"
    diff -u "$W/oracle-$source.out" "$W/livehd.out"
  done
fi
echo 'PASS: Slang plusargs, formats, private initialization, hierarchy, saved IR, and hardware isolation'
