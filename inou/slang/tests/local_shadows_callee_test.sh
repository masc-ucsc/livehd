#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# Verilog keeps modules and nets in separate namespaces, so a unit may declare
# `logic popcount` AND instantiate `popcount #(...) i_popcount` (CVA6's
# instr_queue does). The emitted Pyrope has ONE namespace: the file-scope
# `const popcount = import("popcount.popcount")` and the body's `mut popcount`
# collide, the local shadows the import, and the instantiation re-reads as a
# call to the local's VALUE:
#
#   upass.tolg: call to undefined function '0' -- no such pipe/mod/comb
#
# The writer already renames an import alias that collides with an INSTANCE
# name (`x` vs `mut x = X(...)`); this gate pins that a colliding body LOCAL
# (or port) gets the same treatment. Round trip, because the first leg emits
# fine either way -- the recompile is the load-bearing half.

set -u

LHD=lhd/lhd
SRC=inou/slang/tests/sv/local_shadows_callee.v
TOP=local_shadows_callee
W="${TEST_TMPDIR:-/tmp/local_shadows_callee_$$}"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

"$LHD" compile "$SRC" --reader slang --top "$TOP" \
  --emit-dir pyrope:"$W/prp" --workdir "$W/work" -q 2>/dev/null \
  || fail "verilog -> pyrope failed"

prp="$W/prp/$TOP.prp"
[ -s "$prp" ] || fail "Pyrope unit was not emitted"
[ -s "$W/prp/leaf.prp" ] || fail "the callee unit was not emitted"

# The import is still there, under an alias that is NOT the local's name, and
# the instantiation goes through that alias while the local keeps its name.
grep -q 'import("leaf.leaf")' "$prp" || fail "the callee import vanished"
alias=$(sed -n 's/^const \([A-Za-z_][A-Za-z0-9_]*\) = import("leaf.leaf")$/\1/p' "$prp" | head -1)
[ -n "$alias" ] || fail "could not read the import alias"
[ "$alias" != "leaf" ] || fail "import alias 'leaf' still collides with the local 'leaf'"
grep -qE "^  (mut|const|wire) leaf[: =]" "$prp" || fail "the local 'leaf' lost its name"
grep -q "= ${alias}::\[name=i_leaf\](" "$prp" || fail "the instantiation does not call the aliased import '${alias}'"

# ── and the emitted Pyrope actually recompiles ──────────────────────────────
out=$("$LHD" compile "$prp" --emit-dir lg:"$W/lg" --workdir "$W/rt" 2>&1) \
  || fail "emitted Pyrope does not recompile: $out"

echo "PASS: a local that shadows its callee's name no longer captures the instantiation"
