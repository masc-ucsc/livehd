#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# Regression: user-facing compile diagnostics must carry a SOURCE SPAN (file +
# line), not "span":null. The per-op upass passes resolve a diagnostic's span
# from the LNAST cursor, which is null whenever the cursor sits on a ref/const
# operand (no SourceId) or has run off the end of a `while (move_to_sibling())`
# walk. Lnast_manager::current_span() (→ Lnast::span_of_nearest) centralizes the
# fix: it falls back to the nearest srcid-bearing ancestor, so a diagnostic
# emitted from an operand child still reports its enclosing statement's line.
#
# Two layers of coverage:
#   1. line-exact checks for representative codes across the constprop /
#      typecheck / runner emit paths (each previously emitted "span":null);
#   2. a general sweep asserting NO emitted error carries a null span.
# In-process pyrope compile only — just //lhd.

set -u

LHD="$(pwd)/lhd/lhd"
W="${TEST_TMPDIR:-/tmp/lhd_diag_span_$$}"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

cd "$W" || fail "cd $W"

# The per-diagnostic JSON (carrying the span) is emitted on stderr; the run
# summary on stdout. Merge both so the parser sees the diagnostic line.
run_compile() {
  "$LHD" compile "$1" >out.json 2>&1
}

# check_code SRC CODE LINE: the FIRST emitted error/warning must have `code`,
# a non-null span naming SRC, and point at LINE.
check_code() {
  local src="$1" want_code="$2" want_line="$3"
  run_compile "$src"
  python3 - "$src" "$want_code" "$want_line" <<'PY'
import json, sys
src, want_code, want_line = sys.argv[1], sys.argv[2], int(sys.argv[3])
diag = None
for line in open("out.json"):
    line = line.strip()
    if not line.startswith("{"):
        continue
    try:
        rec = json.loads(line)
    except json.JSONDecodeError:
        continue
    if rec.get("severity") in ("error", "warning") and "code" in rec:
        diag = rec
        break
if diag is None:
    sys.exit("FAIL: {}: no diagnostic emitted".format(src))
if diag.get("code") != want_code:
    sys.exit("FAIL: {}: first diagnostic is {!r}, expected {!r}".format(src, diag.get("code"), want_code))
span = diag.get("span")
if not span:
    sys.exit("FAIL: {} ({}): null span (no file/line)".format(src, want_code))
if not span.get("file", "").endswith(src):
    sys.exit("FAIL: {} ({}): span.file {!r} does not end with {!r}".format(src, want_code, span.get("file"), src))
if span.get("start_line") != want_line:
    sys.exit("FAIL: {} ({}): span.start_line is {}, expected {}".format(src, want_code, span.get("start_line"), want_line))
print("ok: {:24} {} line {} ({})".format(want_code, src, want_line, diag["pass"]))
PY
  [ $? -eq 0 ] || exit 1
}

# assert_has_span SRC: EVERY emitted error must carry a non-null span on SRC
# (the general guard against a null-span regression in any path).
assert_has_span() {
  local src="$1"
  run_compile "$src"
  python3 - "$src" <<'PY'
import json, sys
src = sys.argv[1]
n_err = 0
for line in open("out.json"):
    line = line.strip()
    if not line.startswith("{"):
        continue
    try:
        rec = json.loads(line)
    except json.JSONDecodeError:
        continue
    if rec.get("severity") != "error" or "code" not in rec:
        continue
    n_err += 1
    span = rec.get("span")
    if not span:
        sys.exit("FAIL: {}: error {!r} has a null span".format(src, rec.get("code")))
    if not span.get("file", "").endswith(src):
        sys.exit("FAIL: {}: error {!r} span.file {!r} not on this source".format(src, rec.get("code"), span.get("file")))
if n_err == 0:
    sys.exit("FAIL: {}: expected at least one error".format(src))
print("ok: {} error(s) all spanned: {}".format(n_err, src))
PY
  [ $? -eq 0 ] || exit 1
}

# ── 1. line-exact checks (one per emit path) ────────────────────────────────

# array-element kind change — constprop check_field_store_kind, reached after
# the tuple_set sibling walk leaves the cursor invalid (the original report).
cat > arr_kind.prp <<'EOF'
mut a:[4]u4 = nil
a[0] = 1
a[1] = true
EOF
check_code arr_kind.prp assign-type-mismatch 3

# scalar kind change — typecheck emit_type_error (was an empty default span).
cat > scalar_kind.prp <<'EOF'
mut a:u4 = nil
a = 1
a = true
EOF
check_code scalar_kind.prp assign-type-mismatch 3

# self-assignment — runner, a Diagnostic that omitted .span entirely.
cat > self_assign.prp <<'EOF'
mut a:u4 = 3
a = a
EOF
check_code self_assign.prp irrelevant-assignment 2

# non-ascending range step — constprop, cursor on the step const child (the
# nearest-ancestor fallback recovers the func_call's line).
cat > range_step.prp <<'EOF'
const r = 0..=10 step 0
EOF
check_code range_step.prp invalid-range-step 1

# ── 2. general "no null span" sweep ─────────────────────────────────────────

assert_has_span arr_kind.prp
assert_has_span scalar_kind.prp
assert_has_span self_assign.prp
assert_has_span range_step.prp

echo "PASS"
