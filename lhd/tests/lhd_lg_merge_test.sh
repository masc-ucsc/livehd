#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# Task 1m-C — graph-library linker: assemble several lg:/ln: inputs into one new
# lg: library. One input defines `foo`; another (an ln: source) imports it as a
# black box (`import("lg:foo.foo")`) and instantiates it. The merge
#   lhd compile --top bar lg:L1 ln:L2 --emit-dir lg:L3
# load_merge's L1 into the output library (name-hash gids → conflict-free),
# lowers L2's `bar` against it (the import resolves to a Sub by name), and saves
# L3 holding BOTH bodies. Synthesizing L3 emits both modules with `bar`
# instantiating `foo`.

set -u

LHD=lhd/lhd
W="${TEST_TMPDIR:-/tmp/lhd_lg_merge_$$}"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

# Signed I/O (Pyrope's default): the Sub output wires cleanly through cgen.
cat > "$W/foo.prp" <<'EOF'
pub comb foo(a:s8) -> (r:s9) { r = a + 1 }
EOF
cat > "$W/bar.prp" <<'EOF'
const f = import("lg:foo.foo")
comb bar(x:s8) -> (y:s9) {
  y = f(a=x)
}
EOF

# ── 1. foo.prp → lg:L1 (the definition library) ──────────────────────────────
"$LHD" compile "$W/foo.prp" --emit-dir lg:"$W/L1/" --workdir "$W/w1" -q --result-json "$W/r1.json" \
  || fail "foo compile→lg failed: $(cat "$W/r1.json" 2>/dev/null)"
grep -q '"status":"pass"' "$W/r1.json" || fail "foo result not pass"
[ -f "$W/L1/library.txt" ] || fail "L1 has no library.txt"
grep -q 'graph_io .* foo.foo' "$W/L1/library.txt" || fail "L1 misses foo.foo: $(cat "$W/L1/library.txt")"

# ── 2. bar.prp → ln:L2 (importer, NOT lowered — keeps the lg: import) ─────────
"$LHD" compile "$W/bar.prp" --emit-dir ln:"$W/L2/" --workdir "$W/w2" -q --result-json "$W/r2.json" \
  || fail "bar compile→ln failed: $(cat "$W/r2.json" 2>/dev/null)"
grep -q '"status":"pass"' "$W/r2.json" || fail "bar result not pass"

# ── 3. MERGE: assemble L1 (foo) + L2 (bar) into a new lg: library L3 ──────────
"$LHD" compile --top bar lg:"$W/L1/" ln:"$W/L2/" --emit-dir lg:"$W/L3/" \
  --workdir "$W/w3" -q --result-json "$W/r3.json" \
  || fail "merge compile failed: $(cat "$W/r3.json" 2>/dev/null)"
grep -q '"status":"pass"' "$W/r3.json" || fail "merge result not pass: $(cat "$W/r3.json")"
# L3 holds BOTH graph_ios and BOTH bodies (foo absorbed from L1, bar lowered).
grep -q 'graph_io .* foo.foo' "$W/L3/library.txt" || fail "L3 misses foo.foo: $(cat "$W/L3/library.txt")"
grep -q 'graph_io .* bar.bar' "$W/L3/library.txt" || fail "L3 misses bar.bar: $(cat "$W/L3/library.txt")"
foo_gid=$(awk '/graph_io .* foo.foo/{print $2}' "$W/L3/library.txt")
bar_gid=$(awk '/graph_io .* bar.bar/{print $2}' "$W/L3/library.txt")
[ -f "$W/L3/graph_${foo_gid}/body.bin" ] || fail "L3 missing foo body (graph_${foo_gid})"
[ -f "$W/L3/graph_${bar_gid}/body.bin" ] || fail "L3 missing bar body (graph_${bar_gid})"
# Name-hash gids: foo keeps the SAME gid it had in L1 (conflict-free merge).
grep -q "graph_io ${foo_gid} foo.foo" "$W/L1/library.txt" || fail "foo gid not preserved across merge"

# ── 4. compile the assembled library → Verilog: both modules, bar instantiates foo
"$LHD" compile lg:"$W/L3/" --emit verilog:"$W/out.v" --workdir "$W/w4" -q --result-json "$W/r4.json" \
  || fail "compile of merged library failed: $(cat "$W/r4.json" 2>/dev/null)"
grep -q '"status":"pass"' "$W/r4.json" || fail "compile result not pass"
grep -q 'module \\foo.foo' "$W/out.v" || fail "merged Verilog misses module foo.foo: $(cat "$W/out.v")"
grep -q 'module \\bar.bar' "$W/out.v" || fail "merged Verilog misses module bar.bar"
grep -q '\\foo.foo .*\\u_foo' "$W/out.v" || fail "bar does not instantiate foo: $(cat "$W/out.v")"
grep -q '.a(x)' "$W/out.v" || fail "foo instance input not wired"
grep -Eq '\.r\(' "$W/out.v" || fail "foo instance output not wired"

echo "lhd_lg_merge_test passed"
