#!/bin/bash
# scripts/lgraph_scc.py is the diagnostic that classifies a combinational cycle.
# It is only useful if a clean report means there is no cycle -- so every way it
# could silently drop an edge is tested here, not just the happy path.
#
# The fail-open case that motivated this: an endpoint that resolves to no node
# and a graph-IO endpoint both have "no node".  Counting the first as a cut edge
# would print SCCS=0 on a cyclic graph the moment `lhd tool cat` renamed an
# endpoint or truncated its output.  Each case below is one guard, so a mutant
# that removes that guard is killed by exactly one named case.
set -u
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." 2>/dev/null && pwd)"
SCC=""
for c in "${TEST_SRCDIR:-}/_main/scripts/lgraph_scc.py" "$ROOT/scripts/lgraph_scc.py" \
         "scripts/lgraph_scc.py"; do
  [ -r "$c" ] && { SCC="$c"; break; }
done
[ -n "$SCC" ] || { echo "FAIL: cannot find lgraph_scc.py"; exit 1; }

# ALWAYS A CHILD PATH -- never $TEST_TMPDIR itself, which is bazel's sandbox
# temp root (see lhd/tests/census_consistency_test.sh for the incident).
if [ -n "${TEST_TMPDIR:-}" ]; then TD="$TEST_TMPDIR/lgraph_scc_test_runtime"
else TD="$ROOT/generated/lgraph_scc_test/runtime_tmp"; fi
rm -rf "$TD"; mkdir -p "$TD"

rc=0
fail() { echo "FAIL: $*"; rc=1; }

node() { printf '{"t":"node","mod":"m","nid":%s,"kind":"%s","name":%s,"color":null,"match":null,"src":null,"partitionable":true}\n' "$1" "$2" "${3:-null}"; }
edge() { printf '{"t":"edge","mod":"m","from":"%s","to":"%s","bits":%s}\n' "$1" "$2" "${3:-4}"; }
pin()  { printf '{"t":"pin","mod":"m","nid":%s,"name":%s,"bits":%s,"signed":false,"match":null}\n' "$1" "$2" "$3"; }

# `run <fixture> <cut> [extra args...]` -> prints the report, sets REPORT/RC
run() {
  local f="$1"; shift
  local cut="$1"; shift
  REPORT="$(python3 "$SCC" "$TD/$f" --cut "$cut" --stage "$f" --max-report 0 --max-members 0 "$@" 2>&1)"
  RC=$?
}
sccs() { echo "$REPORT" | grep -oP '^  SCCS=\K[0-9]+' | head -1; }

# --------------------------------------------------------------------------
# 1. a two-node combinational cycle is found
# --------------------------------------------------------------------------
{ node 1 and; node 2 or; edge and_1.p0 or_2.p0; edge or_2.p0 and_1.p0; } > "$TD/two.jsonl"
run two.jsonl lean
[ "$RC" -eq 0 ] && [ "$(sccs)" = "1" ] || fail "two-node cycle: rc=$RC SCCS=$(sccs)"
echo "$REPORT" | grep -q "size=2" || fail "two-node cycle: component is not size 2"
echo "$REPORT" | grep -q "and_1.p0 -> or_2.p0  4b" || fail "two-node cycle: internal edge width lost"

# --------------------------------------------------------------------------
# 2. a single-node self-loop is found (Tarjan alone would not report it)
# --------------------------------------------------------------------------
{ node 3 and; edge and_3.p0 and_3.p0; } > "$TD/self.jsonl"
run self.jsonl lean
[ "$RC" -eq 0 ] && [ "$(sccs)" = "1" ] || fail "self-loop: rc=$RC SCCS=$(sccs)"

# --------------------------------------------------------------------------
# 3. the lean cut: graph input, const and flop drivers break a cycle;
#    --cut none keeps it.  Same fixture, so the cut policy is the only variable.
# --------------------------------------------------------------------------
{ node 6 flop; node 7 and; node 5 const; node 4 and;
  edge and_7.p0 flop_6.din; edge flop_6.p0 and_7.p0;
  edge '$clk' and_4.p0; edge const_5.p0 and_4.p0; edge and_4.p0 '$out'; } > "$TD/cut.jsonl"
