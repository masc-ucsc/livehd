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
  --workdir "$W/lec" -q --result-json "$W/lec.json" \
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

# Module parameters use generic defaults; localparams remain body constants.
# Different elaborated instances must retain their OVERRIDDEN values. Include
# signed defaults and a string needing cooked-string escaping on re-emission.
cat >"$W/params.sv" <<'EOF'
module params #(parameter int STEP = 3, parameter string TAG = "a'b{c}\\d", parameter int NEG = -2)
  (input [7:0] a, output [8:0] y, output [8:0] z);
  localparam BIAS = STEP + 1;
  assign y = a + STEP + NEG;
  assign z = a + BIAS;
endmodule
module pair(input [7:0] a, output [8:0] y, output [8:0] z, output [8:0] v, output [8:0] w);
  params u0(a, y, z);
  params #(.STEP(7), .TAG("other")) u1(a, v, w);
endmodule
module params_override(input [7:0] a, output [8:0] y, output [8:0] z);
  params #(.STEP(9), .NEG(-4)) u(a, y, z);
endmodule
EOF
$LHD compile "$W/params.sv" --top pair --emit-dir pyrope:"$W/params" --workdir "$W/params-w" -q \
  || fail "module parameter emission failed"
grep -q 'STEP=3' "$W/params/params.prp" || fail "default STEP missing from generic header"
grep -q 'STEP=7' "$W/params/params_p1.prp" || fail "overridden STEP missing from specialization header"
grep -Fq 'NEG=(-2)' "$W/params/params.prp" || fail "negative generic default missing"
grep -Fq 'TAG="a' "$W/params/params.prp" || fail "string generic default missing"
grep -Eq 'const STEP\b|const NEG\b' "$W/params/params.prp" && fail "generic parameter rebound in body"
grep -q 'const BIAS' "$W/params/params.prp" || fail "localparam missing from body"
for top in pair params; do
  $LHD lec --ref verilog:"$W/params.sv" --ref-top "$top" --impl pyrope:"$W/params/" --impl-top "$top.$top" \
    --workdir "$W/params-lec-$top" -q --result-json "$W/params-lec-$top.json" \
    || fail "generated generic parameters not equivalent ($top): $(cat "$W/params-lec-$top.json" 2>/dev/null)"
done
cat >"$W/params/params_override.prp" <<'EOF'
const pp = import("params.params")
pub comb params_override(a:u8) -> (y:u9, z:u9) {
  const r = pp<STEP=9, NEG=(-4)>(a=a)
  y = r.y
  z = r.z
}
EOF
$LHD lec --ref verilog:"$W/params.sv" --ref-top params_override --impl pyrope:"$W/params/" \
  --impl-top params_override.params_override --workdir "$W/params-lec-override" -q \
  --result-json "$W/params-lec-override.json" \
  || fail "overriding emitted parameters changed semantics: $(cat "$W/params-lec-override.json" 2>/dev/null)"
echo "PASS: integer/string/negative generic defaults, instance overrides, and localparams are LEC-exact"

# A localparam whose initializer lowers to a BOOLEAN (`A || B`) must stay
# boolean-typed on the const it binds: without the kind mark every later read
# re-compares it to 0 and typecheck refuses ("comparison mixes a bool with an
# int/string"), which is the picorv32 `WITH_PCPI` shape and took the documented
# v2prp flow from exit 0 to a failed compile.
cat >"$W/boolparam.sv" <<'EOF'
module boolparam #(parameter [0:0] EN_A = 0, parameter [0:0] EN_B = 0, parameter [0:0] CATCH = 1)
  (input [7:0] a, output y);
  localparam WITH_X = EN_A || EN_B;
  assign y = (CATCH || WITH_X) && !a[0];
endmodule
EOF
$LHD compile "$W/boolparam.sv" --top boolparam --emit-dir pyrope:"$W/boolp" --workdir "$W/boolp-w" -q   || fail "boolean localparam over module parameters did not compile"
grep -Fq 'const WITH_X = (EN_A != 0) or (EN_B != 0)' "$W/boolp/boolparam.prp"   || fail "boolean localparam lost its symbolic form: $(cat "$W/boolp/boolparam.prp")"
$LHD compile "$W/boolp/boolparam.prp" --top boolparam.boolparam --emit-dir lg:"$W/boolp-lg"   --workdir "$W/boolp-rw" -q || fail "emitted boolean-localparam Pyrope does not re-compile"
echo "PASS: a boolean localparam over module parameters stays boolean and re-compiles"

# A parameter named like a Pyrope RESERVED WORD must NOT become a generic: the
# header would have to backtick-escape it, and prp2lnast registers a generic under
# that raw spelling while body reads canonicalize to the bare name, so the emitted
# file stops re-parsing. Such a parameter falls back to folding instead.
cat >"$W/kwparam.sv" <<'EOF'
module kwparam #(parameter pipe = 3, parameter step = 2, parameter WIDTH = 8)
  (input [7:0] a, output [8:0] y);
  assign y = a + pipe + step;
