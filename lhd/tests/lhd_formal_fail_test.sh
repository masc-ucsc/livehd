#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# 2f-formal FAIL policy: a refuted (definitive counterexample) formal property —
# assert / assume / Hotmux one-hotness — is RECORDED as a build error and the
# build fails (exit != 0), but the compile CONTINUES so cgen still emits the
# design with the failing check kept as a runtime check (never elided, never
# used to optimize). The diagnostic carries the counterexample and a hint that a
# different top-level instantiation may change the result. pass.formal runs as a
# none|fast|normal mode step in `lhd compile` (default fast; none under -O0): fast
# = induction (catches combinational refutations, defers stateful ones to runtime);
# normal = BMC-intent (also trusts stateful refutations). Cases 1-4 use --recipe
# O2 (which also runs cprop/bitwidth); cases 5-7 pin the mode behavior.

set -u

LHD=lhd/lhd
W="${TEST_TMPDIR:-/tmp/lhd_formal_fail_$$}"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

# compile_o2 <name> : compile $W/<name>.prp through O2 + emit verilog/diag.
# Sets globals: RC, DIAG (jsonl path), VOUT (verilog path).
compile_o2() {
  local n="$1"
  DIAG="$W/$n.jsonl"
  VOUT="$W/$n.v"
  "$LHD" compile "$W/$n.prp" --recipe O2 --workdir "$W/$n" \
    --emit "verilog:$VOUT" --emit "diagnostics:$DIAG" >/dev/null 2>&1
  RC=$?
}

# compile_case <prp> <tag> <extra args...> : compile $W/<prp>.prp tagged <tag>
# (no --recipe, so the default fast formal mode runs unless overridden).
# Sets globals: RC, DIAG, VOUT.
compile_case() {
  local prp="$1" tag="$2"
  shift 2
  DIAG="$W/$tag.jsonl"
  VOUT="$W/$tag.v"
  "$LHD" compile "$W/$prp.prp" --workdir "$W/$tag" \
    --emit "verilog:$VOUT" --emit "diagnostics:$DIAG" "$@" >/dev/null 2>&1
  RC=$?
}

# ---------------------------------------------------------------------------
# 1. A refuted `assert` over free inputs: FAIL recorded, build fails, but the
#    netlist is still emitted with the runtime assert KEPT.
# ---------------------------------------------------------------------------
cat >"$W/assert_fail.prp" <<'EOF'
comb chk(a:u8, b:u8) -> (x:u8) {
  assert(a != b, "a and b must differ")
  x = a + b
}
EOF
compile_o2 assert_fail
[ "$RC" -ne 0 ] || fail "refuted assert must fail the build (got rc=0)"
grep -q '"code":"assert-refuted"' "$DIAG" || fail "missing assert-refuted diagnostic: $(cat "$DIAG")"
grep -q 'counterexample:' "$DIAG" || fail "assert-refuted must include a counterexample: $(cat "$DIAG")"
grep -q 'a and b must differ' "$DIAG" || fail "assert-refuted must carry the user message: $(cat "$DIAG")"
grep -q 'different top-level instantiation' "$DIAG" || fail "assert-refuted must hint at a different top: $(cat "$DIAG")"
[ -s "$VOUT" ] || fail "compile must CONTINUE and still emit the netlist on a refuted assert"
grep -q 'assert (' "$VOUT" || fail "the failing assert must be KEPT as a runtime check in the netlist: $(cat "$VOUT")"
grep -q 'a and b must differ' "$VOUT" || fail "runtime assert must keep its \$error message: $(cat "$VOUT")"

# ---------------------------------------------------------------------------
# 2. A refuted `assume`: same FAIL policy. A refuted assume must NOT be turned
#    into a synthesis hypothesis; it is kept as a runtime contract check.
# ---------------------------------------------------------------------------
cat >"$W/assume_fail.prp" <<'EOF'
comb chk(a:u8, b:u8) -> (x:u8) {
  assume(a != b)
  x = a + b
}
EOF
compile_o2 assume_fail
[ "$RC" -ne 0 ] || fail "refuted assume must fail the build (got rc=0)"
grep -q '"code":"assume-refuted"' "$DIAG" || fail "missing assume-refuted diagnostic: $(cat "$DIAG")"
[ -s "$VOUT" ] || fail "compile must CONTINUE and still emit the netlist on a refuted assume"
grep -q 'assume (' "$VOUT" || fail "the failing assume must be KEPT as a runtime check in the netlist: $(cat "$VOUT")"