run cut.jsonl lean
[ "$RC" -eq 0 ] && [ "$(sccs)" = "0" ] || fail "flop cut: rc=$RC SCCS=$(sccs) (expected 0)"
echo "$REPORT" | grep -q "edges_cut_io=2" || fail "flop cut: graph-IO edges not counted as io"
echo "$REPORT" | grep -q "edges_cut_kind=2" || fail "flop cut: flop+const edges not counted as kind"
run cut.jsonl none
[ "$RC" -eq 0 ] && [ "$(sccs)" = "1" ] || fail "--cut none: rc=$RC SCCS=$(sccs) (expected 1)"

# --------------------------------------------------------------------------
# 4. a memory read output is KEPT by `lean` and CUT by `lean-plus-mem`.
#    This is the discriminator the CORE-ET classification turns on.
# --------------------------------------------------------------------------
{ node 8 memory; node 9 and;
  edge memory_8.p1 and_9.p0 256; edge and_9.p0 memory_8.p3 256; } > "$TD/mem.jsonl"
run mem.jsonl lean
[ "$RC" -eq 0 ] && [ "$(sccs)" = "1" ] || fail "memory under lean: rc=$RC SCCS=$(sccs)"
run mem.jsonl lean-plus-mem
[ "$RC" -eq 0 ] && [ "$(sccs)" = "0" ] || fail "memory under lean-plus-mem: rc=$RC SCCS=$(sccs)"

# --------------------------------------------------------------------------
# 5. a node NAME containing dots still resolves (yosys hierarchical wires do).
#    rsplit('.') would attribute the edge to no node and drop it.
# --------------------------------------------------------------------------
{ node 10 and '"u_inst.rf.q"'; node 11 or;
  edge 'and_10:u_inst.rf.q.p0' or_11.p0; edge or_11.p0 'and_10:u_inst.rf.q.p0'; } > "$TD/dot.jsonl"
run dot.jsonl lean
[ "$RC" -eq 0 ] && [ "$(sccs)" = "1" ] || fail "dotted name: rc=$RC SCCS=$(sccs)"
echo "$REPORT" | grep -q "u_inst.rf.q" || fail "dotted name: member not reported"

# 5b. a dotted PIN LABEL.  This is the case a plain rpartition('.') gets wrong
#     even when every node name is plain: yosys pin names routinely carry dots
#     (`auto$bmuxmap.cc:84:execute$832[511:480]`), so the split must search for
#     the longest prefix that IS a declared node, not for the last dot.
{ node 18 mux; node 19 sext;
  edge 'mux_18.auto$bmuxmap.cc:84:execute$832[511:480]' sext_19.p0 32;
  edge sext_19.p0 'mux_18.p2' 32; } > "$TD/dotpin.jsonl"
run dotpin.jsonl lean
[ "$RC" -eq 0 ] && [ "$(sccs)" = "1" ] || fail "dotted pin label: rc=$RC SCCS=$(sccs)"

# --------------------------------------------------------------------------
# 6. an endpoint naming no declared node is an ERROR, not a cut edge.
#    Without this the same fixture reports a clean SCCS=0.
# --------------------------------------------------------------------------
{ node 12 and; node 13 or;
  edge and_12.p0 or_13.p0; edge or_13.p0 nosuch_99.p0; edge nosuch_99.p0 and_12.p0; } > "$TD/bad.jsonl"
run bad.jsonl lean
[ "$RC" -eq 3 ] || fail "unresolved endpoint: rc=$RC (expected 3)"
echo "$REPORT" | grep -q "name no declared node" || fail "unresolved endpoint: no diagnostic"
echo "$REPORT" | grep -q "nosuch_99.p0" || fail "unresolved endpoint: example not printed"

