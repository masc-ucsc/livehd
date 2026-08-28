#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# An instance inside a generate block must carry the generate prefix in its
# name, exactly as a net inside one does. Two instances of the same module in a
# genvar loop share `inst.name`; without the prefix both Subs -- and, after
# flattening, both of their REGISTERS -- land under one hierarchical name.
#
# The downstream damage is not cosmetic: pass/abc's register read-back
# disambiguates duplicate flop names with a `__dup1` suffix on the IMPL side
# only, and the post-synthesis LEC pairs state BY NAME, so the collision showed
# up as a hard rtl-vs-netlist REFUTED under pass.abc.register=true (the mode
# that carries flops into ABC for cross-register optimisation). 11 of
# lhdtrack's 149 netlist rows, all bedrock designs with a replicated sub-module.

set -u

LHD=./bazel-bin/lhd/lhd
if [ ! -x "$LHD" ]; then
  LHD=lhd/lhd
fi

SRC=inou/slang/tests/sv/genblk_instance_name.v
W="${TEST_TMPDIR:-/tmp/genblk_instance_name_$$}"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

"$LHD" compile "$SRC" --reader slang --top genblk_instance_name \
  --emit-dir lg:"$W/lg" --emit-dir pyrope:"$W/prp" --workdir "$W/w" -q >"$W/compile.log" 2>&1 \
  || { cat "$W/compile.log"; fail "compile of a genvar-loop instantiation failed"; }

# The two Subs must have DISTINCT names carrying the generate index.
names=$("$LHD" tool cat lg:"$W/lg" --max 0 2>/dev/null \
        | python3 -c '
import sys, json
out = []
for line in sys.stdin:
    try:
        d = json.loads(line)
    except Exception:
        continue
    if d.get("t") == "node" and d.get("kind") == "sub":
        out.append(d.get("name") or "")
print("\n".join(sorted(out)))')

count=$(printf '%s\n' "$names" | grep -c .)
distinct=$(printf '%s\n' "$names" | sort -u | grep -c .)

[ "$count" -eq 2 ] || { echo "$names"; fail "expected 2 sub instances, got $count"; }
[ "$distinct" -eq 2 ] || { echo "$names"; fail "the two genvar-loop instances share one name (the generate prefix was dropped)"; }

# And the prefix is the generate block's, with the index — the same spelling a
# net inside that block already gets.
printf '%s\n' "$names" | grep -q 'gen_lp_0' \
  || { echo "$names"; fail "instance 0 does not carry its generate-block index"; }
printf '%s\n' "$names" | grep -q 'gen_lp_1' \
  || { echo "$names"; fail "instance 1 does not carry its generate-block index"; }

# The emitted Pyrope must round-trip (a duplicate instance name would collide
# on the ::[name=] attribute too).
prp="$W/prp/genblk_instance_name.prp"
[ -s "$prp" ] || fail "no Pyrope emitted"
grep -q 'TODO' "$prp" && { cat "$prp"; fail "the emission left an unimplemented marker"; }

# ... and still be the same circuit.
lec=$("$LHD" lec --impl pyrope:"$W/prp" --ref verilog:"$SRC" --top genblk_instance_name \
      --workdir "$W/lec" 2>&1)
grep -qa "PROVEN equivalent\|PASS(" <<<"$lec" || fail "emitted Pyrope not proven equivalent: $lec"

echo "PASS: genvar-loop instances carry their generate index and stay distinct"
