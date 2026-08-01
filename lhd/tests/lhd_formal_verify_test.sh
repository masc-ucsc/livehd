#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# 2f-verify V1: `lhd formal verify` — single-design assert/assume BMC on the
# pass/lec engine (lec::prove_properties). Contract under test:
#   * a true STATE invariant (that single-frame induction defers) is PROVEN to
#     the bound, per-assert, with a per-cycle depth in the table;
#   * a reachable violation is REFUTED at its cycle with the per-cycle input
#     trace, carries the user message, and fails the run (exit != 0);
#   * assume discipline: EVERY `assume` is a proof obligation (prove-then-use)
#     — checked as an assert first; a true claim PROVES and constrains, a false
#     one REFUTES the run (an input-only constraint like assume(op==7) can
#     never hold over free inputs, so it refutes with the assume_nocheck hint);
#     `assume_nocheck` is the explicit free UNCHECKED environment constraint
#     (disclosed; the fcore spelling assume_nocheck_formal also warns);
#     assume_nocheck_synth is invisible to verify;
#   * per-obligation timeout isolation: a hard obligation goes UNKNOWN on its
#     own budget while its easy sibling still proves; UNKNOWN FAILS the run
#     (formal.strict defaults TRUE) and is only a warning (exit 0) when the user
#     explicitly opts out with --set formal.strict=false;
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
verify cnt cnt_alias --top cnt --set formal.bound=10
[ "$RC" -ne 0 ] || fail "formal.bound alias must behave like formal.bound (got rc=0)"
grep -q 'REFUTED at cycle 7' "$OUT" || fail "formal.bound=10 must reach the cycle-7 refutation: $(cat "$OUT")"

# ---------------------------------------------------------------------------
# 2. P1 assume discipline, internal (state) assumes are PROVE-THEN-USE:
#    a FALSE state assume (count<5 while count reaches 5) is REFUTED and fails
#    the run — it can no longer fake a PROVEN for its companion assert (which
#    now also refutes honestly).
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
[ "$RC" -ne 0 ] || fail "a FALSE internal assume must REFUTE the run, never fake a PROVEN (got rc=0): $(cat "$OUT")"
grep -q 'assume at.*cnt_assume.prp:4.*REFUTED at cycle' "$OUT" || fail "the false state assume must be REFUTED at its cycle: $(cat "$OUT")"
grep -q 'checked assume(s):.*REFUTED' "$OUT" || fail "the headline must disclose the refuted assume: $(cat "$OUT")"
grep -q 'cnt_assume.prp:5.*REFUTED' "$OUT" || fail "the companion assert must refute honestly (no masking): $(cat "$OUT")"

# 2a. A TRUE state invariant assume PROVES (here: inductively) and is disclosed
#     as used; the run stays green.
cat >"$W/wrap_assume.prp" <<'EOF'
mod wrapcnt(enable:bool) -> (value:u8@[0]) {
  reg count:u8 = 0
  value = count
  assume(count <= 5)
  assert(count != 7, "never 7")
  if enable {
    if count == 5 {
      count = 0
    } else {
      count += 1
    }
  }
}
EOF
verify wrap_assume wrap_assume --top wrapcnt --set formal.bound=8
[ "$RC" -eq 0 ] || fail "a TRUE internal assume must prove and keep the run green (got rc=$RC): $(cat "$OUT")"
grep -q 'assume at.*wrap_assume.prp:4.*PROVEN' "$OUT" || fail "the true state assume must get a PROVEN row: $(cat "$OUT")"
grep -q 'checked assume(s): 1 proven (used)' "$OUT" || fail "the headline must disclose the proven assume: $(cat "$OUT")"

# ---------------------------------------------------------------------------
# 2b. INPUT assumes are proof obligations too: over free primary inputs
#     `assume(a < 4)` can never be proven, so the deep prover REFUTES it and
#     the failure names the sanctioned spelling (assume_nocheck). The
#     sanctioned form — a formal-block assume_nocheck — is a free env
#     constraint in force at EVERY cycle, reset prologue included (SVA
#     semantics): the block's assert_always is checked during the prologue too,
#     so without prologue coverage it would run unconstrained and false-refute
#     at cycle 0.
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
# The explicit escape hatch runs the deep prover — which CHECKS the assume as
# an assert, refutes it (free inputs), and points at assume_nocheck.
verify always_env always_env --top always_env --set formal.bound=4 --set compile.formal.on_refute=warn
[ "$RC" -ne 0 ] || fail "an unprovable input assume must REFUTE in the deep prover too (got rc=0): $(cat "$OUT")"
grep -q 'assume at.*always_env.prp:4.*REFUTED at cycle' "$OUT" || fail "the input assume must get a REFUTED row: $(cat "$OUT")"
grep -q 'spell it assume_nocheck' "$OUT" || fail "the refuted input assume must hint at assume_nocheck: $(cat "$OUT")"
grep -q 'has an assume that fails its check' "$OUT" || fail "the exit headline must say the ASSUME failed, not a design violation: $(cat "$OUT")"
# The sanctioned spelling: a formal-block assume_nocheck. In force at every
# cycle, prologue included, so the block's assert_always proves.
cat >"$W/always_env2.prp" <<'EOF'
mod always_env2(a:u8, en:bool) -> (o:u8@[0]) {
  reg acc:u8 = 0
  o = acc
  if en {
    wrap acc += 1
  }
}
EOF
cat >"$W/always_env2.verify.prp" <<'EOF'
const top = import("always_env2.always_env2")
formal env.bound {
  mut acc = top
  assume_nocheck(acc.a < 4)
  assert_always(acc.a != 200, "env bound")
}
EOF
OUT="$W/always_env2.out"
"$LHD" formal verify "$W/always_env2.prp" "$W/always_env2.verify.prp" --top always_env2 --set formal.bound=4 >"$OUT" 2>&1
RC=$?
[ "$RC" -eq 0 ] || fail "assert_always under a prologue-relevant assume_nocheck must prove (got rc=$RC): $(cat "$OUT")"
grep -q 'assert_always.*PROVEN' "$OUT" || fail "assert_always must be proven incl. the prologue: $(cat "$OUT")"
grep -q 'in force (UNCHECKED assume_nocheck' "$OUT" || fail "the nocheck constraint must be disclosed as UNCHECKED: $(cat "$OUT")"
grep -q 'under 1 UNCHECKED assume(s)' "$OUT" || fail "the headline must disclose the unchecked assume count: $(cat "$OUT")"

