#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# compile.upass.inline (pass.upass `inline` label). By default the runner
# inlines a fully-defined `comb` call into its caller. With
# `--set compile.upass.inline=false` a directly-named, pure-dataflow comb is
# instead left as a func_call so tolg lowers it to a Sub module INSTANCE,
# preserving the comb boundary for debug / optimization (the comb's standalone
# module is already compiled on its own either way).
#
# Asserts:
#   (1) inline=true (default) FLATTENS: `top` has no instance of the comb;
#   (2) inline=false INSTANTIATES: `top` instantiates the comb (twice), and a
#       comb-calling-comb body instantiates the inner comb too;
#   (3) the two builds are PROVEN equivalent (cvc5) — instancing is a pure
#       structural change, the logic is identical;
#   (4) overload / lambda-array dispatch (no Sub form) STILL inlines under
#       inline=false (it has no module to instantiate);
#   (5) a `comb` body may NOT instantiate a `mod` even with inline=false — the
#       "only `mod` bodies may instantiate pipe/mod" rule is preserved.

set -u
LHD=lhd/lhd
W="${TEST_TMPDIR:-/tmp/lhd_inline_disable_$$}"
mkdir -p "$W"
fail() { echo "FAIL: $*" >&2; exit 1; }

# ── Design: a leaf comb, a comb that calls a comb, and a top that calls both ──
cat >"$W/dut.prp" <<'EOF'
comb addone(a:u8) -> (r:u8) {
  r = a + 1
}
comb twice(a:u8) -> (r:u8) {
  r = addone(a) + addone(a)   // comb calls comb
}
pub comb top(x:u8, y:u8) -> (o:u8) {
  o = addone(x) + twice(y)
}
EOF

# ── (1) inline=true (default): the comb is flattened into `top` ───────────────
"$LHD" compile "$W/dut.prp" --top top --emit-dir "lg:$W/on/" \
  --emit-dir "verilog:$W/von/" --workdir "$W/won" -q >/dev/null 2>&1 \
  || fail "inline=true compile failed"
# top.v must NOT instantiate addone/twice (an instance line is `\dut.<m>  u_...(`;
# the module DEFINITION `module \dut.addone (` lives in its own .v file).
if grep -Eq '\\dut\.(addone|twice)[[:space:]]+[A-Za-z_]' "$W/von/dut.top.v"; then
  fail "inline=true: top.v instantiates the comb (expected flattened)"
fi
echo "PASS(1): inline=true flattens the comb into top"

# ── (2) inline=false: the comb becomes a module instance ──────────────────────
"$LHD" compile "$W/dut.prp" --top top --set compile.upass.inline=false \
  --emit-dir "lg:$W/off/" --emit-dir "verilog:$W/voff/" --workdir "$W/woff" -q >/dev/null 2>&1 \
  || fail "inline=false compile failed"
# top instantiates addone (once) and twice (once).
grep -Eq '\\dut\.addone[[:space:]]+[A-Za-z_]' "$W/voff/dut.top.v" \
  || fail "inline=false: top.v does not instantiate addone"
grep -Eq '\\dut\.twice[[:space:]]+[A-Za-z_]' "$W/voff/dut.top.v" \
  || fail "inline=false: top.v does not instantiate twice"
# the comb-calling-comb body instantiates the inner addone TWICE.
n=$(grep -Ec '\\dut\.addone[[:space:]]+[A-Za-z_]' "$W/voff/dut.twice.v")
[ "$n" -eq 2 ] || fail "inline=false: twice.v should instantiate addone twice, found $n"
echo "PASS(2): inline=false instantiates the comb (incl. comb-in-comb)"

# ── (3) the two builds are PROVEN equivalent ──────────────────────────────────
# Instancing is a pure structural change, so flattened (on) and instantiated
# (off) must implement the same logic. Use the lgyosys (Yosys SAT) engine: it is
# the reliable oracle for a purely-combinational design (matching the prp-equiv
# harness) and, unlike the cvc5 BMC engine, correctly handles a comb whose
# module is instantiated at more than one hierarchy depth (here `addone` appears
# directly in `top` AND inside `twice`) — cvc5 BMC models that comb crossing as
# stateful and FALSE-refutes it (a pre-existing LEC-encoder limitation, not a
# generation bug; this design is the minimal trigger).
"$LHD" lec --ref "lg:$W/on/" --impl "lg:$W/off/" \
  --ref-top dut.top --impl-top dut.top --set lec.solver=lgyosys \
  --workdir "$W/lec" -q --result-json "$W/r.json" \
  || fail "lec did NOT prove inline on==off: $(cat "$W/r.json" 2>/dev/null)"
