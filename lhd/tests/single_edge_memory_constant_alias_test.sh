#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# The memory slot gate must not restamp the graph's SHARED constants.
#
# `gu::create_const` delegates to `Graph::create_constant`, which returns a
# CANONICAL pin deduplicated by value representation (graph/node_util.hpp:326;
# pinned HHDS hhds/graph.hpp:1815), and `gu::set_bits` (node_util.hpp:838)
# writes that pin's own attribute.  A gate that mints a zero for a memory's
# quiet arm and then stamps `wensize` onto it therefore changes the width of
# the ONE zero every other consumer reads.
#
# Measured on the retained graphs at b6b0a5cb6: B11's shared zero went raw
# width 0 -> 69 and B03's 0 -> 27, while the Concat lanes reading it stayed 9
# and 1 bits wide -- and `pass.bitwidth` refused both designs.
#
# This fixture puts a shared zero in a 1-bit Concat lane AND gives two memories
# write-enable widths 4 and 9, so a stamping gate corrupts the lane.  The test
# asserts the lane and its metadata SURVIVE normalization, which an isolated
# mux-equality check could not see.
set -u

LHD="${LHD:-lhd/lhd}"
if [ ! -x "$LHD" ]; then
  if [ -x ./bazel-bin/lhd/lhd ]; then LHD=./bazel-bin/lhd/lhd; else
    echo "FAIL: could not find the lhd binary in $(pwd)"; exit 1; fi
fi
CENSUS="${CENSUS:-./bazel-bin/pass/lean/lean_graph_census}"

W="${TEST_TMPDIR:-}"
if [ -z "$W" ]; then
  W="$(cd "$(dirname "$0")/../.." && pwd)/generated/single_edge_const_alias.$$"
fi
mkdir -p "$W"
trap 'rm -rf "$W"' EXIT
fail() { echo "FAIL: $*"; exit 1; }

SRC="$(cd "$(dirname "$0")" && pwd)/rtl/mem_slot_constant_alias.v"
[ -f "$SRC" ] || fail "fixture RTL not found at $SRC"

rm -rf "$W/lg"
"$LHD" compile verilog "$SRC" --reader slang --top mem_slot_constant_alias \
  --emit-dir "lg:$W/lg" --workdir "$W/cw" >"$W/compile.log" 2>&1 \
  || { tail -10 "$W/compile.log"; fail "compile of the alias fixture failed"; }

# The census is REQUIRED, not optional: without it this test cannot observe the
# property it exists for.
[ -x "$CENSUS" ] || fail "lean_graph_census not found at $CENSUS (build //pass/lean:lean_graph_census); the metadata property cannot be checked without it"

# STALENESS GUARD.  `pass/single_edge/BUILD` declares a cc_library, so
# `bazel build //pass/single_edge:all` does NOT relink lhd.  A run against an
# lhd older than the pass source measures the OLD gate and proves nothing.
PASS_SRC="$(cd "$(dirname "$0")/.." && pwd)/pass/single_edge/pass_single_edge.cpp"
if [ -f "$PASS_SRC" ] && [ "$PASS_SRC" -nt "$LHD" ]; then
  fail "$LHD is OLDER than $PASS_SRC -- relink //lhd:lhd before running this test (a cc_library build does not)"
fi

census() { # <lgdir> -> stdout
  "$CENSUS" "$1" mem_slot_constant_alias 2>/dev/null \
    || "$CENSUS" "$1" mem_slot_constant_alias.mem_slot_constant_alias 2>/dev/null
}
census "$W/lg" > "$W/census_src.tsv" || true
[ -s "$W/census_src.tsv" ] || fail "the census of the SOURCE graph is empty"

# THE FIXTURE MUST REALLY BE THE ONE THIS TEST DESCRIBES.
WENS=$(awk -F'\t' '$1=="memory" && $3 ~ /wensize$/ {print $6}' "$W/census_src.tsv" | sort -un | tr '\n' ' ')
NW=$(awk -F'\t' '$1=="memory" && $3 ~ /wensize$/ {print $6}' "$W/census_src.tsv" | sort -un | awk '$1>1' | wc -l)
[ "$NW" -ge 2 ] \
  || fail "the fixture does not carry TWO DISTINCT wide write-enable widths (saw: ${WENS:-none}); it cannot exercise the restamping"
echo "ok: the fixture carries distinct wide write-enable widths: $WENS"

# …and it must really contain a NARROW consumer of a constant zero, or that
# consumer could have folded away and a passing emission would mean nothing.
#
# On B03/B11 that consumer is a `concat` lane (B11 node 3600 pid 2: a 9-bit
# window fed by the shared zero).  THIS frontend lowers the same source shape
# to `get_mask`/`shl` instead, so the check is on the PROPERTY -- a pre-existing
# consumer of the shared zero keeps its operand width and signedness -- and
# asserts a `concat` lane only when the graph actually has one.  The property is
# the same one the Concat window check refuses on.
python3 - "$W/census_src.tsv" > "$W/zero_lanes.txt" <<'PYEOF'
import sys
ops, rows = {}, []
for line in open(sys.argv[1]):
    f = line.rstrip('\n').split('\t')
    if f[0] == 'node':
        ops[f[1]] = f[2]
    elif f[0] == 'operand' and f[6] == 'constant' and f[8] == '0':
        rows.append(f)