# ---------------------------------------------------------------------------
# 3. Per-obligation timeout isolation: a 32-bit multiply identity blows the 2s
#    budget and goes UNKNOWN at its first checked cycle; the easy sibling still
#    proves.
#
#    `distrib` is DELIBERATELY undecidable-in-practice, not merely under-budgeted:
#    it is a bit-vector multiplier-distributivity miter, (a*b)*((a*c)+1) ==
#    a*a*b*c + a*b, which is TRUE mod 2^32 (so no counterexample exists) but whose
#    bit-blasted proof is exponential in the operand width. Measured: at u4 it
#    PROVES inductively in 0.7s; at u8 it is already UNKNOWN against a 120s
#    per-query budget; at u32 it is still UNKNOWN at 120s (240s wall). Raising the
#    budget does not move it — that is the point of the fixture.
#
#    SEVERITY (user ruling 2026-07-29, "an inconclusive should be a fail, user can
#    ignore but not be the default option"): formal.strict defaults TRUE, so an
#    UNKNOWN FAILS the run by default and the failure explains itself. The opt-out
#    --set formal.strict=false restores the exit-0 warning. Both directions are
#    pinned below; the per-obligation isolation (easy PROVEN / distrib UNKNOWN
#    alone) must hold on BOTH, since it is orthogonal to the severity knob.
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
# 3a. DEFAULT (formal.strict is on): the UNKNOWN fails the run, and the failure
#     says it could not DECIDE — it must never read as a refutation, and it must
#     point at the budget knob so the user can retry or opt out.
WDU="$W/wd_report_unknown"
mkdir -p "$WDU"
verify hard hard --top hard --set formal.engine=bmc --set formal.bound=2 \
  --set formal.timeout=1 --set formal.min_timeout=1 --workdir "$WDU"
[ "$RC" -ne 0 ] || fail "an UNKNOWN must FAIL by default (formal.strict defaults true) (got rc=0): $(cat "$OUT")"
grep -q 'could not decide' "$OUT" || fail "the failure must say the run was UNDECIDED, not refuted: $(cat "$OUT")"
grep -q '"class":"unsupported"' "$OUT" || fail "an undecided run must fail as 'unsupported', not as a proof failure: $(cat "$OUT")"
grep -q 'raise --set formal.timeout' "$OUT" || fail "the failure must point at the budget knob so the user can retry: $(cat "$OUT")"
grep -q 'REFUTED' "$OUT" && fail "an undecided run must not be reported as a refutation: $(cat "$OUT")"
# Timeout isolation is orthogonal to the severity knob: it holds here too.
grep -q "'easy'.*PROVEN" "$OUT" || fail "the easy sibling must still prove: $(cat "$OUT")"
grep -q "'distrib'.*UNKNOWN (solver gave up at cycle" "$OUT" || fail "the hard obligation must time out ALONE: $(cat "$OUT")"

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

# The explicit strict opt-out changes the same UNKNOWN class to an exit-0,
# still-loud warning. A no-obligation UNKNOWN exercises that policy without
# paying for the hard multiplier a second time.
verify noprops noprops_nostrict --top pass_through --set formal.strict=false
[ "$RC" -eq 0 ] || fail "--set formal.strict=false must accept an UNKNOWN as a warning (got rc=$RC): $(cat "$OUT")"
grep -q 'formal-inconclusive' "$OUT" || fail "the opted-out UNKNOWN must still emit the loud inconclusive warning: $(cat "$OUT")"
grep -q 'proves nothing and disproves nothing' "$OUT" || fail "the warning must say the run proved nothing: $(cat "$OUT")"

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

# A block assume_nocheck over an INPUT is the env-constraint spelling: freezing
# enable proves count!=5 (same design whose unconstrained run refutes it at
# cycle 7 in case 1). A plain `assume(acc.enable == 0)` would REFUTE instead —
# nothing forces a free input to hold 0 (pinned in the cnt.frozen_checked run).
cat >"$W/frozen.verify.prp" <<'EOF'
const top = import("cnt.cnt")
formal cnt.frozen {
  mut acc = top
  assume_nocheck(acc.enable == 0)
  assert(acc.count != 5, "frozen counter")
}
EOF
OUT="$W/blocks_frozen.out"
"$LHD" formal verify "$W/cnt.prp" "$W/frozen.verify.prp" --formal 'cnt.frozen' --top cnt --set formal.bound=10 >"$OUT" 2>&1
grep -q '\[cnt.frozen\].*in force (UNCHECKED assume_nocheck' "$OUT" || fail "block nocheck assume must be disclosed: $(cat "$OUT")"
grep -q "'frozen counter'.*PROVEN" "$OUT" || fail "block nocheck assume must prune the violation: $(cat "$OUT")"

# The same constraint spelled as a plain (checked) assume REFUTES: an input
# assume is a proof obligation and a free input cannot be proven frozen.
cat >"$W/frozen_checked.verify.prp" <<'EOF'
const top = import("cnt.cnt")
formal cnt.frozen_checked {
  mut acc = top
  assume(acc.enable == 0)
  assert(acc.count != 5, "frozen counter")
}
EOF
OUT="$W/blocks_frozen_checked.out"
"$LHD" formal verify "$W/cnt.prp" "$W/frozen_checked.verify.prp" --formal 'cnt.frozen_checked' --top cnt --set formal.bound=10 >"$OUT" 2>&1
RC=$?
[ "$RC" -ne 0 ] || fail "a plain input assume must refute its check (got rc=0): $(cat "$OUT")"
grep -q '\[cnt.frozen_checked\].*REFUTED at cycle' "$OUT" || fail "the checked input assume must get a REFUTED row: $(cat "$OUT")"
grep -q 'spell it assume_nocheck' "$OUT" || fail "the refuted input assume must hint at assume_nocheck: $(cat "$OUT")"

# 6c. Assume forms in blocks. A plain block assume over STATE is a proof
#     obligation: a false one REFUTES the run. assume_nocheck is the explicit
#     escape: accepted as a free constraint, disclosed as UNCHECKED, and it
#     prunes the block's OWN obligations (the user owns that risk); the fcore
#     spelling assume_nocheck_formal additionally warns per encounter.
#     assume_nocheck_synth is invisible to verify.
#     SCOPE: it prunes only inside its block. The design's own `counter hit 5`
#     assert is a DESIGN-tier obligation and still refutes — a sidecar may not
#     weaken the design's own claims (user ruling, 2026-07-25).
cat >"$W/stateassume.verify.prp" <<'EOF'
const top = import("cnt.cnt")
formal cnt.stateassume {
  mut acc = top
  assume(acc.count < 5)
  assert(acc.count != 5, "shadow")
}
EOF
OUT="$W/blocks_stateassume.out"
"$LHD" formal verify "$W/cnt.prp" "$W/stateassume.verify.prp" --formal 'cnt.stateassume' --top cnt --set formal.bound=10 >"$OUT" 2>&1
[ $? -ne 0 ] || fail "a false block STATE assume must refute the run (got rc=0): $(cat "$OUT")"
grep -q '\[cnt.stateassume\].*REFUTED at cycle' "$OUT" || fail "the false block state assume must be REFUTED: $(cat "$OUT")"

