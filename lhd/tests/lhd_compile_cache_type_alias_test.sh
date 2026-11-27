#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# A parameterized type alias is expanded in the IMPORTER from the exporter's
# source text, so an exporter edit that changes what an application expands to
# must reach the importer through the incremental compile cache (the importer's
# own bytes never change). Before the cache tracked it, the warm compile restored
# the importer's stale graph: the edited alias took no effect and the compile
# passed.
set -u

LHD=lhd/lhd
W="${TEST_TMPDIR:-/tmp/lhd_compile_cache_type_alias_$$}/cc_alias"
rm -rf "$W"
mkdir -p "$W/src"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

compile() {  # <emit-name>
  (cd "$W/src" && "$OLDPWD/$LHD" compile imp.prp --emit "verilog:$1" --workdir ../work 2>&1)
}

cat > "$W/src/imp.prp" <<'EOF'
const t = import("types")
pub mod imp(a:t.Row<4>) -> (o:U4@[0]) { o = a }
EOF

echo 'pub type Row<W=8> = Unsigned(bits=W)' > "$W/src/types.prp"
out=$(compile one.v) || fail "cold compile failed: $out"
grep -q "input \[3:0\] a" "$W/src/one.v" || fail "Row<4> must be 4 bits: $(cat "$W/src/one.v")"

# Edit ONLY the exporter: Row<4> is now 8 bits and the 4-bit output cannot take it.
echo 'pub type Row<W=8> = Unsigned(bits=(W*2))' > "$W/src/types.prp"
out=$(compile two.v) && fail "warm compile reused the importer's stale graph after the alias changed: $out"
echo "$out" | grep -q "may not fit its declared range" || fail "expected the overflow of the widened alias: $out"

# Back to the first alias: compiles again, and a further warm compile reuses it.
echo 'pub type Row<W=8> = Unsigned(bits=W)' > "$W/src/types.prp"
out=$(compile three.v) || fail "compile after restoring the alias failed: $out"
grep -q "input \[3:0\] a" "$W/src/three.v" || fail "Row<4> must be 4 bits again"
out=$(compile four.v) || fail "warm compile failed: $out"
echo "$out" | grep -q '"hits":3,"misses":0' || fail "an unchanged warm compile must reuse every unit: $out"

echo "PASS: an exporter's type alias edit reaches the importer through the compile cache"
