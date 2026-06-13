#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# Task 1m — `pub` exports + `import()` (the LiveHD docs):
#  1. two-invocation flow: elaborate the exporter into an ln: dir (manifest
#     pub index + `<unit>.__pub` wrapper), then compile an importer against it
#     (pub-tuple values/bundles, lambda field calls inside a comb, `ln:` url
#     import, `equals` tree-url identity — all comptime-verified);
#  2. same-invocation multi-file convergence in the worst file order
#     (iterate-until-converged, incl. a 3-deep import chain);
#  3. failure modes: missing unit, true import cycle (no-progress), and a
#     pub value that is not comptime-foldable;
#  4. liveness: a dead-branch import of a missing unit is NOT an error.

set -u

LHD=lhd/lhd
W="${TEST_TMPDIR:-/tmp/lhd_import_$$}"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

# ── fixtures ──────────────────────────────────────────────────────────────────
cat > "$W/explib.prp" <<'EOF'
pub const magic = 3*4
pub const cfg = (const gain=2, const shift=1)
pub comb add1(a:u8) -> (r:u9) { r = a + 1 }
const local_only = 7
EOF

cat > "$W/consumer.prp" <<'EOF'
const b = import("explib")
cassert(b.magic == 12)
cassert(b.cfg.gain == 2)
const bar2 = import("ln:explib.add1")
comb main(x:u8) -> (y:u9) {
  y = b.add1(a=x)
}
cassert(b.add1 equals bar2)
cassert(bar2(a=3) == 4)
EOF

# ── 1. two-invocation: elaborate exporter, import from the ln: dir ────────────
"$LHD" elaborate "$W/explib.prp" --emit-dir ln:"$W/explib_ln/" --workdir "$W/w1" -q --result-json "$W/r1.json" \
  || fail "exporter elaborate failed: $(cat "$W/r1.json" 2>/dev/null)"
grep -q '"status":"pass"' "$W/r1.json" || fail "exporter result not pass: $(cat "$W/r1.json")"

MAN="$W/explib_ln/manifest.json"
[ -f "$MAN" ] || fail "no manifest emitted"
grep -q '"name":"explib.__pub"' "$MAN" || fail "manifest misses the __pub wrapper unit: $(cat "$MAN")"
grep -q '"unit_kind":"pub"' "$MAN" || fail "wrapper unit_kind missing: $(cat "$MAN")"
grep -q '"name":"magic","kind":"value"' "$MAN" || fail "pub index misses value entry: $(cat "$MAN")"
grep -q '"name":"add1","kind":"comb","url":"ln:explib.add1"' "$MAN" || fail "pub index misses lambda url: $(cat "$MAN")"
grep -q '"name":"local_only"' "$MAN" && fail "non-pub const leaked into the pub index: $(cat "$MAN")"

"$LHD" compile "$W/consumer.prp" ln:"$W/explib_ln" \
  --set upass.verifier=true --set upass.verifier_pass=4 --set upass.verifier_fail=0 \
  --workdir "$W/w2" -q --result-json "$W/r2.json" \
  || fail "two-invocation import failed: $(cat "$W/r2.json" 2>/dev/null)"
grep -q '"status":"pass"' "$W/r2.json" || fail "importer result not pass: $(cat "$W/r2.json")"

# ── 2. same-invocation, worst order (importer first → round 2 resolves) ───────
"$LHD" compile "$W/consumer.prp" "$W/explib.prp" \
  --set upass.verifier=true --set upass.verifier_pass=4 --set upass.verifier_fail=0 \
  --workdir "$W/w3" -q --result-json "$W/r3.json" \
  || fail "same-invocation worst-order import failed: $(cat "$W/r3.json" 2>/dev/null)"
grep -q '"status":"pass"' "$W/r3.json" || fail "worst-order result not pass: $(cat "$W/r3.json")"