cat >"$W/nocheck.verify.prp" <<'EOF'
const top = import("cnt.cnt")
formal cnt.nocheck {
  mut acc = top
  assume_nocheck_formal(acc.count < 5)
  assume_nocheck_synth(acc.count < 3)
  assert(acc.count != 5, "shadow")
}
EOF
OUT="$W/blocks_nocheck.out"
"$LHD" formal verify "$W/cnt.prp" "$W/nocheck.verify.prp" --formal 'cnt.nocheck' --top cnt --set formal.bound=10 >"$OUT" 2>&1
grep -q 'formal-unchecked-assume' "$OUT" || fail "assume_nocheck_formal must warn per encounter: $(cat "$OUT")"
grep -q 'in force (UNCHECKED assume_nocheck' "$OUT" || fail "the unchecked assume row must be distinct: $(cat "$OUT")"
grep -q 'under 1 UNCHECKED assume(s)' "$OUT" || fail "the headline must disclose the unchecked count: $(cat "$OUT")"
grep -q "'shadow'.*PROVEN" "$OUT" || fail "the unchecked constraint must prune its OWN block: $(cat "$OUT")"
grep -q 'count < 3' "$OUT" && fail "assume_nocheck_synth must be INVISIBLE to verify: $(cat "$OUT")"
# Scope isolation, the point of the ruling: the SAME run's design-tier assert is
# NOT pruned by the block's unchecked assume — it refutes at its real cycle.
grep -q "'counter hit 5'.*REFUTED at cycle" "$OUT" \
  || fail "a block assume must NOT weaken the design's own assert: $(cat "$OUT")"

# The plain assume_nocheck spelling is the SANCTIONED env-constraint form: same
# UNCHECKED discipline and disclosure, but no per-encounter warning.
cat >"$W/nocheck_plain.verify.prp" <<'EOF'
const top = import("cnt.cnt")
formal cnt.nocheckp {
  mut acc = top
  assume_nocheck(acc.count < 5)
  assert(acc.count != 5, "shadow2")
}
EOF
OUT="$W/blocks_nocheck_plain.out"
"$LHD" formal verify "$W/cnt.prp" "$W/nocheck_plain.verify.prp" --formal 'cnt.nocheckp' --top cnt --set formal.bound=10 >"$OUT" 2>&1
grep -q 'formal-unchecked-assume' "$OUT" && fail "the plain assume_nocheck spelling must NOT warn: $(cat "$OUT")"
grep -q 'in force (UNCHECKED assume_nocheck' "$OUT" || fail "plain assume_nocheck must still be disclosed as UNCHECKED: $(cat "$OUT")"
grep -q 'under 1 UNCHECKED assume(s)' "$OUT" || fail "the headline must disclose the plain nocheck count: $(cat "$OUT")"
grep -q "'shadow2'.*PROVEN" "$OUT" || fail "the plain nocheck constraint must prune its OWN block: $(cat "$OUT")"

# ---------------------------------------------------------------------------
# 6d. SCOPING (fixme issue 3, user ruling 2026-07-25): formal blocks are
#     INDEPENDENT tests, exactly like `test` blocks. Each block's assumes are in
#     force ONLY for that block's own obligations, so two blocks may carry
#     MUTUALLY EXCLUSIVE assumes and both still prove. Before the fix this run
#     reported "assume set contradictory" and every obligation went UNKNOWN,
#     which forced sidecar authors to collapse to one block + implication-style
#     asserts.
# ---------------------------------------------------------------------------
cat >"$W/alu.prp" <<'AEOF'
pub comb aluop(op:u8, x:u8, y:u8) -> (r:u8) {
  r = if op == 0x17 { (x + y) & 0xff } elif op == 0x07 { (x - y) & 0xff } else { 0 }
}
AEOF
cat >"$W/two.verify.prp" <<'AEOF'
const a = import("alu.aluop")

formal alu.addw {
  mut acc = a
  assume_nocheck(acc.op == 0x17)
  assert(acc.r == ((acc.x + acc.y) & 0xff), "ADDW is the sum")
}

formal alu.subw {
  mut acc = a
  assume_nocheck(acc.op == 0x07)
  assert(acc.r == ((acc.x - acc.y) & 0xff), "SUBW is the difference")
}
AEOF
OUT="$W/blocks_two.out"
"$LHD" formal verify "$W/alu.prp" "$W/two.verify.prp" --top aluop --set formal.bound=2 >"$OUT" 2>&1
[ $? -eq 0 ] || fail "two blocks with exclusive assumes must both prove (got rc!=0): $(cat "$OUT")"
grep -q "'ADDW is the sum'.*PROVEN" "$OUT" || fail "block 1 must prove under its own assume: $(cat "$OUT")"
grep -q "'SUBW is the difference'.*PROVEN" "$OUT" || fail "block 2 must prove under its own assume: $(cat "$OUT")"
grep -q 'CONTRADICTORY' "$OUT" && fail "independent blocks must not form one contradictory assume set: $(cat "$OUT")"

# The dual: a block must NOT borrow a SIBLING's assume. `leak` asserts the ADDW
# property under the SUBW assume, so it can only pass if op==0x17 leaked in.
cat >"$W/leak.verify.prp" <<'AEOF'
const a = import("alu.aluop")

formal alu.addw {
  mut acc = a
  assume_nocheck(acc.op == 0x17)
  assert(acc.r == ((acc.x + acc.y) & 0xff), "ADDW is the sum")
}

formal alu.leak {
  mut acc = a
  assume_nocheck(acc.op == 0x07)
  assert(acc.r == ((acc.x + acc.y) & 0xff), "LEAK only holds under addw's assume")
}
AEOF
OUT="$W/blocks_leak.out"
"$LHD" formal verify "$W/alu.prp" "$W/leak.verify.prp" --top aluop --set formal.bound=2 >"$OUT" 2>&1
[ $? -ne 0 ] || fail "a sibling block's assume must not prove a false property (got rc=0): $(cat "$OUT")"
grep -q "'LEAK only holds under addw's assume'.*REFUTED" "$OUT" \
  || fail "the borrowing block must REFUTE: $(cat "$OUT")"
grep -q "'ADDW is the sum'.*PROVEN" "$OUT" || fail "the honest block must still prove alongside it: $(cat "$OUT")"

# A block whose OWN assume set is contradictory is named, is NOT allowed to
# vacuously prove, and fails the run (exit != 0) even without formal.strict —
# while a healthy sibling in the same run still proves.
cat >"$W/contra.verify.prp" <<'AEOF'
const a = import("alu.aluop")

formal alu.good {
  mut acc = a
  assume_nocheck(acc.op == 0x17)
  assert(acc.r == ((acc.x + acc.y) & 0xff), "ADDW is the sum")
}

