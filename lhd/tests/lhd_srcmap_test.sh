#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# ECMA-426 source-map egress ([[1f]]-G): cgen srcmap:1 writes a .v.map sidecar
# (+ //# sourceMappingURL trailer) whose sourcesContent embeds the original
# Pyrope — browser-style viewers can only display originals from there.
# Covers:
#   1. in-process pyrope -> verilog (content served from the locator's memory)
#   2. lg: save -> reload -> verilog (content re-read from disk, validated
#      against the filehash record persisted in srcmap.txt)
#   3. drifted source after the lg: save (hash mismatch must drop the content,
#      never embed bytes the spans were not minted on)

set -u

LHD="$(pwd)/lhd/lhd"
W="${TEST_TMPDIR:-/tmp/lhd_srcmap_$$}"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

# Relative source paths keep the locator's workspace-relative contract AND let
# the post-load disk recovery resolve against the cwd.
cd "$W" || fail "cd $W"
cat > srcmap_in.prp <<'EOF'
mod fun1(a:u4, b:u4) -> (o:u5@[0]) {
  o = a + b
}
EOF

# --- 1. one-process pyrope -> verilog: content comes from locator memory ----
"$LHD" compile srcmap_in.prp --emit-dir verilog:v1 --set cgen.srcmap=1 --workdir wd1 -q --result-json r1.json \
  || fail "compile: $(cat r1.json 2>/dev/null)"
V=v1/srcmap_in.fun1.v
M=$V.map
[ -f "$V" ] || fail "missing $V"
[ -f "$M" ] || fail "missing $M"
grep -q 'sourceMappingURL=srcmap_in.fun1.v.map' "$V" || fail "missing sourceMappingURL trailer in $V"
grep -q '"version":3' "$M" || fail "$M is not a v3 source map"
grep -q '"sources":\["srcmap_in.prp"\]' "$M" || fail "wrong sources list in $M"
grep -qF 'o = a + b' "$M" || fail "sourcesContent does not embed the original pyrope in $M"
grep -q '"x_hhds"' "$M" || fail "missing x_hhds SourceId extra in $M"

# Line coverage: header/ports/wires/always blocks anchor at the mod
# declaration, statements at their own source lines — near-total coverage
# (every line except the sourceMappingURL trailer). Guard the floor.
SEGS=$(sed 's/.*"mappings":"\([^"]*\)".*/\1/' "$M" | tr ';' '\n' | grep -c .)
[ "$SEGS" -ge 12 ] || fail "expected >=12 mapped generated lines, got $SEGS"

# --- 2. lg: round trip: filehash persisted, content recovered from disk -----
"$LHD" compile srcmap_in.prp --emit-dir lg:lgdb --workdir wd2 -q --result-json r2.json \
  || fail "compile to lg:: $(cat r2.json 2>/dev/null)"
grep -q '^filehash ' lgdb/srcmap.txt || fail "lgdb/srcmap.txt lacks the filehash record"
"$LHD" compile lg:lgdb --emit-dir verilog:v2 --set cgen.srcmap=1 --workdir wd3 -q --result-json r3.json \
  || fail "compile from lg:: $(cat r3.json 2>/dev/null)"
M2=v2/srcmap_in.fun1.v.map
[ -f "$M2" ] || fail "missing $M2"
grep -qF 'o = a + b' "$M2" || fail "post-load map lacks sourcesContent (disk recovery failed) in $M2"

# --- 3. drifted source: stale content must be dropped, not embedded ---------
echo '// drifted after the lg: save' >> srcmap_in.prp
"$LHD" compile lg:lgdb --emit-dir verilog:v3 --set cgen.srcmap=1 --workdir wd4 -q --result-json r4.json \
  || fail "compile from lg: (drifted source): $(cat r4.json 2>/dev/null)"
M3=v3/srcmap_in.fun1.v.map
[ -f "$M3" ] || fail "missing $M3"
if grep -q '"sourcesContent"' "$M3"; then
  fail "drifted source must not be embedded in $M3"
fi
grep -q '"version":3' "$M3" || fail "$M3 is not a v3 source map"

echo "PASS"
