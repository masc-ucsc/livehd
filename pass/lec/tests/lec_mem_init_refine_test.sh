#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# One-sided memory power-on contents follow the flop convention (owner ruling
# 2026-10-16):
#   * a REFERENCE memory with power-on contents against an implementation
#     memory with none is REFUTED-cex: the implementation's power-on contents
#     are arbitrary. Both engines used to tie the implementation's array to the
#     reference's contents (BMC pairing, induction's shared array) and answered
#     PROVEN-full -- Verilog `initial mem[i] = 8'h5a` and Pyrope
#     `[initial=...] = nil` alike.
#   * the opposite direction, an uninitialized reference against an initialized
#     implementation, is a refinement (the reference's X allows any value) and
#     stays PROVEN.
set -euo pipefail
LHD="${LHD:-./bazel-bin/lhd/lhd}"
if [ ! -x "$LHD" ]; then LHD=./lhd/lhd; fi
WORK="${TEST_TMPDIR:-$(mktemp -d)}"

cat > "$WORK/vinit.v" <<'V'
module top(input clock, input we, input [1:0] wa, input [7:0] d, input [1:0] ra, output [7:0] q);
  reg [7:0] mem [0:3];
  initial begin
    mem[0] = 8'h5a; mem[1] = 8'h5a; mem[2] = 8'h5a; mem[3] = 8'h5a;
  end
  always @(posedge clock) if (we) mem[wa] <= d;
  assign q = mem[ra];
endmodule
V
sed '/initial begin/,/^  end/d' "$WORK/vinit.v" > "$WORK/vnone.v"
if grep -q initial "$WORK/vnone.v"; then echo "FAIL: fixture still initializes"; exit 1; fi

mkdir -p "$WORK/pinit" "$WORK/pnone"
cat > "$WORK/pinit/top.prp" <<'P'
pub mod top(we:Bool, wa:U2, d:U8, ra:U2) -> (q:U8@[0]) {
  reg mem:[4]U8:[initial=0x04030201, ordering="old"] = nil
  q = mem[ra]
  if we { mem[wa] = d }
}
P
sed 's/initial=0x04030201, //' "$WORK/pinit/top.prp" > "$WORK/pnone/top.prp"
grep -q 'ordering="old"] = nil' "$WORK/pnone/top.prp"

# check TAG REF IMPL EXPECTED [lec args...]; EXPECTED is a verdict, or
# "not-proven" (any verdict that is not PROVEN-*).
check() {
  local tag=$1 ref=$2 impl=$3 expected=$4 code=0
  shift 4
  "$LHD" lec --set formal.effort=proof --ref "$ref" --impl "$impl" --top top --set formal.timeout=20 \
    --set formal.simfail=false "$@" --workdir "$WORK/$tag" --result-json "$WORK/$tag.json" > "$WORK/$tag.log" 2>&1 || code=$?
  case "$expected" in proven-*) test "$code" = 0 ;; refuted-cex) test "$code" = 10 ;; not-proven) true ;; *) false ;; esac || {
    echo "FAIL: $tag: exit $code (expected $expected)"; grep "^lec: " "$WORK/$tag.log" | tail -3; exit 1; }
  python3 - "$WORK/$tag.json" "$expected" <<'PYRESULT' || { echo "FAIL: $tag"; grep "^lec: " "$WORK/$tag.log" | tail -3; exit 1; }
import json, sys
verdict = json.load(open(sys.argv[1]))["lec"]["verdict"]
want = sys.argv[2]
ok = not verdict.startswith("proven") if want == "not-proven" else verdict == want
assert ok, verdict
PYRESULT
  echo "ok: $tag -> $expected"
}
# Initialized reference, uninitialized implementation: refutes (auto races
# both engines, so neither may answer PROVEN first; induction alone must not
# prove either).
check v_init_none "verilog:$WORK/vinit.v" "verilog:$WORK/vnone.v" refuted-cex
check p_init_none "$WORK/pinit/top.prp" "$WORK/pnone/top.prp" refuted-cex
check v_init_none_ind "verilog:$WORK/vinit.v" "verilog:$WORK/vnone.v" not-proven --set formal.engine=ind
# Uninitialized reference, initialized implementation: a refinement.
check v_none_init "verilog:$WORK/vnone.v" "verilog:$WORK/vinit.v" proven-full
check p_none_init "$WORK/pnone/top.prp" "$WORK/pinit/top.prp" proven-full
echo "lec_mem_init_refine_test: PASSED"
