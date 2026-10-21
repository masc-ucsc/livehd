#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# A `$readmem` / std.readmemh image is externally loadable power-on state:
# DEFINED contents that are never compared file to file (two images prove
# equal) and never folded to constants (lhd/tests/readmem_smoke.sh). So a
# REFERENCE image pairs only with an implementation image. An implementation
# memory without one -- uninitialized, power-on initialized or reset -- does
# not load the reference's contents and refutes (owner ruling 2026-10-16: a
# defined reference against an uninitialized implementation is REFUTED-cex);
# an uninitialized reference against an implementation image is a refinement.
#
# Before, a reference image WITH a write port was modelled as an X memory: BMC
# tied it to a constant-initialized implementation and both engines shared its
# array with an uninitialized one, so all three came back PROVEN-full -- while
# the same pairs without a write port refuted.
set -euo pipefail
LHD="${LHD:-./bazel-bin/lhd/lhd}"
if [ ! -x "$LHD" ]; then LHD=./lhd/lhd; fi
WORK="${TEST_TMPDIR:-$(mktemp -d)}"
LHD="$(cd "$(dirname "$LHD")" && pwd)/$(basename "$LHD")"

printf '00 00 00 00\n' > "$WORK/zero.hex"
printf '01 02 03 04\n' > "$WORK/other.hex"
# wmem DIR INIT: a written memory; rom DIR INIT: no write port.
wmem() {
  mkdir -p "$WORK/$1"
  cat > "$WORK/$1/top.prp" <<P
pub mod top(addr:U2, wen:Bool, din:U8) -> (data:U8@[]) {
  reg mem:[4]U8:[$2ordering="old"] = $3
  if wen { mem[addr] = din }
  data = mem[addr]
}
P
}
rom() {
  mkdir -p "$WORK/$1"
  cat > "$WORK/$1/top.prp" <<P
pub mod top(addr:U2) -> (data:U8@[]) {
  reg mem:[4]U8:[$2ordering="old"] = $3
  data = mem[addr]
}
P
}
wmem w_img "" 'std.readmemh("../zero.hex")'
wmem w_img2 "" 'std.readmemh("../other.hex")'
wmem w_init "initial=0, " nil
wmem w_nil "" nil
rom r_img "" 'std.readmemh("../zero.hex")'
rom r_nil "" nil
grep -q 'readmemh("../zero.hex")' "$WORK/w_img/top.prp"
grep -q 'initial=0, ordering' "$WORK/w_init/top.prp"

cat > "$WORK/v_img.v" <<'V'
module top(input clk, input [1:0] addr, input wen, input [7:0] din, output [7:0] data);
  reg [7:0] mem [0:3];
  initial $readmemh("zero.hex", mem);
  always @(posedge clk) if (wen) mem[addr] <= din;
  assign data = mem[addr];
endmodule
V
sed 's/initial $readmemh("zero.hex", mem);/initial begin mem[0] = 0; mem[1] = 0; mem[2] = 0; mem[3] = 0; end/' "$WORK/v_img.v" > "$WORK/v_init.v"
grep -q 'mem\[3\] = 0' "$WORK/v_init.v"

# check TAG REF IMPL EXPECTED [lec args...]; EXPECTED is a verdict, or
# "not-proven" (any verdict that is not PROVEN-*).
check() {
  local tag=$1 ref=$2 impl=$3 expected=$4 code=0
  shift 4
  (cd "$WORK" && "$LHD" lec --set formal.effort=proof --ref "$ref" --impl "$impl" --top top --set formal.timeout=20 \
    --set formal.simfail=false "$@" --workdir "$WORK/$tag" --result-json "$WORK/$tag.json") > "$WORK/$tag.log" 2>&1 || code=$?
  case "$expected" in proven-*) test "$code" = 0 ;; refuted-cex) test "$code" = 10 ;; not-proven) true ;; *) false ;; esac || {
    echo "FAIL: $tag: exit $code (expected $expected)"; grep "^lec: " "$WORK/$tag.log" | tail -3; exit 1; }
  python3 - "$WORK/$tag.json" "$expected" <<'PYRESULT' || { echo "FAIL: $tag"; grep "^lec: " "$WORK/$tag.log" | tail -3; exit 1; }
import json, sys
verdict = json.load(open(sys.argv[1]))["lec"]["verdict"]
want = sys.argv[2]
assert not verdict.startswith("proven") if want == "not-proven" else verdict == want, verdict
PYRESULT
  echo "ok: $tag -> $expected"
}
# Images are not compared file to file.
check w_img_img2 w_img/top.prp w_img2/top.prp proven-full
# A reference image is defined state: the implementation must load it too.
check w_img_init w_img/top.prp w_init/top.prp refuted-cex
check w_img_nil w_img/top.prp w_nil/top.prp refuted-cex
check w_img_nil_ind w_img/top.prp w_nil/top.prp not-proven --set formal.engine=ind
check w_img_init_ind w_img/top.prp w_init/top.prp not-proven --set formal.engine=ind
check r_img_nil r_img/top.prp r_nil/top.prp refuted-cex
check v_img_init verilog:v_img.v verilog:v_init.v refuted-cex
# An uninitialized reference allows the implementation's image.
check w_nil_img w_nil/top.prp w_img/top.prp proven-full
check r_nil_img r_nil/top.prp r_img/top.prp proven-full
echo "lec_mem_image_test: PASSED"
