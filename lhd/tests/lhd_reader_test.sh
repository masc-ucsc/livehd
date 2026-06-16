#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# --reader yosys-verilog|yosys-slang|slang contract:
#  - yosys-verilog / yosys-slang elaborate to LGraphs via yosys (lg:/verilog:)
#  - slang is the direct inou.slang SV -> LNAST front-end: ln:/pyrope: emits
#    work, lg:/verilog: are locked `unsupported` until inou.slang catches up
#    with the current io/typesystem LNAST conventions
#  - the old yosys|slang two-value spelling is rejected

set -u

LHD=lhd/lhd
SV=lhd/tests/trivial.v
W="${TEST_TMPDIR:-/tmp/lhd_reader_$$}"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

# slang reader: SV -> LNAST -> ln: (Forest dir)
"$LHD" compile "$SV" --reader slang --emit-dir ln:"$W/lns/" --workdir "$W/w1" -q --result-json "$W/r1.json" \
  || fail "slang reader ln: emit exited non-zero: $(cat "$W/r1.json" 2>/dev/null)"
[ -f "$W/lns/forest.txt" ] || fail "slang reader produced no forest.txt"
grep -q '"inou.slang' "$W/r1.json" || fail "expected an inou.slang step: $(cat "$W/r1.json")"

# slang reader: pyrope: re-emission (post-upass prp_writer)
"$LHD" compile "$SV" --reader slang --emit-dir pyrope:"$W/prps/" --workdir "$W/w2" -q --result-json "$W/r2.json" \
  || fail "slang reader pyrope: emit exited non-zero: $(cat "$W/r2.json" 2>/dev/null)"
ls "$W/prps/"*.prp >/dev/null 2>&1 || fail "slang reader produced no .prp re-emission"

# slang reader: verilog: emit goes through the same upass+tolg pipeline as
# pyrope sources (todo/ 2s lifted the old "stops at LNAST" gate)
"$LHD" compile "$SV" --reader slang --emit verilog:"$W/x.v" --workdir "$W/w3" -q --result-json "$W/r3.json" 2>/dev/null \
  || fail "slang reader verilog: emit exited non-zero: $(cat "$W/r3.json" 2>/dev/null)"
[ -s "$W/x.v" ] || fail "slang reader produced no verilog"

# yosys-verilog reader: the plain yosys verilog frontend, end-to-end
"$LHD" compile "$SV" --reader yosys-verilog --emit verilog:"$W/yv.v" --workdir "$W/w4" -q --result-json "$W/r4.json" \
  || fail "yosys-verilog reader exited non-zero: $(cat "$W/r4.json" 2>/dev/null)"
[ -s "$W/yv.v" ] || fail "yosys-verilog reader produced no verilog"

# the old two-value spelling is rejected with a usage error (argv-stage
# errors write the result JSON to stdout, --result-json is not parsed yet)
out=$("$LHD" compile "$SV" --reader yosys --emit-dir lg:"$W/lg/" -q 2>/dev/null)
[ $? -ne 0 ] || fail "--reader yosys (old spelling) must be a usage error"
grep -q '"class":"usage"' <<<"$out" || fail "expected error.class=usage: $out"

echo "PASS: reader trichotomy (yosys-verilog | yosys-slang | slang) behaves per contract"