formal alu.contra {
  mut acc = a
  assume_nocheck(acc.op == 0x17)
  assume_nocheck(acc.op == 0x07)
  assert(acc.r == 0xde, "anything at all")
}
AEOF
OUT="$W/blocks_contra.out"
"$LHD" formal verify "$W/alu.prp" "$W/contra.verify.prp" --top aluop --set formal.bound=2 >"$OUT" 2>&1
[ $? -ne 0 ] || fail "a contradictory assume set must fail the run without formal.strict (got rc=0): $(cat "$OUT")"
grep -q "CONTRADICTORY in block 'alu.contra'" "$OUT" || fail "the contradiction must NAME its block: $(cat "$OUT")"
grep -q "'anything at all'.*PROVEN" "$OUT" && fail "a contradictory block must not prove anything vacuously: $(cat "$OUT")"
grep -q "'ADDW is the sum'.*PROVEN" "$OUT" || fail "a healthy sibling block must survive: $(cat "$OUT")"

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
# 6b2. Submodule PORT binding (encoder "\x05tap:" outputs): a submodule-bound
#      block reaches the instance's input/output PORTS as well as its
#      registers; an input-port assume_nocheck freezes each instance
#      independently; a false port claim REFUTES with the @instance
#      attribution (taps are never vacuous).
# ---------------------------------------------------------------------------
cat >"$W/leafports.verify.prp" <<'HEOF'
const sub = import("hier.leafcnt")
formal leaf.ports {
  mut acc = sub
  assume_nocheck(acc.en == 0)
  assert(acc.c == 0 and acc.v == 0, "frozen leaf pins register and port at 0")
}
HEOF
OUT="$W/leafports.out"
"$LHD" formal verify "$W/hier.prp" "$W/leafports.verify.prp" --top duo --set formal.bound=6 >"$OUT" 2>&1
[ $? -eq 0 ] || fail "submodule port binding must prove (got rc!=0): $(cat "$OUT")"
n_rows=$(grep -c 'frozen leaf pins.*PROVEN' "$OUT")
[ "$n_rows" -eq 2 ] || fail "the port block must bind BOTH leafcnt instances (got $n_rows rows): $(cat "$OUT")"
grep -q 'in force (UNCHECKED assume_nocheck' "$OUT" || fail "an instance input-port nocheck assume must be disclosed: $(cat "$OUT")"

cat >"$W/leafbad.verify.prp" <<'HEOF'
const sub = import("hier.leafcnt")
formal leaf.badport {
  mut acc = sub
  assert(acc.v == 5, "always five")
}
HEOF
OUT="$W/leafbad.out"
"$LHD" formal verify "$W/hier.prp" "$W/leafbad.verify.prp" --top duo --set formal.bound=6 >"$OUT" 2>&1
[ $? -ne 0 ] || fail "a false submodule port assert must refute (got rc=0): $(cat "$OUT")"
grep -q '\[leaf.badport@.*REFUTED at cycle' "$OUT" || fail "the port refute must carry @instance: $(cat "$OUT")"

# ---------------------------------------------------------------------------
# 8. --workdir on a REFUTED run writes simfail_<test>.prp: a self-contained
#    `lhd sim` testbench driving the violating input trace. Generation only
#    (simfail_run=false keeps the test hermetic —
#    no sim runtime headers needed); the trace arrays and provenance must be
#    in the file.
# ---------------------------------------------------------------------------
WD="$W/wd_simfail"
mkdir -p "$WD"
OUT="$W/simfail.out"
"$LHD" formal verify "$W/cnt.prp" --top cnt --set formal.bound=10 --workdir "$WD" \
  --set formal.simfail_run=false >"$OUT" 2>&1
[ $? -ne 0 ] || fail "the refuted run must still exit non-zero"
grep -q 'wrote counterexample simulation test' "$OUT" || fail "--workdir must write simfail_cnt.prp: $(cat "$OUT")"
[ -s "$WD/simfail_cnt.prp" ] || fail "simfail_cnt.prp missing in --workdir"
grep -q 'AUTO-GENERATED by `lhd formal verify`' "$WD/simfail_cnt.prp" || fail "testbench must carry provenance"
grep -q 'counter hit 5' "$WD/simfail_cnt.prp" || fail "testbench must name the violated obligation"
grep -q '_drv_enable = \[0, 0, 1, 1, 1, 1, 1, 0\]' "$WD/simfail_cnt.prp" || fail "testbench must drive the violating enable trace: $(cat "$WD/simfail_cnt.prp")"
grep -q 'tick 8 {' "$WD/simfail_cnt.prp" || fail "testbench must step all 8 trace cycles"

# ---------------------------------------------------------------------------
# 8b. formal_report.json (P2 agent feedback): written on EVERY run — the
#     REFUTED run above included (before the exit throw) — with per-obligation
#     verdicts/ids/solve_ms, assume counts, and existing artifact paths; and
#     simfail_cnt.json (the F7 witness JSON) parses with a mapped root cut.
# ---------------------------------------------------------------------------
[ -s "$WD/formal_report.json" ] || fail "the REFUTED run must still write formal_report.json"
grep -q 'wrote report' "$OUT" || fail "the report path must be announced on stdout: $(cat "$OUT")"
python3 - "$WD" <<'PYEOF' || fail "formal_report.json / simfail_cnt.json check failed"
import json, sys
wd = sys.argv[1]
d = json.load(open(wd + "/formal_report.json"))
assert d["schema_version"] == 1 and d["kind"] == "formal_report"
assert d["run"]["verdict"] == "refuted", d["run"]["verdict"]
obs = d["obligations"]
assert len(obs) == 2, obs  # parity assert + count!=5 assert
ref = [o for o in obs if o["verdict"] == "refuted"]
assert len(ref) == 1 and ref[0]["refuted_at"] == 7 and ref[0]["witness"], ref
assert ref[0]["id"].startswith("assert@") and ref[0]["file"].endswith("cnt.prp") and ref[0]["line"] > 0, ref
assert all(o["solve_ms"] >= 0 for o in obs)
assert "simfail" in d["artifacts"] and "simfail_json" in d["artifacts"], d["artifacts"]
ac = d["run"]["assume_counts"]
assert set(ac) == {"unchecked", "checked_proven", "checked_unproven", "checked_refuted"}
# simfail JSON (F7): the same schema and kind are used by verify and LEC.
w = json.load(open(d["artifacts"]["simfail_json"]))
assert w["kind"] == "simfail" and w["root_cut"]["line"] > 0 and w["root_cut"]["file"].endswith("cnt.prp"), w["root_cut"]
assert len(w["trace"]["cycles"]) == w["diverge_cycle"] + 1
PYEOF

# PROVEN run report: verdict + the internal-assume ledger from case 2a.
WDP="$W/wd_report_proven"
mkdir -p "$WDP"
"$LHD" formal verify "$W/wrap_assume.prp" --top wrapcnt --set formal.bound=8 --workdir "$WDP" >"$W/report_proven.out" 2>&1
[ $? -eq 0 ] || fail "proven report run must pass: $(cat "$W/report_proven.out")"
python3 - "$WDP" <<'PYEOF' || fail "PROVEN formal_report.json contract check failed"
import json, sys
d = json.load(open(sys.argv[1] + "/formal_report.json"))
assert d["run"]["verdict"] == "proven"
assert d["run"]["assume_counts"]["checked_proven"] == 1
a = [o for o in d["obligations"] if o["kind"] == "assume"]
assert len(a) == 1 and a[0]["aclass"] == "internal" and a[0]["verdict"] == "proven", a
PYEOF

# UNKNOWN run report: the structured timeout_core names the straggler by id and
# in_timeout_core marks it; the easy sibling stays out of the core.
python3 - "$WDU" <<'PYEOF' || fail "UNKNOWN formal_report.json contract check failed"
import json, sys
d = json.load(open(sys.argv[1] + "/formal_report.json"))
assert d["run"]["verdict"] == "unknown"
unk = [o for o in d["obligations"] if o["verdict"] == "unknown"]
assert len(unk) == 1 and unk[0]["unknown_why"] and unk[0]["msg"] == "'distrib'", unk
if d["timeout_core"]:  # best-effort cvc5 API: when present it must be consistent
    assert unk[0]["in_timeout_core"] and unk[0]["id"] in d["timeout_core"], (unk, d["timeout_core"])
    easy = [o for o in d["obligations"] if o["verdict"] == "proven"]
    assert all(not o["in_timeout_core"] for o in easy)
