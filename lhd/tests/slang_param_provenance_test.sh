#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# Regression for named-constant provenance in slang→Pyrope emission
# (compile.slang.preserve_param_provenance): a bare package-parameter reference
# emits as a symbolic `pkg.PARAM` plus a `pub comptime const` package unit
# instead of folding to its literal.
#
# Guards, specifically:
#  (1) the package unit carries the REAL values with constprop=1 AND constprop=0
#      (the values are stamped by the reader via set_pub_values; before, only
#      the constprop harvest filled them and constprop=0 emitted `= 0` for all)
#  (2) the conversion peel is value-preserving only: `4'(BIG)` with BIG=300
#      truncates to 12 — it must NOT emit the symbolic ref (== 300, a miscompile)
#  (3) the emitted Pyrope recompiles clean (const-rebind through a %tmp/tuple_get
#      chain: a pkg-valued single-store net emits `mut`, not comptime `const`)
#  (4) the loop is LEC-exact: emitted Pyrope proven equivalent to the source SV

set -u
LHD=lhd/lhd
W="${TEST_TMPDIR:-/tmp/lhd_param_provenance_$$}"
mkdir -p "$W"
fail() { echo "FAIL: $*" >&2; exit 1; }

cat >"$W/tpkg.sv" <<'EOF'
package tpkg;
  localparam SEL_A = 3;
  localparam SEL_B = 12;
  localparam logic signed [9:0] NEG = -12;
  localparam int BIG = 300;
endpackage
EOF

cat >"$W/tmod.sv" <<'EOF'
import tpkg::*;
module tmod(input [3:0] s, input [9:0] d, output logic [9:0] r,
            output logic signed [9:0] rs, output logic hit);
  // bare refs in a mux target chain: `r` defaults to a pkg-valued net, then a
  // conditional override — the shape that tripped the const-rebind on recompile
  always_comb begin
    r = SEL_A;
    if (s == SEL_B[3:0])
      r = 10'd7;
  end
  // signed bare ref in a same-type context (value-preserving — must stay symbolic)
  assign rs = NEG;
  // value-CHANGING narrowing cast: 4'(300) == 12; must fold, not stay symbolic
  assign hit = (s == 4'(BIG));
endmodule
EOF

# ── (1) provenance emission, default constprop ────────────────────────────────
$LHD compile "$W/tpkg.sv" "$W/tmod.sv" --top tmod --emit-dir pyrope:"$W/p1" --workdir "$W/w1" -q \
  --set compile.slang.preserve_param_provenance=true \
  || fail "provenance emission (constprop=1) did not compile"
grep -q 'pub comptime const SEL_A = 3' "$W/p1/tpkg.prp" || fail "constprop=1 pkg unit lacks SEL_A=3: $(cat "$W/p1/tpkg.prp")"
grep -q 'pub comptime const NEG = -12' "$W/p1/tpkg.prp" || fail "constprop=1 pkg unit lacks NEG=-12: $(cat "$W/p1/tpkg.prp")"
grep -q 'tpkg\.SEL_A' "$W/p1/tmod.prp" || fail "module body folded tpkg.SEL_A: $(cat "$W/p1/tmod.prp")"
echo "PASS: provenance emission carries symbolic refs + real package values (constprop=1)"

# ── (2) same with constprop=0 (used to emit `= 0` for every const) ────────────
$LHD compile "$W/tpkg.sv" "$W/tmod.sv" --top tmod --emit-dir pyrope:"$W/p0" --workdir "$W/w0" -q \
  --set compile.slang.preserve_param_provenance=true --set compile.upass.constprop=0 \
  || fail "provenance emission (constprop=0) did not compile"
grep -q 'pub comptime const SEL_A = 3' "$W/p0/tpkg.prp" || fail "constprop=0 pkg unit lacks SEL_A=3: $(cat "$W/p0/tpkg.prp")"
grep -q 'pub comptime const NEG = -12' "$W/p0/tpkg.prp" || fail "constprop=0 pkg unit lacks NEG=-12: $(cat "$W/p0/tpkg.prp")"
echo "PASS: constprop=0 package unit still carries real values"

# ── (3) value-changing narrowing cast around the ref must NOT stay symbolic ───
grep -q 'tpkg\.BIG' "$W/p1/tmod.prp" && fail "4'(BIG) kept symbolic tpkg.BIG (==300, miscompile): $(cat "$W/p1/tmod.prp")"
echo "PASS: value-changing narrowing cast folds (peel guard)"

# ── (4) recompile + LEC-exact vs the source SV ────────────────────────────────
$LHD compile "$W/p1"/*.prp --top tmod.tmod --workdir "$W/rc" -q \
  || fail "emitted provenance Pyrope did not recompile"
$LHD compile "$W/tpkg.sv" "$W/tmod.sv" --top tmod --emit-dir lg:"$W/ref_lg" --workdir "$W/refw" -q \
  || fail "reference SV did not compile to lg"
$LHD lec --impl pyrope:"$W/p1"/ --impl-top tmod.tmod --ref lg:"$W/ref_lg" --ref-top tmod \
  --set formal.solver=cvc5 --workdir "$W/lec" -q --result-json "$W/lec.json" \
  || fail "provenance Pyrope not proven equivalent: $(cat "$W/lec.json" 2>/dev/null)"
grep -q '"status":"pass"' "$W/lec.json" || fail "lec not pass: $(cat "$W/lec.json")"
echo "PASS: provenance Pyrope recompiles and is LEC-proven vs the source SV"

# ── (5) provenance OFF is byte-stable: no pkg refs, no package unit ───────────
$LHD compile "$W/tpkg.sv" "$W/tmod.sv" --top tmod --emit-dir pyrope:"$W/poff" --workdir "$W/woff" -q \
  || fail "provenance-OFF emission did not compile"
[ -f "$W/poff/tpkg.prp" ] && fail "provenance OFF still emitted a package unit"
grep -q 'tpkg\.' "$W/poff/tmod.prp" && fail "provenance OFF emitted pkg refs: $(cat "$W/poff/tmod.prp")"
echo "PASS: provenance OFF unchanged (folds, no package unit)"

echo "ALL PASS"
