#!/bin/bash
# translate.sh — per-module Verilog->Pyrope translation + LEC for soomrv.
#
#   translate.sh <TOP> <readfile1> [readfile2 ...]
#
# Compiles <TOP> (--reader slang) to a single .prp, LEC-checks it against the
# original, classifies, places the .prp in soomrv/pass/ or soomrv/fail/ (+ .md),
# and prints one structured line.
#
# Both LEC sides are pre-compiled to lg: with the slang flags applied (passing a
# raw multi-file .sv to `lhd lec` does NOT forward the flags, so use-before-
# declare enums etc. break the ref read); lg-vs-lg LEC with cvc5 + lgyosys then
# confirms the round-trip (.prp -> lg  ==  slang(original) -> lg) by two
# independent engines (SMT + yosys-SAT).
#
# Verdict: LEC-FAIL if either engine REFUTES; PASS if either PROVES and neither
# refutes (cvc5 UNKNOWN on memory modules is covered by lgyosys PROVEN, and vice
# versa); else LEC-FAIL (undecided).  COMPILE-FAIL (reader) / WRITER-FAIL (.prp
# does not re-compile) short-circuit earlier.
set -u
LHD=./bazel-bin/lhd/lhd
HERE=soomrv
SLANG_FLAGS="--single-unit --std latest --allow-use-before-declare --relax-enum-conversions --ignore-unknown-modules --allow-toplevel-iface-ports -Wno-explicit-static -Wno-missing-top -Wno-multiple-always-assigns"

TOP="$1"; shift
FILES="$@"
# Inject (* blackbox *) stubs for TOP's instantiated submodules (deps.txt), so the
# per-file compile keeps them as Sub instances (LEC blackbox-collapses both sides).
STUBS=""
if [ -f "$HERE/stubs/deps.txt" ]; then
  for d in $(grep -E "^$TOP:" "$HERE/stubs/deps.txt" | cut -d: -f2-); do
    [ -f "$HERE/stubs/$d.stub.sv" ] && STUBS="$STUBS $HERE/stubs/$d.stub.sv"
  done
fi
FILES="$FILES $STUBS"   # stubs AFTER packages (their port types come from Include.sv)
W=/tmp/sv_$TOP
rm -rf "$W"; mkdir -p "$W"
PRP="$W/$TOP.prp"

emit_fail_md() {  # $1=reason  $2=detail-file-or-text
  {
    echo "# $TOP — translation FAIL"; echo
    echo "**Reason:** $1"; echo
    echo "**Read set:** $FILES"; echo
    echo "**Detail:**"; echo '```'
    [ -f "$2" ] && cat "$2" || echo "$2"
    echo '```'
  } > "$HERE/fail/$TOP.md"
}
place_fail() { rm -f "$HERE/pass/$TOP.prp"; [ -f "$PRP" ] && cp "$PRP" "$HERE/fail/"; }

# 1. compile to pyrope
$LHD compile --reader slang --top "$TOP" --recipe O0 --emit-dir pyrope:"$W/prp/" \
  --workdir "$W/c" --emit diagnostics:"$W/diag.jsonl" -- $SLANG_FLAGS $FILES > "$W/compile.log" 2>&1
if [ $? -ne 0 ] || [ ! -f "$W/prp/$TOP.prp" ]; then
  grep '"severity":"error"' "$W/diag.jsonl" 2>/dev/null | grep -oE '"code":"[^"]*"|"message":"[^"]*"' | paste - - | sort | uniq -c | sort -rn > "$W/errsum.txt"
  place_fail; emit_fail_md "COMPILE-FAIL (slang reader could not lower)" "$W/errsum.txt"
  echo "$TOP verdict=COMPILE-FAIL errors=$(grep -c '"severity":"error"' "$W/diag.jsonl")"; exit 0
fi
cp "$W/prp/$TOP.prp" "$PRP"
todos=$(grep -c "TODO: unhandled" "$PRP" 2>/dev/null)
kind=$(grep -oE "pub (comb|pipe|mod)" "$PRP" | head -1 | awk '{print $2}')

# 2. re-compile the .prp -> lg (writer-fidelity gate + impl lg)
$LHD compile "$PRP" --top "$TOP" --recipe O0 --emit-dir lg:"$W/impllg/" \
  --workdir "$W/pc" --emit diagnostics:"$W/pdiag.jsonl" > "$W/pcompile.log" 2>&1
if [ $? -ne 0 ] || [ "$todos" != "0" ]; then
  grep '"severity":"error"' "$W/pdiag.jsonl" 2>/dev/null | grep -oE '"message":"[^"]*"' | sort -u > "$W/perrsum.txt"
  echo "TODOs=$todos" >> "$W/perrsum.txt"
  place_fail; emit_fail_md "WRITER-FAIL (.prp does not re-compile; kind=$kind, todos=$todos)" "$W/perrsum.txt"
  echo "$TOP verdict=WRITER-FAIL kind=$kind todos=$todos"; exit 0
fi

# 3. ref (original) -> lg with the slang flags
$LHD compile --reader slang --top "$TOP" --recipe O0 --emit-dir lg:"$W/reflg/" \
  --workdir "$W/rc" -- $SLANG_FLAGS $FILES > "$W/refcompile.log" 2>&1

# 4. lg-vs-lg LEC.  lgyosys (primary verdict) first with a generous timeout;
# cvc5 (secondary signal) capped short — its array theory can hang on a wide/deep
# memory, which would otherwise eat the whole budget.
timeout 200 $LHD lec --impl lg:"$W/impllg/" --ref lg:"$W/reflg/" --top "$TOP" --workdir "$W/lecy" --set lec.solver=lgyosys > "$W/lecy.log" 2>&1
v_ys=$(grep -oE "(PROVEN|REFUTED|UNKNOWN)" "$W/lecy.log" | head -1)
timeout 90 $LHD lec --impl lg:"$W/impllg/" --ref lg:"$W/reflg/" --top "$TOP" --workdir "$W/lec" > "$W/lec.log" 2>&1
v_cvc5=$(grep -oE "(PROVEN|REFUTED|UNKNOWN)" "$W/lec.log" | head -1)

# lgyosys (yosys SAT) is the PRIMARY verdict: on lg-vs-lg it has no original-
# reading blind spots, and it models `'x` as a true don't-care (the in-process
# cvc5 encoder treats `'x` as a concrete value and can false-REFUTE on designs
# that assign `'x`, e.g. `curTVal.sqN <= 'x`).  cvc5 is the fallback when lgyosys
# cannot decide (e.g. it times out), and is reported either way.
detail="cvc5=${v_cvc5:-NONE} lgyosys=${v_ys:-NONE}"
if [ "$v_ys" = "PROVEN" ]; then
  verdict=PASS
  [ "$v_cvc5" = "REFUTED" ] && detail="$detail (cvc5 'x/don't-care artifact)"
elif [ "$v_ys" = "REFUTED" ]; then
  verdict=LEC-FAIL
elif [ "$v_cvc5" = "PROVEN" ]; then
  verdict=PASS
else
  verdict=LEC-FAIL
fi

if [ "$verdict" = "PASS" ]; then
  cp "$PRP" "$HERE/pass/"; rm -f "$HERE/fail/$TOP.prp" "$HERE/fail/$TOP.md"
else
  place_fail
  emit_fail_md "LEC-FAIL ($detail)" "$(grep -oE 'counterexample[^\"]*' $W/lec.log $W/lecy.log | grep -v schema | head -2)"
fi
echo "$TOP verdict=$verdict kind=$kind todos=$todos $detail"