# ---------------------------------------------------------------------------
# 3. An overlapping `unique if` (Hotmux one-hotness refuted): the old hard
#    .fatal() path is gone — same FAIL policy (record + continue + emit).
# ---------------------------------------------------------------------------
cat >"$W/hotmux_fail.prp" <<'EOF'
comb chk(p:bool, q:bool) -> (y:u8) {
  mut y = 0
  unique if p { y = 1 } elif q { y = 2 }
}
EOF
compile_o2 hotmux_fail
[ "$RC" -ne 0 ] || fail "refuted Hotmux one-hotness must fail the build (got rc=0)"
grep -q '"code":"onehot-violated"' "$DIAG" || fail "missing onehot-violated diagnostic: $(cat "$DIAG")"
[ -s "$VOUT" ] || fail "compile must CONTINUE (not fatal-abort) and still emit on a refuted Hotmux"

# ---------------------------------------------------------------------------
# 4. A provably one-hot `unique if` (distinct constant arms): PROVEN, so it
#    compiles clean (no error) and the netlist is emitted. Proven is the only
#    path that may elide/optimize.
# ---------------------------------------------------------------------------
cat >"$W/onehot_ok.prp" <<'EOF'
comb chk(x:u2, a:u8, b:u8) -> (y:u8) {
  mut y = 0
  unique if x == 0 { y = a } elif x == 1 { y = b }
}
EOF
compile_o2 onehot_ok
[ "$RC" -eq 0 ] || fail "a provably one-hot unique-if must compile clean (got rc=$RC): $(cat "$DIAG")"
grep -q '"severity":"error"' "$DIAG" && fail "proven one-hot must emit no error: $(cat "$DIAG")"
[ -s "$VOUT" ] || fail "proven one-hot must still emit the netlist"

# ---------------------------------------------------------------------------
# 5. DEFAULT `lhd compile` (no --recipe) runs formal in `fast` mode: a purely
#    COMBINATIONAL refuted assert is caught (induction is exact for comb logic).
# ---------------------------------------------------------------------------
cat >"$W/comb_ref.prp" <<'EOF'
comb chk(a:u8, b:u8) -> (x:u8) {
  assert(a != b)
  x = a + b
}
EOF
compile_case comb_ref comb_default
[ "$RC" -ne 0 ] || fail "default (fast) compile must catch a combinational refuted assert (got rc=0): $(cat "$DIAG")"
grep -q '"code":"assert-refuted"' "$DIAG" || fail "default fast must record assert-refuted: $(cat "$DIAG")"
[ -s "$VOUT" ] || fail "default fast must CONTINUE and still emit the netlist"

# ---------------------------------------------------------------------------
# 6. `--set compile.formal.mode=none` skips the pass entirely: the same refuted
#    assert compiles clean (the runtime check is still emitted, just unverified).
# ---------------------------------------------------------------------------
compile_case comb_ref comb_none --set compile.formal.mode=none
[ "$RC" -eq 0 ] || fail "mode=none must skip formal and compile clean (got rc=$RC): $(cat "$DIAG")"
grep -q '"code":"assert-refuted"' "$DIAG" && fail "mode=none must NOT run the formal check: $(cat "$DIAG")"
[ -s "$VOUT" ] || fail "mode=none must still emit the netlist"