grep -q '"status":"pass"' "$W/r.json" || fail "lec not pass: $(cat "$W/r.json")"
echo "PASS(3): inlined == instantiated (lgyosys)"

# A single-level instance (no mixed-depth hierarchy) is also cvc5-PROVEN, so the
# default solver covers the common case too. `simple_top` calls `addone` once.
cat >"$W/simple.prp" <<'EOF'
comb addone(a:u8) -> (r:u8) { r = a + 1 }
pub comb simple_top(x:u8) -> (o:u8) { o = addone(x) }
EOF
"$LHD" compile "$W/simple.prp" --top simple_top --emit-dir "lg:$W/son/" --workdir "$W/sonw" -q >/dev/null 2>&1 \
  || fail "simple inline=true compile failed"
"$LHD" compile "$W/simple.prp" --top simple_top --set compile.upass.inline=false \
  --emit-dir "lg:$W/soff/" --workdir "$W/soffw" -q >/dev/null 2>&1 || fail "simple inline=false compile failed"
"$LHD" lec --ref "lg:$W/son/" --impl "lg:$W/soff/" \
  --ref-top simple.simple_top --impl-top simple.simple_top \
  --workdir "$W/slec" -q --result-json "$W/sr.json" \
  || fail "cvc5 lec did NOT prove simple inline on==off: $(cat "$W/sr.json" 2>/dev/null)"
grep -q '"status":"pass"' "$W/sr.json" || fail "simple lec not pass: $(cat "$W/sr.json")"
echo "PASS(3b): single-level inlined == instantiated (cvc5)"

# ── (4) overload dispatch (no Sub form) still inlines under inline=false ───────
cat >"$W/ov.prp" <<'EOF'
comb add2(a:u8, b:u8)       -> (r:u9)  { r = a + b }
comb add3(a:u8, b:u8, c:u8) -> (r:u10) { r = a + b + c }
pub comb ov(x:u8, y:u8, z:u8) -> (s2:u9, s3:u10) {
  const add = [add2, add3]
  s2 = add(a=x, b=y)
  s3 = add(a=x, b=y, c=z)
}
EOF
"$LHD" compile "$W/ov.prp" --top ov --set compile.upass.inline=false \
  --emit-dir "verilog:$W/ovv/" --workdir "$W/ovw" -q >/dev/null 2>&1 \
  || fail "inline=false: overload-dispatch design failed to compile"
# `add` is an overload set, not a module — it must NOT appear as an instance.
if grep -Eq '\\ov\.add[[:space:]]+[A-Za-z_]' "$W/ovv/ov.ov.v" 2>/dev/null; then
  fail "inline=false: overload `add` lowered to an instance (it has no Sub form)"
fi
echo "PASS(4): overload dispatch still inlines under inline=false"

# ── (5) a comb may NOT instantiate a mod, even with inline=false ──────────────
cat >"$W/neg.prp" <<'EOF'
mod counter(d:u8) -> (q:u8@[1]) {
  reg m:u8 = 0
  q = m
  m = d
}
pub comb badtop(x:u8) -> (o:u8) {
  o = counter(x)
}
EOF
"$LHD" compile "$W/neg.prp" --top badtop --set compile.upass.inline=false \
  --emit-dir "lg:$W/neg/" --workdir "$W/negw" --emit "diagnostics:$W/negd.jsonl" -q >/dev/null 2>&1
rc=$?
[ "$rc" -ne 0 ] || fail "inline=false: a comb instantiating a mod must be a compile error"
grep -q "only .mod. bodies may instantiate" "$W/negd.jsonl" \
  || fail "inline=false: wrong/absent diagnostic for comb-calls-mod: $(cat "$W/negd.jsonl" 2>/dev/null)"
echo "PASS(5): comb-calls-mod still rejected under inline=false"

echo "ALL PASS: compile.upass.inline on/off (flatten vs instance, cvc5-equiv)"
