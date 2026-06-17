#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# End-to-end test for `lhd semdiff` (task 2f-semdiff): the structural diff/match
# marks corresponding nodes of two lg: libraries with a shared `match` attribute,
# and the diff is greppable (`tool grep match=0`) and visualizable
# (`tool diff --match`). Covers: identical/commutative full match, a real
# difference isolated to the differing region (surrounding logic matched), the
# matching_names flop anchor, id_granularity=region, and the grep -v invert.
# The per-algorithm matching invariants are also covered by the gtests in
# pass/semdiff; this exercises the lhd plumbing end to end.

set -u

LHD=lhd/lhd
W="${TEST_TMPDIR:-/tmp/lhd_semdiff_$$}"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

# compile <src> <lgdir>
compile() {
  "$LHD" compile "$1" --emit-dir lg:"$2" -q --workdir "$W/wc" >/dev/null 2>&1 || fail "compile $1"
}

# match0_count <lgdir> : number of nodes with match=0 (the unmatched / diff set)
match0_count() {
  "$LHD" tool grep match=0 lg:"$1" --target node --diag-fmt jsonl 2>/dev/null | grep -c '"t":"node"'
}
matched_count() {  # nodes with a nonzero match id (the matched set)
  "$LHD" tool grep -v match=0 lg:"$1" --target node --diag-fmt jsonl 2>/dev/null | grep -c '"t":"node"'
}

# ---------------------------------------------------------------------------
# 1. Identical / commutative: a+b+c vs b+a+c must match fully.
# ---------------------------------------------------------------------------
cat > "$W/g1.prp" <<'EOF'
mod m(a:u8, b:u8, c:u8) -> (y:u9@[0]) {
  y = a + b + c
}
EOF
cat > "$W/o1.prp" <<'EOF'
mod m(a:u8, b:u8, c:u8) -> (y:u9@[0]) {
  y = b + a + c
}
EOF
compile "$W/g1.prp" "$W/g1"
compile "$W/o1.prp" "$W/o1"
"$LHD" semdiff --ref lg:"$W/g1" --impl lg:"$W/o1" -q --workdir "$W/w1" >/dev/null 2>&1 || fail "semdiff #1"
[ "$(match0_count "$W/g1")" -eq 0 ] || fail "#1 ref has unmatched nodes (expected full commutative match)"
[ "$(match0_count "$W/o1")" -eq 0 ] || fail "#1 impl has unmatched nodes (expected full commutative match)"
[ "$(matched_count "$W/g1")" -gt 0 ] || fail "#1 ref matched nothing"
"$LHD" tool diff lg:"$W/g1" lg:"$W/o1" --match 2>/dev/null | grep -q "similarity 1.000" || fail "#1 diff --match not 1.000"
echo "PASS: identical/commutative -> full match, similarity 1.000"

# ---------------------------------------------------------------------------
# 2. Real difference: (a&b)+c vs (a|b)+c. The &/| node and the dependent sum
#    diverge; the shared get_mask on c is still matched (surrounding logic).
# ---------------------------------------------------------------------------
cat > "$W/g2.prp" <<'EOF'
mod m(a:u8, b:u8, c:u8) -> (y:u9@[0]) {
  const t = a & b
  y = t + c
}
EOF
cat > "$W/o2.prp" <<'EOF'
mod m(a:u8, b:u8, c:u8) -> (y:u9@[0]) {
  const t = a | b
  y = t + c
}
EOF
compile "$W/g2.prp" "$W/g2"
compile "$W/o2.prp" "$W/o2"
"$LHD" semdiff --ref lg:"$W/g2" --impl lg:"$W/o2" -q --workdir "$W/w2" >/dev/null 2>&1 || fail "semdiff #2"
[ "$(match0_count "$W/g2")" -gt 0 ] || fail "#2 expected the and/sum to be unmatched"
[ "$(match0_count "$W/o2")" -gt 0 ] || fail "#2 expected the or/sum to be unmatched"
[ "$(matched_count "$W/g2")" -gt 0 ] || fail "#2 expected the shared logic to still match"
"$LHD" tool grep match=0 lg:"$W/g2" --target node 2>/dev/null | grep -q '"kind":"and"' || fail "#2 the and node should be the gap"
"$LHD" tool diff lg:"$W/g2" lg:"$W/o2" --match 2>/dev/null | grep -q '^  + ' || fail "#2 diff --match shows no impl-only (+) line"
"$LHD" tool diff lg:"$W/g2" lg:"$W/o2" --match 2>/dev/null | grep -q '^  - ' || fail "#2 diff --match shows no ref-only (-) line"
echo "PASS: real difference -> isolated gap, surrounding logic matched, diff --match shows -/+"

