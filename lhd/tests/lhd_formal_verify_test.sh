#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# 2f-verify V1: `lhd formal verify` — single-design assert/assume BMC on the
# pass/lec engine (lec::prove_properties). Contract under test:
#   * a true STATE invariant (that single-frame induction defers) is PROVEN to
#     the bound, per-assert, with a per-cycle depth in the table;
#   * a reachable violation is REFUTED at its cycle with the per-cycle input
#     trace, carries the user message, and fails the run (exit != 0);
#   * a user `assume` is an ENVIRONMENT CONSTRAINT: it flips the same assert to
#     bounded-proven and is disclosed in the table;
#   * per-obligation timeout isolation: a hard obligation goes UNKNOWN on its
#     own budget while its easy sibling still proves; UNKNOWN is a warning
#     (exit 0) unless formal.strict=true;
#   * knob namespaces: formal.* and the legacy lec.* spelling both work;
#   * `lhd formal lec` is the lec command (behavior-preserving alias);
#   * a design with no obligations is UNKNOWN (never a vacuous PASS).

set -u

LHD=lhd/lhd
W="${TEST_TMPDIR:-/tmp/lhd_formal_verify_$$}"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

# verify <prp> <tag> <extra args...> : run `lhd formal verify` on $W/<prp>.prp.
# Sets globals: RC, OUT (combined stdout+stderr text).
verify() {
  local prp="$1" tag="$2"
  shift 2
  OUT="$W/$tag.out"
  "$LHD" formal verify "$W/$prp.prp" "$@" >"$OUT" 2>&1
  RC=$?
}