PYEOF

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
  --set formal.bound=6 --workdir "$WD2" --set formal.simfail_run=false >"$OUT" 2>&1
[ -s "$WD2/simfail_duo_sum.prp" ] || fail "block refutation must write simfail_duo_sum.prp: $(cat "$OUT")"
grep -q 'if clock == ' "$WD2/simfail_duo_sum.prp" || fail "embedded check must target the violating cycle: $(cat "$WD2/simfail_duo_sum.prp")"
grep -q 'assert(_dut.s != 2, "both leaves advanced")' "$WD2/simfail_duo_sum.prp" || fail "the failing block assertion must be embedded over _dut paths: $(cat "$WD2/simfail_duo_sum.prp")"

# ---------------------------------------------------------------------------
# 8c. A COMBINATIONAL design gets a witness too. prp_writer picks the lambda
#     keyword from the body (`pub mod` when it holds state, `pub comb` when it
#     does not), so the re-emitted copy the generator parses has NO `mod`
#     keyword for a stateless design — matching only `mod` silently dropped the
#     whole combinational class ("no Pyrope modules were re-emitted"). Every
#     design above is stateful, which is why this never showed up here.
# ---------------------------------------------------------------------------
cat >"$W/combdut.prp" <<'EOF'
pub comb combdut(a:u8) -> (r:u8) {
  r = a
}
EOF
cat >"$W/combdut.verify.prp" <<'EOF'
const top = import("combdut.combdut")
formal combdut.bad {
  mut acc = top
  assert(acc.r != 5, "r never 5")
}
EOF
WD3="$W/wd_combfail"
mkdir -p "$WD3"
OUT="$W/combfail.out"
"$LHD" formal verify "$W/combdut.prp" "$W/combdut.verify.prp" --top combdut \
  --set formal.bound=6 --workdir "$WD3" --set formal.simfail_run=false >"$OUT" 2>&1
[ $? -ne 0 ] || fail "the false comb assert must refute: $(cat "$OUT")"
grep -q 'no Pyrope modules were re-emitted' "$OUT" && fail "a comb top must not be reported as un-re-emitted: $(cat "$OUT")"
[ -s "$WD3/simfail_combdut_bad.prp" ] || fail "a combinational design must get simfail_combdut_bad.prp: $(cat "$OUT")"
grep -q 'import("combdut.combdut")' "$WD3/simfail_combdut_bad.prp" || fail "the comb testbench must import the pub top: $(cat "$WD3/simfail_combdut_bad.prp")"
grep -q '_drv_a = \[0, 0, 5\]' "$WD3/simfail_combdut_bad.prp" || fail "the comb testbench must drive the violating trace: $(cat "$WD3/simfail_combdut_bad.prp")"
grep -q 'assert(_dut.r != 5, "r never 5")' "$WD3/simfail_combdut_bad.prp" || fail "the comb testbench must embed the failing assert: $(cat "$WD3/simfail_combdut_bad.prp")"

# ---------------------------------------------------------------------------
# 8d. A refuted plain `assume` is embedded AS AN ASSERT. Under the checked-
#     assume discipline an `assume` IS an obligation, so a refuted one is
#     exactly what the replay must re-fire; only the `assume_nocheck*` spellings
#     (free by user fiat, never refuted) stay out of the testbench body.
# ---------------------------------------------------------------------------
cat >"$W/combassume.verify.prp" <<'EOF'
const top = import("combdut.combdut")
formal combdut.env {
  mut acc = top
  assume(acc.a == 7)
}
EOF
WD4="$W/wd_assumefail"
mkdir -p "$WD4"
OUT="$W/assumefail.out"
"$LHD" formal verify "$W/combdut.prp" "$W/combassume.verify.prp" --top combdut \
  --set formal.bound=6 --workdir "$WD4" --set formal.simfail_run=false >"$OUT" 2>&1
[ $? -ne 0 ] || fail "an unprovable input assume must refute: $(cat "$OUT")"
[ -s "$WD4/simfail_combdut_env.prp" ] || fail "a refuted assume must get simfail_combdut_env.prp: $(cat "$OUT")"
grep -q 'assert(_dut.a == 7)' "$WD4/simfail_combdut_env.prp" \
  || fail "a refuted CHECKED assume must be embedded as a test assert: $(cat "$WD4/simfail_combdut_env.prp")"
grep -q 'assume(' "$WD4/simfail_combdut_env.prp" && fail "the testbench must not carry a bare assume: $(cat "$WD4/simfail_combdut_env.prp")"

# The nocheck spelling is a free constraint: it never refutes, so it never
# reaches the testbench body (here the assert is what fails).
cat >"$W/combnocheck.verify.prp" <<'EOF'
const top = import("combdut.combdut")
formal combdut.nock {
  mut acc = top
  assume_nocheck(acc.a == 5)
  assert(acc.r != 5, "r never 5")
}
EOF
WD5="$W/wd_nocheckfail"
mkdir -p "$WD5"
OUT="$W/nocheckfail.out"
"$LHD" formal verify "$W/combdut.prp" "$W/combnocheck.verify.prp" --top combdut \
  --set formal.bound=6 --workdir "$WD5" --set formal.simfail_run=false >"$OUT" 2>&1
[ $? -ne 0 ] || fail "the assert under a nocheck constraint must refute: $(cat "$OUT")"
grep -q 'assert(_dut.r != 5, "r never 5")' "$WD5/simfail_combdut_nock.prp" || fail "the failing assert must be embedded: $(cat "$WD5/simfail_combdut_nock.prp")"
grep -q 'assume_nocheck' "$WD5/simfail_combdut_nock.prp" && fail "an assume_nocheck must never reach the testbench: $(cat "$WD5/simfail_combdut_nock.prp")"
grep -q 'assume(' "$WD5/simfail_combdut_nock.prp" && fail "an assume_nocheck must not be rewritten into a plain assume: $(cat "$WD5/simfail_combdut_nock.prp")"

# ---------------------------------------------------------------------------
# 8e. Same-line statements are AMBIGUOUS, not silently mis-embedded. A verdict
#     identifies its statement by block + file:line only, so `a; b` on one line
#     is indistinguishable — embedding whichever won the map write would attach
#     the WRONG check (here: a tautology that passes, so the replay would run
#     clean and read as "the counterexample was spurious"). Emit no check, and
#     say why, in both the diagnostic and the file header.
# ---------------------------------------------------------------------------
cat >"$W/combcoll.verify.prp" <<'EOF'
const top = import("combdut.combdut")
formal combdut.two {
  mut acc = top
  assert(acc.r != 5, "r never 5"); assert(acc.r == acc.r, "taut")
}
EOF
WD6="$W/wd_collfail"
mkdir -p "$WD6"
OUT="$W/collfail.out"
"$LHD" formal verify "$W/combdut.prp" "$W/combcoll.verify.prp" --top combdut \
  --set formal.bound=6 --workdir "$WD6" --set formal.simfail_run=false >"$OUT" 2>&1