# ---------------------------------------------------------------------------
# 3. matching_names: same flop name `r`, differing D-side logic. off -> the
#    flop is a gap; on -> the flop anchors by name and matches (more matched).
# ---------------------------------------------------------------------------
cat > "$W/g3.prp" <<'EOF'
mod m(a:u8, b:u8) -> (q:u8@[1]) {
  reg r:u8 = 0
  q = r
  r = a & b
}
EOF
cat > "$W/o3.prp" <<'EOF'
mod m(a:u8, b:u8) -> (q:u8@[1]) {
  reg r:u8 = 0
  q = r
  r = a | b
}
EOF
compile "$W/g3.prp" "$W/g3off"
compile "$W/o3.prp" "$W/o3off"
cp -r "$W/g3off" "$W/g3on"
cp -r "$W/o3off" "$W/o3on"
"$LHD" semdiff --ref lg:"$W/g3off" --impl lg:"$W/o3off" -q --workdir "$W/w3a" >/dev/null 2>&1 || fail "semdiff #3 off"
"$LHD" semdiff --ref lg:"$W/g3on" --impl lg:"$W/o3on" --set semdiff.matching_names=true -q --workdir "$W/w3b" >/dev/null 2>&1 || fail "semdiff #3 on"
OFF=$(matched_count "$W/g3off")
ON=$(matched_count "$W/g3on")
[ "$ON" -gt "$OFF" ] || fail "#3 matching_names=true ($ON) should match more than off ($OFF) (the named flop)"
echo "PASS: matching_names anchors the renamed-region flop ($OFF -> $ON matched)"

# ---------------------------------------------------------------------------
# 4. id_granularity=region unions the connected matched nodes of #1 into ONE id.
# ---------------------------------------------------------------------------
compile "$W/g1.prp" "$W/g4"
compile "$W/o1.prp" "$W/o4"
"$LHD" semdiff --ref lg:"$W/g4" --impl lg:"$W/o4" --set semdiff.id_granularity=region -q --workdir "$W/w4" >/dev/null 2>&1 || fail "semdiff #4"
REGIONS=$("$LHD" tool grep -v match=0 lg:"$W/g4" --target node --attr match --diag-fmt jsonl 2>/dev/null | grep -o '"match":[0-9]*' | sort -u | wc -l | tr -d ' ')
[ "$REGIONS" -eq 1 ] || fail "#4 region granularity should union the connected cone into 1 id, got $REGIONS"
echo "PASS: id_granularity=region unions the matched cone into one id"

# ---------------------------------------------------------------------------
# 5. The grep -v invert: match=0 and -v match=0 partition the node set.
# ---------------------------------------------------------------------------
TOTAL=$("$LHD" tool cat lg:"$W/g2" --target node --diag-fmt jsonl 2>/dev/null | grep -c '"t":"node"')
U=$(match0_count "$W/g2")
M=$(matched_count "$W/g2")
[ "$((U + M))" -eq "$TOTAL" ] || fail "#5 grep -v did not partition the node set ($U + $M != $TOTAL)"
echo "PASS: grep -v match=0 inverts cleanly (partitions the node set)"

# ---------------------------------------------------------------------------
# 6. usage guards: non-lg sides and same dir are rejected.
# ---------------------------------------------------------------------------
"$LHD" semdiff --ref "$W/g1.prp" --impl "$W/o1.prp" -q --workdir "$W/w6" >/dev/null 2>&1 && fail "#6 expected non-lg sides to be rejected"
"$LHD" semdiff --ref lg:"$W/g1" --impl lg:"$W/g1" -q --workdir "$W/w6b" >/dev/null 2>&1 && fail "#6 expected same lg: dir to be rejected"
echo "PASS: usage guards (non-lg sides, identical dirs) reject"

echo "PASS: all lhd semdiff scenarios passed"
