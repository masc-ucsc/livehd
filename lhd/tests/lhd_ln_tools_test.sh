#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# ln.cat / ln.diff: the LNAST debug tools (payload on stdout, no result
# envelope unless --result-json). ln.cat prints the post-upass tree for
# sources and the stored units for ln: dirs; ln.diff prints a line diff plus
# the hhds tree edit distance (type+name cost; loc/fname ignored).

set -u

LHD=lhd/lhd
PRP=inou/prp/tests/equiv/trivial_if.prp
W="${TEST_TMPDIR:-/tmp/lhd_ln_tools_$$}"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

cp "$PRP" "$W/old.prp"
sed 's/+ 1/+ 2/' "$PRP" > "$W/new.prp"
grep -q '+ 2' "$W/new.prp" || fail "test setup: sed produced no change"

# 1. ln.cat of a source: bare post-upass dump on stdout, nothing else.
#    Post-upass the comb fdef lives in its own unit (old.fun3).
"$LHD" ln.cat "$W/old.prp" --workdir "$W/w1" -q >"$W/cat.out" 2>/dev/null || fail "ln.cat exited nonzero"
head -1 "$W/cat.out" | grep -q '^old$' || fail "ln.cat must start with the module-name line: $(head -1 "$W/cat.out")"
grep -q '^old.fun3$' "$W/cat.out" || fail "ln.cat dump is missing the old.fun3 unit"
grep -q 'get_mask' "$W/cat.out" || fail "ln.cat dump has no get_mask node (post-upass body)"
grep -q 'schema_version' "$W/cat.out" && fail "ln.cat stdout must not carry the result envelope"

# 2. ln.cat of an ln: dir prints the stored (post-parse) units.
"$LHD" elaborate "$W/old.prp" --emit-dir ln:"$W/lns/" --workdir "$W/w2" -q --result-json "$W/r.json" 2>/dev/null \
  || fail "elaborate to ln: failed: $(cat "$W/r.json")"
"$LHD" ln.cat ln:"$W/lns/" -q >"$W/cat2.out" 2>/dev/null || fail "ln.cat ln: exited nonzero"
head -1 "$W/cat2.out" | grep -q '^old$' || fail "ln.cat ln: wrong first line: $(head -1 "$W/cat2.out")"

# 3. ln.diff identical inputs: distance 0, no -/+ lines.
"$LHD" ln.diff "$W/old.prp" "$W/old.prp" --workdir "$W/w3" -q >"$W/d0.out" 2>/dev/null || fail "ln.diff same exited nonzero"
grep -q 'identical' "$W/d0.out" || fail "ln.diff same: missing 'identical': $(cat "$W/d0.out")"
grep -q 'tree-edit-distance: 0' "$W/d0.out" || fail "ln.diff same: distance not 0: $(cat "$W/d0.out")"
grep -qE '^[-+] ' "$W/d0.out" && fail "ln.diff same: unexpected diff lines"

# 4. ln.diff differing inputs: the changed const shows as -/+, distance 1.
"$LHD" ln.diff "$W/old.prp" "$W/new.prp" --workdir "$W/w4" -q >"$W/d1.out" 2>/dev/null || fail "ln.diff exited nonzero"
grep -q "^- .*const '1'" "$W/d1.out" || fail "ln.diff: missing - const '1': $(cat "$W/d1.out")"
grep -q "^+ .*const '2'" "$W/d1.out" || fail "ln.diff: missing + const '2': $(cat "$W/d1.out")"
grep -q 'tree-edit-distance: 1' "$W/d1.out" || fail "ln.diff: expected distance 1: $(grep distance "$W/d1.out")"

# 5. ln.diff against an ln: dir, --top selecting one unit per side.
"$LHD" ln.diff ln:"$W/lns/" "$W/old.prp" --top old --workdir "$W/w5" -q >"$W/d2.out" 2>/dev/null \
  || fail "ln.diff ln: vs prp --top exited nonzero"
grep -q 'tree-edit-distance:' "$W/d2.out" || fail "ln.diff ln: vs prp: no distance line"

# 6. Stage/kind validation: lg: inputs and --dump are usage errors.
"$LHD" ln.cat lg:"$W/lgs/" -q >"$W/e1.json" 2>/dev/null
grep -q '"class":"usage"' "$W/e1.json" || fail "ln.cat lg: must be a usage error: $(cat "$W/e1.json")"
"$LHD" ln.diff "$W/old.prp" -q >"$W/e2.json" 2>/dev/null
grep -q '"class":"usage"' "$W/e2.json" || fail "ln.diff with one input must be a usage error: $(cat "$W/e2.json")"

echo "PASS: ln.cat / ln.diff"
