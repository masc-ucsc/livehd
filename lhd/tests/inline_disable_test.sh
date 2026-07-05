#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# compile.upass.inline (pass.upass `inline` label). Inlining a fully-defined
# `comb` is OFF by default: a directly-named comb whose call has runtime
# arguments is emitted as a Sub module INSTANCE, preserving the comb boundary
# for debug/optimization (its standalone module is compiled either way). The O2
# recipe — or an explicit `--set compile.upass.inline=true` — flattens by
# inlining. A comb call with all-COMPTIME-CONSTANT arguments still inlines (so it
# folds to a value) regardless of the flag.
#
# Asserts:
#   (1) default (inline off) INSTANTIATES: `top` instantiates the comb (twice),
#       and a comb-calling-comb body instantiates the inner comb too;
#   (2) `--recipe O2` and `--set compile.upass.inline=true` both FLATTEN: `top`
#       has no instance of the comb;
#   (3) the default and flattened builds are PROVEN equivalent (logic is the
#       same; instancing is a pure structural change);
#   (4) a const-argument comb call still FOLDS at the default (comptime
#       evaluation / casserts keep working — no runtime instance to preserve);
#   (5) overload / lambda-array dispatch (no Sub form) still inlines;
#   (6) a `comb` body may NOT instantiate a `mod` — the "only `mod` bodies may
#       instantiate pipe/mod" rule is preserved.

set -u
LHD=lhd/lhd
W="${TEST_TMPDIR:-/tmp/lhd_inline_disable_$$}"
mkdir -p "$W"
fail() { echo "FAIL: $*" >&2; exit 1; }

# instance line in a module body is `\dut.<m>  u_...(`; the module DEFINITION
# `module addone (` lives in that module's own .v file, so grepping the PARENT's
# .v for the callee name finds only instances. The instance TYPE is the FLAT
# Verilog module name (`addone`, at line start); its instance NAME may be an
# escaped `\u_dut.addone…`, so allow an optional `\` before the instance id.
has_inst() { grep -Eq '^'"$2"'[[:space:]]+\\?[A-Za-z_]' "$1"; }

# ── Design: a leaf comb, a comb that calls a comb, and a top that calls both,
#    all with RUNTIME arguments (the module inputs) — the hardware path. ────────
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

# ── (1) default (inline OFF): the comb becomes a module instance ──────────────
"$LHD" compile "$W/dut.prp" --top top \
  --emit-dir "lg:$W/off/" --emit-dir "verilog:$W/voff/" --workdir "$W/woff" -q >/dev/null 2>&1 \
  || fail "default compile failed"
has_inst "$W/voff/dut.top.v" 'addone' || fail "default: top.v does not instantiate addone"
has_inst "$W/voff/dut.top.v" 'twice'  || fail "default: top.v does not instantiate twice"
# the comb-calling-comb body instantiates the inner addone TWICE.
n=$(grep -Ec '^addone[[:space:]]+\\?[A-Za-z_]' "$W/voff/dut.twice.v")
[ "$n" -eq 2 ] || fail "default: twice.v should instantiate addone twice, found $n"
echo "PASS(1): default instantiates the comb (incl. comb-in-comb)"

# ── (2) O2 and explicit inline=true both flatten ──────────────────────────────
"$LHD" compile "$W/dut.prp" --top top --recipe O2 \
  --emit-dir "verilog:$W/vo2/" --workdir "$W/wo2" -q >/dev/null 2>&1 || fail "O2 compile failed"
if has_inst "$W/vo2/dut.top.v" '(addone|twice)'; then
  fail "O2: top.v instantiates the comb (expected flattened)"
fi
"$LHD" compile "$W/dut.prp" --top top --set compile.upass.inline=true \
  --emit-dir "lg:$W/on/" --emit-dir "verilog:$W/von/" --workdir "$W/won" -q >/dev/null 2>&1 \
  || fail "inline=true compile failed"
if has_inst "$W/von/dut.top.v" '(addone|twice)'; then
  fail "inline=true: top.v instantiates the comb (expected flattened)"
fi
echo "PASS(2): --recipe O2 and inline=true flatten the comb into top"

# ── (3) default (instanced) and flattened builds are PROVEN equivalent ────────
# Instancing is a pure structural change. Use the lgyosys (Yosys SAT) engine: it
# is the reliable oracle for a purely-combinational design (matching the
# prp-equiv harness) and, unlike the cvc5 BMC engine, correctly handles a comb
# whose module is instantiated at more than one hierarchy depth (here `addone`
# appears directly in `top` AND inside `twice`) — cvc5 BMC models that comb
# crossing as stateful and FALSE-refutes it (a pre-existing LEC-encoder
# limitation, not a generation bug; this design is the minimal trigger).
"$LHD" lec --ref "lg:$W/on/" --impl "lg:$W/off/" \
  --ref-top dut.top --impl-top dut.top --set lec.solver=lgyosys \
  --workdir "$W/lec" -q --result-json "$W/r.json" \
  || fail "lec did NOT prove flattened==instanced: $(cat "$W/r.json" 2>/dev/null)"
