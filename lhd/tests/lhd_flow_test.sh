#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# Flow-matrix test for the lhd kernel (docs/contracts/future_cli.md):
#   $1 = input kind   (pyrope | verilog | ln | lg)
#   $2 = output kind  (pyrope | verilog | ln | lg)
#   $3 = expectation  (pass | unsupported)
#
# Uses the inou/prp/tests/equiv trivial_if pyrope/verilog golden pair. ln:
# (Forest dir) and lg: (GraphLibrary dir) inputs are first materialized from
# the pyrope source. ln:/lg:/pyrope: outputs are directory containers
# (--emit-dir only); verilog --emit is the one single-file netlist form.

set -u

IN_KIND="$1"
OUT_KIND="$2"
EXPECT="$3"

LHD=lhd/lhd
PRP=inou/prp/tests/equiv/trivial_if.prp
V0=inou/prp/tests/equiv/trivial_if.v
TOP='trivial_if.fun3'
W="${TEST_TMPDIR:-/tmp/lhd_flow_$$}"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

prep_lhd() { "$LHD" "$@" --workdir "$W/prep" -q --result-json "$W/prep.json" || fail "input prep: $* -> $(cat "$W/prep.json" 2>/dev/null)"; }

# --- materialize the input artifact for the IR input kinds ------------------
IN_LN="$W/in_lns"
IN_LG="$W/in_lgs"
case "$IN_KIND" in
  ln)
    prep_lhd elaborate "$PRP" --emit-dir ln:"$IN_LN/"
    [ -f "$IN_LN/forest.txt" ] || fail "input prep: missing $IN_LN/forest.txt"
    ;;
  lg)
    prep_lhd elaborate "$PRP" --emit-dir lg:"$IN_LG/"
    [ -f "$IN_LG/library.txt" ] || fail "input prep: missing $IN_LG/library.txt"
    ;;
esac

OUT_V="$W/out.v"
OUT_PRP="$W/out_prp"
OUT_LN="$W/out_lns"
OUT_LG="$W/out_lgs"

cmd=()
case "$IN_KIND-$OUT_KIND" in
  pyrope-verilog) cmd=(compile "$PRP" --emit verilog:"$OUT_V") ;;
  pyrope-pyrope) cmd=(compile "$PRP" --emit-dir pyrope:"$OUT_PRP/") ;;
  pyrope-ln) cmd=(elaborate "$PRP" --emit-dir ln:"$OUT_LN/") ;;
  pyrope-lg) cmd=(elaborate "$PRP" --emit-dir lg:"$OUT_LG/") ;;
  verilog-verilog) cmd=(compile "$V0" --reader yosys-verilog --top "$TOP" --emit verilog:"$OUT_V") ;;
  verilog-pyrope) cmd=(compile "$V0" --reader yosys-verilog --top "$TOP" --emit-dir pyrope:"$OUT_PRP/") ;;
  verilog-ln) cmd=(elaborate "$V0" --reader yosys-verilog --top "$TOP" --emit-dir ln:"$OUT_LN/") ;;
  verilog-lg) cmd=(elaborate "$V0" --reader yosys-verilog --top "$TOP" --emit-dir lg:"$OUT_LG/") ;;
  ln-verilog) cmd=(synth ln:"$IN_LN" --emit verilog:"$OUT_V") ;;
  ln-pyrope) cmd=(synth ln:"$IN_LN" --emit-dir pyrope:"$OUT_PRP/") ;;
  ln-ln) cmd=(synth ln:"$IN_LN" --emit-dir ln:"$OUT_LN/") ;;  # post-upass forest
  ln-lg) cmd=(synth ln:"$IN_LN" --emit-dir lg:"$OUT_LG/") ;;
  lg-verilog) cmd=(synth lg:"$IN_LG" --emit verilog:"$OUT_V") ;;
  lg-pyrope) cmd=(synth lg:"$IN_LG" --emit-dir pyrope:"$OUT_PRP/") ;;
  lg-ln) cmd=(synth lg:"$IN_LG" --emit-dir ln:"$OUT_LN/") ;;
  lg-lg) cmd=(synth lg:"$IN_LG" --emit-dir lg:"$OUT_LG/") ;;
  *) fail "unknown flow $IN_KIND -> $OUT_KIND" ;;
esac

"$LHD" "${cmd[@]}" --workdir "$W/work" -q --result-json "$W/r.json"
rc=$?

if [ "$EXPECT" = "unsupported" ]; then
  [ $rc -ne 0 ] || fail "expected an unsupported error, got exit 0"
  grep -q '"class":"unsupported"' "$W/r.json" || fail "expected error.class=unsupported, got: $(cat "$W/r.json")"
  echo "PASS: $IN_KIND -> $OUT_KIND is rejected as unsupported (documented v0 contract)"
  exit 0
fi

[ $rc -eq 0 ] || fail "flow exited $rc: $(cat "$W/r.json" 2>/dev/null)"
grep -q '"status":"pass"' "$W/r.json" || fail "result status is not pass: $(cat "$W/r.json")"

case "$OUT_KIND" in
  verilog)
    [ -s "$OUT_V" ] || fail "missing/empty $OUT_V"
    grep -q "^module" "$OUT_V" || fail "no module in $OUT_V"
    ;;
  pyrope)
    [ -f "$OUT_PRP/manifest.json" ] || fail "missing $OUT_PRP/manifest.json"
    ls "$OUT_PRP"/*.prp >/dev/null 2>&1 || fail "no .prp units in $OUT_PRP"
    ;;
  ln)
    [ -f "$OUT_LN/forest.txt" ] || fail "missing $OUT_LN/forest.txt (hhds Forest save)"
    [ -f "$OUT_LN/manifest.json" ] || fail "missing $OUT_LN/manifest.json"
    ;;
  lg)
    [ -f "$OUT_LG/library.txt" ] || fail "missing $OUT_LG/library.txt (hhds GraphLibrary save)"
    ;;
esac

echo "PASS: $IN_KIND -> $OUT_KIND"
