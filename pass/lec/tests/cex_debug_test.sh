#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# Stage-0 cex-debug (todo/livehd/2d-cex_debug): on a REFUTE, the verdict must name
# the EARLIEST diverging INTERNAL state cut (the ROOT) ahead of the inherited
# primary output. A `reg` that mis-holds on its un-covered path diverges internally
# first; the output merely inherits it. The first-diverging-cut scan reads the
# per-cycle paired flop state straight from the already-solved SAT model (pure
# getValue — no new SMT terms, no UNKNOWN risk).

set -u

LHD=./bazel-bin/lhd/lhd
if [ ! -x "$LHD" ]; then
  if [ -x ./lhd/lhd ]; then LHD=./lhd/lhd; else
    echo "FAIL: could not find the lhd binary in $(pwd)"; exit 1; fi
fi

WORK="${TEST_TMPDIR:-/tmp/cexdbg}"
mkdir -p "$WORK/a" "$WORK/b"
fail=0

# ref: `s` HOLDS on the !inc path (the correct conditional-reg semantics).
cat > "$WORK/a/m.prp" <<'EOF'
mod m(inc:bool, val:u8) -> (out:u8@[0]) {
  reg s:u8 = 0
  if inc { s = s + val }
  out = s
}
EOF
# impl (buggy): the un-covered path mis-writes `s = val` instead of holding.
cat > "$WORK/b/m.prp" <<'EOF'
mod m(inc:bool, val:u8) -> (out:u8@[0]) {
  reg s:u8 = 0
  if inc { s = s + val } else { s = val }
  out = s
}
EOF

OUT=$($LHD lec --ref "pyrope:$WORK/a/m.prp" --impl "pyrope:$WORK/b/m.prp" --top m.m --workdir "$WORK/q_$$" 2>&1)

if echo "$OUT" | grep -q "REFUTED"; then
  echo "ok: REFUTED"
else
  echo "FAIL: expected REFUTED"; echo "$OUT" | tail -2; fail=1
fi

# The ROOT must be the internal reg `s`, named ahead of the inherited output.
# (The root clause lists ALL cuts diverging at the earliest cycle, sorted —
# `s(` must appear within it.)
if echo "$OUT" | grep -q "STATE cut(s) (root): .*s("; then
  echo "ok: first diverging internal cut = s (root)"
else
  echo "FAIL: witness did not name the internal root cut 's'"; fail=1
fi

# The inherited primary output must still be carried.
if echo "$OUT" | grep -q "out(ref="; then
  echo "ok: inherited output 'out' carried"
else
  echo "FAIL: witness did not carry the inherited output 'out'"; fail=1
fi

if [ $fail -ne 0 ]; then echo "cex_debug_test: FAILED"; exit 1; fi
echo "cex_debug_test: PASSED"
exit 0
