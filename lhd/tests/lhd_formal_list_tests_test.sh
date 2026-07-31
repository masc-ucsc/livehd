#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# `lhd formal verify` test-unit face: a `formal` block is an INDEPENDENT test, so
# it is enumerated and selected with the SAME syntax `lhd sim` gives its `test`
# blocks. Contract under test:
#   * `--list-tests` is a pure parse of the `formal` blocks -> their dotted names
#     as JSON (no design load, no solver — it works even against a design that
#     does not compile), and a human listing under --diag-fmt pretty;
#   * the JSON keeps sim's envelope ({"file":…,"tests":[{"name":…,"params":[]}]})
#     so one enumerator reads both commands, plus additive per-block fields;
#   * a LONE NON-PATH positional selects one block — anywhere among the
#     positionals — and narrows both `--list-tests` and a real proof run;
#   * it is the same fnmatch filter as --formal: a glob selects a family, and
#     passing both spellings at once is a usage error;
#   * a selector that matches NOTHING fails loudly (listing the real names)
#     instead of silently proving only the design's own obligations.

set -u

LHD="${LHD:-lhd/lhd}"
W="${TEST_TMPDIR:-/tmp/lhd_formal_lt_$$}"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

# A design plus a two-block sidecar. The blocks carry MUTUALLY EXCLUSIVE assumes
# (mode 0 vs mode 1) — legal because each block is its own test — so a run that
# leaked one block's assume into the other would go UNKNOWN and be visible here.
cat >"$W/alu.prp" <<'EOF'
mod alu(mode:bool, a:u8, b:u8) -> (y:u9@[0]) {
  y = if mode { u9(a) + u9(b) } else { u9(a) - u9(b) }
}
EOF

cat >"$W/alu.verify.prp" <<'EOF'
const top = import("alu.alu")

formal alu.sum {
  mut acc = top
  assume(acc.mode == true)
  assert(acc.a <= 255, "inputs are bytes")
}

formal alu.diff {
  mut acc = top
  assume(acc.mode == false)
  assert(acc.b <= 255)
}
EOF

# ---- --list-tests is a pure parse -> JSON (no design load, no solver) --------
LT="$("$LHD" formal verify "$W/alu.prp" "$W/alu.verify.prp" --top alu --list-tests --diag-fmt jsonl 2>/dev/null | head -1)" \
  || fail "--list-tests failed"
echo "$LT" | grep -q '"file":"'          || fail "--list-tests JSON missing the file field: $LT"
echo "$LT" | grep -q '"name":"alu.sum"'  || fail "--list-tests JSON missing alu.sum: $LT"
echo "$LT" | grep -q '"name":"alu.diff"' || fail "--list-tests JSON missing alu.diff: $LT"
# sim's envelope: a formal block has no parameter list, so params is always []
echo "$LT" | grep -q '"params":\[\]'     || fail "--list-tests JSON must keep sim's params field: $LT"
echo "$LT" | grep -q '"asserts":1'       || fail "--list-tests JSON missing the assert count: $LT"
echo "$LT" | grep -q '"assumes":1'       || fail "--list-tests JSON missing the assume count: $LT"

# ...even when the DESIGN does not COMPILE: listing parses the block sources and
# stops there — it never elaborates or lowers the design (nor calls a solver).
cat >"$W/broken.prp" <<'EOF'
mod broken(a:u8) -> (y:u8) {
  y = this_identifier_does_not_exist
}
EOF
"$LHD" formal verify "$W/broken.prp" "$W/alu.verify.prp" --top broken --set formal.bound=2 >"$W/broken.out" 2>&1
[ $? -ne 0 ] || fail "the broken design must fail a real verify run: $(cat "$W/broken.out")"
LTB="$("$LHD" formal verify "$W/broken.prp" "$W/alu.verify.prp" --top broken --list-tests --diag-fmt jsonl 2>/dev/null | head -1)" \
  || fail "--list-tests must not need a compilable design"
echo "$LTB" | grep -q '"name":"alu.sum"' || fail "--list-tests over a broken design lost the blocks: $LTB"

# pretty mode renders a human listing (not JSON), honoring --diag-fmt
PT="$("$LHD" formal verify "$W/alu.prp" "$W/alu.verify.prp" --top alu --list-tests --diag-fmt pretty 2>/dev/null)" \
  || fail "--list-tests pretty failed"
echo "$PT" | grep -q 'alu.sum'  || fail "pretty --list-tests missing alu.sum: $PT"
echo "$PT" | grep -q 'alu.diff' || fail "pretty --list-tests missing alu.diff: $PT"
echo "$PT" | grep -q '"name"'   && fail "pretty --list-tests must not emit raw JSON: $PT"

# ---- a lone non-path positional selects one block ---------------------------
LT1="$("$LHD" formal verify "$W/alu.prp" "$W/alu.verify.prp" alu.diff --top alu --list-tests --diag-fmt jsonl 2>/dev/null | head -1)" \
  || fail "--list-tests (selector) failed"