# 3-deep chain, worst order: a imports b imports c.
cat > "$W/chain_a.prp" <<'EOF'
const m = import("chain_b")
pub const top_val = m.mid_val + 1
cassert(top_val == 22)
EOF
cat > "$W/chain_b.prp" <<'EOF'
const base = import("chain_c")
pub const mid_val = base.base_val * 2 + 1
EOF
cat > "$W/chain_c.prp" <<'EOF'
pub const base_val = 10
EOF
"$LHD" compile "$W/chain_a.prp" "$W/chain_b.prp" "$W/chain_c.prp" \
  --set upass.verifier=true --set upass.verifier_pass=1 --set upass.verifier_fail=0 \
  --workdir "$W/w4" -q --result-json "$W/r4.json" \
  || fail "3-deep chain failed: $(cat "$W/r4.json" 2>/dev/null)"
grep -q '"status":"pass"' "$W/r4.json" || fail "chain result not pass: $(cat "$W/r4.json")"

# ── 3. failure modes ──────────────────────────────────────────────────────────
# Missing unit: hard error naming the import string.
"$LHD" compile "$W/consumer.prp" --workdir "$W/w5" -q --result-json "$W/r5.json" 2>/dev/null
[ $? -ne 0 ] || fail "missing-unit import must exit non-zero"
grep -q 'unresolved import' "$W/r5.json" || fail "expected unresolved-import error: $(cat "$W/r5.json")"

# True cycle: both files block every round → no-progress error.
cat > "$W/cyc_a.prp" <<'EOF'
const x = import("cyc_b")
pub const va = 1
EOF
cat > "$W/cyc_b.prp" <<'EOF'
const y = import("cyc_a")
pub const vb = 2
EOF
"$LHD" compile "$W/cyc_a.prp" "$W/cyc_b.prp" --workdir "$W/w6" -q --result-json "$W/r6.json" 2>/dev/null
[ $? -ne 0 ] || fail "import cycle must exit non-zero"
grep -q 'blocked on unresolved import' "$W/r6.json" || fail "expected no-progress error: $(cat "$W/r6.json")"

# Non-foldable pub value: rejected when the EXPORTING file elaborates.
printf 'pub const bad = 0sb?\n' > "$W/nf.prp"
"$LHD" elaborate "$W/nf.prp" --emit-dir ln:"$W/nf_ln/" --workdir "$W/w7" -q --result-json "$W/r7.json" 2>/dev/null
[ $? -ne 0 ] || fail "non-foldable pub value must exit non-zero"
grep -q 'not comptime-foldable' "$W/r7.json" || fail "expected pub-not-comptime error: $(cat "$W/r7.json")"

# Ambiguous unit across two ln: inputs (§2): importing it errors, but a
# non-imported collision is tolerated.
mkdir -p "$W/dupA" "$W/dupB"
printf 'pub const v = 1\n' > "$W/dupA/dup.prp"
printf 'pub const v = 2\n' > "$W/dupB/dup.prp"
"$LHD" elaborate "$W/dupA/dup.prp" --emit-dir ln:"$W/dupA_ln/" --workdir "$W/w8a" -q 2>/dev/null
"$LHD" elaborate "$W/dupB/dup.prp" --emit-dir ln:"$W/dupB_ln/" --workdir "$W/w8b" -q 2>/dev/null
printf 'const b = import("dup")\ncassert(b.v == 1)\n' > "$W/imp_dup.prp"
"$LHD" compile "$W/imp_dup.prp" ln:"$W/dupA_ln" ln:"$W/dupB_ln" --set upass.verifier=true \
  --workdir "$W/w8" -q --result-json "$W/r8.json" 2>/dev/null
[ $? -ne 0 ] || fail "ambiguous import must exit non-zero"
grep -q 'ambiguous import' "$W/r8.json" || fail "expected ambiguity error: $(cat "$W/r8.json")"
# Same two inputs, but nobody imports `dup` → tolerated.
printf 'const k = 5\ncassert(k == 5)\n' > "$W/no_imp.prp"
"$LHD" compile "$W/no_imp.prp" ln:"$W/dupA_ln" ln:"$W/dupB_ln" --set upass.verifier=true \
  --workdir "$W/w8c" -q --result-json "$W/r8c.json" 2>/dev/null \
  || fail "non-imported collision must be tolerated: $(cat "$W/r8c.json" 2>/dev/null)"
grep -q '"status":"pass"' "$W/r8c.json" || fail "non-imported collision should pass: $(cat "$W/r8c.json")"

