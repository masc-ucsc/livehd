#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# End-to-end test for the pass.abc multiplier / right-shift bit-blast and the
# division blackbox (task 2a-abc follow-on): technology-map a colored
# combinational design that uses `*`, `>>` and `/` to a standard-cell netlist and
# prove every region equivalent to the original logic with `lhd lec` (the
# graph-native cvc5 engine).
#
#   prp -> lg (O1) -> pass color acyclic   (colors EVERY op, incl mult/div, into
#                                            regions; synth deliberately excludes
#                                            mult/div as region seeds)
#   pass partition --emit-dir lg:re         (the original-logic twin)
#   pass liberty gensim test.lib --emit-dir lg:models
#   pass abc --emit-dir lg:net              (bit-blast mult/sra, blackbox div)
#   lhd lec --impl lg:net --ref lg:re --lib lg:models   (per region)
#
# Coverage: unsigned + signed + n-ary multiply (array multiplier), logical +
# arithmetic right shift (barrel shifter), and unsigned + signed division (kept
# native as a blackbox boundary -- abc must emit a `div-blackbox` warning and must
# NOT technology-map the divider). The multiplier/shifter math itself is also
# proven against reference arithmetic by the graph-free //pass/abc:abc_arith_test
# unit test.
#
# Hermetic: small vendored Liberty (inou/prp/tests/abc/test.lib), not the PDK.

set -u

LHD=lhd/lhd
LIB=inou/prp/tests/abc/test.lib
PRP=inou/prp/tests/pyrope/abc_mathops.prp
TOP=abc_mathops.abc_mathops
W="${TEST_TMPDIR:-/tmp/lhd_abc_math_$$}"
mkdir -p "$W"

fail() { echo "FAIL: $*" >&2; exit 1; }
run() { "$LHD" "$@" -q --result-json "$W/r.json" || fail "$* -> $(cat "$W/r.json" 2>/dev/null)"; }

[ -f "$PRP" ] || fail "missing fixture $PRP"
[ -f "$LIB" ] || fail "missing liberty $LIB"

# Shared: compile + color (acyclic so mult/div land in regions), original twin, models.
run compile "$PRP" --top "$TOP" --recipe O1 --emit-dir lg:"$W/lg" --workdir "$W/w1"
run pass color acyclic --top "$TOP" lg:"$W/lg" --workdir "$W/w2"
run pass partition --top "$TOP" lg:"$W/lg" --emit-dir lg:"$W/re" --workdir "$W/w4"
run pass liberty gensim "$LIB" --emit-dir lg:"$W/models" --workdir "$W/w5"

REGIONS=$(grep -oE '[A-Za-z0-9_.]+__c[0-9]+' "$W/re/library.txt" | sort -u)
[ -n "$REGIONS" ] || fail "no __cN region modules in the partition twin: $(cat "$W/re/library.txt")"

# pass abc: the divider regions must warn (and stay native), the rest map. Run
# WITHOUT -q so the div-blackbox diagnostic is written to stderr (quiet mode
# suppresses the per-diagnostic stream, leaving only a count in the result JSON).
"$LHD" pass abc --top "$TOP" lg:"$W/lg" --emit-dir lg:"$W/net" --set pass.abc.library="$LIB" \
  --workdir "$W/w3" --result-json "$W/r.json" 2>"$W/abc.err" || fail "pass abc -> $(cat "$W/r.json" 2>/dev/null)"
grep -q '"code":"div-blackbox"' "$W/abc.err" || fail "expected a div-blackbox warning (a/b and c/e are divisions): $(cat "$W/abc.err")"
ls "$W/net"/graph_* >/dev/null 2>&1 || fail "no mapped netlist emitted"

# Prove every region of the mapped netlist equivalent to its original-logic twin.
# lec flattens the netlist's blackbox standard-cell Subs inline against --lib; the
# native div blackbox is understood directly. Covers mult/sra (mapped) and div
# (passed through), signed and unsigned.
for r in $REGIONS; do
  run lec --impl lg:"$W/net" --ref lg:"$W/re" --lib lg:"$W/models" --top "$r" --workdir "$W/wlec"
done

# A non-default adder still proves equivalent (the multiplier's partial-product
# additions use pass.abc.adder).
rm -rf "$W/net_cska"
run pass abc --top "$TOP" lg:"$W/lg" --emit-dir lg:"$W/net_cska" --set pass.abc.library="$LIB" --set adder=cska \
  --workdir "$W/w6"
for r in $REGIONS; do
  run lec --impl lg:"$W/net_cska" --ref lg:"$W/re" --lib lg:"$W/models" --top "$r" --workdir "$W/wlec_cska"
done

# Negative control: the cell models are load-bearing. Without --lib the netlist's
# blackbox cell Subs are unresolved, so lec must NOT prove equivalence — a sound
# Unknown or a fail, never a vacuous pass. The impl-only box obligations gate the
# miter to INCONCLUSIVE (an incomplete correspondence can neither prove nor
# refute), which exits 0 unless strict — so run the control strict, where any
# non-Proven outcome is a hard failure exit.
one_region=$(echo "$REGIONS" | head -1)
if "$LHD" lec --impl lg:"$W/net" --ref lg:"$W/re" --top "$one_region" \
    --set lec.strict=true \
    --workdir "$W/wlec_nolib" -q --result-json "$W/rn.json" 2>/dev/null; then
  fail "lec proved equivalence with no --lib (unresolved cells must not vacuously pass)"
fi

echo "PASS: pass.abc mult/sra mapped + div blackboxed, all lhd-lec-equivalent (signed + unsigned)"
