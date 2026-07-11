#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# run_id slice scoping: a lec --ref/--impl hhds graph-library directory hashes
# only its per-side --top slice (the top graph plus transitive Sub
# dependencies) into the run_id. A whole-design library holds every module, so
# an edit to a module OUTSIDE the proven subtree must not move the run_id,
# while an edit to a child INSIDE it must. A top that cannot be resolved falls
# back to whole-directory hashing (and still yields a run_id, never a crash).

set -u

LHD=lhd/lhd
W="${TEST_TMPDIR:-/tmp/lhd_runid_slice_$$}"
mkdir -p "$W"

fail() { echo "FAIL: $*" >&2; exit 1; }

cat > "$W/des.prp" <<'EOF'
pub comb kid(a:u4) -> (o:u5) {
  o = a + 1
}

pub comb par(a:u4) -> (o:u6) {
  o = kid(a=a) + 1
}
EOF
cat > "$W/orphan.prp" <<'EOF'
pub comb orphan(a:u4) -> (o:u5) {
  o = a + 2
}
EOF
cp "$W/des.prp" "$W/impl.prp"  # impl bytes stay fixed: only the ref lib moves

"$LHD" compile "$W/des.prp" --emit-dir lg:"$W/lib" -q --workdir "$W/wc1" >/dev/null 2>&1 \
  || fail "compile des.prp"
"$LHD" compile "$W/orphan.prp" --emit-dir lg:"$W/lib" -q --workdir "$W/wc2" >/dev/null 2>&1 \
  || fail "compile orphan.prp"

# run <json>: lec des.par (ref lib) vs the fixed impl copy; prints the run_id.
run() {
  "$LHD" lec --top des.par --impl-top impl.par --ref lg:"$W/lib" --impl "$W/impl.prp" \
    -q --result-json "$1" --workdir "$W/wl" >/dev/null 2>&1 \
    || fail "lec run: $(cat "$1" 2>/dev/null)"
  sed 's/.*"run_id":"\([^"]*\)".*/\1/' "$1"
}

id_base=$(run "$W/r1.json")
[ -n "$id_base" ] || fail "empty baseline run_id"

id_again=$(run "$W/r2.json")
[ "$id_base" = "$id_again" ] || fail "run_id not deterministic: '$id_base' vs '$id_again'"

# An out-of-slice edit (orphan is not reachable from des.par) must not move
# it. Same filename: graph names are file.entity, so an in-place edit is what
# updates the existing orphan.orphan graph.
sed 's/a + 2/a + 3/' "$W/orphan.prp" > "$W/orphan.tmp" && mv "$W/orphan.tmp" "$W/orphan.prp"
"$LHD" compile "$W/orphan.prp" --emit-dir lg:"$W/lib" -q --workdir "$W/wc3" >/dev/null 2>&1 \
  || fail "recompile orphan"
id_orphan=$(run "$W/r3.json")
[ "$id_base" = "$id_orphan" ] || fail "unrelated-module edit moved run_id: '$id_base' vs '$id_orphan'"

# An in-slice edit (kid is a Sub of des.par) must move it.
sed 's/o = a + 1/o = (a + 1) | 0/' "$W/des.prp" > "$W/des.tmp" && mv "$W/des.tmp" "$W/des.prp"
"$LHD" compile "$W/des.prp" --emit-dir lg:"$W/lib" -q --workdir "$W/wc4" >/dev/null 2>&1 \
  || fail "recompile des"
id_kid=$(run "$W/r4.json")
[ "$id_base" != "$id_kid" ] || fail "child (Sub) edit did not move run_id: '$id_kid'"

# Unresolvable top: whole-directory fallback — a proper lec config error, with
# a run_id still stamped in the envelope.
"$LHD" lec --top nosuch_mod --ref lg:"$W/lib" --impl "$W/impl.prp" \
  -q --result-json "$W/r5.json" --workdir "$W/wf" >/dev/null 2>&1 \
  && fail "lec with unknown top unexpectedly passed"
grep -q '"run_id":"[0-9a-f]' "$W/r5.json" || fail "fallback run missing run_id: $(cat "$W/r5.json")"
grep -q '"class":"config"' "$W/r5.json" || fail "unknown top not a config error: $(cat "$W/r5.json")"

# IO declarations live only in library.txt (bodies carry no port names/bits),
# and lec pairs pins by name — a decl-only edit inside the slice must move the
# run_id even though every graph_<gid>/ dir is byte-identical.
sed 's/bits=4/bits=9/' "$W/lib/library.txt" > "$W/lib/library.tmp" && mv "$W/lib/library.tmp" "$W/lib/library.txt"
id_decl=$("$LHD" lec --top des.par --impl-top impl.par --ref lg:"$W/lib" --impl "$W/impl.prp" \
  -q --result-json "$W/r6.json" --workdir "$W/wd" >/dev/null 2>&1; \
  sed 's/.*"run_id":"\([^"]*\)".*/\1/' "$W/r6.json")
[ -n "$id_decl" ] || fail "decl-edit run missing run_id: $(cat "$W/r6.json")"
[ "$id_kid" != "$id_decl" ] || fail "library.txt IO-decl edit did not move run_id: '$id_decl'"

# Per-side tops are proof obligations: two runs differing only in --impl-top
# must not share a run_id (the impl side here is a plain file).
ia=$("$LHD" lec --top des.par --impl-top impl.par --ref lg:"$W/lib" --impl "$W/impl.prp" \
  -q --result-json "$W/r7.json" --workdir "$W/wt1" >/dev/null 2>&1; \
  sed 's/.*"run_id":"\([^"]*\)".*/\1/' "$W/r7.json")
ib=$("$LHD" lec --top des.par --impl-top impl.kid --ref lg:"$W/lib" --impl "$W/impl.prp" \
  -q --result-json "$W/r8.json" --workdir "$W/wt2" >/dev/null 2>&1; \
  sed 's/.*"run_id":"\([^"]*\)".*/\1/' "$W/r8.json")
[ -n "$ia" ] && [ -n "$ib" ] || fail "impl-top runs missing run_id"
[ "$ia" != "$ib" ] || fail "--impl-top change did not move run_id: '$ia'"

echo "PASS"
