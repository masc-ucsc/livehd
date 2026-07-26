#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# An `--emit-dir` may not name one of the run's own IR inputs.
#
# An emission REPLACES the directory: the declaration file (library.txt /
# forest.txt) is rewritten in full and body directories no longer in the
# artifact are pruned. Aliasing an input therefore destroys it in place, and
# before this check the run still exited 0 — the input library was silently
# replaced by the output. It must be a usage error instead.
#
# Covers: ln: in == ln: out, ln: in == lg: out, a non-normalized spelling of the
# same directory, and the IR-only (no source) shape. Plus the two legitimate
# shapes that must keep working: distinct in/out dirs, and ln:X + lg:X as two
# OUTPUTS of the same run (no input involved).
set -u

LHD="${LHD:-lhd/lhd}"
[ -x "$LHD" ] || LHD=./bazel-bin/lhd/lhd
[ -x "$LHD" ] || { echo "FAIL: lhd binary not found"; exit 3; }

W="${TEST_TMPDIR:-/tmp/lhd_emit_alias_$$}"
mkdir -p "$W"
rc=0
fail() { echo "FAIL: $*"; rc=1; }

cat > "$W/lib_thing.prp" <<'EOF'
pub mod thing(a:u8) -> (o:u8@[0]) { o = a }
EOF
cat > "$W/use.prp" <<'EOF'
const lib_thing = import("lib_thing")
pub mod use_top(a:u8) -> (o:u8@[0]) { o = lib_thing.thing(a=a).o }
EOF

# The input library the aliasing runs would clobber.
$LHD compile "$W/lib_thing.prp" --emit-dir "ln:$W/IN" --workdir "$W/w0" -q \
  || { echo "FAIL: could not build the ln: input"; exit 3; }
units_before=$(grep -c '^tree_io ' "$W/IN/forest.txt")

# reject LABEL <lhd args...> — must exit non-zero with a `usage` diagnostic.
reject() {
  local label=$1
  shift
  if "$LHD" "$@" >"$W/out.log" 2>&1; then
    fail "$label: expected a usage error, but the run succeeded"
    return
  fi
  if grep -q '"class":"usage"' "$W/out.log" && grep -q 'is also a' "$W/out.log"; then
    echo "ok: $label rejected"
  else
    fail "$label: failed, but not with the expected usage diagnostic:"
    tail -3 "$W/out.log" >&2
  fi
}

reject "ln: in == ln: out" compile "$W/use.prp" "ln:$W/IN" --emit-dir "ln:$W/IN" --workdir "$W/w1"
reject "ln: in == lg: out" compile "$W/use.prp" "ln:$W/IN" --emit-dir "lg:$W/IN" --workdir "$W/w2"
reject "non-normalized spelling" compile "$W/use.prp" "ln:$W/./IN/" --emit-dir "ln:$W/IN" --workdir "$W/w3"
reject "IR-only in == out" compile "ln:$W/IN" --emit-dir "ln:$W/IN" --workdir "$W/w4"

units_after=$(grep -c '^tree_io ' "$W/IN/forest.txt")
if [ "$units_before" = "$units_after" ]; then
  echo "ok: the input library was left intact ($units_after units)"
else
  fail "the input library was modified: $units_before -> $units_after units"
fi

# --- the shapes that must keep working --------------------------------------
if $LHD compile "$W/use.prp" "ln:$W/IN" --emit-dir "ln:$W/OUT" --workdir "$W/w5" -q; then
  echo "ok: distinct in/out directories still compile"
else
  fail "distinct in/out directories were rejected"
fi

if $LHD compile "$W/lib_thing.prp" --emit-dir "ln:$W/BOTH" --emit-dir "lg:$W/BOTH" \
     --workdir "$W/w6" -q; then
  echo "ok: ln: and lg: sharing one OUTPUT directory still compile"
else
  fail "ln:X + lg:X as two outputs was rejected (no input is involved)"
fi

exit $rc