echo "$LT1" | grep -q '"name":"alu.diff"' || fail "selected --list-tests missing alu.diff: $LT1"
echo "$LT1" | grep -q '"name":"alu.sum"'  && fail "selected --list-tests must not include alu.sum: $LT1"

# the selector may sit anywhere among the positionals (the design is the first path)
LT2="$("$LHD" formal verify alu.diff "$W/alu.prp" "$W/alu.verify.prp" --top alu --list-tests --diag-fmt jsonl 2>/dev/null | head -1)" \
  || fail "--list-tests (leading selector) failed"
echo "$LT2" | grep -q '"name":"alu.diff"' || fail "a leading selector must still select: $LT2"
echo "$LT2" | grep -q '"name":"alu.sum"'  && fail "a leading selector must still exclude: $LT2"

# a glob selects a family (same fnmatch filter --formal uses)
LTG="$("$LHD" formal verify "$W/alu.prp" "$W/alu.verify.prp" 'alu.*' --top alu --list-tests --diag-fmt jsonl 2>/dev/null | head -1)" \
  || fail "--list-tests (glob selector) failed"
echo "$LTG" | grep -q '"name":"alu.sum"'  || fail "glob selector dropped alu.sum: $LTG"
echo "$LTG" | grep -q '"name":"alu.diff"' || fail "glob selector dropped alu.diff: $LTG"

# ---- the selector narrows a REAL proof run ----------------------------------
OUT="$W/all.out"
"$LHD" formal verify "$W/alu.prp" "$W/alu.verify.prp" --top alu --set formal.bound=2 >"$OUT" 2>&1 \
  || fail "the unselected run must pass: $(cat "$OUT")"
grep -q '\[alu.sum\]'  "$OUT" || fail "the unselected run must prove alu.sum: $(cat "$OUT")"
grep -q '\[alu.diff\]' "$OUT" || fail "the unselected run must prove alu.diff: $(cat "$OUT")"

OUT="$W/one.out"
"$LHD" formal verify "$W/alu.prp" "$W/alu.verify.prp" alu.sum --top alu --set formal.bound=2 >"$OUT" 2>&1 \
  || fail "the selected run must pass: $(cat "$OUT")"
grep -q '\[alu.sum\]'  "$OUT" || fail "the positional selector must keep alu.sum: $(cat "$OUT")"
grep -q '\[alu.diff\]' "$OUT" && fail "the positional selector must exclude alu.diff: $(cat "$OUT")"

# ---- an unmatched selector FAILS (never a silent design-only run) ------------
OUT="$W/miss.out"
"$LHD" formal verify "$W/alu.prp" "$W/alu.verify.prp" alu.nope --top alu --set formal.bound=2 >"$OUT" 2>&1
[ $? -ne 0 ] || fail "an unmatched selector must fail the run: $(cat "$OUT")"
grep -q "no formal block named 'alu.nope'" "$OUT" || fail "unmatched selector must name it: $(cat "$OUT")"
grep -q 'alu.sum'                          "$OUT" || fail "unmatched selector must list the real names: $(cat "$OUT")"

# ...and so does an unmatched --formal, which shares the filter
OUT="$W/miss2.out"
"$LHD" formal verify "$W/alu.prp" "$W/alu.verify.prp" --formal 'alu.nope' --top alu --set formal.bound=2 >"$OUT" 2>&1
[ $? -ne 0 ] || fail "an unmatched --formal must fail the run: $(cat "$OUT")"

# ---- selector and --formal are the same knob: passing both is an error -------
OUT="$W/conflict.out"
"$LHD" formal verify "$W/alu.prp" "$W/alu.verify.prp" alu.sum --formal 'alu.diff' --top alu >"$OUT" 2>&1
[ $? -ne 0 ] || fail "selector + --formal must be refused: $(cat "$OUT")"
grep -q 'conflicts with --formal' "$OUT" || fail "the conflict must explain itself: $(cat "$OUT")"

# two selectors are equally ambiguous
OUT="$W/two_sel.out"
"$LHD" formal verify "$W/alu.prp" "$W/alu.verify.prp" alu.sum alu.diff --top alu >"$OUT" 2>&1
[ $? -ne 0 ] || fail "two block selectors must be refused: $(cat "$OUT")"

# ---- a design with no block source cannot be listed -------------------------
OUT="$W/nolist.out"
"$LHD" formal verify "$W/alu.prp" --top alu --list-tests --diag-fmt pretty >"$OUT" 2>&1
[ $? -ne 0 ] || fail "--list-tests over a design with no formal blocks must fail: $(cat "$OUT")"
grep -q 'no formal blocks found' "$OUT" || fail "the empty listing must say so: $(cat "$OUT")"

echo "PASS: lhd formal verify --list-tests + positional block selection"
