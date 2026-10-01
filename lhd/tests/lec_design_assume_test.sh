#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# Design-authored assumptions in hierarchical LEC.  The child implementations
# differ outside a<4, but their explicit child contract narrows the legal domain.
# The parent proof must inline/retain that occurrence contract instead of losing
# it when the child is represented as a hierarchy box.

set -u

LHD="${LHD:-lhd/lhd}"
[ -x "$LHD" ] || LHD=./bazel-bin/lhd/lhd
[ -x "$LHD" ] || { echo "FAIL: lhd binary not found" >&2; exit 3; }
W="${TEST_TMPDIR:-/tmp/lhd_lec_design_assume_$$}"
mkdir -p "$W"
fail() { echo "FAIL: $*" >&2; exit 1; }

cat >"$W/ref_sub.prp" <<'EOF'
pub comb sub(a:U8) -> (o:U8) {
  assume_nocheck(a < 4)
  o = a
}
EOF
cat >"$W/impl_sub.prp" <<'EOF'
pub comb sub(a:U8) -> (o:U8) {
  assume_nocheck(a < 4)
  o = a & 3
}
EOF
cat >"$W/ref_top.prp" <<'EOF'
const ref_sub = import("ref_sub")
pub comb ref_top(a:U8) -> (o:U8) { o = ref_sub.sub(a=a).o }
EOF
cat >"$W/impl_top.prp" <<'EOF'
const impl_sub = import("impl_sub")
pub comb impl_top(a:U8) -> (o:U8) { o = impl_sub.sub(a=a).o }
EOF

if ! "$LHD" lec --ref "$W/ref_top.prp" --impl "$W/impl_top.prp" \
     --ref-top ref_top --impl-top impl_top  \
     --set formal.engine=ind --workdir "$W/assumed_w" >"$W/assumed.out" 2>&1; then
  cat "$W/assumed.out" >&2
  fail "hierarchical LEC did not consume the child occurrence assumption"
fi
grep -qE 'PASS\(|PROVEN equivalent' "$W/assumed.out" \
  || { cat "$W/assumed.out" >&2; fail "assumed-domain run exited cleanly without a proof"; }
grep -q 'unchecked assume' "$W/assumed.out" \
  || { cat "$W/assumed.out" >&2; fail "LEC did not disclose the active design assumption"; }
echo "PASS: hierarchical LEC retained the child occurrence assumption"

# Removing the contracts exposes the real a>=4 mismatch and must refute.
grep -v assume_nocheck "$W/ref_sub.prp" >"$W/ref_plain_sub.prp"
grep -v assume_nocheck "$W/impl_sub.prp" >"$W/impl_plain_sub.prp"
sed 's/import("ref_sub")/import("ref_plain_sub")/' "$W/ref_top.prp" >"$W/ref_plain_top.prp"
sed 's/import("impl_sub")/import("impl_plain_sub")/' "$W/impl_top.prp" >"$W/impl_plain_top.prp"
if "$LHD" lec --ref "$W/ref_plain_top.prp" --impl "$W/impl_plain_top.prp" \
     --ref-top ref_top --impl-top impl_top  \
     --set formal.engine=ind --workdir "$W/plain_w" >"$W/plain.out" 2>&1; then
  cat "$W/plain.out" >&2
  fail "the out-of-domain implementation mismatch passed without assumptions"
fi
grep -q 'REFUTED' "$W/plain.out" \
  || { cat "$W/plain.out" >&2; fail "the no-assumption mismatch did not report REFUTED"; }
echo "PASS: removing the assumption exposes the mismatch"

# A plain `assume` on a selected-top input is a different animal from the
# assume_nocheck contract above: every plain assume is CHECKED (docs
# 05-assert), and over a free top input it refutes -- pass.formal reports
# assume-refuted and it never becomes a hypothesis. lec's obligation is
# TWO-sided over SHARED inputs: taking an unproved claim would narrow the miter
# of a comparison whose ref never made it -- `o = a & 3` vs `o = a` differ at
# every a >= 4 and would come back "PROVEN equivalent" with exit 0. lec must
# compare the full input space instead and REFUTE.
cat >"$W/uncheckable.prp" <<'EOF'
pub comb dut(a:U8) -> (o:U8) {
  assume(a < 4)
  o = a & 3
}
EOF
cat >"$W/golden.v" <<'EOF'
module dut(input [7:0] a, output [7:0] o);
  assign o = a;
endmodule
EOF
if "$LHD" lec --ref "$W/golden.v" --impl "$W/uncheckable.prp" --top dut \
     --workdir "$W/uncheckable_w" >"$W/uncheckable.out" 2>&1; then
  cat "$W/uncheckable.out" >&2
  fail "a refuted top IO assume must never become a LEC hypothesis"
fi
grep -q '"code":"assume-refuted"' "$W/uncheckable.out" \
  || { cat "$W/uncheckable.out" >&2; fail "the plain top IO assume must be checked and refuted"; }
grep -q 'assume_nocheck' "$W/uncheckable.out" \
  || { cat "$W/uncheckable.out" >&2; fail "the refute must name the sanctioned spelling"; }
