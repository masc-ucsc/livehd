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
  localparam SEL_W = 4;
  localparam SEL_A = 3;
  localparam SEL_B = 12;
  localparam logic signed [9:0] NEG = -12;
  localparam int BIG = 300;
  localparam DIFF = (SEL_B - SEL_A);
endpackage
EOF

cat >"$W/tmod.sv" <<'EOF'
import tpkg::*;
module tmod(input [SEL_W-1:0] s, input [9:0] d, output logic [9:0] r,
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
  // param defined via an expression of other params (package-unit fidelity)
  wire [9:0] unused_d = d + DIFF;
  // MODULE-LOCAL param: body-level `const LOCAL_TH = tpkg.SEL_A + 1`
  localparam LOCAL_TH = (SEL_A + 1);
  wire [9:0] unused_e = d + LOCAL_TH;
endmodule
EOF

# ── (1) provenance emission is the DEFAULT for a pyrope-emitting compile ──────
$LHD compile "$W/tpkg.sv" "$W/tmod.sv" --top tmod --emit-dir pyrope:"$W/p1" --workdir "$W/w1" -q \
  || fail "provenance emission (constprop=1) did not compile"
grep -q 'pub comptime const SEL_A = 3' "$W/p1/tpkg.prp" || fail "constprop=1 pkg unit lacks SEL_A=3: $(cat "$W/p1/tpkg.prp")"
grep -q 'pub comptime const NEG:s10 = -12' "$W/p1/tpkg.prp" || fail "pkg unit lacks typed NEG:s10=-12: $(cat "$W/p1/tpkg.prp")"
grep -q 'tpkg\.SEL_A' "$W/p1/tmod.prp" || fail "module body folded tpkg.SEL_A: $(cat "$W/p1/tmod.prp")"
# defining EXPRESSION preserved (not the folded 9), in SOURCE order (SEL_A first)
grep -qE 'pub comptime const DIFF = \(SEL_B - SEL_A\)' "$W/p1/tpkg.prp" \
  || fail "pkg unit folded DIFF's defining expression: $(cat "$W/p1/tpkg.prp")"
head -1 "$W/p1/tpkg.prp" | grep -q 'SEL_W' || fail "pkg unit not in source order: $(head -3 "$W/p1/tpkg.prp")"
# a `[SEL_W-1:0]` port dim mints an exported scalar type alias + a typed port
grep -q 'pub type SEL_W_T = u4' "$W/p1/tpkg.prp" || fail "pkg unit lacks the SEL_W_T alias: $(cat "$W/p1/tpkg.prp")"
grep -q 's:tpkg\.SEL_W_T' "$W/p1/tmod.prp" || fail "port did not use the imported alias: $(head -3 "$W/p1/tmod.prp")"
# a module-local param becomes a body-level const with its defining expression
grep -qE 'const LOCAL_TH = tpkg\.SEL_A \+ 1' "$W/p1/tmod.prp" \
  || fail "module-local param not preserved: $(cat "$W/p1/tmod.prp")"
echo "PASS: symbolic refs, typed consts, defining exprs, source order, dim aliases, local params"

# ── (2) same with constprop=0 (used to emit `= 0` for every const) ────────────
$LHD compile "$W/tpkg.sv" "$W/tmod.sv" --top tmod --emit-dir pyrope:"$W/p0" --workdir "$W/w0" -q \
  --set compile.upass.constprop=0 \
  || fail "provenance emission (constprop=0) did not compile"
grep -q 'pub comptime const SEL_A = 3' "$W/p0/tpkg.prp" || fail "constprop=0 pkg unit lacks SEL_A=3: $(cat "$W/p0/tpkg.prp")"
grep -q 'pub comptime const NEG:s10 = -12' "$W/p0/tpkg.prp" || fail "constprop=0 pkg unit lacks NEG=-12: $(cat "$W/p0/tpkg.prp")"
echo "PASS: constprop=0 package unit still carries real values"

# ── (3) value-changing narrowing cast must keep its truncation ────────────────
# 4'(BIG) with BIG=300 == 12: a BARE symbolic `tpkg.BIG` (no slice/mask applied)
# would read back as 300 — the peel-guard miscompile. The structural Conversion
# path may keep the symbol WITH the truncation (tpkg.BIG#[0..=3]), which is fine
# (and LEC-checked below).
grep -E 'tpkg\.BIG($|[^#.])' "$W/p1/tmod.prp" && fail "4'(BIG) kept a BARE symbolic tpkg.BIG (==300, miscompile): $(cat "$W/p1/tmod.prp")"
echo "PASS: value-changing narrowing cast keeps its truncation (peel guard)"

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

# ── (5) explicit =false is the debug escape: folds, no package unit ───────────
$LHD compile "$W/tpkg.sv" "$W/tmod.sv" --top tmod --emit-dir pyrope:"$W/poff" --workdir "$W/woff" -q \
  --set compile.slang.preserve_param_provenance=false \
  || fail "provenance-OFF emission did not compile"
[ -f "$W/poff/tpkg.prp" ] && fail "provenance OFF still emitted a package unit"
grep -q 'tpkg\.' "$W/poff/tmod.prp" && fail "provenance OFF emitted pkg refs: $(cat "$W/poff/tmod.prp")"
echo "PASS: explicit provenance OFF folds (debug escape, no package unit)"

# ── (6) explicit =true + a graphs flow is refused (would nil-wire the refs) ───
$LHD compile "$W/tpkg.sv" "$W/tmod.sv" --top tmod --emit-dir verilog:"$W/vg" --workdir "$W/wg" -q \
  --set compile.slang.preserve_param_provenance=true >"$W/g.log" 2>&1 \
  && fail "provenance=true with a verilog emit was not refused"
grep -q "pyrope-only" "$W/g.log" || fail "graphs-flow refusal lacks the directing message: $(cat "$W/g.log")"
# and the DEFAULT for a graphs flow keeps folding (no symbolic refs to nil-wire)
$LHD compile "$W/tpkg.sv" "$W/tmod.sv" --top tmod --emit-dir verilog:"$W/vd" --workdir "$W/wd" -q \
  || fail "default graphs flow did not compile"
echo "PASS: graphs flow folds by default; explicit =true there is refused"

echo "ALL PASS"