[ $? -ne 0 ] || fail "the false assert must still refute when it shares a line: $(cat "$OUT")"
grep -q 'simfail-embed-ambiguous' "$OUT" || fail "a same-line statement pair must be reported ambiguous: $(cat "$OUT")"
[ -s "$WD6/simfail_combdut_two.prp" ] || fail "an ambiguous embed must still write the input-trace testbench: $(cat "$OUT")"
grep -q '_drv_a = \[0, 0, 5\]' "$WD6/simfail_combdut_two.prp" || fail "the trace must still be driven: $(cat "$WD6/simfail_combdut_two.prp")"
grep -q 'assert(_dut' "$WD6/simfail_combdut_two.prp" && fail "an ambiguous obligation must embed NO check: $(cat "$WD6/simfail_combdut_two.prp")"
grep -q 'NO runtime check is embedded' "$WD6/simfail_combdut_two.prp" || fail "the header must not claim the replay fails: $(cat "$WD6/simfail_combdut_two.prp")"

# Multiple refuted formal tests get separate, test-named simulation files.
cat >"$W/combmulti.verify.prp" <<'EOF'
const top = import("combdut.combdut")
formal combdut.five {
  mut acc = top
  assert(acc.r != 5, "r never 5")
}
formal combdut.seven {
  mut acc = top
  assert(acc.r != 7, "r never 7")
}
EOF
WD7="$W/wd_multifail"
mkdir -p "$WD7"
OUT="$W/multifail.out"
"$LHD" formal verify "$W/combdut.prp" "$W/combmulti.verify.prp" --top combdut \
  --set formal.bound=6 --workdir "$WD7" --set formal.simfail_run=false >"$OUT" 2>&1
[ $? -ne 0 ] || fail "the two false formal tests must refute: $(cat "$OUT")"
[ -s "$WD7/simfail_combdut_five.prp" ] || fail "missing simfail_combdut_five.prp: $(cat "$OUT")"
[ -s "$WD7/simfail_combdut_seven.prp" ] || fail "missing simfail_combdut_seven.prp: $(cat "$OUT")"

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

# ---------------------------------------------------------------------------
# 9. R1 — a property inside an `if`/`elif`/`else`/`match` arm is GUARDED by
#    that arm's path condition, i.e. the obligation is `guard implies cond`,
#    never the bare `cond`. This is SystemVerilog's procedural-assertion
#    semantics: the statement is only evaluated when control flow reaches it.
#
#    Before this, upass.tolg's lower_cassert connected the bare condition pin,
#    so the obligation was STRICTLY STRONGER than what the user wrote — wrong
#    in both directions, and silently:
#      * 9a: an assert fired on paths the guard excludes (`assert(a < 10)`
#        under `if a < 4` refuted with a=10, a value the guard rules out);
#      * 9c: an assume over-constrained the environment and manufactured a
#        FALSE PROVEN — the dangerous direction, since it hides real bugs.
#    9a and 9c are the guard-drop detectors: both flip verdict if the fold in
#    lower_cassert is removed. 9b is the OVER-correction check and does NOT
#    discriminate on its own (measured: its counterexample is a=0 either way,
#    which satisfies the guard) — it exists so that honoring the guard cannot
#    be "fixed" by making guarded properties vacuous, and it pins that the
#    witness stays consistent with the guard the obligation now carries.
# ---------------------------------------------------------------------------
cat >"$W/guarded.prp" <<'EOF'
mod guarded(a:u8, b:u8) -> (o:u8@[0]) {
  o = a
  if a < 4 {
    if b < 4 {
      assert(a + b < 10, "nested")
    }
    assert(a < 4, "then")
  } elif a < 8 {
    assert(a >= 4, "elif")
  } else {
    assert(a >= 8, "else")
  }
  match a {
    == 1 { assert(a == 1, "match-eq") }
    < 5  { assert(a != 1, "match-lt") }
    else { assert(a >= 5, "match-else") }
  }
}
EOF
verify guarded guarded --top guarded --set formal.bound=4
[ "$RC" -eq 0 ] || fail "every guarded property is true UNDER ITS GUARD and must prove (rc=$RC): $(cat "$OUT")"
# Each arm kind is named so a regression says WHICH gate was dropped. `match`
# rides the same path stack (it lowers to a unique_if whose arms are
# branch-lowered before the Hotmux merge), and `nested` needs BOTH guards.
for m in nested then elif else match-eq match-lt match-else; do
  grep -q "'$m'\": PROVEN" "$OUT" || fail "guarded property '$m' must prove (its arm's path condition was dropped): $(cat "$OUT")"
done

# 9b. The guard must not swallow a real violation, and the counterexample must
#     satisfy the guard (a < 4). A counterexample with a >= 4 means the guard
#     was dropped and the run refuted for the wrong reason.
cat >"$W/guarded_bad.prp" <<'EOF'
mod guarded_bad(a:u8) -> (o:u8@[0]) {
  o = a
  if a < 4 {
    assert(a > 2, "false under its own guard")
  }
}
EOF
verify guarded_bad guarded_bad --top guarded_bad --set formal.bound=4
[ "$RC" -ne 0 ] || fail "a property FALSE under its own guard must still refute: $(cat "$OUT")"
grep -q "false under its own guard" "$OUT" || fail "the refutation must name the failing assertion: $(cat "$OUT")"
CEX=$(grep -oE 'counterexample: a=[0-9]+' "$OUT" | head -1 | grep -oE '[0-9]+$')
[ -n "$CEX" ] || fail "the refutation must carry a counterexample: $(cat "$OUT")"
[ "$CEX" -lt 4 ] || fail "the counterexample (a=$CEX) must SATISFY the guard a<4 — a>=4 means the guard was dropped: $(cat "$OUT")"

# 9c. The over-constraint direction: a guarded `assume` constrains only where
#     its guard holds. Dropping the guard here makes `a < 3` an unconditional
#     environment constraint, which PROVES the assert below — a false PROVEN.
cat >"$W/guarded_assume.prp" <<'EOF'
mod guarded_assume(a:u8) -> (o:u8@[0]) {
  if a >= 100 {
    assume(a < 3)
  }
  o = a
  assert(a < 3, "must NOT be provable")
}
EOF
verify guarded_assume guarded_assume --top guarded_assume --set formal.bound=4
[ "$RC" -ne 0 ] || fail "a guarded assume must not over-constrain into a FALSE PROVEN: $(cat "$OUT")"
grep -q "must NOT be provable" "$OUT" || fail "the refutation must name the assert the over-constrained assume was faking: $(cat "$OUT")"