# --------------------------------------------------------------------------
# 7. the plain-text truncation notice `lhd tool cat` prints at --max is an
#    ERROR.  Skipping it would analyse a partial graph and call it clean.
# --------------------------------------------------------------------------
{ node 14 and; node 15 or; edge and_14.p0 or_15.p0;
  echo '-- output truncated at --max 200 rows; narrow with --top/filters'; } > "$TD/trunc.jsonl"
run trunc.jsonl lean
[ "$RC" -eq 3 ] || fail "truncation notice: rc=$RC (expected 3)"
echo "$REPORT" | grep -q "not JSON" || fail "truncation notice: no diagnostic"

# --------------------------------------------------------------------------
# 8. a pin record whose width contradicts its edges is an ERROR: the two views
#    of the dump have drifted and neither can be trusted.
# --------------------------------------------------------------------------
{ node 16 and; node 17 or; pin 16 '"q"' 8; edge 'and_16.q' or_17.p0 4; } > "$TD/width.jsonl"
run width.jsonl lean
[ "$RC" -eq 3 ] || fail "width disagreement: rc=$RC (expected 3)"

# --------------------------------------------------------------------------
# 9. --require-member is the gate used to cross-check a real pass.lean back
#    edge, so it must actually fail when the node is NOT in an SCC.
# --------------------------------------------------------------------------
run two.jsonl lean --require-member 1
[ "$RC" -eq 0 ] || fail "--require-member 1: rc=$RC (node 1 IS in the SCC)"
{ node 20 and; node 21 or; edge and_20.p0 or_21.p0; } > "$TD/acyclic.jsonl"
run acyclic.jsonl lean --require-member 20
[ "$RC" -eq 3 ] || fail "--require-member on an acyclic graph: rc=$RC (expected 3)"

# --------------------------------------------------------------------------
# 10. a REAL extract: the pass.lean back edge from minion_frontend_thread_buffer
#     (`and_1664 reads get_mask_19520`) and the two memories the cycle closes
#     through.  This is an EXTRACT of a real `lhd tool cat` dump, committed so
#     the endpoint format and the lean/lean-plus-mem discrimination are pinned
#     against real data; it is NOT a re-derivation of the full-graph result,
#     which lives in pass/lean/CYCLE_PROVENANCE.txt.
# --------------------------------------------------------------------------
EXTRACT=""
for c in "${TEST_SRCDIR:-}/_main/lhd/tests/lgraph_scc_tb_extract.jsonl" \
         "$ROOT/lhd/tests/lgraph_scc_tb_extract.jsonl" \
         "lhd/tests/lgraph_scc_tb_extract.jsonl"; do
  [ -r "$c" ] && { EXTRACT="$c"; break; }
done
if [ -z "$EXTRACT" ]; then
  echo "FAIL: the committed real extract lgraph_scc_tb_extract.jsonl is missing"; rc=1
else
  cp "$EXTRACT" "$TD/real.jsonl"
  run real.jsonl lean --require-member 1664 --require-member 19520
  [ "$RC" -eq 0 ] || fail "real extract under lean: rc=$RC"
  [ "$(sccs)" = "1" ] || fail "real extract under lean: SCCS=$(sccs) (expected 1)"
  echo "$REPORT" | grep -q "get_mask_19520.p0 -> and_1664.p0" \
    || fail "real extract: the pass.lean back edge is not an internal SCC edge"
  grep -q 'bmuxmap' "$TD/real.jsonl" \
    || fail "real extract: no dotted pin label left in it, so it no longer pins that case"
  run real.jsonl lean-plus-mem
  [ "$RC" -eq 0 ] || fail "real extract under lean-plus-mem: rc=$RC"
  [ "$(sccs)" = "0" ] || fail "real extract under lean-plus-mem: SCCS=$(sccs) (expected 0 -- the cycle closes only through a memory read)"
fi

[ "$rc" -eq 0 ] || { echo "FAIL: lgraph_scc_test"; exit 1; }
echo "PASS: lgraph_scc_test"
