#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# run_id covers the Pyrope import closure: a compile reads every file its
# imports resolve to (transitively), so those bytes are run inputs just like
# the files named on the command line. Editing an imported file must move the
# run_id, two trees that share a byte-identical harness but import different
# designs must not share one, and swapping lec --impl/--ref is a different run.
# The hash stays path-free: the same tree copied elsewhere keeps its run_id.

set -u

LHD="$(pwd)/lhd/lhd"
W="${TEST_TMPDIR:-/tmp/lhd_runid_import_$$}"
mkdir -p "$W"

fail() { echo "FAIL: $*" >&2; exit 1; }

# rid DIR [extra args]: compile DIR/t.prp from DIR, print the run_id.
rid() {
  local dir=$1
  shift
  (cd "$dir" && "$LHD" compile t.prp --top t -q --result-json r.json "$@" >/dev/null 2>&1)
  sed 's/.*"run_id":"\([^"]*\)".*/\1/' "$dir/r.json"
}

mk_tree() {  # DIR ADD: t -> mid -> leaf (transitive), plus an unrelated file
  mkdir -p "$1"
  cat > "$1/leaf.prp" <<EOF
pub comb inc(a:U8) -> (y:U9) { y = a + $2 }
EOF
  cat > "$1/mid.prp" <<'EOF'
const inc = import("leaf.inc")
pub comb twice(a:U8) -> (y:U9) { wrap y = inc(a=a) }
EOF
  cat > "$1/t.prp" <<'EOF'
const twice = import("mid.twice")
pub mod t(a:U8) -> (y:U9@[0]) { y = twice(a=a) }
EOF
  cat > "$1/unrelated.prp" <<'EOF'
pub comb unused(a:U8) -> (y:U8) { y = a }
EOF
}

mk_tree "$W/a" 1
base=$(rid "$W/a")
[ -n "$base" ] || fail "empty run_id: $(cat "$W/a/r.json")"
[ "$base" = "$(rid "$W/a")" ] || fail "run_id not deterministic"

# A file the design never imports is not an input.
echo "// comment" >> "$W/a/unrelated.prp"
[ "$base" = "$(rid "$W/a")" ] || fail "an edit of a file nobody imports moved the run_id"

# An edit two imports deep moves it; undoing the edit restores it.
sed 's/a + 1/a + 2/' "$W/a/leaf.prp" > "$W/a/leaf.tmp" && mv "$W/a/leaf.tmp" "$W/a/leaf.prp"
edited=$(rid "$W/a")
[ -n "$edited" ] && [ "$edited" != "$base" ] || fail "an edit of a transitively imported file did not move the run_id ($base)"
sed 's/a + 2/a + 1/' "$W/a/leaf.prp" > "$W/a/leaf.tmp" && mv "$W/a/leaf.tmp" "$W/a/leaf.prp"
[ "$base" = "$(rid "$W/a")" ] || fail "undoing the imported-file edit did not restore the run_id"

# Path-free: the same bytes elsewhere keep the run_id; a tree whose harness
# t.prp is byte-identical but imports a different design does not.
mk_tree "$W/b" 1
[ "$base" = "$(rid "$W/b")" ] || fail "the same tree at another path changed the run_id"
mk_tree "$W/c" 3
cmp -s "$W/a/t.prp" "$W/c/t.prp" || fail "harness files differ (test setup)"
[ "$base" != "$(rid "$W/c")" ] || fail "two designs sharing a byte-identical harness share a run_id"

# lec: the side role is part of the run; swapping --impl and --ref is another
# proof (a direction-sensitive one when either side carries assumptions).
cp "$W/a/leaf.prp" "$W/other_leaf.prp"
lec_rid() {  # IMPL REF JSON
  "$LHD" lec --impl "$1" --ref "$2" --top leaf.inc --impl-top "$(basename "$1" .prp).inc" \
    --ref-top "$(basename "$2" .prp).inc" -q --result-json "$3" --workdir "$W/wl" \
    --set formal.timeout=60 >/dev/null 2>&1
  sed 's/.*"run_id":"\([^"]*\)".*/\1/' "$3"
}
fwd=$(lec_rid "$W/a/leaf.prp" "$W/other_leaf.prp" "$W/l1.json")
rev=$(lec_rid "$W/other_leaf.prp" "$W/a/leaf.prp" "$W/l2.json")
[ -n "$fwd" ] && [ -n "$rev" ] || fail "lec runs missing run_id: $(cat "$W/l1.json")"
[ "$fwd" != "$rev" ] || fail "swapping lec --impl and --ref kept the run_id ($fwd)"

echo "PASS"
