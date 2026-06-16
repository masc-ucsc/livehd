#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# `lhd tool cat|diff` on the ln/source path (2f-cli; the former ln.cat/ln.diff).
# Payload on stdout, no result envelope unless --result-json. `tool cat` prints
# the post-upass tree for sources and the stored units for ln: dirs; `tool diff`
# prints a line diff plus the hhds tree edit distance (type+name cost; loc/fname
# ignored).

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

# 1. tool cat of a source: bare post-upass dump on stdout, nothing else.
#    Post-upass the comb fdef lives in its own unit (old.fun3).
"$LHD" tool cat "$W/old.prp" --workdir "$W/w1" -q >"$W/cat.out" 2>/dev/null || fail "tool cat exited nonzero"
head -1 "$W/cat.out" | grep -q '^old$' || fail "tool cat must start with the module-name line: $(head -1 "$W/cat.out")"
grep -q '^old.fun3$' "$W/cat.out" || fail "tool cat dump is missing the old.fun3 unit"
grep -q 'get_mask' "$W/cat.out" || fail "tool cat dump has no get_mask node (post-upass body)"
grep -q 'schema_version' "$W/cat.out" && fail "tool cat stdout must not carry the result envelope"

# 2. tool cat of an ln: dir prints the stored (post-parse) units.
"$LHD" compile "$W/old.prp" --emit-dir ln:"$W/lns/" --workdir "$W/w2" -q --result-json "$W/r.json" 2>/dev/null \
  || fail "compile to ln: failed: $(cat "$W/r.json")"
"$LHD" tool cat ln:"$W/lns/" -q >"$W/cat2.out" 2>/dev/null || fail "tool cat ln: exited nonzero"
head -1 "$W/cat2.out" | grep -q '^old$' || fail "tool cat ln: wrong first line: $(head -1 "$W/cat2.out")"

# 3. tool diff identical inputs: distance 0, no -/+ lines.
"$LHD" tool diff "$W/old.prp" "$W/old.prp" --workdir "$W/w3" -q >"$W/d0.out" 2>/dev/null || fail "tool diff same exited nonzero"
grep -q 'identical' "$W/d0.out" || fail "tool diff same: missing 'identical': $(cat "$W/d0.out")"
grep -q 'tree-edit-distance: 0' "$W/d0.out" || fail "tool diff same: distance not 0: $(cat "$W/d0.out")"
grep -qE '^[-+] ' "$W/d0.out" && fail "tool diff same: unexpected diff lines"

# 4. tool diff differing inputs: the changed const shows as -/+, distance 1.
"$LHD" tool diff "$W/old.prp" "$W/new.prp" --workdir "$W/w4" -q >"$W/d1.out" 2>/dev/null || fail "tool diff exited nonzero"
grep -q "^- .*const '1'" "$W/d1.out" || fail "tool diff: missing - const '1': $(cat "$W/d1.out")"
grep -q "^+ .*const '2'" "$W/d1.out" || fail "tool diff: missing + const '2': $(cat "$W/d1.out")"
grep -q 'tree-edit-distance: 1' "$W/d1.out" || fail "tool diff: expected distance 1: $(grep distance "$W/d1.out")"

# 5. tool diff against an ln: dir, --top selecting one unit per side.
"$LHD" tool diff ln:"$W/lns/" "$W/old.prp" --top old --workdir "$W/w5" -q >"$W/d2.out" 2>/dev/null \
  || fail "tool diff ln: vs prp --top exited nonzero"
grep -q 'tree-edit-distance:' "$W/d2.out" || fail "tool diff ln: vs prp: no distance line"

# 5b. Dead-store elimination: a comptime `mut` chain (`mut a=1; a+=1; a+=1`)
#     supersedes itself every write, so the coalescer must collapse it to a
#     single surviving store (= the final value).
printf 'mut a = 1\na += 1\na += 1\na += 1\n' > "$W/dse.prp"
"$LHD" tool cat "$W/dse.prp" --workdir "$W/w5b" -q >"$W/dse.out" 2>/dev/null || fail "tool cat dse exited nonzero"
dse_stores=$(grep -c '^[[:space:]]*[├└].* store' "$W/dse.out")
[ "$dse_stores" -eq 1 ] || fail "DSE: expected 1 surviving store, got $dse_stores: $(cat "$W/dse.out")"
grep -q "const '4'" "$W/dse.out" || fail "DSE: surviving store must hold the final value 4: $(cat "$W/dse.out")"

# 6. Usage validation: one-input diff and a missing verb are usage errors.
"$LHD" tool diff "$W/old.prp" -q >"$W/e2.json" 2>/dev/null
grep -q '"class":"usage"' "$W/e2.json" || fail "tool diff with one input must be a usage error: $(cat "$W/e2.json")"
"$LHD" tool -q >"$W/e0.json" 2>/dev/null
grep -q '"class":"usage"' "$W/e0.json" || fail "tool with no verb must be a usage error: $(cat "$W/e0.json")"

# 7. Unit-count mismatch: both unit lists named in the config error.
cat > "$W/twolam.prp" <<'EOF'
comb addone(a:u8) -> (z:u9) { z = a + 1 }
comb xorit(a:u8, b:u8) -> (z:u8) { z = a ^ b }
EOF
"$LHD" tool diff "$W/old.prp" "$W/twolam.prp" --workdir "$W/w7" -q >"$W/e3.json" 2>/dev/null
grep -q 'unit count mismatch' "$W/e3.json" || fail "tool diff unit mismatch must name the error: $(cat "$W/e3.json")"
grep -q 'twolam.addone' "$W/e3.json" || fail "tool diff mismatch error must list the unit names: $(cat "$W/e3.json")"

echo "PASS: tool cat / tool diff (ln path)"