grep -q "^lec: .* REFUTED" "$W/uncheckable.out" \
  || { cat "$W/uncheckable.out" >&2; fail "lec must compare the full input space and refute"; }
# The same on the --ref side, which is lowered FIRST: its assume-refuted is a
# deferred error (it fails the run) and must not abort the impl side's
# lowering before lec has compared the two.
if "$LHD" lec --ref "$W/uncheckable.prp" --impl "$W/golden.v" --top dut \
     --workdir "$W/uncheckable_ref_w" >"$W/uncheckable_ref.out" 2>&1; then
  cat "$W/uncheckable_ref.out" >&2
  fail "a refuted top IO assume on the ref side must never become a LEC hypothesis"
fi
grep -q '"code":"assume-refuted"' "$W/uncheckable_ref.out" \
  || { cat "$W/uncheckable_ref.out" >&2; fail "the ref side's plain top IO assume must be checked and refuted"; }
grep -q "^lec: .* REFUTED" "$W/uncheckable_ref.out" \
  || { cat "$W/uncheckable_ref.out" >&2; fail "lec must compare the full input space and refute with the assume on the ref side"; }
echo "PASS: lec checks a plain top IO assume instead of proving under it (either side)"

# ...and the refute is about the missing CONTRACT, not about assumptions in
# lec: either sanctioned spelling of the same constraint still narrows the miter
# and proves it (a & 3 == a for every a < 4).
sed 's/assume(/assume_nocheck(/' "$W/uncheckable.prp" >"$W/contracted.prp"
if ! "$LHD" lec --ref "$W/golden.v" --impl "$W/contracted.prp" --top dut \
     --workdir "$W/contracted_w" >"$W/contracted.out" 2>&1; then
  cat "$W/contracted.out" >&2
  fail "the assume_nocheck spelling must still constrain the miter and prove"
fi
grep -q 'PROVEN under 1 unchecked assume' "$W/contracted.out" \
  || { cat "$W/contracted.out" >&2; fail "the disclosed contract run must report its active assumption"; }
if ! "$LHD" lec --ref "$W/golden.v" --impl "$W/uncheckable.prp" --top dut \
     --set formal.assume_check=false --workdir "$W/nocheck_w" >"$W/nocheck.out" 2>&1; then
  cat "$W/nocheck.out" >&2
  fail "formal.assume_check=false must accept the same constraint"
fi
grep -q 'PROVEN under 1 unchecked assume' "$W/nocheck.out" \
  || { cat "$W/nocheck.out" >&2; fail "the assume_check=false run must report its active assumption"; }
echo "PASS: both sanctioned spellings still constrain the miter"

# An `lg:` side is not recompiled by lec, so the CLASS of an active assume comes
# from its compile-time stamp, not lec's own assume_check: a library built with
# formal.assume_check=false accepted the constraint without proof, and a later
# default lec (assume_check=true) must still disclose it as UNCHECKED, never as
# a proven fact.
if ! "$LHD" compile "$W/uncheckable.prp" --top dut --emit-dir "lg:$W/LG_NOCHECK" \
     --set formal.assume_check=false --workdir "$W/lg_nocheck_cw" >"$W/lg_nocheck_c.out" 2>&1; then
  cat "$W/lg_nocheck_c.out" >&2
  fail "compile with formal.assume_check=false to lg: failed"
fi
if ! "$LHD" lec --ref "$W/golden.v" --impl "lg:$W/LG_NOCHECK" --top dut \
     --workdir "$W/lg_nocheck_w" >"$W/lg_nocheck.out" 2>&1; then
  cat "$W/lg_nocheck.out" >&2
  fail "the lg: side's accepted constraint must still constrain the miter"
fi
grep -q 'PROVEN under 1 unchecked assume' "$W/lg_nocheck.out" \
  || { cat "$W/lg_nocheck.out" >&2; fail "an lg: assume accepted under assume_check=false must be disclosed as unchecked"; }
grep -q 'proven assume' "$W/lg_nocheck.out" \
  && { cat "$W/lg_nocheck.out" >&2; fail "an unproven lg: assume was labeled proven"; }
echo "PASS: an lg: side's unproven assume is disclosed as unchecked"

# Even a structurally identical self-LEC must check its active environment:
# two explicit constraints whose conjunction is empty may not be laundered by
# the hierarchy's no-solver structural-identity shortcut.
cat >"$W/contra_top.prp" <<'EOF'
pub comb contra_top(a:U8) -> (o:U8) {
  assume_nocheck(a < 4)
  assume_nocheck(a >= 4)
  o = a
}
EOF
if "$LHD" lec --ref "$W/contra_top.prp" --impl "$W/contra_top.prp" \
     --top contra_top  \
     --set formal.engine=ind --workdir "$W/contra_w" >"$W/contra.out" 2>&1; then
  cat "$W/contra.out" >&2
  fail "contradictory unchecked assumptions produced a vacuous pass"
fi
grep -q 'CONTRADICTORY' "$W/contra.out" \
  || { cat "$W/contra.out" >&2; fail "contradictory assumptions were not diagnosed"; }
echo "PASS: contradictory design assumptions reject vacuous LEC"