for f in rows:
    print('\t'.join([f[1], f[2], f[4], f[5], ops.get(f[1], '?')]))  # nid, pid, width, signed, op
PYEOF
LANES=$(wc -l < "$W/zero_lanes.txt")
[ "$LANES" -ge 1 ] \
  || fail "no consumer in the SOURCE graph is driven by a constant zero -- the fixture folded it away and this test would prove nothing"
# the narrow anchor: the smallest wide write-enable width must EXCEED the
# declared width of at least one constant-zero consumer, or restamping could
# not break anything here.
MINWEN=$(awk -F'\t' '$1=="memory" && $3 ~ /wensize$/ {print $6}' "$W/census_src.tsv" | sort -un | awk '$1>1' | head -1)
NARROW=$(awk -F'\t' -v m="$MINWEN" '$3+0 < m+0 || $3=="0"' "$W/zero_lanes.txt" | wc -l)
[ "$NARROW" -ge 1 ] \
  || fail "no constant-zero consumer is narrower than the smallest wide write-enable width ($MINWEN); restamping could not break this fixture"
echo "ok: $LANES constant-zero consumer(s), $NARROW narrower than the smallest wide enable ($MINWEN)"
CONCATS=$(awk -F'\t' '$5=="concat"' "$W/zero_lanes.txt" | wc -l)
echo "note: $CONCATS of them are concat lanes (B03/B11's instance of this property)"

rm -rf "$W/lg_norm"
"$LHD" pass single_edge --top mem_slot_constant_alias "lg:$W/lg" \
  --emit-dir "lg:$W/lg_norm" --workdir "$W/pw" >"$W/pass.log" 2>&1 \
  || { tail -10 "$W/pass.log"; fail "pass single_edge refused the alias fixture"; }
grep -q "memory(ies) slotted" "$W/pass.log" \
  || { grep -o "P=[0-9]* slots[^\"]*" "$W/pass.log" | head -1; fail "no memory was slotted -- the fixture exercises nothing"; }
echo "ok: the fixture lowered, with its memories slotted"

census "$W/lg_norm" > "$W/census_norm.tsv" || true
[ -s "$W/census_norm.tsv" ] || fail "the census of the NORMALIZED graph is empty"

# THE PROPERTY: every Concat lane that was driven by a constant zero keeps the
# SAME declared width AND the SAME signedness.  This is what the stamping
# mutant breaks, and it is checked per node/pid rather than by any threshold.
python3 - "$W/zero_lanes.txt" "$W/census_norm.tsv" <<'PYEOF'
import sys
want = {}
for line in open(sys.argv[1]):
    parts = line.rstrip('\n').split('\t')
    want[(parts[0], parts[1])] = (parts[2], parts[3], parts[4])   # width, signed, src op
got, bad, rewired = {}, [], 0
for line in open(sys.argv[2]):
    f = line.rstrip('\n').split('\t')
    if f[0] == 'operand':
        got[(f[1], f[2])] = (f[4], f[5], f[6], f[8])
for k, (w, sg, op) in sorted(want.items()):
    g = got.get(k)
    # A pin that DISAPPEARS is a rewiring, which this pass does by design: it
    # drops a retyped flop's `posclk` and replaces a memory write port's
    # `enable` sink.  What must never happen is a pin that REMAINS changing its
    # declared width or signedness -- that is precisely what restamping the
    # shared constant does, and what the Concat window check refuses on.
    if g is None or g[2] != 'constant':
        rewired += 1
        continue
    if g[0] != w or g[1] != sg:
        print(f"CHANGED  node {k[0]} ({op}) pid {k[1]}: width {w}->{g[0]} signed {sg}->{g[1]} (value {g[3]})")
        bad.append(k)
if bad:
    print(f"{len(bad)} constant-zero operand(s) changed metadata")
    sys.exit(1)
print(f"all {len(want) - rewired} surviving constant-zero operand(s) kept their width and signedness "
      f"({rewired} rewired by the pass, as intended)")
PYEOF
[ $? -eq 0 ] || fail "a constant-zero operand changed width or signedness -- the shared pin was restamped"
echo "ok: every constant-zero operand kept its exact width and signedness"

# NO GENERATION LEG HERE, DELIBERATELY.
#
# Emission succeeding does NOT establish this property -- a lane can fold away,
# or an unrelated limit can stop the run, and either way the constant metadata
# would go unchecked.  (This fixture does stop later, at pass.lean's "RAM with
# initial contents" refusal, which is unrelated to constants.)  The assertion
# above is the property, checked per surviving operand against the pre-pass
# census.  B03 and B11 exercise the same property through a Concat window, and
# their generation gates are run separately.

echo "PASS: single_edge_memory_constant_alias_test"
