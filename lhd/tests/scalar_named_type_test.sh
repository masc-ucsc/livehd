#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# Regression for SCALAR named-type aliases carrying their width to a
# declaration. Before this, a literal `wire x:u10` constrained the net to 10
# bits, but a `type W = u10; wire x:W` (local OR imported `pkg.W`) left `x`
# untyped/loose — the alias name resolved to a bundle with no scalar range, so
# the width was silently dropped. Now the alias's declared range is borrowed
# (upass.runner bake_decl_pre_step) and the declare's type slot is concretized
# to `prim_type_int(max,min)` (emit_scalar_named_type_slot), so `:W` behaves
# exactly like the literal type while the `typename` provenance rides along.
#
# Also guards the `pub type` grammar (prpparse) needed to EXPORT a scalar type
# alias from a package unit.

set -u
LHD=lhd/lhd
W="${TEST_TMPDIR:-/tmp/lhd_scalar_named_type_$$}"
mkdir -p "$W"
fail() { echo "FAIL: $*" >&2; exit 1; }

# ── (1) local scalar type alias constrains the net width ──────────────────────
cat >"$W/local.prp" <<'EOF'
pub comb local_alias(a:u16) -> (r:u16) {
  type W10 = u10
  wire x:W10 = nil
  x = a
  r = x
}
EOF
$LHD compile "$W/local.prp" --top local_alias --emit-dir verilog:"$W/lv" --workdir "$W/lw" -q \
  || fail "local scalar type alias did not compile"
cat "$W/lv"/*.v >"$W/l_all.v" 2>/dev/null
# The u10 alias must truncate the u16 source: a 10-bit net (`[9:0]`) must appear.
grep -qE '\[9:0\]' "$W/l_all.v" || fail "type W10=u10 did not constrain x to 10 bits: $(cat "$W/l_all.v")"
echo "PASS: local scalar type alias (type W10=u10) constrains the net to 10 bits"

# ── (2) `pub type` parses and exports; the importer compiles ──────────────────
cat >"$W/pkg.prp" <<'EOF'
pub comptime const P = 10
pub type PType = u10
EOF
$LHD compile "$W/pkg.prp" --workdir "$W/pw" -q \
  || fail "`pub type` package unit did not compile"
cat >"$W/use.prp" <<'EOF'
const pkg = import("pkg")
pub comb use_pkg(a:u10) -> (r:u10) {
  wire x:pkg.PType = nil
  x = a
  r = x
}
EOF
$LHD compile "$W/pkg.prp" "$W/use.prp" --top use_pkg --workdir "$W/uw" -q \
  || fail "importing a `pub type` scalar alias did not compile"
echo "PASS: pub type scalar alias parses, exports, and imports"

# ── (3) a tuple named type is UNAFFECTED (no scalar-range borrow) ─────────────
cat >"$W/tuple.prp" <<'EOF'
type Complex = (v1:u8 = 0, v2:u8 = 0)
pub comb tup(a:u8) -> (r:u8) {
  mut c:Complex = (v1=a, v2=0)
  r = c.v1
}
EOF
$LHD compile "$W/tuple.prp" --top tup --workdir "$W/tw" -q \
  || fail "tuple named type regressed (scalar-range borrow must skip aggregates)"
echo "PASS: tuple named types are unaffected by the scalar-alias borrow"

echo "PASS: all scalar named-type regressions"