# ── 4. mod instantiation through an import tuple → Sub instance + Verilog ────
cat > "$W/modlib.prp" <<'EOF'
pub mod scale(a:u8) -> (r:u9@[1]) { reg racc:u9 = 0; r = racc; racc = a + a }
EOF
cat > "$W/modcons.prp" <<'EOF'
const lib = import("modlib")
mod top(x:u8) -> (y:u9@[1]) {
  y = lib.scale(a=x)
}
EOF
"$LHD" compile "$W/modcons.prp" "$W/modlib.prp" --top modcons.top --recipe O0 \
  --emit verilog:"$W/modc.v" --workdir "$W/w9" -q --result-json "$W/r9.json" \
  || fail "mod-via-import lowering failed: $(cat "$W/r9.json" 2>/dev/null)"
grep -q '"status":"pass"' "$W/r9.json" || fail "mod-via-import result not pass: $(cat "$W/r9.json")"
grep -q 'modlib.scale  *u_scale' "$W/modc.v" || fail "expected a \\modlib.scale Sub instance: $(cat "$W/modc.v")"

# ── 5. liveness: a dead-branch import never resolves (and never errors) ──────
cat > "$W/dead.prp" <<'EOF'
const use_ext = false
if use_ext {
  const b = import("missing_unit")
  cassert(b.x == 1)
}
cassert(true)
EOF
"$LHD" compile "$W/dead.prp" --set upass.verifier=true --workdir "$W/w8" -q --result-json "$W/r8.json" \
  || fail "dead-branch import must not error: $(cat "$W/r8.json" 2>/dev/null)"
grep -q '"status":"pass"' "$W/r8.json" || fail "dead-branch result not pass: $(cat "$W/r8.json")"

# ── 6. 2j: closure-capture of an imported read-only tuple ────────────────────
# Nested and top-level combs reference outer-scope names bound to an imported
# tuple. func_extract snapshots the import into each extracted body, so the
# imported value-field / scalar reads fold INSIDE the extracted unit — the
# import-aware half of the closure-capture rule (ex-todo/ entry 2j).
cat > "$W/caplib.prp" <<'EOF'
pub const cfg = (const gain=3, const offset=5)
pub const k = 100
EOF
cat > "$W/capuse.prp" <<'EOF'
const lib = import("caplib")
comb outer(x:u8) -> (r:u12) {                       // NESTED comb captures lib
  comb inner(y:u8) -> (r2:u12) { r2 = y * lib.cfg.gain + lib.cfg.offset }
  r = inner(y=x)
}
comb apply(x:u8) -> (r:u12) { r = x * lib.cfg.gain + lib.cfg.offset }  // tuple value
comb addk(x:u8)  -> (r:u9)  { r = x + lib.k }                          // scalar const
cassert(outer(x=10) == 35)
cassert(apply(x=2)  == 11)
cassert(addk(x=5)   == 105)
EOF
# same-invocation, worst order (importer first → capture resolves after a retry)
"$LHD" compile "$W/capuse.prp" "$W/caplib.prp" \
  --set upass.verifier=true --set upass.verifier_pass=3 --set upass.verifier_fail=0 \
  --workdir "$W/w10" -q --result-json "$W/r10.json" \
  || fail "2j capture (same-invocation) failed: $(cat "$W/r10.json" 2>/dev/null)"
grep -q '"status":"pass"' "$W/r10.json" || fail "2j capture same-invocation not pass: $(cat "$W/r10.json")"
# two-invocation: capture an imported tuple from a pre-elaborated ln: dir
"$LHD" elaborate "$W/caplib.prp" --emit-dir ln:"$W/caplib_ln/" --workdir "$W/w11" -q \
  || fail "2j capture exporter elaborate failed"
"$LHD" compile "$W/capuse.prp" ln:"$W/caplib_ln" \
  --set upass.verifier=true --set upass.verifier_pass=3 --set upass.verifier_fail=0 \
  --workdir "$W/w12" -q --result-json "$W/r12.json" \
  || fail "2j capture (two-invocation) failed: $(cat "$W/r12.json" 2>/dev/null)"
grep -q '"status":"pass"' "$W/r12.json" || fail "2j capture two-invocation not pass: $(cat "$W/r12.json")"

echo "PASS: pub/import flows (two-invocation, worst-order convergence, chain, cycle/missing/non-foldable errors, dead-branch liveness, 2j closure-capture of imported tuple)"