grep -q '"status":"pass"' "$W/r.json" || fail "lec not pass: $(cat "$W/r.json")"
echo "PASS(3): instanced == flattened (lgyosys)"

# A single-level instance (no mixed-depth hierarchy) is also cvc5-PROVEN.
cat >"$W/simple.prp" <<'EOF'
comb addone(a:u8) -> (r:u8) { r = a + 1 }
pub comb simple_top(x:u8) -> (o:u8) { o = addone(x) }
EOF
"$LHD" compile "$W/simple.prp" --top simple_top --set compile.upass.inline=true \
  --emit-dir "lg:$W/son/" --workdir "$W/sonw" -q >/dev/null 2>&1 || fail "simple inline=true compile failed"
"$LHD" compile "$W/simple.prp" --top simple_top \
  --emit-dir "lg:$W/soff/" --workdir "$W/soffw" -q >/dev/null 2>&1 || fail "simple default compile failed"
"$LHD" lec --ref "lg:$W/son/" --impl "lg:$W/soff/" \
  --ref-top simple.simple_top --impl-top simple.simple_top \
  --workdir "$W/slec" -q --result-json "$W/sr.json" \
  || fail "cvc5 lec did NOT prove simple flattened==instanced: $(cat "$W/sr.json" 2>/dev/null)"
grep -q '"status":"pass"' "$W/sr.json" || fail "simple lec not pass: $(cat "$W/sr.json")"
echo "PASS(3b): single-level instanced == flattened (cvc5)"

# ── (4) a const-argument comb call still folds at the default (inline off) ─────
# Only a RUNTIME call produces a Sub instance. `addone(3)` has a comptime
# actual, so it must inline+fold to a constant (there is no runtime hardware to
# preserve) — comptime evaluation keeps working regardless of the flag. With one
# runtime call and one const call to `addone`, exactly ONE instance appears and
# the const call collapses into a literal in the datapath.
cat >"$W/mix.prp" <<'EOF'
comb addone(a:u8) -> (r:u8) { r = a + 1 }
pub comb mix(x:u8) -> (o:u8) {
  o = addone(x) + addone(3)
}
EOF
"$LHD" compile "$W/mix.prp" --top mix \
  --emit-dir "verilog:$W/mixv/" --workdir "$W/mixw" -q >/dev/null 2>&1 || fail "mix compile failed"
m=$(grep -Ec '^addone[[:space:]]+\\?[A-Za-z_]' "$W/mixv/mix.mix.v")
[ "$m" -eq 1 ] || fail "default: expected 1 addone instance (runtime call only), found $m — const-arg call did not fold"
grep -Eq "4'sh4|8'sh04|'h4\b|\+ 4\b" "$W/mixv/mix.mix.v" || fail "default: const-arg addone(3) did not fold to the literal 4"
echo "PASS(4): const-argument comb call still folds at the default (1 instance, +4 literal)"

# ── (5) overload dispatch (no Sub form) still inlines ─────────────────────────
cat >"$W/ov.prp" <<'EOF'
comb add2(a:u8, b:u8)       -> (r:u9)  { r = a + b }
comb add3(a:u8, b:u8, c:u8) -> (r:u10) { r = a + b + c }
pub comb ov(x:u8, y:u8, z:u8) -> (s2:u9, s3:u10) {
  const add = [add2, add3]
  s2 = add(a=x, b=y)
  s3 = add(a=x, b=y, c=z)
}
EOF
"$LHD" compile "$W/ov.prp" --top ov \
  --emit-dir "verilog:$W/ovv/" --workdir "$W/ovw" -q >/dev/null 2>&1 \
  || fail "default: overload-dispatch design failed to compile"
# `add` is an overload set, not a module — it must NOT appear as an instance.
if has_inst "$W/ovv/ov.ov.v" 'add'; then
  fail "default: overload `add` lowered to an instance (it has no Sub form)"
fi
echo "PASS(5): overload dispatch still inlines"

# ── (6) a comb may NOT instantiate a mod ──────────────────────────────────────
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
"$LHD" compile "$W/neg.prp" --top badtop \
  --emit-dir "lg:$W/neg/" --workdir "$W/negw" --emit "diagnostics:$W/negd.jsonl" -q >/dev/null 2>&1
rc=$?
[ "$rc" -ne 0 ] || fail "a comb instantiating a mod must be a compile error"
grep -q "only .mod. bodies may instantiate" "$W/negd.jsonl" \
  || fail "wrong/absent diagnostic for comb-calls-mod: $(cat "$W/negd.jsonl" 2>/dev/null)"
echo "PASS(6): comb-calls-mod still rejected"

echo "ALL PASS: compile.upass.inline (default instance, O2 flatten, const-fold, cvc5/lgyosys-equiv)"
