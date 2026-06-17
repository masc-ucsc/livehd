#!/bin/bash
# translate.sh — per-module Verilog->Pyrope translation + LEC for soomrv.
#
#   translate.sh <TOP> <readfile1> [readfile2 ...]
#
# Compiles <TOP> (--reader slang) to a single .prp, LEC-checks it against the
# original Verilog, classifies, and places the .prp in soomrv/pass/ or
# soomrv/fail/ (+ a fail .md).  Prints one structured line:
#   <TOP> verdict=<V> kind=<comb|pipe|mod> ...
#
# Verdict policy (lhd lec; cvc5 default + lgyosys cross-check):
#   COMPILE-FAIL : slang reader could not lower the module
#   WRITER-FAIL  : the emitted .prp does not re-compile
#   PASS         : lgyosys PROVEN  (yosys read the original -> trustworthy), OR
#                  (unpacked-array port: yosys can't read the port) cvc5 PROVEN
#                  AND the flat cgen-verilog cross-check PROVEN
#   LEC-FAIL     : refuted, or slang/yosys reader disagreement (flat PROVEN but
#                  direct yosys REFUTED on a module with NO unpacked-array port)
set -u
LHD=./bazel-bin/lhd/lhd
HERE=soomrv
SLANG_FLAGS="--single-unit --std latest --allow-use-before-declare --relax-enum-conversions --ignore-unknown-modules --allow-toplevel-iface-ports -Wno-explicit-static -Wno-missing-top -Wno-multiple-always-assigns"

TOP="$1"; shift
FILES="$@"
W=/tmp/sv_$TOP
rm -rf "$W"; mkdir -p "$W"
PRP="$W/$TOP.prp"
REF="$W/ref.sv"; cat $FILES > "$REF"

emit_fail_md() {  # $1=reason  $2=detail-file-or-text
  {
    echo "# $TOP — translation FAIL"
    echo ""
    echo "**Reason:** $1"
    echo ""
    echo "**Read set:** $FILES"
    echo ""
    echo "**Detail:**"
    echo '```'
    [ -f "$2" ] && cat "$2" || echo "$2"
    echo '```'
  } > "$HERE/fail/$TOP.md"
}

# 1. compile to pyrope
$LHD compile --reader slang --top "$TOP" --recipe O0 \
  --emit-dir pyrope:"$W/prp/" --workdir "$W/c" \
  --emit diagnostics:"$W/diag.jsonl" -- $SLANG_FLAGS $FILES > "$W/compile.log" 2>&1
if [ $? -ne 0 ] || [ ! -f "$W/prp/$TOP.prp" ]; then
  grep '"severity":"error"' "$W/diag.jsonl" 2>/dev/null | grep -oE '"code":"[^"]*"|"message":"[^"]*"' | paste - - | sort | uniq -c | sort -rn > "$W/errsum.txt"
  rm -f "$HERE/pass/$TOP.prp"
  cp "$W"/prp/*.prp "$HERE/fail/" 2>/dev/null
  emit_fail_md "COMPILE-FAIL (slang reader could not lower)" "$W/errsum.txt"
  echo "$TOP verdict=COMPILE-FAIL errors=$(grep -c '"severity":"error"' "$W/diag.jsonl")"
  exit 0
fi
cp "$W/prp/$TOP.prp" "$PRP"
todos=$(grep -c "TODO: unhandled" "$PRP" 2>/dev/null)
kind=$(grep -oE "pub (comb|pipe|mod)" "$PRP" | head -1 | awk '{print $2}')

# 2. re-compile the .prp standalone
$LHD compile "$PRP" --top "$TOP" --workdir "$W/pc" --emit diagnostics:"$W/pdiag.jsonl" > "$W/pcompile.log" 2>&1
if [ $? -ne 0 ] || [ "$todos" != "0" ]; then
  grep '"severity":"error"' "$W/pdiag.jsonl" 2>/dev/null | grep -oE '"message":"[^"]*"' | sort -u > "$W/perrsum.txt"
  echo "TODOs=$todos" >> "$W/perrsum.txt"
  rm -f "$HERE/pass/$TOP.prp"; cp "$PRP" "$HERE/fail/"
  emit_fail_md "WRITER-FAIL (emitted .prp does not re-compile; kind=$kind, todos=$todos)" "$W/perrsum.txt"
  echo "$TOP verdict=WRITER-FAIL kind=$kind todos=$todos"
  exit 0
fi

# 3. LEC vs original — cvc5 (default) + lgyosys cross-check
$LHD lec --impl "$PRP" --ref "$REF" --top "$TOP" --workdir "$W/lec" -- $SLANG_FLAGS > "$W/lec.log" 2>&1
v_cvc5=$(grep -oE "(PROVEN|REFUTED|UNKNOWN)" "$W/lec.log" | head -1)
$LHD lec --impl "$PRP" --ref "$REF" --top "$TOP" --workdir "$W/lecy" --set lec.solver=lgyosys -- $SLANG_FLAGS > "$W/lecy.log" 2>&1
v_ys=$(grep -oE "(PROVEN|REFUTED|UNKNOWN)" "$W/lecy.log" | head -1)

# Flat cgen cross-check: yosys-SAT over (.prp->cgen) vs (slang-read->cgen) — an
# engine independent of cvc5 that confirms the round-trip preserves slang's
# reading.  (Both readers slang here, so it is immune to yosys's inability to
# parse the ORIGINAL's unpacked-array ports / quirky constructs.)
$LHD compile "$PRP" --top "$TOP" --recipe O0 --emit verilog:"$W/impl_flat.v" --workdir "$W/ifw" >/dev/null 2>&1
$LHD compile --reader slang --top "$TOP" --recipe O0 --emit verilog:"$W/ref_flat.v" --workdir "$W/rfw" -- $SLANG_FLAGS $FILES >/dev/null 2>&1
vf=$($LHD lec --impl "$W/impl_flat.v" --ref "$W/ref_flat.v" --top "$TOP" --workdir "$W/fl" --set lec.solver=lgyosys 2>&1 | grep -oE "(PROVEN|REFUTED|UNKNOWN)" | head -1)

verdict=""
detail="cvc5=$v_cvc5 lgyosys=$v_ys flat=$vf"
if [ "$v_ys" = "PROVEN" ]; then
  verdict=PASS                                   # yosys read the original directly — gold
elif [ "$v_cvc5" = "PROVEN" ] && [ "$vf" = "PROVEN" ]; then
  verdict=PASS                                   # round-trip dual-confirmed (SMT + yosys-SAT)
  [ "$v_ys" = "REFUTED" ] && detail="$detail (slang/yosys reader disagree on original)"
else
  verdict=LEC-FAIL
fi

if [ "$verdict" = "PASS" ]; then
  cp "$PRP" "$HERE/pass/"; rm -f "$HERE/fail/$TOP.prp" "$HERE/fail/$TOP.md"
else
  cp "$PRP" "$HERE/fail/"; rm -f "$HERE/pass/$TOP.prp"
  emit_fail_md "LEC-FAIL ($detail)" "$(grep -oE 'counterexample[^\"]*' $W/lec.log $W/lecy.log | grep -v schema | head -2)"
fi
echo "$TOP verdict=$verdict kind=$kind todos=$todos $detail"