# ---------------------------------------------------------------------------
# 10. R1 Phase 2 — ANTECEDENT vacuity. R1 (case 9) made a guarded property mean
#     `guard implies cond`, which is right but introduced a new silent failure:
#     a guard that can NEVER hold proves trivially while checking nothing. Each
#     obligation now carries `guarded` + `vacuous_guard` in formal_report.json,
#     prints a VACUOUS continuation row, and — since formal.strict defaults TRUE
#     (user ruling 2026-07-29) — FAILS the run; --set formal.strict=false demotes
#     it back to a loud warning.
#
#     10d is the load-bearing case: the measure must be a FREE frame, not the
#     unrolled window. "Was the guard ever true in the cycles we checked?" is
#     the intuitive question and it is wrong — an inductive proof settles at ONE
#     checked step, so a live `count == 5` guard would be reported dead, and the
#     answer would flip with whichever strategy wins the ind/bmc race.
# ---------------------------------------------------------------------------
cat >"$W/vacuity.prp" <<'EOF'
mod vacuity(a:u8) -> (o:u8@[0]) {
  o = a
  if a > 200 and a < 100 {
    assert(a == 7, "dead guard")
  }
  if a < 4 {
    assert(a < 10, "live guard")
  }
}
EOF
WDV="$W/wd_vacuity"
mkdir -p "$WDV"
verify vacuity vacuity --top vacuity --set formal.bound=4 --workdir "$WDV"
[ "$RC" -ne 0 ] || fail "a vacuous obligation FAILS by default (formal.strict defaults true) (rc=$RC): $(cat "$OUT")"
grep -q "VACUOUS obligation" "$OUT" || fail "the default failure must name the vacuous obligations: $(cat "$OUT")"
grep -q "obligation(s) VACUOUS (guard can never be true)" "$OUT" || fail "the run detail must count vacuous obligations: $(cat "$OUT")"
grep -q "formal-vacuous-guard" "$OUT" || fail "a vacuous obligation must emit the formal-vacuous-guard warning: $(cat "$OUT")"
grep -q "VACUOUS: its \`if\`/\`match\` guard can never be true" "$OUT" || fail "the obligation's own row must carry the vacuity note: $(cat "$OUT")"
# The note is a CONTINUATION of the verdict row, never a replacement: the
# obligation is honestly PROVEN (a dead antecedent leaves it true), so vacuity
# must not be implemented by downgrading the verdict.
grep -q "'dead guard'\": PROVEN" "$OUT" || fail "vacuity is a diagnostic, not a verdict downgrade: $(cat "$OUT")"
python3 - "$WDV" <<'PYEOF' || fail "vacuity formal_report.json contract check failed"
import json, sys
d = json.load(open(sys.argv[1] + "/formal_report.json"))
by = {o["msg"].strip("'"): o for o in d["obligations"]}
assert set(by) == {"dead guard", "live guard"}, list(by)
dead, live = by["dead guard"], by["live guard"]
# Both are guarded; only the dead one is vacuous. A check that flagged BOTH
# would "pass" a sloppier assertion, so pin the live one explicitly.
assert dead["guarded"] and dead["vacuous_guard"], dead
assert live["guarded"] and not live["vacuous_guard"], live
assert dead["verdict"] == "proven" and live["verdict"] == "proven", (dead, live)
PYEOF

# 10c. --set formal.strict=false demotes it back to a warning (exit 0) — the
#      escape hatch, not the default. It exists because a guard unreachable at
#      THIS top can be reachable under another parent, and unlike a contradictory
#      assume set the obligation is still genuinely true. Demoted, it must stay
#      LOUD: the diagnostic and the row note both survive the opt-out, so the
#      knob buys a green exit code and nothing else.
verify vacuity vacuity_nostrict --top vacuity --set formal.bound=4 --set formal.strict=false
[ "$RC" -eq 0 ] || fail "--set formal.strict=false must demote a vacuous obligation to a warning (rc=$RC): $(cat "$OUT")"
grep -q "formal-vacuous-guard" "$OUT" || fail "the opted-out vacuity must still emit its warning: $(cat "$OUT")"
grep -q "VACUOUS: its \`if\`/\`match\` guard can never be true" "$OUT" || fail "the opted-out vacuity must still carry the row note: $(cat "$OUT")"
grep -q "'dead guard'\": PROVEN" "$OUT" || fail "the opt-out must not change the verdict, only the exit code: $(cat "$OUT")"
# Explicit strict=true is still accepted and agrees with the new default.
verify vacuity vacuity_strict --top vacuity --set formal.bound=4 --set formal.strict=true
[ "$RC" -ne 0 ] || fail "explicit formal.strict=true must keep a vacuous obligation a failure: $(cat "$OUT")"
grep -q "VACUOUS obligation" "$OUT" || fail "the strict failure must name the vacuous obligations: $(cat "$OUT")"

# 10d. BOUND- AND ENGINE-INDEPENDENCE (the reason the measure is a free frame).
#      `count == 5` is a LIVE guard that a shallow bound cannot reach, and the
#      ind engine settles this design in ONE checked step at any bound. A
#      window-based check reports it dead at every bound; the free-frame check
#      must report it at none. Run with AND without --workdir: those take
#      different engine paths (the F3 strategy race forks), which also pins that
#      `vacuous_guard` survives the serialize_verify/deserialize_verify codec —
#      an unserialized field is silently lost and the parent sees false.
cat >"$W/vacuity_deep.prp" <<'EOF'
mod vacuity_deep(enable:bool) -> (value:u8@[0]) {
  reg count:u8 = 0
  value = count
  if count == 5 {
    assert(count != 6, "live but deep")
  }
  if enable {
    wrap count += 1
  }
}
EOF
for b in 3 10; do
  verify vacuity_deep "vacuity_deep_$b" --top vacuity_deep --set formal.bound=$b
  [ "$RC" -eq 0 ] || fail "vacuity_deep must pass at bound=$b (rc=$RC): $(cat "$OUT")"
  grep -q "VACUOUS" "$OUT" && fail "a LIVE guard the bound cannot reach must NOT be called vacuous (bound=$b) — the measure regressed to the unrolled window: $(cat "$OUT")"
done
# Same design, --workdir path (the non-forking / cached path): still not vacuous.
WDD="$W/wd_vac_deep"
mkdir -p "$WDD"
verify vacuity_deep vacuity_deep_wd --top vacuity_deep --set formal.bound=6 --workdir "$WDD"
[ "$RC" -eq 0 ] || fail "vacuity_deep must pass on the --workdir path: $(cat "$OUT")"
grep -q "VACUOUS" "$OUT" && fail "a LIVE guard must not be vacuous on the --workdir path either: $(cat "$OUT")"
python3 - "$WDD" <<'PYEOF' || fail "vacuity_deep report must record guarded-but-not-vacuous"
import json, sys
o = [p for p in json.load(open(sys.argv[1] + "/formal_report.json"))["obligations"] if p["kind"] == "assert"]
assert len(o) == 1 and o[0]["guarded"] and not o[0]["vacuous_guard"], o
PYEOF

# 10e. The vacuity flag must cross the fork codec. The default (no --workdir) run
#      above races strategies through serialize_verify; assert the dead-guard
#      design still reports it there, not only on the --workdir path used in 10a.
#      A lost flag now shows up twice over: the row note disappears AND the run
#      goes green, since the default severity rides on the same bit.
verify vacuity vacuity_fork --top vacuity --set formal.bound=4
grep -q "VACUOUS: its" "$OUT" || fail "vacuous_guard was LOST across the verify fork codec (serialize_verify/deserialize_verify): $(cat "$OUT")"
[ "$RC" -ne 0 ] || fail "the fork-path vacuity must also FAIL by default — a green exit means the flag did not survive the codec: $(cat "$OUT")"

