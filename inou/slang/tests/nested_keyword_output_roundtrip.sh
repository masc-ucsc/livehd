#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

set -euo pipefail

LHD=lhd/lhd
SRC=inou/slang/tests/sv/nested_keyword_output.v
TOP=nested_keyword_output
W="${TEST_TMPDIR:-/tmp/nested_keyword_output_$$}"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  [ -f "$W/prp/$TOP.prp" ] && sed -n '1,120p' "$W/prp/$TOP.prp" >&2
  [ -f "$W/lec/logs/001_lhd_lec_lgcheck.log" ] && tail -80 "$W/lec/logs/001_lhd_lec_lgcheck.log" >&2
  exit 1
}

"$LHD" compile "$SRC" --reader slang --top "$TOP" \
  --emit-dir pyrope:"$W/prp" --workdir "$W/read" -q >/dev/null 2>&1 \
  || fail "Verilog-to-Pyrope emission failed"

PRP="$W/prp/$TOP.prp"
[ -s "$PRP" ] || fail "Pyrope unit was not emitted"
grep -q 'io_out.toS1.`sat` = ' "$PRP" \
  || fail "nested keyword field was not emitted as a field assignment"
if grep -q 'const io_out.toS1.`sat`' "$PRP"; then
  fail "nested keyword field was emitted as an illegal tuple-path declaration"
fi

"$LHD" compile "$PRP" --top "$TOP" --emit verilog:"$W/out.v" \
  --workdir "$W/recompile" -q >/dev/null 2>&1 \
  || fail "emitted Pyrope did not recompile"

LGCHECK_EQUIV_TIMEOUT=60 "$LHD" lec \
  --impl verilog:"$W/out.v" --ref verilog:"$SRC" --top "$TOP" \
  --set formal.solver=lgyosys \
  --set formal.lec.gold_reader=slang --set formal.lec.gate_reader=slang \
  --set formal.lec.normalize_split_ports=true \
  --workdir "$W/lec" -q >/dev/null 2>&1 \
  || fail "round-tripped Verilog is not equivalent to the source"

echo "PASS: nested keyword output remains a field assignment through the Pyrope round trip"
