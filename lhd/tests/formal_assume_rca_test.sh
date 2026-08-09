#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# A structural ripple-carry adder has an explicit top-level environment contract
# that its carry input is one.  Under that contract it is equivalent to a flat adder
# with +1 hard-coded, and its companion assertion is true.  Removing only the
# contract must refute both LEC and verify, which keeps the positive cases from
# passing because the assumption was ignored.

set -u

LHD="${LHD:-lhd/lhd}"
[ -x "$LHD" ] || LHD=./bazel-bin/lhd/lhd
[ -x "$LHD" ] || { echo "FAIL: lhd binary not found" >&2; exit 3; }
W="${TEST_TMPDIR:-/tmp/lhd_formal_assume_rca_$$}"
mkdir -p "$W"
fail() { echo "FAIL: $*" >&2; exit 1; }

cat >"$W/rca_assumed.prp" <<'EOF'
comb full_adder(a:bool, b:bool, carry_in:bool) -> (result:u2) {
  const sum = a ^ b ^ carry_in
  const carry_out = (a & b) | (a & carry_in) | (b & carry_in)
  result = sum | (carry_out << 1)
}

comb rca(a:u4, b:u4, carry_in:bool) -> (sum:u5) {
  const lane0 = full_adder(a=a#[0], b=b#[0], carry_in=carry_in)
  const lane1 = full_adder(a=a#[1], b=b#[1], carry_in=(lane0 & 2) != 0)
  const lane2 = full_adder(a=a#[2], b=b#[2], carry_in=(lane1 & 2) != 0)
  const lane3 = full_adder(a=a#[3], b=b#[3], carry_in=(lane2 & 2) != 0)

  sum = (lane0 & 1)
      | ((lane1 & 1) << 1)
      | ((lane2 & 1) << 2)
      | ((lane3 & 1) << 3)
      | ((lane3 & 2) << 3)
}

pub comb rca_top(a:u4, b:u4, carry_in:bool) -> (sum:u5) {
  assume_nocheck(carry_in)
  sum = rca(a=a, b=b, carry_in=carry_in)
}
EOF

cat >"$W/plus_one.prp" <<'EOF'
pub comb plus_one(a:u4, b:u4, carry_in:bool) -> (sum:u5) {
  sum = u5(a) + u5(b) + 1
}
EOF

cat >"$W/rca_assumed.verify.prp" <<'EOF'
const top = import("rca_assumed.rca_top")
formal carry_in_one {
  mut acc = top
  assert(acc.sum == u5(acc.a) + u5(acc.b) + 1, "RCA matches hard-coded +1")
}
EOF

# The selected-top contract must constrain the shared carry input while the RCA
# itself remains hierarchical.  The flat implementation deliberately ignores
# that input.
if ! "$LHD" lec --ref "$W/rca_assumed.prp" --impl "$W/plus_one.prp" \
     --ref-top rca_top --impl-top plus_one --set formal.lec.hier=true \
     --set formal.engine=ind --workdir "$W/lec_assumed_w" >"$W/lec_assumed.out" 2>&1; then
  cat "$W/lec_assumed.out" >&2
  fail "RCA did not match hard-coded +1 under carry_in==true"
fi
grep -qE 'PASS\(|PROVEN equivalent' "$W/lec_assumed.out" \
  || { cat "$W/lec_assumed.out" >&2; fail "assumed RCA LEC exited cleanly without a proof"; }
grep -q 'unchecked assume' "$W/lec_assumed.out" \
  || { cat "$W/lec_assumed.out" >&2; fail "RCA LEC did not disclose its active carry contract"; }

# Removing only the contract admits carry_in==false.  The RCA then computes
# a+b while the other side still computes a+b+1, so LEC must find a witness.
grep -v assume_nocheck "$W/rca_assumed.prp" >"$W/rca_plain.prp"
if "$LHD" lec --ref "$W/rca_plain.prp" --impl "$W/plus_one.prp" \
     --ref-top rca_top --impl-top plus_one --set formal.lec.hier=true \
     --set formal.engine=ind --workdir "$W/lec_plain_w" >"$W/lec_plain.out" 2>&1; then
  cat "$W/lec_plain.out" >&2
  fail "RCA unexpectedly matched hard-coded +1 without the carry contract"
fi
grep -q 'REFUTED' "$W/lec_plain.out" \
  || { cat "$W/lec_plain.out" >&2; fail "no-assumption RCA LEC did not report REFUTED"; }
grep -q 'carry_in=0' "$W/lec_plain.out" \
  || { cat "$W/lec_plain.out" >&2; fail "no-assumption RCA LEC witness did not use carry_in=0"; }

# Verify must consume the same design-authored top-level assumption.
if ! "$LHD" formal verify "$W/rca_assumed.prp" "$W/rca_assumed.verify.prp" \
     --top rca_top --workdir "$W/verify_assumed_w" \
     >"$W/verify_assumed.out" 2>&1; then
  cat "$W/verify_assumed.out" >&2
  fail "RCA +1 assertion did not prove under carry_in==true"
fi
grep -q 'RCA matches hard-coded +1.*PROVEN' "$W/verify_assumed.out" \
  || { cat "$W/verify_assumed.out" >&2; fail "assumed RCA verify did not report PROVEN"; }
grep -q 'in force (UNCHECKED assume_nocheck' "$W/verify_assumed.out" \
  || { cat "$W/verify_assumed.out" >&2; fail "verify did not disclose the active carry contract"; }

# Point the same formal block at the assumption-free source.  Its assertion is
# now false for carry_in==false and therefore must refute.
sed 's/rca_assumed\.rca_top/rca_plain.rca_top/' \
  "$W/rca_assumed.verify.prp" >"$W/rca_plain.verify.prp"
if "$LHD" formal verify "$W/rca_plain.prp" "$W/rca_plain.verify.prp" \
     --top rca_top --workdir "$W/verify_plain_w" \
     >"$W/verify_plain.out" 2>&1; then
  cat "$W/verify_plain.out" >&2
  fail "RCA +1 assertion unexpectedly passed without the carry contract"
fi
grep -q 'RCA matches hard-coded +1.*REFUTED' "$W/verify_plain.out" \
  || { cat "$W/verify_plain.out" >&2; fail "no-assumption RCA verify did not report REFUTED"; }
grep -q 'carry_in=0' "$W/verify_plain.out" \
  || { cat "$W/verify_plain.out" >&2; fail "no-assumption RCA verify witness did not use carry_in=0"; }

echo "PASS: carry_in assume_nocheck is required by RCA +1 LEC and verify"