# ---------------------------------------------------------------------------
# 11. Code-review fixes on the R1 / Phase-2 work (2026-07-26). Each case below
#     FLIPPED verdict or attribution before its fix.
# ---------------------------------------------------------------------------
# 11a. and2/or2/not1 stamp `bits=1`, which the encoder fits to [0:0]. A guarded
#      property therefore kept only the LSB of a multi-bit condition, and a
#      NESTED guard kept only the LSB of the outer one. `flags | 0x2` is always
#      nonzero, so the first must PROVE; the second is a false PROVEN detector —
#      the clamped guard `(flags|2)[0] & d` never checks flags==0, where the
#      property is false, so it must REFUTE.
cat >"$W/widecond.prp" <<'EOF'
mod widecond(flags:u8, d:bool) -> (o:u8@[0]) {
  o = flags
  if flags | 0x2 {
    assert(flags | 0x4, "multi-bit cond under a multi-bit guard")
  }
}
EOF
verify widecond widecond --top widecond --set formal.bound=2
[ "$RC" -eq 0 ] || fail "a multi-bit assert condition must not be truncated to its LSB: $(cat "$OUT")"

cat >"$W/widecond_bad.prp" <<'EOF'
mod widecond_bad(flags:u8, d:bool) -> (o:u8@[0]) {
  o = flags
  if flags | 0x2 {
    if d {
      assert(flags#[0] == 1, "false on even flags")
    }
  }
}
EOF
verify widecond_bad widecond_bad --top widecond_bad --set formal.bound=2
[ "$RC" -ne 0 ] || fail "a NESTED multi-bit guard clamped to its LSB hides the flags==0 violation (false PROVEN): $(cat "$OUT")"

# 11b. A contradictory assume set makes every checkSatAssuming UNSAT, which used
#      to stamp `vacuous_guard` on a perfectly live guard and — under strict —
#      throw about the user's `if` instead of the conflicting assumes. Only
#      UNCHECKED (assume_nocheck) constraints can manufacture a contradiction
#      now — checked assumes are each proven before use — so the fixture is a
#      formal block with contradictory nocheck constraints beside a live-guard
#      design assert.
cat >"$W/contra_guard.prp" <<'EOF'
mod contra_guard(a:u8) -> (o:u8@[0]) {
  o = a
  if a < 4 {
    assert(a < 10, "live guard")
  }
}
EOF
cat >"$W/contra_guard.verify.prp" <<'EOF'
const top = import("contra_guard.contra_guard")
formal cg.contra {
  mut acc = top
  assume_nocheck(acc.a < 10)
  assume_nocheck(acc.a > 20)
  assert(acc.o == 0xde, "anything at all")
}
EOF
OUT="$W/contra_guard.out"
"$LHD" formal verify "$W/contra_guard.prp" "$W/contra_guard.verify.prp" --top contra_guard --set formal.bound=2 >"$OUT" 2>&1
RC=$?
grep -q "contradictory assume set" "$OUT" || fail "the contradictory assume set must be the reported problem: $(cat "$OUT")"
grep -q "VACUOUS: its" "$OUT" && fail "a LIVE guard must not be called dead just because the assume set is contradictory: $(cat "$OUT")"

# 11c. The strict vacuity throw must not pre-empt a more severe exit: a design
#      with BOTH a dead branch and a reachable violation must report the
#      violation (equiv_fail), not an `unsupported` dead-branch complaint.
cat >"$W/vac_and_refute.prp" <<'EOF'
mod vac_and_refute(a:u8) -> (o:u8@[0]) {
  o = a
  if a > 200 and a < 100 {
    assert(a == 7, "dead")
  }
  assert(a != 3, "genuinely reachable")
}
EOF
verify vac_and_refute vac_and_refute --top vac_and_refute --set formal.bound=2 --set formal.strict=true
[ "$RC" -ne 0 ] || fail "a reachable violation must fail the run"
grep -q "reachable property violation" "$OUT" \
  || fail "the REFUTATION must be the reported failure, not the vacuous dead branch: $(cat "$OUT")"

# 11d. A dead-guard ASSUME is reported but must NOT gate `formal.strict` — the
#      compile tier skips assumes, and the two tiers must agree on whether the
#      same source is clean. The assert alongside it keeps the run decidable.
cat >"$W/vac_assume.prp" <<'EOF'
mod vac_assume(a:u8) -> (o:u8@[0]) {
  o = a
  if a > 200 and a < 100 {
    assume(a < 3)
  }
  assert(a < 256, "trivially true")
}
EOF
WDVA="$W/wd_vac_assume"
mkdir -p "$WDVA"
verify vac_assume vac_assume --top vac_assume --set formal.bound=2 --set formal.strict=true --workdir "$WDVA"
[ "$RC" -eq 0 ] || fail "a dead-guard ASSUME must not fail the run under strict: $(cat "$OUT")"
python3 - "$WDVA" <<'PYEOF' || fail "the dead-guard assume must still be REPORTED (flagged but not gating)"
import json, sys
obs = json.load(open(sys.argv[1] + "/formal_report.json"))["obligations"]
a = [o for o in obs if o["kind"] == "assume"]
assert len(a) == 1 and a[0]["vacuous_guard"], a
PYEOF

# 11e. Per-block assume attribution must survive the FORK codec. Run WITHOUT
#      --workdir (the forking strategy race) on a block whose UNCHECKED
#      (nocheck) assumes are contradictory: the parent must name the block, not
#      blame the design.
cat >"$W/blk.prp" <<'EOF'
mod blk(enable:bool) -> (value:u8@[0]) {
  reg count:u8 = 0
  value = count
  if enable { wrap count += 1 }
}
EOF
cat >"$W/blk.verify.prp" <<'EOF'
const top = import("blk.blk")
formal blk.bad {
  mut acc = top
  assume_nocheck(acc.enable)
  assume_nocheck(not acc.enable)
  assert(acc.value != 9, "under a contradictory block")
}
EOF
OUT="$W/blkfork.out"
"$LHD" formal verify "$W/blk.prp" "$W/blk.verify.prp" --top blk --set formal.bound=4 >"$OUT" 2>&1
grep -q "block 'blk.bad'" "$OUT" \
  || fail "the fork path must attribute the contradictory assumes to block 'blk.bad' (Prop_result::scope lost in the codec): $(cat "$OUT")"
grep -q "contradictory assume set in the design" "$OUT" \
  && fail "the fork path blamed the DESIGN for a BLOCK's contradictory assumes: $(cat "$OUT")"

echo "PASS: 2f-verify V1-V3 + assume discipline (bounded/inductive ladder; refuted-at-cycle + trace; every assume checked-as-assert with the assume_nocheck escape; formal blocks + filter; timeout isolation; inconclusive/vacuous FAIL by default with the formal.strict=false opt-out; aliases; no vacuous pass; R1 if/elif/else/match property guards + antecedent vacuity + the 2026-07-26 review fixes)"