# ---------------------------------------------------------------------------
# 1. Parity invariant over two registers: TRUE from reset but NOT provable by
#    the compile-time single-frame induction (pass.formal defers it) — the BMC
#    engine must prove it to the bound. The sibling count!=5 IS reachable:
#    REFUTED at its exact cycle (2 reset-hold + 5 enabled increments) with the
#    driving input trace and the user message.
# ---------------------------------------------------------------------------
cat >"$W/cnt.prp" <<'EOF'
mod cnt(enable:bool) -> (value:u8@[0]) {
  reg count:u8 = 0
  reg par:bool = false
  value = count
  assert(u1(par) == count#[0])
  assert(count != 5, "counter hit 5")
  if enable {
    wrap count += 1
    par = not par
  }
}
EOF
verify cnt cnt --top cnt --set formal.bound=10
[ "$RC" -ne 0 ] || fail "a reachable violation must fail the run (got rc=0): $(cat "$OUT")"
grep -q 'REFUTED$\|REFUTED (' "$OUT" || fail "aggregate verdict must be REFUTED: $(cat "$OUT")"
grep -q 'cnt.prp:5.*PROVEN' "$OUT" || fail "the parity invariant must be PROVEN per-assert: $(cat "$OUT")"
grep -q 'cnt.prp:6.*REFUTED at cycle 7' "$OUT" || fail "count!=5 must be REFUTED at cycle 7: $(cat "$OUT")"
grep -q 'counter hit 5' "$OUT" || fail "the refuted row must carry the user message: $(cat "$OUT")"
grep -q 'counterexample inputs: cyc0:' "$OUT" || fail "REFUTED must print the per-cycle input trace: $(cat "$OUT")"
grep -q 'enable=1' "$OUT" || fail "the trace must drive enable to reach count==5: $(cat "$OUT")"

# The legacy lec.* spelling is an alias for the same knob.
verify cnt cnt_alias --top cnt --set lec.bound=10
[ "$RC" -ne 0 ] || fail "lec.bound alias must behave like formal.bound (got rc=0)"
grep -q 'REFUTED at cycle 7' "$OUT" || fail "lec.bound=10 must reach the cycle-7 refutation: $(cat "$OUT")"

# ---------------------------------------------------------------------------
# 2. `assume` = environment constraint: pruning count<5 makes count!=5 bounded-
#    proven; the table DISCLOSES the assume (verdicts are conditional on it).
# ---------------------------------------------------------------------------
cat >"$W/cnt_assume.prp" <<'EOF'
mod cnt(enable:bool) -> (value:u8@[0]) {
  reg count:u8 = 0
  value = count
  assume(count < 5)
  assert(count != 5, "counter hit 5")
  if enable {
    wrap count += 1
  }
}
EOF
verify cnt_assume assume --top cnt --set formal.bound=10
[ "$RC" -eq 0 ] || fail "under the assume the assert must be bounded-proven (got rc=$RC): $(cat "$OUT")"
grep -q 'PROVEN (bounded)' "$OUT" || fail "aggregate must be PROVEN (bounded): $(cat "$OUT")"
grep -q 'assume.*in force' "$OUT" || fail "the assume must be disclosed in the table: $(cat "$OUT")"
grep -q 'cnt_assume.prp:5.*PROVEN' "$OUT" || fail "count!=5 must be proven under the assume: $(cat "$OUT")"

# ---------------------------------------------------------------------------
# 2b. Assumes are in force at EVERY cycle, reset prologue included (SVA
#     semantics): an assert_always is checked during the prologue too, so an
#     env constraint it depends on must already hold there — without this, the
#     prologue check runs unconstrained and false-refutes at cycle 0.
# ---------------------------------------------------------------------------
cat >"$W/always_env.prp" <<'EOF'
mod always_env(a:u8, en:bool) -> (o:u8@[0]) {
  reg acc:u8 = 0
  o = acc
  assume(a < 4)
  assert_always(a != 200, "env bound")
  if en {
    wrap acc += 1
  }
}
EOF
# The compile gate keeps its normal FAIL policy on the user's design (user
# ruling): a root-module INPUT assume refutes at the gate and fails the load.
verify always_env always_env_gate --top always_env --set formal.bound=4
[ "$RC" -ne 0 ] || fail "a design-inline input assume must hard-fail the load gate (got rc=0): $(cat "$OUT")"
grep -q 'assume-refuted' "$OUT" || fail "the load failure must be the gate's assume-refuted: $(cat "$OUT")"
# The explicit escape hatch runs the deep prover, where the assume is an env
# constraint in force at EVERY cycle, prologue included.
verify always_env always_env --top always_env --set formal.bound=4 --set compile.formal.on_refute=warn
[ "$RC" -eq 0 ] || fail "assert_always under a prologue-relevant assume must prove (got rc=$RC): $(cat "$OUT")"
grep -q 'assert_always.*PROVEN' "$OUT" || fail "assert_always must be proven incl. the prologue: $(cat "$OUT")"

# ---------------------------------------------------------------------------
# 3. Per-obligation timeout isolation: a 32-bit multiply identity blows the 2s
#    budget and goes UNKNOWN at its first checked cycle; the easy sibling still
#    proves. UNKNOWN is a loud warning with exit 0; formal.strict makes it fail.
# ---------------------------------------------------------------------------
cat >"$W/hard.prp" <<'EOF'
mod hard(a:u32, b:u32, c:u32, en:bool) -> (o:u8@[0]) {
  reg acc:u8 = 0
  o = acc
  assert(a + b == b + a, "easy")
  assert((a * b) * ((a * c) + 1) == (a * a * b * c) + (a * b), "distrib")
  if en {
    wrap acc += 1
  }
}
EOF
verify hard hard --top hard --set formal.bound=2 --set formal.timeout=2
[ "$RC" -eq 0 ] || fail "UNKNOWN must be a warning, not a failure (got rc=$RC): $(cat "$OUT")"
grep -q "'easy'.*PROVEN" "$OUT" || fail "the easy sibling must still prove: $(cat "$OUT")"
grep -q "'distrib'.*UNKNOWN (solver gave up at cycle" "$OUT" || fail "the hard obligation must time out ALONE: $(cat "$OUT")"
grep -q 'formal-inconclusive' "$OUT" || fail "UNKNOWN must emit the loud inconclusive warning: $(cat "$OUT")"

verify hard hard_strict --top hard --set formal.bound=2 --set formal.timeout=2 --set formal.strict=true
[ "$RC" -ne 0 ] || fail "formal.strict=true must turn UNKNOWN into a failure (got rc=0): $(cat "$OUT")"

# ---------------------------------------------------------------------------
# 4. No obligations: UNKNOWN with an explicit note — never a vacuous PASS.
# ---------------------------------------------------------------------------
cat >"$W/noprops.prp" <<'EOF'
comb pass_through(a:u8) -> (x:u8) {
  x = a
}
EOF
verify noprops noprops --top pass_through
grep -q 'no assert/assert_always obligations found' "$OUT" || fail "no-obligation run must say so: $(cat "$OUT")"
grep -q 'PROVEN' "$OUT" && fail "no-obligation run must not claim PROVEN: $(cat "$OUT")"

# ---------------------------------------------------------------------------
# 6. V2 formal blocks: a sidecar .prp with `formal name.dotted { ... }` blocks
#    binding the design through a file-scope import alias. The parity block
#    (with a u1() cast and a #[0] bit-select through the rewrite) proves; the
#    speculative block refutes at its exact cycle; rows carry the ORIGINAL
#    sidecar file:line plus the block name; --formal <glob> selects blocks;
#    an unresolvable signal path is a clean usage error.
# ---------------------------------------------------------------------------
cat >"$W/cnt.verify.prp" <<'EOF'
const top = import("cnt.cnt")

formal cnt.parity {
  mut acc = top
  assert(u1(acc.par) == acc.count#[0], "parity tracks bit0")
}

formal cnt.speculative {
  mut acc = top
  assert(acc.count != 3, "counter hit 3")
}
EOF
OUT="$W/blocks.out"
"$LHD" formal verify "$W/cnt.prp" "$W/cnt.verify.prp" --top cnt --set formal.bound=10 >"$OUT" 2>&1
RC=$?
[ "$RC" -ne 0 ] || fail "the speculative block's violation must fail the run (got rc=0): $(cat "$OUT")"
grep -q 'cnt.verify.prp:5.*\[cnt.parity\].*PROVEN' "$OUT" || fail "block parity assert must prove with original loc + block name: $(cat "$OUT")"
grep -q 'cnt.verify.prp:10.*\[cnt.speculative\].*REFUTED at cycle 5' "$OUT" || fail "block count!=3 must refute at cycle 5: $(cat "$OUT")"

OUT="$W/blocks_filter.out"
"$LHD" formal verify "$W/cnt.prp" "$W/cnt.verify.prp" --top cnt --formal 'cnt.parity' --set formal.bound=6 >"$OUT" 2>&1
grep -q '\[cnt.parity\]' "$OUT" || fail "--formal must keep the selected block: $(cat "$OUT")"
grep -q '\[cnt.speculative\]' "$OUT" && fail "--formal must exclude the unselected block: $(cat "$OUT")"

cat >"$W/bad.verify.prp" <<'EOF'
const top = import("cnt.cnt")
formal cnt.bad {
  mut acc = top
  assert(acc.nonexistent_signal == 0)
}
EOF
OUT="$W/blocks_bad.out"
"$LHD" formal verify "$W/cnt.prp" "$W/bad.verify.prp" --top cnt >"$OUT" 2>&1
RC=$?
[ "$RC" -ne 0 ] || fail "an unresolvable block signal path must be an error (got rc=0)"
grep -q "signal path 'nonexistent_signal' does not resolve" "$OUT" || fail "unresolvable path must name the signal: $(cat "$OUT")"

# A block assume prunes like a design assume: freezing enable proves count!=5
# (same design whose unconstrained run refutes it at cycle 7 in case 1).
cat >"$W/frozen.verify.prp" <<'EOF'
const top = import("cnt.cnt")
formal cnt.frozen {
  mut acc = top
  assume(acc.enable == 0)
  assert(acc.count != 5, "frozen counter")
}
EOF
OUT="$W/blocks_frozen.out"
"$LHD" formal verify "$W/cnt.prp" "$W/frozen.verify.prp" --formal 'cnt.frozen' --top cnt --set formal.bound=10 >"$OUT" 2>&1
grep -q '\[cnt.frozen\].*in force' "$OUT" || fail "block assume must be disclosed: $(cat "$OUT")"
grep -q "'frozen counter'.*PROVEN" "$OUT" || fail "block assume must prune the violation: $(cat "$OUT")"

# ---------------------------------------------------------------------------
# 6b. A block may target a SUBMODULE (user ruling): it binds to EVERY instance
#     of that module inside the top, each reported as [block@instance]; a
#     module the top does not instantiate is a clean usage error.
# ---------------------------------------------------------------------------
cat >"$W/hier.prp" <<'HEOF'
mod leafcnt(en:bool) -> (v:u4@[0]) {
  reg c:u4 = 0
  v = c
  if en {
    wrap c += 1
  }
}
mod duo(e0:bool, e1:bool) -> (s:u5@[0]) {
  const a = leafcnt(en = e0)
  const b = leafcnt(en = e1)
  s = a + b
}
HEOF
cat >"$W/hier.verify.prp" <<'HEOF'
const sub = import("hier.leafcnt")
formal leaf.small {
  mut acc = sub
  assert(acc.c != 9, "leaf hit 9")
}
HEOF
OUT="$W/hier.out"
"$LHD" formal verify "$W/hier.prp" "$W/hier.verify.prp" --top duo --set formal.bound=6 >"$OUT" 2>&1
[ $? -eq 0 ] || fail "submodule block within bound must pass (got rc!=0): $(cat "$OUT")"
n_rows=$(grep -c '\[leaf.small@' "$OUT")
[ "$n_rows" -eq 2 ] || fail "the block must bind to BOTH leafcnt instances (got $n_rows rows): $(cat "$OUT")"

cat >"$W/orphan.verify.prp" <<'HEOF'
const sub = import("hier.nosuchmod")
formal leaf.orphan {
  mut acc = sub
  assert(acc.c != 9)
}
HEOF
OUT="$W/orphan.out"
"$LHD" formal verify "$W/hier.prp" "$W/orphan.verify.prp" --top duo >"$OUT" 2>&1
[ $? -ne 0 ] || fail "a block targeting an un-instantiated module must error (got rc=0)"
grep -q 'does not instantiate' "$OUT" || fail "orphan-target error must say so: $(cat "$OUT")"

# ---------------------------------------------------------------------------
# 8. --workdir on a REFUTED run writes formalfail.prp (the lec-lecfail
#    analogue): a self-contained `lhd sim` testbench driving the violating
#    input trace. Generation only (prpfailrun=false keeps the test hermetic —
#    no sim runtime headers needed); the trace arrays and provenance must be
#    in the file.
# ---------------------------------------------------------------------------
WD="$W/wd_formalfail"
mkdir -p "$WD"
OUT="$W/formalfail.out"
"$LHD" formal verify "$W/cnt.prp" --top cnt --set formal.bound=10 --workdir "$WD" \
  --set formal.prpfailrun=false >"$OUT" 2>&1
[ $? -ne 0 ] || fail "the refuted run must still exit non-zero"
grep -q 'wrote counterexample testbench' "$OUT" || fail "--workdir must write formalfail.prp: $(cat "$OUT")"
[ -s "$WD/formalfail.prp" ] || fail "formalfail.prp missing in --workdir"
grep -q 'AUTO-GENERATED by `lhd formal verify`' "$WD/formalfail.prp" || fail "testbench must carry provenance"
grep -q 'counter hit 5' "$WD/formalfail.prp" || fail "testbench must name the violated obligation"
grep -q '_drv_enable = \[0, 0, 1, 1, 1, 1, 1, 0\]' "$WD/formalfail.prp" || fail "testbench must drive the violating enable trace: $(cat "$WD/formalfail.prp")"
grep -q 'tick 8 {' "$WD/formalfail.prp" || fail "testbench must step all 8 trace cycles"

# A refuted FORMAL-BLOCK obligation is embedded into the testbench as a
# test-body assert at the violating cycle (re-targeted at _dut.<path> reads),
# so the replay TRIGGERS the proven-to-fail assertion.
WD2="$W/wd_blockfail"
mkdir -p "$WD2"
cat >"$W/hier_bad.verify.prp" <<'HEOF'
const top = import("hier.duo")
formal duo.sum {
  mut acc = top
  assert(acc.s != 2, "both leaves advanced")
}
HEOF
OUT="$W/blockfail.out"
"$LHD" formal verify "$W/hier.prp" "$W/hier_bad.verify.prp" --top duo \
  --set formal.bound=6 --workdir "$WD2" --set formal.prpfailrun=false >"$OUT" 2>&1
[ -s "$WD2/formalfail.prp" ] || fail "block refutation must also write formalfail.prp: $(cat "$OUT")"
grep -q 'if clock == ' "$WD2/formalfail.prp" || fail "embedded check must target the violating cycle: $(cat "$WD2/formalfail.prp")"
grep -q 'assert(_dut.s != 2, "both leaves advanced")' "$WD2/formalfail.prp" || fail "the failing block assertion must be embedded over _dut paths: $(cat "$WD2/formalfail.prp")"

# ---------------------------------------------------------------------------
# 5. `lhd formal lec` is the lec command (alias): a design LECs against itself.
# ---------------------------------------------------------------------------
"$LHD" formal lec --impl "$W/cnt.prp" --ref "$W/cnt.prp" --top cnt >"$W/flec.out" 2>&1
[ $? -eq 0 ] || fail "formal lec self-check must pass: $(cat "$W/flec.out")"
grep -q "lec: 'cnt.cnt' PROVEN equivalent" "$W/flec.out" || fail "formal lec must run the lec command: $(cat "$W/flec.out")"

# ---------------------------------------------------------------------------
# 7. V3 verdict ladder: the parity invariant is INDUCTIVE (relative to the
#    candidate set) and upgrades to PROVEN-unbounded; a fact that is true to
#    the bound but NOT inductive (count != 200 — a free state count=199 steps
#    to 200) is Houdini-dropped and keeps its BOUNDED verdict. Sound both ways.
# ---------------------------------------------------------------------------
cat >"$W/ladder.prp" <<'EOF'
mod cnt2(enable:bool) -> (value:u8@[0]) {
  reg count:u8 = 0
  reg par:bool = false
  value = count
  assert(u1(par) == count#[0], "parity")
  assert(count != 200, "bounded only")
  if enable {
    wrap count += 1
    par = not par
  }
}
EOF
verify ladder ladder --top cnt2 --set formal.bound=6
[ "$RC" -eq 0 ] || fail "ladder design must pass (got rc=$RC): $(cat "$OUT")"
grep -q "'parity'\": PROVEN (inductive" "$OUT" || fail "the inductive invariant must upgrade to unbounded: $(cat "$OUT")"
grep -q "'bounded only'\": PROVEN to cycle 7 (bounded)" "$OUT" || fail "a non-inductive fact must STAY bounded: $(cat "$OUT")"

echo "PASS: 2f-verify V1-V3 (bounded/inductive ladder; refuted-at-cycle + trace; assume discloses+prunes; formal blocks + filter; timeout isolation; strict; aliases; no vacuous pass)"
