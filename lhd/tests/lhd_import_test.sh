#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# Task 1m — `pub` exports + `import()` (docs/contracts/task_1m_plan.md):
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
pub const cfg = (gain=2, shift=1)
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

echo "PASS: pub/import flows (two-invocation, worst-order convergence, chain, cycle/missing/non-foldable errors, dead-branch liveness)"