endmodule
EOF
$LHD compile "$W/kwparam.sv" --top kwparam --emit-dir pyrope:"$W/kwp" --workdir "$W/kwp-w" -q   || fail "reserved-word parameter emission failed"
grep -q 'WIDTH=8' "$W/kwp/kwparam.prp" || fail "non-keyword parameter lost its generic header"
grep -Fq '`pipe`' "$W/kwp/kwparam.prp" && fail "a reserved-word parameter was emitted as a backticked generic"
$LHD compile "$W/kwp/kwparam.prp" --top kwparam.kwparam --emit-dir lg:"$W/kwp-lg" --workdir "$W/kwp-rw" -q   || fail "emitted Pyrope with a reserved-word parameter does not re-parse"
echo "PASS: a reserved-word Verilog parameter folds instead of becoming an unparseable generic"

# A `pyrope:` RE-EMIT of a generated generic unit must not be silently dropped.
# pass.prp_writer used to skip every template, which wrote a ZERO-BYTE .prp at
# exit 0 with the unit still listed in manifest.json -- and destroyed the input
# when the emit dir was the input dir.
$LHD compile "$W/params/pair.prp" "$W/params/params.prp" "$W/params/params_p1.prp"   --emit-dir pyrope:"$W/reemit" --workdir "$W/reemit-w" -q || fail "re-emit of generated Pyrope failed"
for u in params params_p1; do
  [ -s "$W/reemit/$u.prp" ] || fail "re-emit wrote a ZERO-BYTE $u.prp (template silently dropped)"
  grep -q 'STEP=' "$W/reemit/$u.prp" || fail "re-emitted $u.prp lost its generic header"
done
echo "PASS: a fully-defaulted generic unit survives a pyrope -> pyrope re-emit"

# Cross-frontend def pairing on the verilog -> pyrope -> lg leg. This USED to be a
# known gap: recompiling the emitted Pyrope renamed a parameterized STATEFUL module
# to its mangled specialization (`rtsub__u1_u8_N_3_h…`), so the two legs could not be
# paired by name. The identity-specialization naming fixed it -- a specialization
# whose generics all take their DECLARED DEFAULTS keeps the template's own name -- so
# the assertion below is now the real one rather than a pinned limitation.
# `rtsub` (N=3, the default) keeps its name; `rtsub_p1` (N=5) is a distinct
# specialization and gets its own emitted def. Neither may carry a `__` mangle.
cat >"$W/rt.v" <<'EOF'
module rtsub #(parameter int N = 3)(input clk, input [7:0] a, output reg [8:0] y);
  always @(posedge clk) y <= a + N;
endmodule
module rttop(input clk, input [7:0] a, output [8:0] y, output [8:0] z);
  rtsub u0(clk, a, y);
  rtsub #(.N(5)) u1(clk, a, z);
endmodule
EOF
$LHD compile "$W/rt.v" --top rttop --emit-dir lg:"$W/rt-lgv" --workdir "$W/rt-wv" -q \
  || fail "stateful parameterized reference did not compile"
$LHD tool tree lg:"$W/rt-lgv" 2>/dev/null | grep -q '^rtsub ' \
  || fail "the verilog -> lg leg should keep the plain module name"
$LHD compile "$W/rt.v" --top rttop --emit-dir pyrope:"$W/rt-p" --workdir "$W/rt-wp" -q \
  || fail "stateful parameterized pyrope emission failed"
$LHD compile "$W/rt-p"/*.prp --top rttop.rttop --emit-dir lg:"$W/rt-lgp" --workdir "$W/rt-wr" -q \
  || fail "recompiling the generated stateful Pyrope failed"
! $LHD tool tree lg:"$W/rt-lgp" 2>/dev/null | grep -q 'rtsub__' \
  || fail "recompiled parameterized stateful module kept a mangled specialization name: $($LHD tool tree lg:"$W/rt-lgp" 2>/dev/null)"
for d in rtsub rtsub_p1; do
  $LHD tool tree lg:"$W/rt-lgv" 2>/dev/null | grep -q "^$d " \
    || fail "verilog -> lg leg lost the plain def name '$d'"
  $LHD tool tree lg:"$W/rt-lgp" 2>/dev/null | grep -q "^$d\.$d " \
    || fail "pyrope -> lg leg lost the plain def name '$d': $($LHD tool tree lg:"$W/rt-lgp" 2>/dev/null)"
done
echo "PASS: a parameterized STATEFUL module keeps its plain name across verilog -> pyrope -> lg (both specializations pair)"

echo "ALL PASS"