# ---------------------------------------------------------------------------
# 7. A SEQUENTIAL (flop-cut) refutation: `fast`/induction can only find the
#    counterexample over free register state, which may be unreachable, so it
#    DEFERS to a runtime check; `normal` (BMC-intent) trusts it and fails.
# ---------------------------------------------------------------------------
cat >"$W/seq_ref.prp" <<'EOF'
mod chk(d:u4) -> (q:u4@[1]) {
  reg r:u4 = 0
  assert(r != 5)
  r = d
  q = r
}
EOF
compile_case seq_ref seq_fast --top chk
[ "$RC" -eq 0 ] || fail "fast must DEFER a stateful refutation, not fail the build (got rc=$RC): $(cat "$DIAG")"
grep -q '"code":"assert-deferred"' "$DIAG" || fail "fast must warn assert-deferred on a stateful refutation: $(cat "$DIAG")"
[ -s "$VOUT" ] || fail "fast must still emit the netlist on a deferred stateful refutation"

compile_case seq_ref seq_normal --top chk --set compile.formal.mode=normal
[ "$RC" -ne 0 ] || fail "normal must CATCH the stateful refutation (got rc=0): $(cat "$DIAG")"
grep -q '"code":"assert-refuted"' "$DIAG" || fail "normal must record assert-refuted for the stateful refutation: $(cat "$DIAG")"

# ---------------------------------------------------------------------------
# 8. "Not enough top": a refuted assert in a SUBMODULE (instantiated by a parent
#    that constrains its inputs) is a NON-ERROR — reported as a loud DEFERRED
#    warning, never a build failure. The SAME module compiled ALONE (a genuine
#    root / design boundary) DOES fail. (don't mask, but don't false-fail.)
# ---------------------------------------------------------------------------
cat >"$W/hier_ref.prp" <<'EOF'
mod leaf(a:u4) -> (b:u4@[0]) {
  assert(a != 5)
  b = a + 1
}
mod root(c:u4) -> (d:u4@[0]) {
  const s = leaf(a = 3)
  d = s
}
EOF
compile_case hier_ref hier_top --top root
[ "$RC" -eq 0 ] || fail "a submodule refutation (not enough top) must NOT fail the build (got rc=$RC): $(cat "$DIAG")"
grep -q '"code":"assert-deferred"' "$DIAG" || fail "submodule refutation must emit a deferred warning: $(cat "$DIAG")"
grep -q 'not enough top' "$DIAG" || fail "deferred warning must explain the missing top context: $(cat "$DIAG")"
grep -q '"code":"assert-refuted"' "$DIAG" && fail "a non-top submodule refutation must NOT be a build-failing error: $(cat "$DIAG")"

cat >"$W/leaf_solo.prp" <<'EOF'
mod leaf(a:u4) -> (b:u4@[0]) {
  assert(a != 5)
  b = a + 1
}
EOF
compile_case leaf_solo leaf_solo --top leaf
[ "$RC" -ne 0 ] || fail "the same module compiled ALONE (a real root) must fail on the refuted assert (got rc=0): $(cat "$DIAG")"
grep -q '"code":"assert-refuted"' "$DIAG" || fail "a root refutation must be a build-failing error: $(cat "$DIAG")"

# ---------------------------------------------------------------------------
# 9. on_refute=warn DOWNGRADES even a confirmed (root, combinational) refutation
#    to a loud warning instead of failing the build — the escape hatch when a
#    design is checked without enough top context. (A proven property is always
#    sound; only a 'fail' can be spurious, so only a 'fail' is downgradable.)
# ---------------------------------------------------------------------------
cat >"$W/downgrade_ref.prp" <<'EOF'
comb chk(a:u8, b:u8) -> (x:u8) {
  assert(a != b)
  x = a + b
}
EOF
compile_case downgrade_ref downgrade_warn --set compile.formal.on_refute=warn
[ "$RC" -eq 0 ] || fail "on_refute=warn must downgrade a refutation to a warning (got rc=$RC): $(cat "$DIAG")"
grep -q '"code":"assert-deferred"' "$DIAG" || fail "on_refute=warn must emit a deferred warning: $(cat "$DIAG")"
grep -q 'on_refute=warn' "$DIAG" || fail "the downgrade warning must name the on_refute=warn knob: $(cat "$DIAG")"
grep -q '"severity":"error"' "$DIAG" && fail "on_refute=warn must NOT emit any error: $(cat "$DIAG")"

echo "PASS: 2f-formal FAIL policy (record+continue+emit; proven clean; modes; not-enough-top defers; on_refute downgrade)"
