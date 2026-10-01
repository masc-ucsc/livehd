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
pub comb foo(a:S8) -> (r:S9) { r = a + 1 }
EOF
cat > "$W/bar.prp" <<'EOF'
const f = import("lg:foo.foo")
comb bar(x:S8) -> (y:S9) {
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
# Verilog module names are FLAT (`foo`, not the internal `\foo.foo`); the
# instance TYPE is the flat name, its instance NAME still embeds `\u_foo.foo…`.
grep -qE '^module foo\(' "$W/out.v" || fail "merged Verilog misses module foo: $(cat "$W/out.v")"
grep -qE '^module bar\(' "$W/out.v" || fail "merged Verilog misses module bar"
grep -qE '^foo .*u_foo' "$W/out.v" || fail "bar does not instantiate foo: $(cat "$W/out.v")"
grep -q '.a(x)' "$W/out.v" || fail "foo instance input not wired"
grep -Eq '\.r\(' "$W/out.v" || fail "foo instance output not wired"

# ── 5. an lg: black box has no callee Lnast: omitting one of its inputs is a
# clean "does not bind" error (was a SIGSEGV in the omitted-input rules).
cat > "$W/foo2.prp" <<'EOF'
pub comb foo2(a:S8, b:S8) -> (r:S9) { r = a + b }
EOF
cat > "$W/bar2.prp" <<'EOF'
const f = import("lg:foo2.foo2")
comb bar2(x:S8) -> (y:S9) {
  y = f(a=x)
}
EOF
"$LHD" compile "$W/foo2.prp" --emit-dir lg:"$W/L5/" --workdir "$W/w5a" -q || fail "foo2 compile→lg failed"
"$LHD" compile "$W/bar2.prp" --emit-dir ln:"$W/L6/" --workdir "$W/w5b" -q || fail "bar2 compile→ln failed"
"$LHD" compile --top bar2 lg:"$W/L5/" ln:"$W/L6/" --emit-dir lg:"$W/L7/" --workdir "$W/w5c" -q \
  --result-json "$W/r5.json" > "$W/r5.log" 2>&1
grep -q "does not bind declared input 'b'" "$W/r5.json" \
  || fail "omitted lg: input not a clean error: $(cat "$W/r5.json" "$W/r5.log" 2>/dev/null)"

# ── 6. a conditional call to an lg: black box gates its clk/clock by the
# Verilog name convention (no Clock/Reset types), OR-ing in its reset at the
# level its name says (`rst_n` active-low), so its state holds while absent.
cat > "$W/bb.v" <<'EOF'
module bbk(input clk, input rst_n, input [7:0] d, output reg [7:0] q);
  always @(posedge clk) if (!rst_n) q <= 0; else q <= q + d;
endmodule
module bbc(input clock, input reset, input [7:0] d, output reg [7:0] q);
  always @(posedge clock) if (reset) q <= 0; else q <= q + d;
endmodule
EOF
cat > "$W/gtop.prp" <<'EOF'
const k = import("lg:bbk")
const m = import("lg:bbc")
mod gtop(clk:Clock, rst:Reset, c:Bool, e:Bool, d:U8) -> (y:U8@[], z:U8@[]) {
  y = 0
  z = 0
  if c { y = k(clk=clk, rst_n=rst, d=d) }
  if e { z = m(clock=clk, reset=rst, d=d) }
}
EOF
"$LHD" compile "$W/bb.v" --emit-dir lg:"$W/LB/" --workdir "$W/w6a" -q || fail "bb.v compile→lg failed"
"$LHD" compile --top gtop lg:"$W/LB/" "$W/gtop.prp" --emit verilog:"$W/gtop.v" --workdir "$W/w6b" -q \
  --result-json "$W/r6.json" || fail "gated lg: call compile failed: $(cat "$W/r6.json" 2>/dev/null)"
grep -Eq '\.clk\(clock_cell_[0-9]+\)' "$W/gtop.v" || fail "conditional lg: clk not gated: $(cat "$W/gtop.v")"
grep -Eq '\.clock\(clock_cell_[0-9]+\)' "$W/gtop.v" || fail "conditional lg: clock not gated: $(cat "$W/gtop.v")"
grep -Eq '<= \(c \| \(\(\(rst\) == ' "$W/gtop.v" || fail "rst_n gate not active-low: $(cat "$W/gtop.v")"
grep -Eq '<= \(e \| rst\);' "$W/gtop.v" || fail "reset gate not active-high: $(cat "$W/gtop.v")"

# ── 7. a constant clock into an idle memory write port is legal however the
# held-off enable is bound: named or positional (positional was a false error).
cat > "$W/rsub.v" <<'EOF'
module rsub(input clk, input we, input [1:0] wa, input [7:0] wd, input [1:0] ra, output [7:0] q);
  reg [7:0] mem[0:3];
  always @(posedge clk) if (we) mem[wa] <= wd;
  assign q = mem[ra];
endmodule
EOF
cat > "$W/rtop.prp" <<'EOF'
const rsub = import("lg:rsub")
mod rtop(wa:U2, wd:U8, ra:U2) -> (q:U8@[0], p:U8@[0]) {
  q = rsub(clk=0, we=0, wa=wa, wd=wd, ra=ra)
  p = rsub(0, 0, wa, wd, ra)
}
EOF
"$LHD" compile "$W/rsub.v" --top rsub --emit-dir lg:"$W/LR/" --workdir "$W/w7a" -q || fail "rsub.v compile→lg failed"
"$LHD" compile --top rtop lg:"$W/LR/" "$W/rtop.prp" --emit verilog:"$W/rtop.v" --workdir "$W/w7b" -q \
  --result-json "$W/r7.json" || fail "idle-port const clock refused: $(cat "$W/r7.json" 2>/dev/null)"

echo "lhd_lg_merge_test passed"
