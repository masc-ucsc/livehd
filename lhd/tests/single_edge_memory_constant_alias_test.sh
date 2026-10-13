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

# the fixture must really contain two DIFFERENT write-enable widths, or it
# cannot exercise the restamping at all
if [ -x "$CENSUS" ]; then
  "$CENSUS" "$W/lg" mem_slot_constant_alias > "$W/census_src.tsv" 2>/dev/null \
    || "$CENSUS" "$W/lg" mem_slot_constant_alias.mem_slot_constant_alias > "$W/census_src.tsv" 2>/dev/null
  WENS=$(awk -F'\t' '$1=="memory" && $3 ~ /wensize$/ {print $6}' "$W/census_src.tsv" | sort -u | tr '\n' ' ')
  echo "note: write-enable widths in the fixture: ${WENS:-<none found>}"
fi

rm -rf "$W/lg_norm"
"$LHD" pass single_edge --top mem_slot_constant_alias "lg:$W/lg" \
  --emit-dir "lg:$W/lg_norm" --workdir "$W/pw" >"$W/pass.log" 2>&1 \
  || { tail -10 "$W/pass.log"; fail "pass single_edge refused the alias fixture"; }
grep -q "memory(ies) slotted" "$W/pass.log" \
  || { grep -o "P=[0-9]* slots[^\"]*" "$W/pass.log" | head -1; fail "no memory was slotted -- the fixture exercises nothing"; }
echo "ok: the fixture lowered, with its memories slotted"

# THE PROPERTY.  A later pass reads every operand's width against its declared
# window; a restamped shared zero is exactly what it refuses.  Running the
# generation gate is therefore the end-to-end check, and it is what failed on
# B03/B11 at b6b0a5cb6.
rm -rf "$W/lean"
if ! "$LHD" compile "lg:$W/lg_norm" --top mem_slot_constant_alias \
      --workdir "$W/lw" --emit-dir "lean:$W/lean" \
      --set formal.lean.strict=true --set formal.lean.emit_cert=true \
      --set formal.lean.mode=verified_compiler >"$W/lean.log" 2>&1; then
  if grep -qi "concat" "$W/lean.log"; then
    grep -oiE "Concat lane[^\"]*" "$W/lean.log" | head -1
    fail "a Concat lane was refused after normalization -- a shared constant's width was restamped"
  fi
  tail -6 "$W/lean.log"
  fail "generation from the normalized alias fixture failed"
fi
echo "ok: generation from the normalized graph succeeds -- no Concat lane was widened"

# …and the narrow lane is still narrow in the normalized graph itself.
if [ -x "$CENSUS" ]; then
  "$CENSUS" "$W/lg_norm" mem_slot_constant_alias > "$W/census_norm.tsv" 2>/dev/null \
    || "$CENSUS" "$W/lg_norm" mem_slot_constant_alias.mem_slot_constant_alias > "$W/census_norm.tsv" 2>/dev/null
  if [ -s "$W/census_norm.tsv" ]; then
    WIDE=$(awk -F'\t' '$1=="operand" && $7=="constant" && $9=="0" && $5>8 {c++} END{print c+0}' "$W/census_norm.tsv")
    [ "$WIDE" = "0" ] \
      || fail "a constant ZERO operand is now wider than 8 bits in the normalized graph ($WIDE of them) -- the shared pin was restamped"
    echo "ok: no constant-zero operand was widened in the normalized graph"
  fi
fi

echo "PASS: single_edge_memory_constant_alias_test"
